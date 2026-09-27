#!/usr/bin/env python3
"""Capture real CSG shadows and compare holes, thickness, overlap, movement and camera independence."""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys

from csg_shadow_reference import ANALYTIC_ARMS, ARMS, compare_analytic_frames, compare_frames
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


def capture_environment(arm, light, atlas="boxes", cadence="every_frame", map_resolution_divisor=1):
    environment = dict(os.environ)
    for key in tuple(environment):
        if key.startswith(("NWB_CSG_SHADOW_", "NWB_SOFTWARE_SHADOW_", "NWB_SHADOW_")):
            environment.pop(key)
    environment.update({
        "NWB_CSG_SHADOW_ARM": arm,
        "NWB_CSG_SHADOW_LIGHT": light,
        "NWB_CSG_SHADOW_ATLAS": atlas,
        "NWB_SOFTWARE_SHADOW_BACKEND": "automatic",
        "NWB_SOFTWARE_SHADOW_COVERAGE": "reference",
        "NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH": "reference_grid9",
        "NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE": cadence,
        "NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION": str(512 // map_resolution_divisor),
        "NWB_SOFTWARE_SHADOW_POINT_RESOLUTION": str(256 // map_resolution_divisor),
        "NWB_SOFTWARE_SHADOW_BUDGET_MIB": "256",
        "NWB_SHADOW_TRANSPARENT_SAMPLING": "reference_three",
        "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667",
    })
    environment.pop("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", None)
    environment.pop("NWB_GPU_TIMING_FILE", None)
    return environment


def capture_arm(args, arm):
    output = args.output_directory / (arm + ".bmp")
    log = output.with_suffix(".log")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        "--timeout", str(args.timeout), "--log-output", str(log),
        "--expect-log-message", "CsgShadowSmokeProject: shutdown",
        "--expect-log-message", f"CsgShadowSmokeProject: atlas arm={ARMS.index(arm)} light={args.light} hardware={int(args.route == 'hardware')}",
        "--expect-log-message", "caster_z_max=-5 receiver_z=0",
        "--expect-log-message", "SoftwareShadowSmoke: requested backend=0",
        "--application-arg=--gpudbg"]
    if args.atlas == "analytic":
        command += ["--expect-log-message", "CsgShadowSmokeProject: analytic atlas tiles=6 shapes=plane,sphere,capsule"]
    if arm == "moved":
        command += ["--expect-log-message", "CsgShadowSmokeProject: cutters moved at update40"]
    if args.route == "software":
        command += ["--application-arg=--disable-hardware-ray-tracing",
            "--expect-log-message", "RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0",
            "--reject-log-message", "RendererSystem: dispatched hardware transparent shadow traversal"]
        if arm in ("reference", "uncut"):
            command += ["--expect-log-message", "RendererSystem: dispatched light-space shadow maps"]
    elif args.atlas == "analytic" and arm == "cut":
        command += ["--reject-log-message", "RendererSystem: dispatched hardware transparent shadow traversal"]
    else:
        command += ["--expect-log-message", "RendererSystem: dispatched hardware transparent shadow traversal"]
    if arm not in ("reference", "uncut"):
        command += ["--expect-log-message", "RendererSystem: dispatched CSG light-space shadows (hardware_compose="
            + ("1" if args.route == "hardware" else "0")]
    if args.capture_cadence == "reuse_one_frame" and (args.route == "software" or arm not in ("reference", "uncut")):
        command += ["--expect-log-message", "RendererSystem: accepted light-space capture reuse (cadence=2)"]
    for message in ("[ERROR]", "VUID-", "Validation Error", "failed to resolve shader", "retaining all-lit visibility",
        "preserving opaque visibility", "ray-traced shadow visibility pass failed"):
        command += ["--reject-log-message", message]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing CSG shadow {args.route}/{args.light}/{args.atlas}/{arm}...", flush=True)
    environment = capture_environment(arm, args.light, args.atlas, args.capture_cadence, args.map_resolution_divisor)
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + 45.0)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"CSG shadow {arm} capture failed: {result.returncode}")
    frame = read_bmp_24_rows(output)
    if frame[:2] != (960, 720):
        raise SmokeFailure("CSG shadow smoke requires the actual 960x720 framebuffer")
    return frame


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--route", choices=("hardware", "software"), required=True)
    parser.add_argument("--light", choices=("directional", "point"), default="directional")
    parser.add_argument("--atlas", choices=("boxes", "analytic"), default="boxes")
    parser.add_argument("--capture-cadence", choices=("every_frame", "reuse_one_frame"), default="every_frame")
    parser.add_argument("--map-resolution-divisor", type=int, choices=(1, 2, 4), default=1)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames < 100 or not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("frames must be at least 100 (60 after cutter motion), and timeout must be finite and positive")
    if args.atlas == "analytic" and args.light != "directional":
        parser.error("the analytic atlas requires a directional light")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        frames = {}
        for arm in ANALYTIC_ARMS if args.atlas == "analytic" else ARMS:
            frame = capture_arm(args, arm)
            if frame is None:
                return SKIP_EXIT_CODE
            frames[arm] = frame
        metrics = compare_analytic_frames(frames) if args.atlas == "analytic" else compare_frames(frames, args.light)
        metrics["atlas"] = args.atlas
        metrics["route"] = args.route
        metrics["capture_cadence"] = args.capture_cadence
        metrics["map_resolution_divisor"] = args.map_resolution_divisor
        (args.output_directory / "result.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
        if args.atlas == "analytic":
            print("PASS: CSG shadows match analytic plane, ellipsoid, capsule and cutter-union optical lengths")
        else:
            print("PASS: CSG shadows match carved geometry, thickness, overlap, cutter motion and camera shift")
        return 0
    except (SmokeFailure, subprocess.TimeoutExpired) as error:
        (args.output_directory / "failure.json").write_text(json.dumps({"error": str(error)}, indent=2) + "\n", encoding="utf-8")
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
