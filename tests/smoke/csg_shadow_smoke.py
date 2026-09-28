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

# Shared literals (no inline hardcodes below this block).
LIT_REFERENCE_GRID9 = "reference_grid9"
LIT_BOXES = "boxes"
LIT_EVERY_FRAME = "every_frame"
LIT_HARD = "hard"
LIT_REFERENCE = "reference"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_ANALYTIC = "analytic"
LIT_SOFTWARE = "software"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_UNCUT = "uncut"
LIT_HARDWARE = "hardware"
LIT_REUSE_ONE_FRAME = "reuse_one_frame"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_DIRECTIONAL = "directional"
LIT_N = "\n"
LIT_UTF_8 = "utf-8"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


BLOCKER_SEARCH = {LIT_REFERENCE_GRID9: 0, "compact_cross5": 1, "center1": 2}


def capture_environment(arm, light, atlas=LIT_BOXES, cadence=LIT_EVERY_FRAME, map_resolution_divisor=1,
    blocker_search=LIT_REFERENCE_GRID9, light_source=LIT_HARD):
    environment = dict(os.environ)
    for key in tuple(environment):
        if key.startswith(("NWB_CSG_SHADOW_", "NWB_SOFTWARE_SHADOW_", "NWB_SHADOW_")):
            environment.pop(key)
    environment.update({
        "NWB_CSG_SHADOW_ARM": arm,
        "NWB_CSG_SHADOW_LIGHT": light,
        "NWB_CSG_SHADOW_LIGHT_SOURCE": light_source,
        "NWB_CSG_SHADOW_ATLAS": atlas,
        "NWB_SOFTWARE_SHADOW_BACKEND": "automatic",
        "NWB_SOFTWARE_SHADOW_COVERAGE": LIT_REFERENCE,
        "NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH": blocker_search,
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
        LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        LIT_TIMEOUT, str(args.timeout), "--log-output", str(log),
        LIT_EXPECT_LOG_MESSAGE, "CsgShadowSmokeProject: shutdown",
        LIT_EXPECT_LOG_MESSAGE, f"CsgShadowSmokeProject: atlas arm={ARMS.index(arm)} light={args.light} hardware={int(args.route == 'hardware')}",
        LIT_EXPECT_LOG_MESSAGE, "caster_z_max=-5 receiver_z=0",
        LIT_EXPECT_LOG_MESSAGE, "SoftwareShadowSmoke: requested backend=0 directional_resolution="
            + f"{512 // args.map_resolution_divisor} point_resolution={256 // args.map_resolution_divisor} "
            + f"budget_bytes=268435456 coverage=0 blocker_search={BLOCKER_SEARCH[args.blocker_search]} "
            + f"capture_cadence={('every_frame', 'reuse_one_frame', 'reuse_two_frames').index(args.capture_cadence)}",
        LIT_EXPECT_LOG_MESSAGE, f"CsgShadowSmokeProject: light_source={args.light_source} "
            + f"angular_radius={0.005 if args.light_source == 'finite' and args.light == 'directional' else 0.0:.3f} "
            + f"source_radius={0.02 if args.light_source == 'finite' and args.light == 'point' else 0.0:.3f}",
        "--application-arg=--gpudbg"]
    if args.atlas == LIT_ANALYTIC:
        command += [LIT_EXPECT_LOG_MESSAGE, "CsgShadowSmokeProject: analytic atlas tiles=6 shapes=plane,sphere,capsule"]
    if arm == "moved":
        command += [LIT_EXPECT_LOG_MESSAGE, "CsgShadowSmokeProject: cutters moved at update40"]
    if args.route == LIT_SOFTWARE:
        command += ["--application-arg=--disable-hardware-ray-tracing",
            LIT_EXPECT_LOG_MESSAGE, "RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0",
            LIT_REJECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA]
        if arm in (LIT_REFERENCE, LIT_UNCUT):
            command += [LIT_EXPECT_LOG_MESSAGE, "RendererSystem: dispatched light-space shadow maps"]
    elif args.atlas == LIT_ANALYTIC and arm == "cut":
        command += [LIT_REJECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA]
    else:
        command += [LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA]
    if arm not in (LIT_REFERENCE, LIT_UNCUT):
        command += [LIT_EXPECT_LOG_MESSAGE, "RendererSystem: dispatched CSG light-space shadows (hardware_compose="
            + ("1" if args.route == LIT_HARDWARE else "0")]
    if args.capture_cadence != LIT_EVERY_FRAME and (args.route == LIT_SOFTWARE or arm not in (LIT_REFERENCE, LIT_UNCUT)):
        cadence = 2 if args.capture_cadence == LIT_REUSE_ONE_FRAME else 3
        other_cadence = 3 if cadence == 2 else 2
        command += [LIT_EXPECT_LOG_MESSAGE, f"RendererSystem: accepted light-space capture reuse (cadence={cadence})",
            LIT_REJECT_LOG_MESSAGE, f"RendererSystem: accepted light-space capture reuse (cadence={other_cadence})"]
    for message in ("[ERROR]", "VUID-", "Validation Error", "failed to resolve shader", "retaining all-lit visibility",
        "preserving opaque visibility", "ray-traced shadow visibility pass failed"):
        command += [LIT_REJECT_LOG_MESSAGE, message]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing CSG shadow {args.route}/{args.light}/{args.atlas}/{arm}...", flush=True)
    environment = capture_environment(arm, args.light, args.atlas, args.capture_cadence, args.map_resolution_divisor,
        args.blocker_search, args.light_source)
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
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--route", choices=(LIT_HARDWARE, LIT_SOFTWARE), required=True)
    parser.add_argument("--light", choices=(LIT_DIRECTIONAL, "point"), default=LIT_DIRECTIONAL)
    parser.add_argument("--light-source", choices=(LIT_HARD, "finite"), default=LIT_HARD)
    parser.add_argument("--blocker-search", choices=tuple(BLOCKER_SEARCH), default=LIT_REFERENCE_GRID9,
        help="Center1 estimates penumbra width from the fully checked central blocker only.")
    parser.add_argument("--atlas", choices=(LIT_BOXES, LIT_ANALYTIC), default=LIT_BOXES)
    parser.add_argument("--capture-cadence", choices=(LIT_EVERY_FRAME, LIT_REUSE_ONE_FRAME, "reuse_two_frames"), default=LIT_EVERY_FRAME)
    parser.add_argument("--map-resolution-divisor", type=int, choices=(1, 2, 4), default=1)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument(LIT_TIMEOUT, type=float, default=90.0)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    args = parser.parse_args(argv)
    if args.frames < 100 or not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("frames must be at least 100 (60 after cutter motion), and timeout must be finite and positive")
    if args.atlas == LIT_ANALYTIC and args.light != LIT_DIRECTIONAL:
        parser.error("the analytic atlas requires a directional light")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        frames = {}
        for arm in ANALYTIC_ARMS if args.atlas == LIT_ANALYTIC else ARMS:
            frame = capture_arm(args, arm)
            if frame is None:
                return SKIP_EXIT_CODE
            frames[arm] = frame
        metrics = compare_analytic_frames(frames) if args.atlas == LIT_ANALYTIC else compare_frames(frames, args.light)
        metrics["atlas"] = args.atlas
        metrics["route"] = args.route
        metrics["light_source"] = args.light_source
        metrics["blocker_search"] = args.blocker_search
        metrics["capture_cadence"] = args.capture_cadence
        metrics["map_resolution_divisor"] = args.map_resolution_divisor
        (args.output_directory / "result.json").write_text(json.dumps(metrics, indent=2) + LIT_N, encoding=LIT_UTF_8)
        if args.atlas == LIT_ANALYTIC:
            print("PASS: CSG shadows match analytic plane, ellipsoid, capsule and cutter-union optical lengths")
        else:
            print("PASS: CSG shadows match carved geometry, thickness, overlap, cutter motion and camera shift")
        return 0
    except (SmokeFailure, subprocess.TimeoutExpired) as error:
        (args.output_directory / "failure.json").write_text(json.dumps({"error": str(error)}, indent=2) + LIT_N, encoding=LIT_UTF_8)
        print(str(error), file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
