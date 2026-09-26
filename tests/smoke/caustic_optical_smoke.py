#!/usr/bin/env python3
"""Separate reflection, camera-refraction and photon-caustic contributions in one actual static scene."""

import argparse
import base64
import html
import json
import math
import os
from pathlib import Path
import subprocess
import sys

import caustic_quality_smoke
from reflection_smoke import validate_frame
from caustic_optical_reference import exterior_samples, expected_environment_color, predictor_metadata
from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


VARIANTS = ("combined", "reflection_disabled", "caustics_disabled", "refraction_disabled")


def capture_environment(variant, software_ray_tracing=False, caustic_photon_grid_divisor=1):
    environment = dict(os.environ)
    for key in tuple(environment):
        if key.startswith(("NWB_REFLECTION_SMOKE_", "NWB_REFRACTION_SMOKE_", "NWB_CAUSTIC_SMOKE_")) or key == "NWB_GPU_TIMING_FILE":
            environment.pop(key)
    environment.update({"NWB_REFLECTION_SMOKE_MODE": "disabled" if variant == "reflection_disabled" else ("screen" if software_ray_tracing else "hardware"),
        "NWB_REFRACTION_SMOKE_ENABLED": "0" if variant == "refraction_disabled" else "1",
        "NWB_REFRACTION_SMOKE_HARDWARE": "1", "NWB_CAUSTIC_SMOKE_ENABLED": "0" if variant == "caustics_disabled" else "1",
        "NWB_CAUSTIC_SMOKE_REFLECTION_COMPARISON": "1", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "0", "NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0",
        "NWB_TRANSPARENT_MULTI_SPIN_SPEED": "0",
        "NWB_CAUSTIC_PHOTON_GRID_DIVISOR": str(caustic_photon_grid_divisor)})
    if software_ray_tracing:
        environment["NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BACKDROP"] = "1"
    return environment


