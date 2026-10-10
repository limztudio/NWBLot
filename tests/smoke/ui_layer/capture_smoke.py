#!/usr/bin/env python3
"""Run the UI-only application, capture real presentation output, and verify SDR pixels."""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys

from capture_reference import analyze_frame, failure_description


SMOKE_DIRECTORY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SMOKE_DIRECTORY))

from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows  # noqa: E402
from fixture_environment import build_fixture_environment


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--mode", choices=("framebuffer", "resize"), default="framebuffer")
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--application-arg", action="append", default=[])
    parser.add_argument("--gpu-validation", action=argparse.BooleanOptionalAction, default=True,
        help="Request graphics validation; use --no-gpu-validation for a fin application.")
    args = parser.parse_args(argv)
    if args.frames < 3:
        parser.error("--frames must be at least 3 so the empty startup frames precede painted capture")
    if not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("--timeout must be finite and positive")
    return args


def capture_command(args, output):
    command = [
        sys.executable, str(SMOKE_DIRECTORY / "window_capture_smoke.py"),
        "--executable", str(args.executable.resolve()),
        "--working-directory", str(args.working_directory.resolve()),
        "--output", str(output),
        "--window-title", "NWB UI Layer Smoke",
        "--timeout", str(args.timeout),
        "--expect-log-message", "Loader: project startup complete",
        "--expect-log-message", "UiLayerSmokeProject: standalone layer ready; default atlas; SDR; empty startup frames=2",
        "--expect-log-message", "UiLayerSmokeProject: deterministic solid, skin, alpha and nested clip geometry submitted",
        "--expect-log-message", "UiLayerSmokeProject: shutdown",
        "--gpu-validation" if args.gpu_validation else "--no-gpu-validation",
    ]
    if args.mode == "framebuffer":
        command += ["--application-capture", "--application-capture-frame-count", str(args.frames),
            "--log-output", str(output.with_suffix(".log"))]
    else:
        command += ["--resize-client", "800", "600", "--settle-seconds", "1.0", "--resize-settle-seconds", "1.0"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable.resolve())]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    return command


def run(args):
    args.output_directory = args.output_directory.resolve()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    output = args.output_directory / "ui_layer.bmp"
    environment = build_fixture_environment(os.environ)
    result = subprocess.run(capture_command(args, output), env=environment, check=False, timeout=args.timeout + 45.0)
    if result.returncode == SKIP_EXIT_CODE:
        return SKIP_EXIT_CODE
    if result.returncode:
        raise SmokeFailure(f"UI layer {args.mode} capture failed with exit {result.returncode}")

    capture_paths = [output]
    expected_extents = [(960, 540)]
    if args.mode == "resize":
        capture_paths.insert(0, output.with_name(output.stem + ".before-resize" + output.suffix))
        expected_extents.append((800, 600))
    reports = []
    for path, expected_extent in zip(capture_paths, expected_extents):
        frame = read_bmp_24_rows(path)
        if frame[:2] != expected_extent:
            raise SmokeFailure(f"UI capture '{path.name}' has extent {frame[:2]}, expected {expected_extent}")
        report = analyze_frame(frame)
        report["capture"] = str(path)
        reports.append(report)
    (args.output_directory / "pixels.json").write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
    failures = [f"{Path(report['capture']).name}: {failure_description(report)}" for report in reports if not report["passed"]]
    if failures:
        raise SmokeFailure("; ".join(failures))
    print(f"UI layer {args.mode}: {len(reports)} captures, {sum(len(report['probes']) for report in reports)} pixel probes passed", flush=True)
    return 0


def main(argv):
    args = parse_args(argv)
    try:
        return run(args)
    except (SmokeFailure, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
