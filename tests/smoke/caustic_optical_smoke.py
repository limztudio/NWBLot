#!/usr/bin/env python3
"""Separate reflection, camera-refraction and photon-caustic contributions in one actual static scene."""

from reflection_image_analysis import ReflectionImageAnalysis

import argparse
import base64
import html
import json
import os
from pathlib import Path
import subprocess
import sys

import caustic_quality_smoke
from caustic_optical_image_evidence import VARIANTS, CausticOpticalImageEvidence
from framebuffer_png_encoder import FramebufferPngEncoder
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90

LIT_GPU_VALIDATION = "--gpu-validation"

LIT_COMBINED = "combined"
LIT_REFLECTION_DISABLED = "reflection_disabled"
LIT_CAUSTICS_DISABLED = "caustics_disabled"
LIT_REFRACTION_DISABLED = "refraction_disabled"
LIT_DISABLED = "disabled"
LIT_HARDWARE = "hardware"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_LOG = ".log"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
LIT_REFLECTION_RESOLVE_HARDWARE = "Reflection resolve: hardware"
LIT_NATURAL_SOFTWARE_ONLY_SHADOW_ROUTE_SEL = "natural software-only shadow route selected because RayQuery-capable hardware is unavailable"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_UTF_8 = "utf-8"
LIT_EXTERIOR_ENVIRONMENT = "exterior_environment"
LIT_STORE_TRUE = "store_true"
LIT_MAIN = "__main__"
LIT_APPEND = "append"




def capture_environment(variant, software_ray_tracing=False, caustic_photon_grid_divisor=1):
    environment = dict(os.environ)
    for key in tuple(environment):
        if key.startswith(("NWB_REFLECTION_SMOKE_", "NWB_REFRACTION_SMOKE_", "NWB_CAUSTIC_SMOKE_")) or key == "NWB_GPU_TIMING_FILE":
            environment.pop(key)
    environment.update({"NWB_REFLECTION_SMOKE_MODE": LIT_DISABLED if variant == LIT_REFLECTION_DISABLED else ("screen" if software_ray_tracing else LIT_HARDWARE), "NWB_REFRACTION_SMOKE_ENABLED": "0" if variant == LIT_REFRACTION_DISABLED else "1",
        "NWB_REFRACTION_SMOKE_HARDWARE": "1", "NWB_CAUSTIC_SMOKE_ENABLED": "0" if variant == LIT_CAUSTICS_DISABLED else "1", "NWB_CAUSTIC_SMOKE_REFLECTION_COMPARISON": "1", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "0", "NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0", "NWB_TRANSPARENT_MULTI_SPIN_SPEED": "0", "NWB_CAUSTIC_PHOTON_GRID_DIVISOR": str(caustic_photon_grid_divisor)})
    if software_ray_tracing:
        environment["NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BACKDROP"] = "1"
    return environment