def capture(args, variant):
    output = args.output_directory / (variant + ".bmp")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "360",
        "--timeout", str(args.timeout), "--log-output", str(output.with_suffix(".log")),
        "--expect-log-message", "TransparentMultiSmokeProject: shutdown"]
    enabled = variant != "reflection_disabled"
    command += ["--expect-log-message", "CausticSphereSmokeProject: reflection mode "
        + (("1" if args.software_ray_tracing else "2") if enabled else "0"),
        "--expect-log-message", "Reflection resolve: "
        + (("screen-space" if args.software_ray_tracing else "hardware") if enabled else "disabled")]
    if not enabled:
        command += ["--reject-log-message", "Reflection resolve: hardware"]
    command += ["--expect-log-message", "CausticSphereSmokeProject: camera refraction "
        + ("disabled" if variant == "refraction_disabled" else "enabled")]
    if variant == "refraction_disabled" or args.software_ray_tracing:
        # Glass reflection retains optical composition when transmission is disabled; a no-RT device also uses this screen-space route.
        command += ["--expect-log-message", "AVBOIT refraction resolve: screen-space",
            "--reject-log-message", "AVBOIT refraction resolve: hardware"]
    else:
        command += ["--expect-log-message", "AVBOIT refraction resolve:"]
    command += ["--expect-log-message" if variant != "caustics_disabled" else "--reject-log-message", "caustic producer ("]
    if args.software_ray_tracing:
        command += ["--application-arg=--disable-hardware-ray-tracing", "--application-arg=--gpudbg"]
        for message in (
            "Loader: hardware ray tracing disabled before device creation",
            "Vulkan: hardware ray tracing policy=disabled",
            "RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0",
            "natural software-only shadow route selected because RayQuery-capable hardware is unavailable",
            "RendererSystem: dispatched software shadow traversal",
            "CausticSphereSmokeProject: screen refraction striped backdrop created (24 opaque strips)",
        ):
            command += ["--expect-log-message", message]
        if variant != "caustics_disabled":
            command += ["--expect-log-message", "RendererSystem: dispatched software caustic producer"]
        for message in (
            "Reflection resolve: hardware",
            "RendererSystem: dispatched hardware transparent shadow traversal",
            "RendererSystem: dispatched hardware caustic producer",
            "RendererSystem: created surfel HW trace compute pipeline",
            "RendererSystem: created refraction resolve pipeline (hardware ray query)",
        ):
            command += ["--reject-log-message", message]
    else:
        command += ["--expect-log-message" if args.require_hardware else "--skip-log-message",
            "natural hardware shadow route selected on RayQuery-capable hardware" if args.require_hardware
            else "natural software-only shadow route selected because RayQuery-capable hardware is unavailable"]
        command += ["--expect-log-message", "RendererSystem: dispatched hardware transparent shadow traversal"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print("Capturing combined caustic scene / " + variant + " at presentation360...", flush=True)
    result = subprocess.run(command, env=capture_environment(variant, args.software_ray_tracing, args.caustic_photon_grid_divisor), check=False, timeout=args.timeout + 90)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(variant + " combined capture failed: " + str(result.returncode))
    caustic_quality_smoke.verify_settings(output.with_suffix(".log").read_text(encoding="utf-8"),
        args.caustic_photon_grid_divisor, producer_enabled=variant != "caustics_disabled")
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != (1280, 900):
        raise SmokeFailure("combined caustic comparison requires the actual 1280x900 framebuffer")
    return frame


def analyze_exterior_reflection(combined, disabled):
    width, height, rows = validate_frame(combined)
    if disabled[:2] != combined[:2]:
        raise SmokeFailure("exterior reflection captures have different dimensions")
    selected = exterior_samples(width, height)
    tested, weak, errors, ratios, missing = 0, 0, [], [], []
    for x, y, fresnel in selected:
        base, observed = disabled[2][y][x], rows[y][x]
        expected = expected_environment_color(base, fresnel)
        expected_gain = sum(a - b for a, b in zip(expected, base)) / 3
        if expected_gain < 8:
            weak += 1
            continue
        observed_gain = sum(a - b for a, b in zip(observed, base)) / 3
        tested += 1
        ratios.append(observed_gain / expected_gain)
        errors.extend(abs(a - b) for a, b in zip(observed, expected))
        # Two display bytes cover baseline/output quantization and FP16 arithmetic. Missing half the expected
        # visible signal after that allowance is a transport failure, including an otherwise small local patch.
        if observed_gain + 2 < 0.5 * expected_gain:
            missing.append((x, y))
    if tested < 2000 or weak > len(selected) * 0.1:
        raise SmokeFailure("too few mesh-proven exterior samples with visible environment reflection")
    if missing:
        raise SmokeFailure("missing exterior Fresnel/environment reflection at " + str(len(missing))
            + " stable mesh samples; first pixels " + str(missing[:8]))
    errors.sort()
    mae = sum(errors) / len(errors)
    if mae > 1.5:
        raise SmokeFailure("exterior reflection disagrees with the fixed-environment radiometric oracle: byte MAE " + str(mae))
    return {"predictor": predictor_metadata(), "mesh_selected_samples": len(selected), "tested_samples": tested,
        "weak_samples": weak, "missing_samples": 0, "minimum_predicted_gain_fraction": min(ratios),
        "rgb_byte_mae": mae, "rgb_byte_p99_error": errors[min(len(errors) - 1, int(len(errors) * 0.99))]}


def analyze_frames(frames, software_ray_tracing=False):
    combined = frames["combined"]
    width, height, rows = validate_frame(combined)
    if any(frame[:2] != combined[:2] for frame in frames.values()):
        raise SmokeFailure("combined optical captures have different dimensions")
    # Perspective sphere silhouette: camera(0,.85,-2.2), center(0,.85,0), radius.7, default60deg vertical FOV.
    radius = height / (2 * math.tan(math.pi / 6)) * 0.7 / math.sqrt(2.2 ** 2 - 0.7 ** 2)
    results = {}
    for variant in VARIANTS[1:]:
        sphere_changed, ground_changed, sphere_absolute, outside_absolute, outside_channels, ground_gain = 0, 0, 0, 0, 0, 0
        ground_cells = {}
        for y in range(height):
            for x in range(width):
                a, b = rows[y][x], frames[variant][2][y][x]
                delta = sum(abs(i - j) for i, j in zip(a, b))
                in_sphere = (x + 0.5 - width / 2) ** 2 + (y + 0.5 - height / 2) ** 2 <= (radius + 3) ** 2
                if in_sphere:
                    sphere_changed += delta > 9
                    sphere_absolute += delta
                else:
                    outside_absolute += delta
                    outside_channels += 3
                if not in_sphere and y > height * 0.78 and width * 0.15 < x < width * 0.85:
                    ground_changed += delta > 9
                    gain = sum(a) - sum(b)
                    ground_gain += gain
                    if gain > 0:
                        # 8x8 focus cells over the ground strip: a sphere-lens caustic converges into a
                        # smooth crescent, so its positive photon gain must concentrate in a few cells
                        # instead of spreading uniformly like run-to-run quantization noise.
                        key = (x // (width // 8 + 1), y // (height // 8 + 1))
                        ground_cells[key] = ground_cells.get(key, 0) + gain
        if variant == "reflection_disabled":
            if sphere_changed < max(100, math.pi * radius * radius * 0.005):
                raise SmokeFailure("reflection toggle produced no visible reflected contribution on the glass sphere")
            if outside_absolute / max(outside_channels, 1) > 0.6:
                raise SmokeFailure("reflection toggle changed the nonreflective ground/caustic control")
        elif variant == "caustics_disabled":
            positive_gain = sum(ground_cells.values())
            ordered_cells = sorted(ground_cells.values(), reverse=True)
            focus_share = sum(ordered_cells[:3]) / positive_gain if positive_gain > 0 else 0.0
            # The tonemapped bright ground compresses the focused crescent to a few LSBs per
            # pixel, so per-pixel delta counts cannot see it; the concentrated positive gain can.
            if ground_gain < 1500 or focus_share < 0.4:
                raise SmokeFailure("caustic toggle produced no visible positive photon contribution on the ground")
        elif sphere_changed < max(100, math.pi * radius * radius * 0.005):
            raise SmokeFailure("camera refraction toggle produced no visible sphere transmission change")
        results[variant] = {"changed_sphere_pixels": sphere_changed, "sphere_absolute_channel_delta": sphere_absolute,
            "changed_ground_pixels": ground_changed, "ground_channel_gain": ground_gain,
            "outside_sphere_byte_mae": outside_absolute / max(outside_channels, 1)}
    if software_ray_tracing:
        results["reflection_disabled"]["exterior_environment"] = {
            "evaluated": False,
            "reason": "Hardware-only geometric ray-miss oracle; screen-space hits are projected approximations.",
        }
    else:
        results["reflection_disabled"]["exterior_environment"] = analyze_exterior_reflection(combined, frames["reflection_disabled"])
    return results


def write_report(args, frames, metrics=None):
    cards = []
    for name, frame in frames.items():
        png = png_rgb_bytes(frame)
        (args.output_directory / (name + ".png")).write_bytes(png)
        cards.append(f'<article><h2>{name}</h2><a href="{name}.bmp">Raw BMP</a> / <a href="{name}.log">Launch log</a>'
            f'<img alt="Actual {name} caustic scene" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    note = "Actual frame360 readbacks from the same static camera, sphere, ground and light. Individual typed controls separate the visible reflection, camera refraction and caustic contributions. Ground F0 remains zero. Reflection uses a fixed bright environment. "
    if args.software_ray_tracing:
        note += "Hardware ray tracing is disabled at logical-device creation. Shadow and photon caustic tracing use software BVHs; camera refraction and reflection use screen-space approximations. A fixed unlit striped backdrop supplies opaque in-screen depth and visible refractive displacement for every software toggle. This validates separate visible contributions and localization. The hardware-only geometric ray-miss/Fresnel oracle is explicitly excluded; this is not proof of equivalent offscreen transport."
    else:
        note += "Mesh-derived exterior rays that miss both sphere and ground require their predicted smooth Fresnel/environment contribution, including local patches. Secondary ray-hit shading does not claim to sample the primary-camera caustic cache."
    metadata = {"frame_source": "actual application framebuffer readback", "presentation_frame": 360,
        "caustic_photon_grid_divisor": args.caustic_photon_grid_divisor,
        "logical_device_policy": "disabled" if args.software_ray_tracing else "automatic",
        "background_fixture": "screen_refraction_stripes" if args.software_ray_tracing else "original",
        "optical_validation": "software_contributions" if args.software_ray_tracing else "hardware_contributions_and_exterior_radiometry",
        "variants": list(frames), "metrics": metrics, "scope": note}
    (args.output_directory / "caustic_optical_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    document = '<!doctype html><html lang="en"><meta charset="utf-8"><title>Combined optical caustic scene</title>'
    document += '<style>body{font:16px system-ui;background:#141922;color:#eee;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#8cf}pre{white-space:pre-wrap}</style>'
    document += '<h1>Caustics, camera refraction and reflection</h1><p>' + html.escape(note) + '</p><main>' + ''.join(cards) + '</main><pre>'
    document += html.escape(json.dumps(metrics, indent=2) if metrics else 'Visual acceptance pending.') + '</pre></html>'
    (args.output_directory / "caustic_optical.html").write_text(document, encoding="utf-8")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--timeout", type=float, default=60)
    route = parser.add_mutually_exclusive_group()
    route.add_argument("--require-hardware", action="store_true")
    route.add_argument("--software-ray-tracing", action="store_true",
        help="Disable logical-device hardware ray tracing and validate software/screen-space optical contributions.")
    parser.add_argument("--application-arg", action="append", default=[])
    caustic_quality_smoke.add_arguments(parser)
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory = args.output_directory.resolve()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    frames = {}
    try:
        for variant in VARIANTS:
            frame = capture(args, variant)
            if frame is None:
                return SKIP_EXIT_CODE
            frames[variant] = frame
        write_report(args, frames)
        metrics = analyze_frames(frames, args.software_ray_tracing)
        write_report(args, frames, metrics)
        print("PASS: separate visible reflection, camera-refraction and caustic contributions\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
