#!/usr/bin/env python3
"""Capture actual CPU/GPU publications for matched ordinary/CSG slabs; diagnostic evidence, not FPS qualification."""

import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

import renderer_ab_benchmark as ab
from renderer_workload_registry import RendererWorkloadRegistry
from renderer_benchmark_identity import RendererBenchmarkIdentity
from reflection_cpu_timing_evidence import CASES, LIT_DIAGNOSTIC, LIT_HARDWARE, LIT_PRESENTATIONS, ReflectionCpuTimingEvidence
import reflection_benchmark as reflection
from smoke_volume_identity import file_identity
from smoke_failure_reporting import write_failure_report
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (SmokeFailure, SmokeSkip, build_launch_environment, launch_logserver, launch_testbed, require_normal_process_exit, shutdown_logserver_and_collect)


LIT_PYTHON_FILE_GLOB = "*.py"

LIT_NWB_REFLECTION_CPU_GPU_TIMING_FILE = "NWB_REFLECTION_CPU_GPU_TIMING_FILE"
LIT_UTF_8 = "utf-8"
LIT_REFLECTION_CPU_DIAGNOSTIC = "reflection CPU diagnostic"
LIT_PROCESS_TAIL_TXT = "process_tail.txt"
LIT_RUNTIME_LOG = "runtime.log"
LIT_CPU_GPU_SUMMARY_JSON = "cpu_gpu_summary.json"

RAW_NAME = "cpu_gpu_timing.txt"


def launch_environment(base, case, raw_path):
    # Run the reflection workload with the CPU and GPU timing probe.
    workload = RendererWorkloadRegistry.reflection_workload(LIT_DIAGNOSTIC, case, LIT_HARDWARE, 0.0, False, False, reflection.HARDWARE)
    env, overrides = ab.configure_environment(base, workload, raw_path)
    env.pop("NWB_GPU_TIMING_FILE", None)
    env.pop(LIT_NWB_REFLECTION_CPU_GPU_TIMING_FILE, None)
    env["NWB_REFLECTION_SMOKE_CPU_DIAGNOSTICS"] = "1"
    env[LIT_NWB_REFLECTION_CPU_GPU_TIMING_FILE] = str(raw_path)
    return env


def identities(args):
    helpers = sorted(Path(__file__).resolve().parent.glob(LIT_PYTHON_FILE_GLOB))
    helpers += sorted((Path(__file__).resolve().parents[1] / "ab").glob(LIT_PYTHON_FILE_GLOB))
    return dict(renderer=RendererBenchmarkIdentity.binary_identity(args.executable), logger=RendererBenchmarkIdentity.binary_identity(args.logserver_executable), runtime=RendererBenchmarkIdentity.runtime_identity(args.working_directory), python=file_identity(Path(sys.executable)),
        helpers={str(path): file_identity(path) for path in helpers})


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)


def acquire(args):
    output = args.output_directory
    if output.exists() or any(output == protected or output in protected.parents or protected in output.parents for protected in (args.working_directory, args.executable.parent, args.logserver_executable.parent)):
        raise SmokeFailure("reflection diagnostic requires a fresh, separate output directory")
    output.mkdir(parents=True)
    before = identities(args)
    env = launch_environment(build_launch_environment(args), args.case, output / RAW_NAME)
    write_json(output / "launch.json", dict(argv=sys.argv, application_args=args.application_arg, executable=str(args.executable), runtime=str(args.working_directory), case=args.case,
        environment={key: value for key, value in env.items() if key.startswith(("NWB_", "VK_"))}, identity_before=before))
    process = logger = log_directory = None
    baseline, pattern, collected = {}, "", False
    try:
        logger, port, log_directory, baseline, pattern = launch_logserver(args, args.executable, env)
        process = launch_testbed(args, args.executable, env, port)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure("reflection CPU diagnostic did not self-exit before its deadline") from error
        code, tail = CaptureProcessTermination.terminate_process(process, LIT_REFLECTION_CPU_DIAGNOSTIC)
        process = None
        (output / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
        require_normal_process_exit(code, tail, LIT_REFLECTION_CPU_DIAGNOSTIC)
        text = shutdown_logserver_and_collect(logger, log_directory, baseline, pattern)
        logger, collected = None, True
        (output / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
        measurement = ReflectionCpuTimingEvidence.parse_runtime_log(text, args.case)
        publications = ReflectionCpuTimingEvidence.validate_publications(text, output / RAW_NAME, measurement)
        write_json(output / LIT_CPU_GPU_SUMMARY_JSON, publications)
        after = identities(args)
        if before != after:
            raise SmokeFailure("reflection diagnostic executable, dependencies, resources or Python closure changed")
        result = dict(schema=1, passed=True, diagnostic_only=True, performance_qualification=False, exit_code=code, case=args.case, measurement=measurement, cpu_gpu_diagnostics=publications, identity_before=before, identity_after=after,
            artifacts={name: file_identity(output / name) for name in (LIT_RUNTIME_LOG, LIT_PROCESS_TAIL_TXT, RAW_NAME, LIT_CPU_GPU_SUMMARY_JSON)},
            interpretation="Same final renderer and resource contract; scopes overlap and are not summed. This is CPU/frame diagnostic evidence, not an FPS or speed qualification.")
        write_json(output / "result.json", result)
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = CaptureProcessTermination.terminate_process(process, LIT_REFLECTION_CPU_DIAGNOSTIC)
                (output / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
            except Exception as error:
                errors.append(str(error))
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logger, log_directory, baseline, pattern)
                (output / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
                logger = None
            except Exception as error:
                errors.append(str(error))
        try:
            CaptureProcessTermination.terminate_process(logger, "logserver")
            after = identities(args)
            write_json(output / "identity_after.json", after)
            if before != after:
                errors.append("guarded inputs changed during acquisition")
        except Exception as error:
            errors.append(str(error))
        if errors:
            print("; ".join(errors), file=sys.stderr, flush=True)
            write_failure_report(write_json, output / "cleanup_error.json", dict(errors=errors))
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--case", choices=CASES, required=True)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise SmokeFailure("reflection diagnostic deadline must be finite and positive")
    for name in ("executable", "working_directory", "logserver_executable", "output_directory"):
        setattr(args, name, getattr(args, name).resolve())
    if not args.executable.is_file() or not args.logserver_executable.is_file() or not args.working_directory.is_dir():
        raise SmokeFailure("reflection diagnostic executable, logger or runtime is missing")
    args.no_logserver, args.log_port, args.software_vulkan = False, 0, "off"
    return args


def main(argv=None):
    args = None
    try:
        args = parse_args(argv)
        result = acquire(args)
        print(f"PASS reflection CPU/GPU diagnostic {args.case}: {result['measurement'][LIT_PRESENTATIONS]} presentations")
        return 0
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        if args is not None and args.output_directory.is_dir():
            write_failure_report(write_json, args.output_directory / "failure.json", dict(passed=False, error=str(error), diagnostic_only=True))
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
