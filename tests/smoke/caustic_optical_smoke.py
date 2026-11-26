#!/usr/bin/env python3
"""Separate reflection, camera-refraction and photon-caustic contributions in one actual static scene."""

from reflection_image_analysis import ReflectionImageAnalysis

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import caustic_quality_smoke
from caustic_optical_image_evidence import VARIANTS, CausticOpticalImageEvidence
from smoke_capture_suite import build_window_capture_command, run_window_capture_subprocess, write_evidence_gallery_report
from window_capture_image_codec import Bmp24Codec
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_GPU_VALIDATION = "--gpu-validation"

LIT_REFLECTION_DISABLED = "reflection_disabled"
LIT_CAUSTICS_DISABLED = "caustics_disabled"
LIT_REFRACTION_DISABLED = "refraction_disabled"
LIT_DISABLED = "disabled"
LIT_HARDWARE = "hardware"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_LOG = ".log"
LIT_REFLECTION_RESOLVE_HARDWARE = "Reflection resolve: hardware"
LIT_NATURAL_SOFTWARE_SHADOW_ROUTE_SELECTED = "natural software-only shadow route selected because RayQuery-capable hardware is unavailable"
LIT_HARDWARE_TRANSPARENT_SHADOW_DISPATCH_LOG = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_UTF_8 = "utf-8"
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
    log_path = output.with_suffix(LIT_LOG)
    enabled = variant != LIT_REFLECTION_DISABLED
    expect_logs = [
        "TransparentMultiSmokeProject: shutdown",
        "CausticSphereSmokeProject: reflection mode " + (("1" if args.software_ray_tracing else "2") if enabled else "0"),
        "Reflection resolve: " + (("screen-space" if args.software_ray_tracing else LIT_HARDWARE) if enabled else LIT_DISABLED),
    ]
    reject_logs = [] if enabled else [LIT_REFLECTION_RESOLVE_HARDWARE]
    expect_logs.append("CausticSphereSmokeProject: camera refraction " + (LIT_DISABLED if variant == LIT_REFRACTION_DISABLED else "enabled"))
    if variant == LIT_REFRACTION_DISABLED or args.software_ray_tracing:
        expect_logs.append("AVBOIT refraction resolve: screen-space")
        reject_logs.append("AVBOIT refraction resolve: hardware")
    else:
        expect_logs.append("AVBOIT refraction resolve:")
    if variant != LIT_CAUSTICS_DISABLED:
        expect_logs.append("caustic producer (")
    else:
        reject_logs.append("caustic producer (")
    skip_logs = []
    extra_args = []
    if args.software_ray_tracing:
        extra_args.append("--application-arg=--disable-hardware-ray-tracing")
        expect_logs.extend(("Loader: hardware ray tracing disabled before device creation", LIT_NATURAL_SOFTWARE_SHADOW_ROUTE_SELECTED,
            "RendererSystem: dispatched software shadow traversal", "CausticSphereSmokeProject: screen refraction striped backdrop created (24 opaque strips)"))
        if variant != LIT_CAUSTICS_DISABLED:
            expect_logs.append("RendererSystem: dispatched software caustic producer")
        reject_logs.extend((LIT_REFLECTION_RESOLVE_HARDWARE, LIT_HARDWARE_TRANSPARENT_SHADOW_DISPATCH_LOG, "RendererSystem: dispatched hardware caustic producer",
            "RendererSystem: created surfel HW trace compute pipeline", "RendererSystem: created refraction resolve pipeline (hardware ray query, sparse queue)",
            "RendererSystem: created refraction resolve pipeline (hardware ray query, dense grid)"))
    else:
        if args.require_hardware:
            expect_logs.append("natural hardware shadow route selected on RayQuery-capable hardware")
        else:
            skip_logs.append(LIT_NATURAL_SOFTWARE_SHADOW_ROUTE_SELECTED)
        expect_logs.append(LIT_HARDWARE_TRANSPARENT_SHADOW_DISPATCH_LOG)
    command = build_window_capture_command(args, output, frames=360, log_output=log_path, expect_logs=expect_logs, reject_logs=reject_logs,
        skip_logs=skip_logs, extra_args=extra_args, gpu_validation=args.gpu_validation)
    print("Capturing combined caustic scene / " + variant + " at presentation360...", flush=True)
    if run_window_capture_subprocess(command, env=capture_environment(variant, args.software_ray_tracing, args.caustic_photon_grid_divisor),
            timeout=args.timeout, failure_message=variant + " combined capture failed: {code}") is None:
        return None
    caustic_quality_smoke.verify_settings(log_path.read_text(encoding=LIT_UTF_8), args.caustic_photon_grid_divisor, producer_enabled=variant != LIT_CAUSTICS_DISABLED)
    frame = Bmp24Codec.read_bmp_24_rows(output)
    ReflectionImageAnalysis.validate_frame(frame)
    if frame[:2] != (1280, 900):
        raise SmokeFailure("combined caustic comparison requires the actual 1280x900 framebuffer")
    return frame


def write_report(args, frames, metrics=None):
    note = "Actual frame360 readbacks from the same static camera, sphere, ground and light. Individual typed controls separate the visible reflection, camera refraction and caustic contributions. Ground F0 remains zero. Reflection uses a fixed bright environment. "
    if args.software_ray_tracing:
        note += "Hardware ray tracing is disabled at logical-device creation. Shadow and photon caustic tracing use software BVHs; camera refraction and reflection use screen-space approximations. A fixed unlit striped backdrop supplies opaque in-screen depth and visible refractive displacement for every software toggle. This validates separate visible contributions and localization. The hardware-only geometric ray-miss/Fresnel oracle is explicitly excluded; this is not proof of equivalent offscreen transport."
    else:
        note += "Mesh-derived exterior rays that miss both sphere and ground require their predicted smooth Fresnel/environment contribution, including local patches. Secondary ray-hit shading does not claim to sample the primary-camera caustic cache."
    metadata = {"frame_source": "actual application framebuffer readback", "presentation_frame": 360, "caustic_photon_grid_divisor": args.caustic_photon_grid_divisor, "logical_device_policy": LIT_DISABLED if args.software_ray_tracing else "automatic",
        "background_fixture": "screen_refraction_stripes" if args.software_ray_tracing else "original", "optical_validation": "software_contributions" if args.software_ray_tracing else "hardware_contributions_and_exterior_radiometry",
        "variants": list(frames), "metrics": metrics, "scope": note}
    items = [(name, name, name + ".bmp", name + LIT_LOG, "Launch log", f"Actual {name} caustic scene", frame) for name, frame in frames.items()]
    style = "body{font:16px system-ui;background:#141922;color:#eee;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#8cf}pre{white-space:pre-wrap}"
    write_evidence_gallery_report(args.output_directory, manifest_name="caustic_optical_manifest.json", html_name="caustic_optical.html",
        metadata=metadata, title="Combined optical caustic scene", heading="Caustics, camera refraction and reflection",
        note=note, items=items, metrics=metrics, pending_message="Visual acceptance pending.", style=style)


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
