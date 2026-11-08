#!/usr/bin/env python3
"""Balanced, source-frozen CPU gathering A/B; memory acquisition is a separate mode."""

import argparse
from dataclasses import asdict
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time
from types import SimpleNamespace

import renderer_ab_benchmark as ab
from renderer_workload_log_policy import RendererWorkloadLogPolicy
from renderer_benchmark_identity import RendererBenchmarkIdentity
from renderer_gather_comparison import GatherComparison
from renderer_gather_measurement import GatherMeasurement
from experiment_statistics import PairedExperimentAnalysis
from smoke_volume_identity import file_identity
from smoke_failure_reporting import write_failure_report
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, create_capture_backend, launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, window_capture_source_paths, write_status,)
from window_capture_validation import validate_expected_log_text

from renderer_gather_contract import (
    LIT_WINDOW_TIMEOUT_SECONDS,
    LIT_PROCESS_POLL_SECONDS,
    LIT_RUNTIME,
    LIT_GRAPHICS_PREPARE_RESOURCES,
    LIT_GRAPHICS_RENDER_PASSES,
    LIT_GRAPHICS_RENDER,
    LIT_TIMING,
    LIT_MEMORY,
    LIT_CONFIGURATION,
    LIT_WORKLOAD,
    LIT_MODE,
    LIT_RUNTIME_RENDERERS,
    LIT_RUNTIME_OWNERS,
    LIT_RENDERERS,
    LIT_TRANSPARENT_RENDERERS,
    LIT_SCOPE,
    LIT_UTF_8,
    LIT_BLOCK,
    LIT_POSITION,
    LIT_ARM,
    LIT_RESULT,
    LIT_BASELINE,
    LIT_CANDIDATE,
    LIT_GPU_CONTROLS,
    LIT_NWB,
    LIT_NWB_LINUX_BACKEND,
    LIT_EXECUTABLE,
    LIT_VK,
    LIT_RENDERER_GATHER_BENCHMARK,
    LIT_PROCESS_TAIL_TXT,
    LIT_RUNTIME_LOG,
    LIT_RUNTIME_SIGNATURE,
    LIT_STORE_TRUE,
    LIT_RESOURCES,
    LIT_AUTHORED_VOLUME_HASHES,
    LIT_MAIN,
    WORKLOADS,
    GPU_CONTROLS,
    FROZEN_SETTINGS,
)


def environment(base, workload, mode, output, compiler_statistics_output=None):
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if base.get(key, "").strip():
            raise SmokeFailure(f"explicit Vulkan layer override invalidates acquisition: {key}")
    env = {key: value for key, value in base.items() if not key.startswith(LIT_NWB) or key == LIT_NWB_LINUX_BACKEND}
    env.update(NWB_GATHER_BENCHMARK_WORKLOAD=workload, NWB_GATHER_BENCHMARK_MODE=mode, NWB_GATHER_BENCHMARK_OUTPUT=str(output), NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS="0.016666667", NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME="0")
    if compiler_statistics_output is not None:
        env["NWB_GATHER_COMPILER_STATISTICS_FILE"] = str(compiler_statistics_output)
    return env


