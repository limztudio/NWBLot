#!/usr/bin/env python3
"""Qualify retained/clipped CSG open shadows and the explicit 64/65 event boundary."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import tempfile

from csg_shadow_smoke import capture_environment
from csg_shadow_open_evidence import CASES, CsgOpenShadowImageEvidence, CsgOpenShadowRuntimeEvidence
import window_capture_smoke as capture
from window_capture_arguments import WindowCaptureArguments
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, SmokeSkip

LIT_WARNING_TAG = "[WARNING]"

LIT_ENABLED = "enabled"
LIT_HARDWARE = "hardware"
LIT_SOFTWARE = "software"
LIT_DISABLED = "disabled"
LIT_TERMINAL65 = "terminal65"
LIT_ROUTE = "route"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_UTF_8 = "utf-8"
LIT_STATUS = "status"


def capture_case(args, case):
    output = args.output_directory / (case + ".bmp")
    runtime_log = output.with_suffix(".log")
    command = [LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory), "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        LIT_TIMEOUT, str(args.timeout), "--log-output", str(runtime_log), LIT_GPU_VALIDATION if args.gpu_validation else "--no-gpu-validation"]
    if args.logserver_executable:
        command.extend((LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)))
    else:
        command.append("--no-logserver")
    if args.route == LIT_SOFTWARE:
        command.append("--application-arg=--disable-hardware-ray-tracing")
    else:
        command.extend(("--skip-log-message", "atlas arm=1 light=directional hardware=0"))
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    capture_args = WindowCaptureArguments(capture.STRICT_LOG_FAILURE_MESSAGES).parse_args(command)
    if case == LIT_TERMINAL65 and args.warning_policy == LIT_ENABLED:
        # Only the warning-enabled terminal arm defers WARNING validation to the exact single flags=16 check below.
        capture_args.reject_log_message.remove(LIT_WARNING_TAG)
    environment = capture_environment("cut", "directional", "open")
    for key in tuple(environment):
        if key.startswith(("NWB_CAUSTIC_", "NWB_REFLECTION_", "NWB_REFRACTION_", "NWB_CSG_CAUSTIC_")):
            environment.pop(key)
    environment["NWB_CSG_SHADOW_OPEN_CASE"] = case
    previous_environment = dict(os.environ)
    print(f"Capturing CSG open shadows {args.route}/{case}...", flush=True)
    try:
        os.environ.clear()
        os.environ.update(environment)
        capture_result = capture.launch_and_capture_application(capture_args)
        if (capture_result.width, capture_result.height) != (960, 720):
            raise SmokeFailure("open-shadow native capture has the wrong framebuffer extent")
    finally:
        os.environ.clear()
        os.environ.update(previous_environment)
    receipt = CsgOpenShadowRuntimeEvidence.validate_runtime(runtime_log.read_text(encoding=LIT_UTF_8), case, args.route, args.frames, args.warning_policy)
    frame = read_bmp_24_rows(output)
    if frame[:2] != (960, 720):
        raise SmokeFailure("open-shadow smoke requires the actual 960x720 framebuffer")
    receipt["bmp_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
    receipt["log_sha256"] = hashlib.sha256(runtime_log.read_bytes()).hexdigest()
    return frame, receipt


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument("--output-directory", type=Path, help="Use the exact evidence directory with the existing freshness checks.")
    output.add_argument("--output-root", type=Path, help="Preserve prior evidence and create a unique child directory for this run.")
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--route", choices=(LIT_HARDWARE, LIT_SOFTWARE), required=True)
    parser.add_argument("--warning-policy", choices=(LIT_ENABLED, LIT_DISABLED), required=True, help="Build warning-log capability; canonical fin disables ordinary warnings.")
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument(LIT_TIMEOUT, type=float, default=120.)
    parser.add_argument("--application-arg", action="append", default=[])
    parser.add_argument(LIT_GPU_VALIDATION, action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args(argv)
    if args.frames < 120 or not math.isfinite(args.timeout) or args.timeout <= 0.:
        parser.error("at least 120 capture frames and a finite positive timeout are required")
    if args.output_directory is not None:
        args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    if args.output_root is not None:
        root = args.output_root.resolve()
        root.mkdir(parents=True, exist_ok=True)
        args.output_directory = Path(tempfile.mkdtemp(prefix="csg_open_shadow_", dir=root))
        print(f"Open-shadow capture evidence: {args.output_directory}", flush=True)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    if any(args.output_directory.iterdir()):
        print("open-shadow output directory must be fresh", file=sys.stderr)
        return 1
    try:
        frames, receipts = {}, {}
        for case in CASES:
            frames[case], receipts[case] = capture_case(args, case)
        result = CsgOpenShadowImageEvidence.compare_frames(frames)
        result.update({LIT_STATUS: "passed", LIT_ROUTE: args.route, "capture_count": len(receipts), "captures": receipts, "limits": "source receipts prove completed static captures and selected route; no per-ray diagnostic attribution or performance claim"})
        (args.output_directory / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding=LIT_UTF_8)
        print("PASS: retained/clipped five/six-sheet optics, exact 64 capacity, and conservative 65 terminal")
        return 0
    except SmokeSkip as error:
        (args.output_directory / "skip.json").write_text(json.dumps({LIT_STATUS: "skipped", "reason": str(error)}, indent=2) + "\n", encoding=LIT_UTF_8)
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError) as error:
        (args.output_directory / "failure.json").write_text(json.dumps({LIT_STATUS: "failed", "error": str(error)}, indent=2) + "\n", encoding=LIT_UTF_8)
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
