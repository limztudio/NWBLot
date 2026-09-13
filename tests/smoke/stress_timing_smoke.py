#!/usr/bin/env python3
"""One self-exiting stress presentation measurement using the existing runtime process owners."""

import argparse
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace

import renderer_ab_benchmark as ab
from smoke_volume_identity import file_identity
from window_capture_smoke import (
    STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, write_status,
)

START = "StressTestSmokeProject: presentation timing warmup_seconds=5 measure_seconds=30 clock=steady accepted_native_present=1"
DONE = "StressTestSmokeProject: presentation measurement complete "
INTERVAL = "StressTestSmokeProject: presentation fps "
SHUTDOWN = "StressTestSmokeProject: shutdown"
GPU_DEBUG = ("Loader: GPU debug validation enabled", "validation layer enabled: yes",
    "Vulkan GPU debug: debug utils messenger installed.")
REQUIRED = (START, SHUTDOWN, "AvboitTimingProbe: in-flight ranges 32",
    "AvboitTimingProbe: render unfocused 1", "AvboitTimingProbe: caustic in-flight ranges 32",
    "StressTestSmokeProject: spawned 10 spinning characters (5 transparent + 5 opaque) over ground, directional + point light")


def parse_sample(line, prefix, rate_key, positive_count):
    match = re.fullmatch(re.escape(prefix) + re.escape(rate_key)
        + r"=(\S+) presentations=(\d+) seconds=(\S+) first=(\d+) last=(\d+)", line)
    if not match:
        raise SmokeFailure("malformed presentation measurement record")
    try:
        fps, frames, seconds, first, last = (float(match[1]), int(match[2]), float(match[3]), int(match[4]), int(match[5]))
    except ValueError as error:
        raise SmokeFailure("invalid presentation measurement number") from error
    if not math.isfinite(fps) or not math.isfinite(seconds) or fps < 0 or seconds <= 0:
        raise SmokeFailure("presentation rate and elapsed time must be finite and nonnegative")
    if not 0 <= first <= last < 2 ** 64 or frames != last - first or (positive_count and frames == 0):
        raise SmokeFailure("presentation count must equal last minus first and be positive for completion")
    if not math.isclose(fps, frames / seconds, rel_tol=1e-9, abs_tol=1e-9):
        raise SmokeFailure("presentation FPS must be count divided by actual wall seconds")
    return {"fps": fps, "presentations": frames, "seconds": seconds, "first": first, "last": last}


def parse_runtime_log(text, exit_code, application_args=()):
    require_normal_process_exit(exit_code, "", "stress presentation timing")
    validate_expected_log_text(text, list(REQUIRED), list(STRICT_LOG_FAILURE_MESSAGES) + [
        "presentation measurement incomplete", "render submission suspended", "render pass skipped", "device recreation"])
    lines = [line.strip() for line in text.splitlines()]
    if any(lines.count(marker) != 1 for marker in REQUIRED):
        raise SmokeFailure("one exact startup, reservation, fixture and shutdown sequence is required")
    if sum(line.startswith("StressTestSmokeProject: presentation timing ") for line in lines) != 1:
        raise SmokeFailure("multiple or conflicting presentation timing configurations")
    if any(value == "--gpudbg" or value.startswith("--gpudbg=") for value in application_args):
        if any(lines.count(marker) != 1 for marker in GPU_DEBUG):
            raise SmokeFailure("requested GPU validation requires actual loader, layer and messenger markers")
    completions = [line for line in lines if line.startswith(DONE)]
    intervals = [parse_sample(line, INTERVAL, "avg", False) for line in lines if line.startswith(INTERVAL)]
    if len(completions) != 1 or not intervals:
        raise SmokeFailure("exactly one completion and its interval evidence are required")
    total = parse_sample(completions[0], DONE, "fps", True)
    if total["seconds"] < 30.0:
        raise SmokeFailure("measurement must span at least 30 actual wall seconds")
    if not lines.index(START) < lines.index(completions[0]) < lines.index(SHUTDOWN):
        raise SmokeFailure("startup, completion and shutdown order is invalid")
    interval_lines = [index for index, line in enumerate(lines) if line.startswith(INTERVAL)]
    if not lines.index(START) < min(interval_lines) <= max(interval_lines) < lines.index(completions[0]):
        raise SmokeFailure("intervals must occur inside the completed measurement")
    previous = total["first"]
    for interval in intervals:
        if interval["first"] != previous:
            raise SmokeFailure("presentation intervals must form a contiguous count chain")
        previous = interval["last"]
    if previous != total["last"] or sum(row["presentations"] for row in intervals) != total["presentations"]:
        raise SmokeFailure("interval counts do not match completion")
    if not math.isclose(sum(row["seconds"] for row in intervals), total["seconds"], rel_tol=1e-9, abs_tol=1e-9):
        raise SmokeFailure("interval wall times do not match completion")
    extents = re.findall(r"deferred rendering targets ready \((\d+)x(\d+),", text)
    if not extents or any(pair != ("1280", "900") for pair in extents):
        raise SmokeFailure("actual stress extent must be 1280x900")
    return {"measurement": total | {"frame_ms": 1000.0 * total["seconds"] / total["presentations"]},
        "intervals": intervals, "width": 1280, "height": 900, "warmup_seconds": 5,
        "requested_measurement_seconds": 30, "clock": "steady", "count": "accepted_native_present"}


def parse_measurement(log_text):
    """Replay the complete raw measurement log without requiring a process launch."""
    parsed = parse_runtime_log(log_text, 0)
    return parsed["measurement"] | {"intervals": parsed["intervals"]}


