#!/usr/bin/env python3
"""Run the cooked AVBOIT depth-warp boundary probe through the normal loader."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
from types import SimpleNamespace
from uuid import uuid4

from window_capture_smoke import (
    STRICT_LOG_FAILURE_MESSAGES,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    launch_logserver,
    launch_testbed,
    require_normal_process_exit,
    shutdown_logserver_and_collect,
    terminate_process,
    validate_expected_log_text,
)


EXPECTED_COUNTS = (1050, 2550, 512, 4112)
MANIFEST = re.compile(
    r"AvboitDepthWarpProbe: manifest boundary=(\d+) exhaustive=(\d+) randomized=(\d+) total=(\d+) seed=0x6b47c2d1"
)
COMPLETION = re.compile(
    r"AvboitDepthWarpProbe: passed (\d+) cases with full LUT/control comparison and sentinel tails"
)
SHUTDOWN = "AvboitDepthWarpProbe: shutdown completed=1"


def validate_runtime_log(text):
    validate_expected_log_text(
        text,
        (
            "Loader: GPU debug validation enabled",
            "Vulkan GPU debug: instance setup",
            "validation layer enabled: yes",
            "debug utils extension enabled: yes",
            "GraphicsRuntime: created device '",
            "Loader: project startup complete",
            SHUTDOWN,
        ),
        STRICT_LOG_FAILURE_MESSAGES,
    )
    manifests = list(MANIFEST.finditer(text))
    completions = list(COMPLETION.finditer(text))
    if len(manifests) != 1 or tuple(map(int, manifests[0].groups())) != EXPECTED_COUNTS:
        raise SmokeFailure("depth-warp probe requires exactly one matching 4112-case manifest")
    if len(completions) != 1 or int(completions[0][1]) != EXPECTED_COUNTS[-1]:
        raise SmokeFailure("depth-warp probe requires exactly one complete 4112-case success record")
    if text.count(SHUTDOWN) != 1:
        raise SmokeFailure("depth-warp probe requires exactly one successful shutdown record")
    startup = text.index("Loader: project startup complete")
    shutdown = text.index(SHUTDOWN)
    if not manifests[0].start() < startup < completions[0].start() < shutdown:
        raise SmokeFailure("depth-warp probe manifest, startup, completion and shutdown order is invalid")
    return {
        "boundary_cases": EXPECTED_COUNTS[0],
        "exhaustive_cases": EXPECTED_COUNTS[1],
        "randomized_cases": EXPECTED_COUNTS[2],
        "passed_cases": EXPECTED_COUNTS[3],
        "gpu_validation_enabled": True,
        "full_output_and_sentinel_validation": True,
    }


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def acquire(args, output):
    launch = SimpleNamespace(
        executable=args.executable,
        working_directory=args.working_directory,
        logserver_executable=args.logserver_executable,
        no_logserver=False,
        log_port=0,
        timeout=args.timeout,
        application_arg=["--gpudbg"],
        software_vulkan="off",
    )
    environment = build_launch_environment(launch)
    write_json(output / "launch.json", {
        "executable": str(args.executable),
        "working_directory": str(args.working_directory),
        "logserver_executable": str(args.logserver_executable),
        "application_args": launch.application_arg,
        "timeout_seconds": args.timeout,
    })
    process = logserver = log_directory = None
    baseline, pattern = {}, ""
    collected = False
    try:
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch, args.executable, environment)
        if logserver is None:
            raise SmokeFailure("depth-warp probe requires the configured logserver")
        process = launch_testbed(launch, args.executable, environment, port)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure("depth-warp probe did not self-exit before timeout") from error
        code, tail = terminate_process(process, "AVBOIT depth-warp probe")
        process = None
        (output / "process_tail.txt").write_text(tail, encoding="utf-8")
        require_normal_process_exit(code, tail, "AVBOIT depth-warp probe")
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected = True
        (output / "runtime.log").write_text(text, encoding="utf-8")
        result = validate_runtime_log(text)
        result.update(schema=1, passed=True, exit_code=code)
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = terminate_process(process, "AVBOIT depth-warp probe")
                (output / "process_tail.txt").write_text(tail, encoding="utf-8")
            except Exception as error:
                errors.append(f"probe cleanup: {error}")
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (output / "runtime.log").write_text(text, encoding="utf-8")
                logserver = None
            except Exception as error:
                errors.append(f"failure log collection: {error}")
        try:
            terminate_process(logserver, "logserver")
        except Exception as error:
            errors.append(f"logger cleanup: {error}")
        if errors:
            write_json(output / "cleanup_error.json", {"errors": errors})
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args(argv)
    for field in ("executable", "working_directory", "logserver_executable", "output_directory"):
        setattr(args, field, getattr(args, field).resolve())
    for field in ("executable", "logserver_executable"):
        if not getattr(args, field).is_file():
            parser.error(f"{field.replace('_', '-')} must name an existing file")
    if not args.working_directory.is_dir() or not (args.working_directory / "res").is_dir():
        parser.error("working-directory must contain the current cooked res directory")
    if not 0.0 < args.timeout <= 600.0:
        parser.error("timeout must be greater than zero and at most 600 seconds")
    return args


def main(argv=None):
    args = parse_args(argv)
    run_name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid4().hex[:8]
    output = args.output_directory / run_name
    output.mkdir(parents=True, exist_ok=False)
    print(f"AVBOIT depth-warp probe artifacts: {output}", flush=True)
    try:
        result = acquire(args, output)
    except (SmokeFailure, SmokeSkip, OSError) as error:
        # Missing display/validation/device support fails this native qualification instead of producing a skip.
        write_json(output / "result.json", {"schema": 1, "passed": False, "error": str(error)})
        print(f"FAIL: {error}", file=sys.stderr, flush=True)
        return 1
    write_json(output / "result.json", result)
    print(f"PASS: all {result['passed_cases']} native depth-warp cases matched", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
