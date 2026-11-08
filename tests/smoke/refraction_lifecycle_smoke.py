#!/usr/bin/env python3
"""Validate refraction tile retirement, movement, edge coverage, and reactivation on real frames."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from refraction_lifecycle_analysis import PHASES, RefractionLifecycleAnalysis
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure


def capture_route(args, name, hardware):
    directory = args.output_directory / name
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / "lifecycle.bmp"
    log_path = directory / "runtime.log"
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--log-output", str(log_path), "--application-capture", "--application-capture-frame-count", str(args.frames), "--timeout", str(args.timeout),
        "--expect-log-message", "RefractionLifecycle: complete samples=7", "--expect-log-message", "RefractionSmokeProject: shutdown", "--expect-log-message",
        "AVBOIT refraction resolve: hardware" if args.require_hardware and hardware else "AVBOIT refraction resolve:" if hardware else "AVBOIT refraction resolve: screen-space"]
    command += ["--logserver-executable", str(args.logserver_executable)] if args.logserver_executable else ["--no-logserver"]
    for argument in args.application_arg:
        command.append("--application-arg=" + argument)
    env = os.environ.copy()
    for key in ("NWB_REFRACTION_SMOKE_CASE", "NWB_REFRACTION_SMOKE_GEOMETRY", "NWB_REFRACTION_SMOKE_TIMING", "NWB_REFRACTION_SMOKE_DENSITY", "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"):
        env.pop(key, None)
    env.update(NWB_REFRACTION_SMOKE_LIFECYCLE="1", NWB_REFRACTION_SMOKE_EXTENT="npot", NWB_REFRACTION_SMOKE_ENABLED="1", NWB_REFRACTION_SMOKE_HARDWARE="1" if hardware else "0",
        NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS="0.016666667")
    result = subprocess.run(command, env=env, check=False, timeout=args.timeout + 30)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode != 0:
        raise SmokeFailure(f"{name} lifecycle capture failed with exit {result.returncode}")
    log = log_path.read_text(encoding="utf-8")
    frame_indices = RefractionLifecycleAnalysis.validate_lifecycle_log(log)
    routes = set(re.findall(r"^AVBOIT refraction resolve: (hardware|screen-space)$", log.replace("\r\n", "\n"), re.MULTILINE))
    if len(routes) != 1:
        raise SmokeFailure("lifecycle requires one unambiguous runtime refraction route")
    trace_route = routes.pop()
    frames = {phase: read_bmp_24_rows(output if phase == PHASES[-1] else Path(str(output) + f".{phase}.bmp")) for phase in PHASES}
    return {"graphics_source_frames": frame_indices, "trace_route": trace_route, "images": RefractionLifecycleAnalysis.analyze_lifecycle(frames, trace_route)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=150)
    parser.add_argument("--require-hardware", action="store_true")
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        metrics = {}
        for name, hardware in (("automatic", True), ("screen_space", False)):
            metrics[name] = capture_route(args, name, hardware)
            if metrics[name] is None:
                return SKIP_EXIT_CODE
        (args.output_directory / "lifecycle_metrics.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
        print("PASS: refraction movement, partial tiles, stale-output retirement, disabling, reactivation, and AVBOIT foreground preservation")
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
