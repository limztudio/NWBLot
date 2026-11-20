#!/usr/bin/env python3
"""Run the hardware-validated physics stress smoke test or multi-tier performance benchmark."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
from types import SimpleNamespace
from uuid import uuid4

from smoke_failure_reporting import write_failure_report
from window_capture_outcome import WindowsDesktopUnavailable
from window_capture_smoke import (
    SKIP_EXIT_CODE,
    STRICT_LOG_FAILURE_MESSAGES,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    finish_logserver_cleanup,
    launch_logserver,
    launch_testbed,
    require_normal_process_exit,
    shutdown_logserver_and_collect,
)
from window_capture_termination import CaptureProcessTermination
from window_capture_validation import validate_expected_log_text


LIT_MAXIMUM_TIMEOUT_SECONDS = 600.0
LIT_PHYSICS_STRESS_SMOKE = "Physics stress smoke"
LIT_LOADER_PROJECT_STARTUP_COMPLETE = "Loader: project startup complete"
LIT_SHUTDOWN = "PhysicsStressSmoke: shutdown completed=1"
LIT_UTF_8 = "utf-8"

CASE_PATTERN = re.compile(
    r"PhysicsStressSmoke:\s+case=(\S+)\s+dyn_bodies=(\d+)\s+total_bodies=(\d+)\s+awake=(\d+)\s+"
    r"contacts=(\d+)\s+pairs=(\d+)\s+joints=(\d+)\s+islands=(\d+)\s+backend=(\S+)\s+"
    r"cpu_bodies=(\d+)\s+gpu_bodies=(\d+)\s+cpu_islands=(\d+)\s+gpu_islands=(\d+)\s+"
    r"avg_ms=([0-9.]+)\s+p50_ms=([0-9.]+)\s+p95_ms=([0-9.]+)\s+max_ms=([0-9.]+)\s+steps_per_sec=([0-9.]+)"
)
COMPLETED_PATTERN = re.compile(r"PhysicsStressSmoke:\s+completed\s+mode=(\S+)\s+cases=(\d+)")
DEVICE_PATTERN = re.compile(r"PhysicsStressSmoke:\s+device='([^']+)'\s+cpu_workers=(\d+)")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)


def parse_case_records(text):
    records = []
    for match in CASE_PATTERN.finditer(text):
        records.append({
            "case": match.group(1),
            "dyn_bodies": int(match.group(2)),
            "total_bodies": int(match.group(3)),
            "awake": int(match.group(4)),
            "contacts": int(match.group(5)),
            "pairs": int(match.group(6)),
            "joints": int(match.group(7)),
            "islands": int(match.group(8)),
            "backend": match.group(9),
            "cpu_bodies": int(match.group(10)),
            "gpu_bodies": int(match.group(11)),
            "cpu_islands": int(match.group(12)),
            "gpu_islands": int(match.group(13)),
            "avg_ms": float(match.group(14)),
            "p50_ms": float(match.group(15)),
            "p95_ms": float(match.group(16)),
            "max_ms": float(match.group(17)),
            "steps_per_sec": float(match.group(18)),
        })
    return records


def format_summary_table(device_name, cpu_workers, records):
    header = (
        f"Device: {device_name} | CPU Workers: {cpu_workers}\n"
        "| Case | Dyn | Awake | Contacts | Pairs | Joints | Isl | Backend | CPU b/i | GPU b/i | Avg ms | P50 ms | P95 ms | Max ms | Steps/s |\n"
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
    )
    lines = [header]
    for r in records:
        lines.append(
            f"| {r['case']} | {r['dyn_bodies']} | {r['awake']} | {r['contacts']} | {r['pairs']} | "
            f"{r['joints']} | {r['islands']} | {r['backend']} | {r['cpu_bodies']}/{r['cpu_islands']} | "
            f"{r['gpu_bodies']}/{r['gpu_islands']} | {r['avg_ms']:.3f} | {r['p50_ms']:.3f} | "
            f"{r['p95_ms']:.3f} | {r['max_ms']:.3f} | {r['steps_per_sec']:.1f} |"
        )
    return "\n".join(lines)


def validate_runtime_log(text, mode, gpu_validation):
    required = [
        "GraphicsRuntime: created device '",
        LIT_LOADER_PROJECT_STARTUP_COMPLETE,
        LIT_SHUTDOWN,
    ]
    if gpu_validation:
        required.insert(0, "Loader: GPU debug validation enabled")
    validate_expected_log_text(text, tuple(required), STRICT_LOG_FAILURE_MESSAGES)

    if text.count(LIT_SHUTDOWN) != 1:
        raise SmokeFailure("physics stress smoke requires exactly one shutdown record")

    completed_matches = list(COMPLETED_PATTERN.finditer(text))
    if len(completed_matches) != 1:
        raise SmokeFailure("physics stress smoke requires exactly one completed summary record")
    if completed_matches[0].group(1) != mode:
        raise SmokeFailure(f"expected mode '{mode}', observed '{completed_matches[0].group(1)}'")
    expected_cases = 3 if mode == "smoke" else 24
    observed_cases = int(completed_matches[0].group(2))
    if observed_cases != expected_cases:
        raise SmokeFailure(f"expected {expected_cases} completed cases, observed {observed_cases}")

    device_match = DEVICE_PATTERN.search(text)
    if device_match is None:
        raise SmokeFailure("missing PhysicsStressSmoke device summary record")

    records = parse_case_records(text)
    if len(records) != expected_cases:
        raise SmokeFailure(f"expected {expected_cases} parsed case rows, observed {len(records)}")

    return {
        "mode": mode,
        "gpu_validation": gpu_validation,
        "device": device_match.group(1),
        "cpu_workers": int(device_match.group(2)),
        "cases": records,
    }


def acquire(args, output):
    app_args = ["--gpudbg"] if args.gpu_validation else []
    launch = SimpleNamespace(
        executable=args.executable,
        working_directory=args.working_directory,
        logserver_executable=args.logserver_executable,
        no_logserver=False,
        log_port=0,
        timeout=args.timeout,
        application_arg=app_args,
        software_vulkan="off",
    )
    environment = build_launch_environment(launch)
    environment["NWB_PHYSICS_STRESS_MODE"] = args.mode
    write_json(
        output / "launch.json",
        {
            "executable": str(args.executable),
            "working_directory": str(args.working_directory),
            "logserver_executable": str(args.logserver_executable),
            "mode": args.mode,
            "gpu_validation": args.gpu_validation,
            "timeout_seconds": args.timeout,
        },
    )

    process = logserver = log_directory = None
    baseline, pattern = {}, ""
    collected = False
    try:
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch, args.executable, environment)
        if logserver is None:
            raise SmokeFailure("physics stress smoke requires the configured logserver")
        process = launch_testbed(launch, args.executable, environment, port)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure("physics stress smoke did not self-exit before timeout") from error
        code, tail = CaptureProcessTermination.terminate_process(process, LIT_PHYSICS_STRESS_SMOKE)
        process = None
        (output / "process_tail.txt").write_text(tail, encoding=LIT_UTF_8)
        require_normal_process_exit(code, tail, LIT_PHYSICS_STRESS_SMOKE)
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected = True
        (output / "runtime.log").write_text(text, encoding=LIT_UTF_8)
        result = validate_runtime_log(text, args.mode, args.gpu_validation)
        result.update(schema=1, passed=True, exit_code=code)
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = CaptureProcessTermination.terminate_process(process, LIT_PHYSICS_STRESS_SMOKE)
                (output / "process_tail.txt").write_text(tail, encoding=LIT_UTF_8)
            except Exception as error:
                errors.append(f"process cleanup: {error}")
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (output / "runtime.log").write_text(text, encoding=LIT_UTF_8)
                logserver = None
            except Exception as error:
                errors.append(f"failure log collection: {error}")
        finish_logserver_cleanup(logserver, output, errors, primary_failure, CaptureProcessTermination.terminate_process, write_json)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--mode", choices=("smoke", "benchmark"), default="smoke")
    gpu_group = parser.add_mutually_exclusive_group()
    gpu_group.add_argument("--gpu-validation", dest="gpu_validation", action="store_true")
    gpu_group.add_argument("--no-gpu-validation", dest="gpu_validation", action="store_false")
    parser.set_defaults(gpu_validation=True)
    parser.add_argument("--timeout", type=float, default=180.0)
    args = parser.parse_args(argv)
    for field in ("executable", "working_directory", "logserver_executable", "output_directory"):
        setattr(args, field, getattr(args, field).resolve())
    for field in ("executable", "logserver_executable"):
        if not getattr(args, field).is_file():
            parser.error(f"{field.replace('_', '-')} must name an existing file")
    if not args.working_directory.is_dir() or not (args.working_directory / "res").is_dir():
        parser.error("working-directory must contain the current cooked res directory")
    if not 0.0 < args.timeout <= LIT_MAXIMUM_TIMEOUT_SECONDS:
        parser.error("timeout must be greater than zero and at most 600 seconds")
    return args


def main(argv=None):
    args = parse_args(argv)
    run_name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid4().hex[:8]
    output = args.output_directory / run_name
    output.mkdir(parents=True, exist_ok=False)
    print(f"Physics stress artifacts: {output}", flush=True)
    try:
        result = acquire(args, output)
    except WindowsDesktopUnavailable as error:
        write_failure_report(write_json, output / "result.json", {"schema": 1, "passed": False, "skipped": True, "skip_reason": str(error)})
        print(f"SKIP: {error}", file=sys.stderr, flush=True)
        return SKIP_EXIT_CODE
    except (SmokeFailure, SmokeSkip, OSError) as error:
        write_failure_report(write_json, output / "result.json", {"schema": 1, "passed": False, "error": str(error)})
        print(f"FAIL: {error}", file=sys.stderr, flush=True)
        return 1
    write_json(output / "result.json", result)
    print(format_summary_table(result["device"], result["cpu_workers"], result["cases"]), flush=True)
    print(f"PASS: physics stress {result['mode']} completed ({len(result['cases'])} cases)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
