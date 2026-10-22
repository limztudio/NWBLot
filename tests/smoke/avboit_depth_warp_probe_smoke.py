#!/usr/bin/env python3
"""Run native AVBOIT and refraction probes, including specialized dense/compact indirect routes."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
from types import SimpleNamespace
from uuid import uuid4

from window_capture_smoke import (finish_logserver_cleanup, SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, WindowsDesktopUnavailable, build_launch_environment, launch_logserver, launch_testbed, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text,)

LIT_MAXIMUM_CAPTURE_TIMEOUT_SECONDS = 600.0

LIT_LOADER_PROJECT_STARTUP_COMPLETE = "Loader: project startup complete"
LIT_PASSED_CASES = "passed_cases"
LIT_UTF_8 = "utf-8"
LIT_EXECUTABLE = "executable"
LIT_WORKING_DIRECTORY = "working_directory"
LIT_LOGSERVER_EXECUTABLE = "logserver_executable"
LIT_AVBOIT_DEPTH_WARP_PROBE = "AVBOIT depth-warp probe"
LIT_PROCESS_TAIL_TXT = "process_tail.txt"
LIT_RUNTIME_LOG = "runtime.log"
LIT_RESULT_JSON = "result.json"
LIT_SCHEMA = "schema"
LIT_PASSED = "passed"


EXPECTED_COUNTS = (1050, 2550, 512, 4112)
MANIFEST = re.compile(r"AvboitDepthWarpProbe: manifest boundary=(\d+) exhaustive=(\d+) randomized=(\d+) total=(\d+) seed=0x6b47c2d1")
COMPLETION = re.compile(r"AvboitDepthWarpProbe: passed (\d+) cases with full LUT/control comparison and sentinel tails")
SHUTDOWN = "AvboitDepthWarpProbe: shutdown completed=1"
INTEGRATION_COMPLETION = "AvboitIntegrationProbe: passed 5 phases with empty, late, partial, saturation, wrapped overflow and active-to-empty clear checks"
REFRACTION_COMPLETION = "RefractionTileProbe: passed 6 phases at 2049x2049 with 66049 tiles, production finalizer, specialized dense/compact mapping, all 32 argument bytes, all-but-one sparse boundary, queue uniqueness, 65535/65536 boundary, indirect Y tail, 25190406 pixel checks and guards"
REFRACTION_PHASE = re.compile(r"RefractionTileProbe: phase (\d+) active_tiles=(\d+) compact=(\d+)x(\d+) dense=(\d+)x(\d+) pixels=(\d+) tail_groups=(\d+) edge_lanes=(\d+) argument_bytes=(\d+)")


def validate_refraction_phases(text):
    phases = list(REFRACTION_PHASE.finditer(text))
    if len(phases) != 6:
        raise SmokeFailure("refraction tile probe requires exactly six finalized route records")
    counts = (66049, 0, 6, 65535, 65536, 66048)
    sparse = (0, 256, 256 * 257, 66048, 65534, 65535)
    for phase, match in enumerate(phases):
        count = counts[phase]
        dense = phase == 0
        compact_x = 0 if dense else min(count, 65535)
        compact_y = 1 if dense else max(1, (count + 65534) // 65535)
        dense_x, dense_y = (257, 257) if dense else (0, 1)
        active = range(count) if phase not in (2, 5) else sparse if phase == 2 else (tile for tile in range(66049) if tile != 33024)
        pixels = sum(min(8, 2049 - (tile % 257) * 8) * min(8, 2049 - (tile // 257) * 8) for tile in active)
        groups = compact_x * compact_y + dense_x * dense_y
        expected = (phase, count, compact_x, compact_y, dense_x, dense_y, pixels, groups - count, count * 64 - pixels, 32)
        if tuple(map(int, match.groups())) != expected:
            raise SmokeFailure(f"refraction tile probe phase {phase} does not match its finalized route, pixel, tail and argument contract")
    return phases


def validate_runtime_log(text):
    validate_expected_log_text(text, ("Loader: GPU debug validation enabled", "GraphicsRuntime: created device '", LIT_LOADER_PROJECT_STARTUP_COMPLETE,
            SHUTDOWN,), STRICT_LOG_FAILURE_MESSAGES,)
    manifests = list(MANIFEST.finditer(text))
    completions = list(COMPLETION.finditer(text))
    if len(manifests) != 1 or tuple(map(int, manifests[0].groups())) != EXPECTED_COUNTS:
        raise SmokeFailure("depth-warp probe requires exactly one matching 4112-case manifest")
    if len(completions) != 1 or int(completions[0][1]) != EXPECTED_COUNTS[-1]:
        raise SmokeFailure("depth-warp probe requires exactly one complete 4112-case success record")
    if text.count(SHUTDOWN) != 1:
        raise SmokeFailure("depth-warp probe requires exactly one successful shutdown record")
    if text.count(INTEGRATION_COMPLETION) != 1:
        raise SmokeFailure("AVBOIT integration probe requires its complete five-phase volume comparison")
    if text.count(REFRACTION_COMPLETION) != 1:
        raise SmokeFailure("refraction tile probe requires all six finalized dense, sparse, queue and pixel-visit comparisons")
    refraction_phases = validate_refraction_phases(text)
    if refraction_phases[-1].end() >= text.index(REFRACTION_COMPLETION):
        raise SmokeFailure("refraction tile probe completion precedes its six route records")
    startup = text.index(LIT_LOADER_PROJECT_STARTUP_COMPLETE)
    shutdown = text.index(SHUTDOWN)
    if not manifests[0].start() < startup < completions[0].start() < shutdown:
        raise SmokeFailure("depth-warp probe manifest, startup, completion and shutdown order is invalid")
    return { "boundary_cases": EXPECTED_COUNTS[0], "exhaustive_cases": EXPECTED_COUNTS[1], "randomized_cases": EXPECTED_COUNTS[2], LIT_PASSED_CASES: EXPECTED_COUNTS[3], "integration_phases": 5, "refraction_tile_phases": 6, "refraction_tile_pixel_checks": 25190406, "refraction_argument_bytes": 32, "refraction_finalizer_and_both_specialized_variants": True, "gpu_validation_enabled": True, "full_output_and_sentinel_validation": True, }


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding=LIT_UTF_8)


def acquire(args, output):
    launch = SimpleNamespace(executable=args.executable, working_directory=args.working_directory, logserver_executable=args.logserver_executable, no_logserver=False, log_port=0, timeout=args.timeout, application_arg=["--gpudbg"], software_vulkan="off",)
    environment = build_launch_environment(launch)
    write_json(output / "launch.json", { LIT_EXECUTABLE: str(args.executable), LIT_WORKING_DIRECTORY: str(args.working_directory), LIT_LOGSERVER_EXECUTABLE: str(args.logserver_executable), "application_args": launch.application_arg, "timeout_seconds": args.timeout,
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
        code, tail = terminate_process(process, LIT_AVBOIT_DEPTH_WARP_PROBE)
        process = None
        (output / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
        require_normal_process_exit(code, tail, LIT_AVBOIT_DEPTH_WARP_PROBE)
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected = True
        (output / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
        result = validate_runtime_log(text)
        result.update(schema=1, passed=True, exit_code=code)
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = terminate_process(process, LIT_AVBOIT_DEPTH_WARP_PROBE)
                (output / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
            except Exception as error:
                errors.append(f"probe cleanup: {error}")
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (output / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
                logserver = None
            except Exception as error:
                errors.append(f"failure log collection: {error}")
        finish_logserver_cleanup(logserver, output, errors, primary_failure, terminate_process, write_json)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args(argv)
    for field in (LIT_EXECUTABLE, LIT_WORKING_DIRECTORY, LIT_LOGSERVER_EXECUTABLE, "output_directory"):
        setattr(args, field, getattr(args, field).resolve())
    for field in (LIT_EXECUTABLE, LIT_LOGSERVER_EXECUTABLE):
        if not getattr(args, field).is_file():
            parser.error(f"{field.replace('_', '-')} must name an existing file")
    if not args.working_directory.is_dir() or not (args.working_directory / "res").is_dir():
        parser.error("working-directory must contain the current cooked res directory")
    if not 0.0 < args.timeout <= LIT_MAXIMUM_CAPTURE_TIMEOUT_SECONDS:
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
    except WindowsDesktopUnavailable as error:
        write_json(output / LIT_RESULT_JSON, {LIT_SCHEMA: 1, LIT_PASSED: False, "skipped": True, "skip_reason": str(error)})
        print(f"SKIP: {error}", file=sys.stderr, flush=True)
        return SKIP_EXIT_CODE
    except (SmokeFailure, SmokeSkip, OSError) as error:
        # Missing validation/device support still fails this native qualification.
        write_json(output / LIT_RESULT_JSON, {LIT_SCHEMA: 1, LIT_PASSED: False, "error": str(error)})
        print(f"FAIL: {error}", file=sys.stderr, flush=True)
        return 1
    write_json(output / LIT_RESULT_JSON, result)
    print(f"PASS: all {result[LIT_PASSED_CASES]} native depth-warp cases matched", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