def acquire(args, arm, block, position):
    directory = args.output_directory / f"block_{block:02d}_{position:02d}_{arm.name}"
    directory.mkdir(parents=True, exist_ok=False)
    result_path = directory / "samples.jsonl"
    launch = SimpleNamespace(**vars(args), working_directory=arm.runtime, executable=arm.executable)
    compiler_output = directory / "compiler_statistics.jsonl" if args.compiler_statistics else None
    env = environment(args.frozen_environment, args.workload, args.mode, result_path, compiler_output)
    ab.write_json(directory / "launch.json", {LIT_ARM: asdict(arm) | {LIT_EXECUTABLE: str(arm.executable), LIT_RUNTIME: str(arm.runtime), "source_manifest": str(arm.source_manifest)},
        "environment": {key: value for key, value in env.items() if key.startswith((LIT_NWB, LIT_VK))}, "cache_before": RendererBenchmarkIdentity.cache_identity(arm.runtime)})
    backend = process = logserver = handle = log_directory = None
    baseline, pattern = {}, ""
    logs_collected = False
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch, arm.executable, env)
        process = launch_testbed(launch, arm.executable, env, port)
        handle = backend.wait_for_window(process.pid, min(LIT_WINDOW_TIMEOUT_SECONDS, args.timeout))
        if not handle:
            raise SmokeFailure("fixed benchmark window never appeared")
        backend.focus_window(handle)
        deadline = time.monotonic() + args.timeout
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(LIT_PROCESS_POLL_SECONDS)
        if process.poll() is None:
            raise SmokeFailure("fixture did not finish its successful frame budget")
        code, tail = CaptureProcessTermination.terminate_process(process, LIT_RENDERER_GATHER_BENCHMARK, handle)
        process = None
        (directory / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
        require_normal_process_exit(code, tail, LIT_RENDERER_GATHER_BENCHMARK)
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        (directory / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
        logs_collected = True
        required = [f"RendererGatherBenchmark: workload {args.workload} mode {args.mode} fixed objects 64", "RendererGatherBenchmark: hardware reflection 4096 queries 16 refraction 1 diagnostics 0 temporal 0 spatial 0 feedback 0",
            "RendererGatherBenchmark: fixed delta 0.016666667 extent 960x720 async compute requested 1", "RendererGatherBenchmark: shutdown successful frames 384", "Reflection resolve: hardware"]
        validate_expected_log_text(text, required, list(STRICT_LOG_FAILURE_MESSAGES) + ["FramebufferCapture:", "render submission suspended", "render pass skipped", "device recreation"])
        signature = RendererWorkloadLogPolicy.device_material_signature(text)
        dims = re.findall(r"deferred rendering targets ready \((\d+)x(\d+),", text)
        if not dims or any(pair != ("960", "720") for pair in dims):
            raise SmokeFailure("actual render extent differs")
        vsync = re.findall(r"RendererGatherBenchmark: vsync ([01])", text)
        if len(vsync) != 1:
            raise SmokeFailure("actual vsync evidence is missing")
        signature["vsync"] = vsync[0]
        result = GatherMeasurement.read_result(result_path, args.workload, args.mode)
        signature["actual_counts"] = {key: result[LIT_CONFIGURATION][key] for key in (LIT_RENDERERS, LIT_RUNTIME_RENDERERS, LIT_TRANSPARENT_RENDERERS, LIT_RUNTIME_OWNERS)}
        return {LIT_ARM: arm.name, LIT_BLOCK: block, LIT_POSITION: position, LIT_RUNTIME_SIGNATURE: signature, LIT_RESULT: result, "artifacts": str(directory), "cache_after": RendererBenchmarkIdentity.cache_identity(arm.runtime)}
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        try:
            if process is not None:
                _, tail = CaptureProcessTermination.terminate_process(process, LIT_RENDERER_GATHER_BENCHMARK, handle)
                (directory / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
        except Exception as error:
            errors.append(f"renderer cleanup: {error}")
        if not logs_collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (directory / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
                logserver = None
            except Exception as error:
                errors.append(f"failure-path log collection: {error}")
        try:
            CaptureProcessTermination.terminate_process(logserver, "logserver")
        except Exception as error:
            errors.append(f"logserver cleanup: {error}")
        try:
            if backend:
                backend.close()
        except Exception as error:
            errors.append(f"window backend cleanup: {error}")
        if errors:
            write_status("; ".join(errors))
            write_failure_report(ab.write_json, directory / "cleanup_error.json", {"errors": errors})
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for arm in (LIT_BASELINE, LIT_CANDIDATE):
        for field in (LIT_EXECUTABLE, LIT_RUNTIME, "source-manifest"):
            parser.add_argument(f"--{arm}-{field}", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--workload", choices=WORKLOADS, required=True)
    parser.add_argument("--mode", choices=(LIT_TIMING, LIT_MEMORY), default=LIT_TIMING)
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=180.)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--analysis-seed", type=int, default=0)
    parser.add_argument("--plan-only", action=LIT_STORE_TRUE)
    parser.add_argument("--compiler-statistics", action=LIT_STORE_TRUE, help="opt-in test-domain compiler snapshots; changes observer workload and requires separate diagnostic analysis")
    args = parser.parse_args(argv)
    if args.blocks < 8 or args.blocks % 2 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("use at least eight even balanced blocks and a positive finite timeout")
    for key, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    args.application_arg = []
    args.no_logserver = False
    args.log_port = 0
    args.software_vulkan = "off"
    return args


def run(args):
    arms = tuple(ab.Arm(name, getattr(args, name + "_executable"), getattr(args, name + "_runtime"), getattr(args, name + "_source_manifest")) for name in (LIT_BASELINE, LIT_CANDIDATE))
    RendererBenchmarkIdentity.validate_arm_separation(arms)
    identities = {arm.name: RendererBenchmarkIdentity.freeze_arm(arm) for arm in arms}
    if identities[LIT_BASELINE][LIT_RESOURCES][LIT_AUTHORED_VOLUME_HASHES] != identities[LIT_CANDIDATE][LIT_RESOURCES][LIT_AUTHORED_VOLUME_HASHES]:
        raise SmokeFailure("CPU-only optimization requires identical authored asset volumes across arms")
    if args.output_directory.exists() and any(args.output_directory.iterdir()):
        raise SmokeFailure("output directory must be empty")
    if not args.logserver_executable.is_file():
        raise SmokeFailure("explicit shared logserver is missing")
    orders = [[arm.name for arm in row] for row in PairedExperimentAnalysis(args.order_seed).balanced_orders(arms, args.blocks)]
    args.frozen_environment = environment(build_launch_environment(args), args.workload, args.mode, "<per-trial-output>")
    modules = ("renderer_gather_contract", "renderer_gather_comparison", "renderer_gather_measurement", "renderer_benchmark_identity", "renderer_ab_benchmark", "renderer_wall_frame_evidence", "renderer_workload_log_policy", "renderer_workload_registry", "renderer_trial_comparison", "gpu_timing_evidence", "reflection_benchmark", "reflection_workload_qualification", "reflection_gpu_coverage", "experiment_statistics", "smoke_volume_identity", "gpu_timing_parse", "name_symbols")
    paths = [Path(__file__).resolve(), Path(sys.executable).resolve(), *window_capture_source_paths()]
    paths.extend(args.logserver_executable.parent / name for name in RendererBenchmarkIdentity.binary_identity(args.logserver_executable))
    paths.extend(Path(sys.modules[name].__file__).resolve() for name in modules)
    shared = {str(path): file_identity(path) for path in paths}
    plan = {LIT_WORKLOAD: args.workload, LIT_MODE: args.mode, "orders": orders, "arms": identities, "shared_files": shared, "settings": FROZEN_SETTINGS, "planned_trials": args.blocks * 2,
        "gpu_environment": {key: value for key, value in args.frozen_environment.items() if key.startswith(LIT_VK) or key == LIT_NWB_LINUX_BACKEND},
        "cpu_primary": LIT_GRAPHICS_RENDER, "cpu_subtotals": [LIT_GRAPHICS_PREPARE_RESOURCES, LIT_GRAPHICS_RENDER_PASSES], LIT_GPU_CONTROLS: GPU_CONTROLS, "memory_policy": "separate acquisition; no timing inference from memory mode",
        "cpu_normalization": "sum scope milliseconds / 256 successful contiguous frames", "gpu_normalization": "sum milliseconds / completed range count inside CPU source window, independently per scope",
        "correctness": "qualify both frozen builds separately; benchmark does not replace visual/native tests", "source_policy": "instrumentation and fixture must be present in both builds before freezing them"}
    if args.compiler_statistics:
        plan["compiler_statistics_diagnostic"] = { "enabled": True, "file": "<per-trial-directory>/compiler_statistics.jsonl", LIT_SCOPE: "existing compiler wall durations; observer cost is included in graphics.render",
            "validation": "separate compiler_statistics_diagnostic.py required; no change to timing/control gates", }
    args.output_directory.mkdir(parents=True, exist_ok=True)
    ab.write_json(args.output_directory / "plan.json", plan)
    if args.plan_only:
        return 0
    by_name = {arm.name: arm for arm in arms}
    trials, signature = [], None
    try:
        for block, order in enumerate(orders):
            for position, name in enumerate(order):
                RendererBenchmarkIdentity.verify_frozen(arms, identities, shared)
                write_status(f"CPU gather {args.workload}/{args.mode} block {block + 1}/{args.blocks}: {name}")
                trial = acquire(args, by_name[name], block, position)
                RendererBenchmarkIdentity.verify_frozen(arms, identities, shared)
                if signature is not None and trial[LIT_RUNTIME_SIGNATURE] != signature:
                    raise SmokeFailure("physical device, material route, or vsync changed")
                signature = trial[LIT_RUNTIME_SIGNATURE]
                trials.append(trial)
                ab.write_json(args.output_directory / "trials.json", trials)
        comparison = GatherComparison.compare(trials, orders, args.mode, args.analysis_seed)
        ab.write_json(args.output_directory / "report.json", {"plan": plan, "trials": trials, "comparison": comparison})
        return 0
    except Exception as error:
        write_failure_report(ab.write_json, args.output_directory / "failure.json", {"error": str(error), "completed_trials": len(trials)})
        raise


def main(argv=None):
    try:
        return run(parse_args(argv))
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        write_status(f"FAIL: {error}")
        return 1


if __name__ == LIT_MAIN:
    sys.exit(main())