def launch_environment(base, args, output):
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if base.get(key, "").strip():
            raise SmokeFailure(f"unrequested Vulkan layer override: {key}")
    result = {key: value for key, value in base.items() if not key.startswith("NWB_")}
    if platform.system() == "Linux":
        result["NWB_LINUX_BACKEND"] = "x11"
    result.update(NWB_STRESS_SMOKE_TIMING="1", NWB_STRESS_TEST_SPIN_ANGLE=str(args.spin_angle),
        NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=str(args.fixed_delta_seconds),
        NWB_GPU_TIMING_FILE=str(output / "gpu_timing.txt"))
    return result


def reserve_output(requested, protected):
    if requested is None:
        return Path(tempfile.mkdtemp(prefix="nwb_stress_presentation_timing_")).resolve()
    output = requested.resolve()
    if output.exists():
        raise SmokeFailure("output must be new; prior evidence is never overwritten")
    for item in protected:
        item = item.resolve()
        if output == item or output.is_relative_to(item) or item.is_relative_to(output):
            raise SmokeFailure("output overlaps a protected input")
    output.mkdir(parents=True, exist_ok=False)
    return output


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def identities(args, helpers):
    return {"renderer": ab.binary_identity(args.executable), "runtime": ab.runtime_identity(args.working_directory),
        "logger": None if args.no_logserver else ab.binary_identity(args.logserver_executable),
        "python": file_identity(Path(sys.executable)),
        "helpers": {str(path): file_identity(path) for path in helpers}}


def acquire(args, output):
    helpers = sorted({Path(module.__file__).resolve() for module in list(sys.modules.values())
        if getattr(module, "__file__", None) and Path(module.__file__).suffix == ".py"
        and ("tests" in Path(module.__file__).parts or Path(module.__file__).resolve() == Path(__file__).resolve())})
    before = identities(args, helpers)
    env = launch_environment(build_launch_environment(args), args, output)
    launch = SimpleNamespace(**vars(args), log_port=0)
    write_json(output / "launch.json", {"executable": str(args.executable), "working_directory": str(args.working_directory),
        "application_args": args.application_arg, "timeout_seconds": args.timeout,
        "environment": {key: value for key, value in env.items() if key.startswith(("NWB_", "VK_"))},
        "identity_before": before})
    process = logserver = log_directory = None
    baseline, pattern = {}, ""
    collected = False
    code = None
    try:
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch, args.executable, env)
        process = launch_testbed(launch, args.executable, env, port)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure("stress measurement did not self-exit before timeout") from error
        code, tail = terminate_process(process, "stress presentation timing")
        process = None
        (output / "process_tail.txt").write_text(tail, encoding="utf-8")
        require_normal_process_exit(code, tail, "stress presentation timing")
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected = True
        (output / "runtime.log").write_text(text, encoding="utf-8")
        result = parse_runtime_log(text, code, args.application_arg)
        result["runtime_signature"] = ab.device_material_signature(text)
        after = identities(args, helpers)
        if before != after:
            raise SmokeFailure("renderer, resources, logger, interpreter or helper identity changed during acquisition")
        result.update(schema=1, passed=True, exit_code=code, identity_before=before, identity_after=after,
            raw_files={name: file_identity(output / name) for name in ("runtime.log", "process_tail.txt", "gpu_timing.txt")
                if (output / name).is_file()})
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = terminate_process(process, "stress presentation timing")
                (output / "process_tail.txt").write_text(tail, encoding="utf-8")
            except Exception as error:
                errors.append(f"renderer cleanup: {error}")
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (output / "runtime.log").write_text(text, encoding="utf-8")
                logserver = None
            except Exception as error:
                errors.append(f"failure-path log collection: {error}")
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
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--no-logserver", action="store_true")
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--spin-angle", type=float, default=0.6)
    parser.add_argument("--fixed-delta-seconds", type=float, default=0.016666667)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    for key in ("executable", "working_directory", "logserver_executable"):
        if getattr(args, key) is not None:
            setattr(args, key, getattr(args, key).resolve())
    if not args.executable.is_file() or not args.working_directory.is_dir():
        parser.error("existing renderer and runtime paths are required")
    if not args.no_logserver and (args.logserver_executable is None or not args.logserver_executable.is_file()):
        parser.error("an existing logger is required unless --no-logserver is selected")
    if not math.isfinite(args.timeout) or not 35 < args.timeout <= 100:
        parser.error("timeout must be finite and greater than 35 through 100 seconds")
    if not math.isfinite(args.spin_angle) or not 0 <= args.spin_angle < math.tau:
        parser.error("spin angle must be finite in [0, 2pi)")
    if not math.isfinite(args.fixed_delta_seconds) or not 0 < args.fixed_delta_seconds <= .25:
        parser.error("fixed simulation delta must be finite in (0, .25]")
    args.software_vulkan = "off"
    return args


def main(argv=None):
    output = None
    try:
        args = parse_args(argv)
        protected = [args.executable.parent, args.working_directory, Path(__file__).resolve().parent]
        if args.logserver_executable is not None:
            protected.append(args.logserver_executable.parent)
        output = reserve_output(args.output_directory, protected)
        write_status(f"Stress presentation timing artifacts: {output}")
        result = acquire(args, output)
        write_json(output / "result.json", result)
        write_status(f"PASS: {result['measurement']['fps']:.4f} accepted presentations/s over {result['measurement']['seconds']:.6f}s")
        return 0
    except SmokeSkip as error:
        if output is not None:
            write_json(output / "failure.json", {"passed": False, "skipped": True, "error": str(error)})
        write_status(f"SKIP: {error}")
        return 77
    except (SmokeFailure, OSError) as error:
        if output is not None:
            write_json(output / "failure.json", {"passed": False, "error": str(error)})
        write_status(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