def capture(args, variant):
    output = args.output_directory / (variant + ".bmp")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "360", LIT_TIMEOUT, str(args.timeout), "--log-output", str(output.with_suffix(LIT_LOG)), LIT_EXPECT_LOG_MESSAGE, "TransparentMultiSmokeProject: shutdown",
        LIT_GPU_VALIDATION if args.gpu_validation else "--no-gpu-validation"]
    enabled = variant != LIT_REFLECTION_DISABLED
    command += [LIT_EXPECT_LOG_MESSAGE, "CausticSphereSmokeProject: reflection mode " + (("1" if args.software_ray_tracing else "2") if enabled else "0"), LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: "
        + (("screen-space" if args.software_ray_tracing else LIT_HARDWARE) if enabled else LIT_DISABLED)]
    if not enabled:
        command += [LIT_REJECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
    command += [LIT_EXPECT_LOG_MESSAGE, "CausticSphereSmokeProject: camera refraction " + (LIT_DISABLED if variant == LIT_REFRACTION_DISABLED else "enabled")]
    if variant == LIT_REFRACTION_DISABLED or args.software_ray_tracing:
        # Disabled transmission and no-RT devices both use screen-space glass reflection.
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve: screen-space", LIT_REJECT_LOG_MESSAGE, "AVBOIT refraction resolve: hardware"]
    else:
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve:"]
    command += [LIT_EXPECT_LOG_MESSAGE if variant != LIT_CAUSTICS_DISABLED else LIT_REJECT_LOG_MESSAGE, "caustic producer ("]
    if args.software_ray_tracing:
        command += ["--application-arg=--disable-hardware-ray-tracing"]
        for message in ("Loader: hardware ray tracing disabled before device creation", LIT_NATURAL_SOFTWARE_ONLY_SHADOW_ROUTE_SEL, "RendererSystem: dispatched software shadow traversal",
            "CausticSphereSmokeProject: screen refraction striped backdrop created (24 opaque strips)",):
            command += [LIT_EXPECT_LOG_MESSAGE, message]
        if variant != LIT_CAUSTICS_DISABLED:
            command += [LIT_EXPECT_LOG_MESSAGE, "RendererSystem: dispatched software caustic producer"]
        for message in (LIT_REFLECTION_RESOLVE_HARDWARE, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA, "RendererSystem: dispatched hardware caustic producer", "RendererSystem: created surfel HW trace compute pipeline",
            "RendererSystem: created refraction resolve pipeline (hardware ray query, sparse queue)", "RendererSystem: created refraction resolve pipeline (hardware ray query, dense grid)",):
            command += [LIT_REJECT_LOG_MESSAGE, message]
    else:
        command += [LIT_EXPECT_LOG_MESSAGE if args.require_hardware else "--skip-log-message", "natural hardware shadow route selected on RayQuery-capable hardware" if args.require_hardware else LIT_NATURAL_SOFTWARE_ONLY_SHADOW_ROUTE_SEL]
        command += [LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print("Capturing combined caustic scene / " + variant + " at presentation360...", flush=True)
    result = subprocess.run(command, env=capture_environment(variant, args.software_ray_tracing, args.caustic_photon_grid_divisor), check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(variant + " combined capture failed: " + str(result.returncode))
    caustic_quality_smoke.verify_settings(output.with_suffix(LIT_LOG).read_text(encoding=LIT_UTF_8), args.caustic_photon_grid_divisor, producer_enabled=variant != LIT_CAUSTICS_DISABLED)
    frame = read_bmp_24_rows(output)
    ReflectionImageAnalysis.validate_frame(frame)
    if frame[:2] != (1280, 900):
        raise SmokeFailure("combined caustic comparison requires the actual 1280x900 framebuffer")
    return frame






def write_report(args, frames, metrics=None):
    cards = []
    for name, frame in frames.items():
        png = FramebufferPngEncoder.encode(frame)
        (args.output_directory / (name + ".png")).write_bytes(png)
        cards.append(f'<article><h2>{name}</h2><a href="{name}.bmp">Raw BMP</a> / <a href="{name}.log">Launch log</a>' f'<img alt="Actual {name} caustic scene" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    note = "Actual frame360 readbacks from the same static camera, sphere, ground and light. Individual typed controls separate the visible reflection, camera refraction and caustic contributions. Ground F0 remains zero. Reflection uses a fixed bright environment. "
    if args.software_ray_tracing:
        note += "Hardware ray tracing is disabled at logical-device creation. Shadow and photon caustic tracing use software BVHs; camera refraction and reflection use screen-space approximations. A fixed unlit striped backdrop supplies opaque in-screen depth and visible refractive displacement for every software toggle. This validates separate visible contributions and localization. The hardware-only geometric ray-miss/Fresnel oracle is explicitly excluded; this is not proof of equivalent offscreen transport."
    else:
        note += "Mesh-derived exterior rays that miss both sphere and ground require their predicted smooth Fresnel/environment contribution, including local patches. Secondary ray-hit shading does not claim to sample the primary-camera caustic cache."
    metadata = {"frame_source": "actual application framebuffer readback", "presentation_frame": 360, "caustic_photon_grid_divisor": args.caustic_photon_grid_divisor, "logical_device_policy": LIT_DISABLED if args.software_ray_tracing else "automatic",
        "background_fixture": "screen_refraction_stripes" if args.software_ray_tracing else "original", "optical_validation": "software_contributions" if args.software_ray_tracing else "hardware_contributions_and_exterior_radiometry",
        "variants": list(frames), "metrics": metrics, "scope": note}
    (args.output_directory / "caustic_optical_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding=LIT_UTF_8)
    document = f'<!doctype html><html lang="en"><meta charset="{LIT_UTF_8}"><title>Combined optical caustic scene</title>'
    document += '<style>body{font:16px system-ui;background:#141922;color:#eee;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#8cf}pre{white-space:pre-wrap}</style>'
    document += '<h1>Caustics, camera refraction and reflection</h1><p>' + html.escape(note) + '</p><main>' + ''.join(cards) + '</main><pre>'
    document += html.escape(json.dumps(metrics, indent=2) if metrics else 'Visual acceptance pending.') + '</pre></html>'
    (args.output_directory / "caustic_optical.html").write_text(document, encoding=LIT_UTF_8)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument(LIT_TIMEOUT, type=float, default=180)
    route = parser.add_mutually_exclusive_group()
    route.add_argument("--require-hardware", action=LIT_STORE_TRUE)
    route.add_argument("--software-ray-tracing", action=LIT_STORE_TRUE, help="Disable logical-device hardware ray tracing and validate software/screen-space optical contributions.")
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    caustic_quality_smoke.add_arguments(parser)
    parser.add_argument(LIT_GPU_VALIDATION, action=argparse.BooleanOptionalAction, default=True, help="Request graphics validation; use --no-gpu-validation for a fin application.")
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
        metrics = CausticOpticalImageEvidence.analyze_frames(frames, args.software_ray_tracing)
        write_report(args, frames, metrics)
        print("PASS: separate visible reflection, camera-refraction and caustic contributions\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
