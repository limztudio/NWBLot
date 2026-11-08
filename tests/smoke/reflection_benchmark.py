#!/usr/bin/env python3
"""Measure reflection GPU work without framebuffer readbacks or diagnostic counters.

Each interleaved block is one statistical unit. Publication-window averages and
CPU frame counts are never used as per-frame GPU timings. Correctness belongs to
the separate reflection smoke suites; this runner measures their stationary cases.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

LIT_MINIMUM_WARMUP_INTERVALS = 1

LIT_WINDOW_TIMEOUT_SECONDS = 5.0
LIT_PROCESS_POLL_SECONDS = .1

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from gpu_timing_parse import load_name_symbols
from experiment_statistics import PairedExperimentAnalysis
from reflection_experiment_analysis import ReflectionExperimentAnalysis
from reflection_gpu_coverage import (FRAME, CLASSIFY, DEPTH, BUILD_ARGS, HARDWARE, TEMPORAL, SPATIAL, CONTROLS, OBSERVED_CONTROLS, KERNELS, KNOWN_SCOPES, ReflectionGpuCoverage)
from reflection_workload_qualification import ReflectionWorkloadQualification
from gpu_timing_evidence import GpuTimingEvidence
from smoke_volume_identity import authored_volume_hashes, file_identity, runtime_pipeline_cache_paths
from smoke_failure_reporting import write_failure_report
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, write_status,)
from window_capture_validation import validate_expected_log_text

LIT_REPORT_JSON = "report.json"

LIT_ROUGH = "rough"
LIT_DISABLED = "disabled"
LIT_HARDWARE_RAW = "hardware_raw"
LIT_HARDWARE = "hardware"
LIT_HYBRID_RAW = "hybrid_raw"
LIT_HYBRID = "hybrid"
LIT_HYBRID_FEEDBACK = "hybrid_feedback"
LIT_SCREEN = "screen"
LIT_TOTAL_MS = "total_ms"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_POSITIVE_FRAME_REPORTS = "positive_frame_reports"
LIT_MEAN_MS = "mean_ms"
LIT_BLOCKS = "blocks"
LIT_BLOCK = "block"
LIT_VARIANT = "variant"
LIT_NAME = "name"
LIT_SCOPES = "scopes"
LIT_KERNEL_WORK_MS = "kernel_work_ms"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_CASE = "case"
LIT_OPTICAL_CLEAR = "optical_clear"
LIT_OPTICAL_CSG_CAP = "optical_csg_cap"
LIT_REFLECTION_RESOLVE = "Reflection resolve: "
LIT_UTF_8 = "utf-8"
LIT_EXECUTABLE = "executable"
LIT_WORKING_DIRECTORY = "working_directory"
LIT_EXECUTABLE_IDENTITY = "executable_identity"
LIT_POSITION = "position"
LIT_REFLECTION_BENCHMARK = "reflection benchmark"
LIT_RUNTIME_LOG = "runtime.log"
LIT_MIP_COUNT = "mip_count"
LIT_RUNTIME_ASSET_IDENTITY = "runtime_asset_identity"
LIT_PATH = "path"
LIT_FEEDBACK_LONG_MISS = "feedback_long_miss"
LIT_OPTICAL_QUERIES = "optical_queries"
LIT_SCREEN_STEPS = "screen_steps"
LIT_ROUGHNESS = "roughness"
LIT_FLOOR = "floor"
LIT_OFFSCREEN = "offscreen"
LIT_STORE_TRUE = "store_true"
LIT_OFF = "off"
LIT_PRACTICAL_MS = "practical_ms"
LIT_PRACTICAL_FRACTION = "practical_fraction"
LIT_CONTROL_FLOOR_MS = "control_floor_ms"
LIT_SAMPLE_INTERVALS = "sample_intervals"
LIT_MINIMUM_FRAME_SAMPLES = "minimum_frame_samples"
LIT_CAPTURE = "capture"
LIT_MAIN = "__main__"
LIT_APPEND = "append"

DIMENSIONS = re.compile(r"deferred rendering targets ready \((\d+)x(\d+),")
FEEDBACK_PROBE_PERIOD = 16
TIMING_IN_FLIGHT_RANGES = 32


@dataclass(frozen=True)
class Variant:
    name: str
    mode: str
    temporal: bool = False
    spatial: bool = False
    feedback: bool = False


@dataclass
class TrialCleanupStatus:
    succeeded: bool = False


def variants_for(family, include_screen=False, include_feedback=False):
    if family == LIT_ROUGH:
        variants = (Variant(LIT_DISABLED, LIT_DISABLED), Variant(LIT_HARDWARE_RAW, LIT_HARDWARE), Variant("hardware_temporal", LIT_HARDWARE, True), Variant("hardware_spatial", LIT_HARDWARE, False, True), Variant("hardware_filtered", LIT_HARDWARE, True, True))
        return variants + ((Variant(LIT_HYBRID_RAW, LIT_HYBRID), Variant(LIT_HYBRID_FEEDBACK, LIT_HYBRID, feedback=True)) if include_feedback else ())
    variants = (Variant(LIT_DISABLED, LIT_DISABLED), Variant(LIT_HARDWARE, LIT_HARDWARE), Variant(LIT_HYBRID, LIT_HYBRID))
    if include_screen:
        variants += (Variant(LIT_SCREEN, LIT_SCREEN),)
    if include_feedback:
        variants += (Variant(LIT_HYBRID_FEEDBACK, LIT_HYBRID, feedback=True),)
    return variants








def timed_environment(base, args, variant, timing_file):
    env = dict(base)
    for name in tuple(env):
        if name.startswith(("NWB_REFLECTION_SMOKE_", "NWB_REFRACTION_SMOKE_", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_")):
            del env[name]
    for name in ("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", LIT_NWB_GPU_TIMING_FILE):
        env.pop(name, None)
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if env.get(key):
            raise SmokeFailure(f"{key} must be unset for the timing benchmark")
    controls = {"NWB_REFLECTION_SMOKE_CASE": args.family, "NWB_REFLECTION_SMOKE_MODE": variant.mode, "NWB_REFLECTION_SMOKE_TIMING": "1", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "0", "NWB_REFLECTION_SMOKE_RAY_BUDGET": str(args.ray_budget),
        "NWB_REFLECTION_SMOKE_TEMPORAL": str(int(variant.temporal)), "NWB_REFLECTION_SMOKE_SPATIAL": str(int(variant.spatial)), "NWB_REFLECTION_SMOKE_FEEDBACK": str(int(variant.feedback)), "NWB_REFLECTION_SMOKE_SCREEN_STEPS": str(args.screen_steps),
        "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": str(args.history_samples), "NWB_REFLECTION_SMOKE_ROUGHNESS": str(args.roughness if args.family == LIT_ROUGH else 0.0), "NWB_REFLECTION_SMOKE_SEED": str(args.sampling_seed), "NWB_REFLECTION_SMOKE_DEBUG": "none",
        "NWB_REFLECTION_SMOKE_FINAL_STATE": "0", "NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": str(args.optical_queries), "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667", LIT_NWB_GPU_TIMING_FILE: str(timing_file)}
    env.update(controls)
    return env, controls


def validate_trial_log(text, args, variant):
    # Match complete startup log values across the entire run.
    lines = text.replace("\r\n", "\n").splitlines()
    expected_fields = {LIT_CASE: f"{args.family} created", "reflection mode": variant.mode, "hardware ray budget": str(args.ray_budget), "screen feedback": str(int(variant.feedback)), "screen steps": str(args.screen_steps), "timing render unfocused": "1",
        "timing in-flight ranges": str(TIMING_IN_FLIGHT_RANGES), "timing depth mip count": str(args.mip_count)}
    if args.family in (LIT_OPTICAL_CLEAR, LIT_OPTICAL_CSG_CAP):
        expected_fields["optical query limit"] = str(args.optical_queries)
    for field, expected in expected_fields.items():
        prefix = "ReflectionSmokeProject: " + field + " "
        values = [line[len(prefix):] for line in lines if line.startswith(prefix)]
        if values != [expected]:
            raise SmokeFailure(f"runtime {field} differs from the single frozen benchmark setting: {values!r}")
    if "ReflectionSmokeProject: shutdown" not in lines:
        raise SmokeFailure("benchmark did not report normal project shutdown")
    route = {LIT_DISABLED: LIT_DISABLED, LIT_SCREEN: "screen-space", LIT_HARDWARE: LIT_HARDWARE, LIT_HYBRID: LIT_HARDWARE}[variant.mode]
    routes = {line.removeprefix(LIT_REFLECTION_RESOLVE) for line in lines if line.startswith(LIT_REFLECTION_RESOLVE)}
    if routes != {route}:
        raise SmokeFailure(f"reflection execution route changed or differs from {route}: {sorted(routes)}")
    rejected = list(STRICT_LOG_FAILURE_MESSAGES) + ["VK_LAYER_KHRONOS_validation", "Vulkan: enabled validation layer", "ReflectionSmokeStatistics:", "ReflectionSmokeHistory:", "ReflectionSmokeMutation:", "ReflectionSmokeOptics:", "ReflectionSmokeFeedback:"]
    validate_expected_log_text(text, [], rejected)
    if "ReflectionSmokeProject: hardware unavailable" in text and variant.mode in (LIT_HARDWARE, LIT_HYBRID):
        raise SmokeFailure("hardware was unavailable for a hardware benchmark route")
    if args.require_hardware:
        validate_expected_log_text(text, ["ReflectionSmokeProject: hardware available"], [])
    dimensions = {(int(width), int(height)) for width, height in DIMENSIONS.findall(text)}
    if dimensions != {(args.width, args.height)}:
        raise SmokeFailure(f"render-target dimensions changed or differ from the benchmark contract: {sorted(dimensions)}")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding=LIT_UTF_8)


def retain_and_validate_process_output(capture, directory):
    if capture is None:
        raise SmokeFailure("reflection process output capture is missing")
    output_path = directory / "process_output.log"
    shutil.copyfile(capture.path, output_path)
    output_text = output_path.read_text(encoding=LIT_UTF_8, errors="replace")
    if output_text:
        validate_expected_log_text(output_text, (), (*STRICT_LOG_FAILURE_MESSAGES, "Exception:"))


def run_trial(args, variant, block, position, symbols, executable_identity, *, cleanup_status=None):
    timing_evidence = GpuTimingEvidence(symbols)
    directory = args.output_directory / f"block_{block:02d}_{position:02d}_{variant.name}"
    directory.mkdir(parents=True, exist_ok=False)
    timing_file = directory / "gpu_timing.txt"
    env, controls = timed_environment(build_launch_environment(args), args, variant, timing_file)
    cache_before = runtime_pipeline_cache_identity(args.working_directory)
    write_json(directory / "launch.json", {LIT_EXECUTABLE: str(args.executable), LIT_WORKING_DIRECTORY: str(args.working_directory), "application_args": args.application_arg, "environment_overrides": controls, LIT_EXECUTABLE_IDENTITY: executable_identity,
        "runtime_pipeline_cache_before": cache_before, "gpu_environment": {key: value for key, value in env.items() if key.startswith("VK_") or key == "NWB_LINUX_BACKEND"}, LIT_BLOCK: block, LIT_POSITION: position, LIT_VARIANT: asdict(variant)})
    runtime_identity = runtime_asset_identity(args.working_directory)
    backend = None
    logserver = None
    runtime = None
    handle = None
    selected = None
    log_directory = None
    baseline = {}
    pattern = ""
    collected_logs = False
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, baseline, pattern = launch_logserver(args, args.executable, env)
        runtime = launch_testbed(args, args.executable, env, port)
        # Keep unmapped window IDs for graceful shutdown while timing continues.
        handle = backend.wait_for_window(runtime.pid, min(args.timeout, LIT_WINDOW_TIMEOUT_SECONDS))
        if handle is None:
            handle = backend.find_window_for_pid(runtime.pid, None, require_mapped=False)
        if handle:
            try:
                backend.focus_window(handle)
            except Exception as error:
                write_status(f"could not focus benchmark window: {error}")
        deadline = time.monotonic() + args.timeout
        last_problem = "timing reports have not arrived"
        while time.monotonic() < deadline:
            ensure_process_running(runtime, "during GPU benchmark")
            if timing_file.is_file():
                reports = timing_evidence.parse_intervals(timing_file.read_text(encoding=LIT_UTF_8))
                if reports:
                    last_problem = "waiting for required warm-up and measured timing reports" if any(FRAME in report for report in reports) \
                        else "timing reports contain no completed GPU frame samples"
                retained_start, retained = timing_evidence.retain_after_warmup(reports, args.warmup_intervals, 2 * FEEDBACK_PROBE_PERIOD if args.include_feedback else 0)
                if len(retained) >= args.sample_intervals:
                    summaries = timing_evidence.summarize_intervals(retained)
                    try:
                        ReflectionGpuCoverage.validate_coverage(summaries, variant, len(retained), args.minimum_frame_samples, args.mip_count, args.history_samples)
                        selected = retained
                        break
                    except SmokeFailure as error:
                        last_problem = str(error)
            time.sleep(LIT_PROCESS_POLL_SECONDS)
        if selected is None:
            raise SmokeFailure(f"GPU benchmark timed out: {last_problem}")
        capture = getattr(runtime, "_nwb_output_capture", None)
        exit_code, tail = CaptureProcessTermination.terminate_process(runtime, LIT_REFLECTION_BENCHMARK, handle, preserve_capture=True)
        runtime = None
        (directory / "process_tail.txt").write_text(tail, encoding=LIT_UTF_8)
        retain_and_validate_process_output(capture, directory)
        require_normal_process_exit(exit_code, tail, LIT_REFLECTION_BENCHMARK)
        log_text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected_logs = True
        (directory / LIT_RUNTIME_LOG).write_text(log_text, encoding=LIT_UTF_8)
        validate_trial_log(log_text, args, variant)
        if runtime_asset_identity(args.working_directory) != runtime_identity:
            raise SmokeFailure("packed runtime resources changed during benchmark")
        if file_identity(args.executable) != executable_identity:
            raise SmokeFailure("executable changed during benchmark")
        scopes = timing_evidence.summarize_intervals(selected)
        result = {LIT_BLOCK: block, LIT_POSITION: position, LIT_VARIANT: asdict(variant), LIT_REPORTS: len(selected), LIT_POSITIVE_FRAME_REPORTS: timing_evidence.positive_frame_reports(selected), "retained_report_range": [retained_start, retained_start + len(selected)],
            LIT_SCOPES: scopes, LIT_KERNEL_WORK_MS: ReflectionGpuCoverage.kernel_work_ms(scopes, variant, args.mip_count, args.history_samples), LIT_MIP_COUNT: args.mip_count, LIT_RUNTIME_ASSET_IDENTITY: runtime_identity, "artifacts": str(directory)}
        result["runtime_pipeline_cache_after"] = runtime_pipeline_cache_identity(args.working_directory)
        write_json(directory / "summary.json", result)
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        cleanup_errors = []
        try:
            if runtime is not None:
                capture = getattr(runtime, "_nwb_output_capture", None)
                exit_code, tail = CaptureProcessTermination.terminate_process(runtime, LIT_REFLECTION_BENCHMARK, handle, preserve_capture=True)
                retain_and_validate_process_output(capture, directory)
                require_normal_process_exit(exit_code, tail, LIT_REFLECTION_BENCHMARK)
        except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
            cleanup_errors.append(f"application cleanup: {error}")
        if log_directory is not None and not collected_logs:
            try:
                log_text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (directory / LIT_RUNTIME_LOG).write_text(log_text, encoding=LIT_UTF_8)
            except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
                cleanup_errors.append(f"log collection: {error}")
            finally:
                try:
                    CaptureProcessTermination.terminate_process(logserver, "logserver")
                except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
                    cleanup_errors.append(f"logserver cleanup: {error}")
        if backend is not None:
            try:
                backend.close()
            except (SmokeFailure, OSError) as error:
                cleanup_errors.append(f"window backend cleanup: {error}")
        if cleanup_status is not None:
            cleanup_status.succeeded = not cleanup_errors
        if cleanup_errors:
            detail = "\n".join(cleanup_errors)
            write_status(detail)
            try:
                (directory / "cleanup_error.txt").write_text(detail, encoding=LIT_UTF_8)
            except OSError as error:
                write_status(f"could not write cleanup error artifact: {error}")
            if not primary_failure:
                raise SmokeFailure(detail)


def resource_file_metadata(paths, directory):
    result = []
    for path in paths:
        identity = path.stat()
        result.append({LIT_PATH: str(path.relative_to(directory)), "bytes": identity.st_size, "modified_ns": identity.st_mtime_ns})
    return result


def runtime_pipeline_cache_identity(directory):
    return resource_file_metadata(runtime_pipeline_cache_paths(directory), directory)


def runtime_asset_identity(directory):
    resources = directory / "res"
    if not resources.exists():
        raise SmokeFailure("working directory has no packed res resources")
    files = [resources] if resources.is_file() else sorted(path for path in resources.rglob("*") if path.is_file())
    mutable_cache = set(runtime_pipeline_cache_paths(directory))
    files = [path for path in files if path not in mutable_cache]
    if not files:
        raise SmokeFailure("packed runtime resource directory has no authored resources")
    return resource_file_metadata(files, directory)




def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--family", choices=(LIT_FLOOR, LIT_OFFSCREEN, LIT_ROUGH, LIT_FEEDBACK_LONG_MISS, LIT_OPTICAL_CLEAR, LIT_OPTICAL_CSG_CAP), default=LIT_FLOOR)
    parser.add_argument("--include-screen", action=LIT_STORE_TRUE)
    parser.add_argument("--include-feedback", action=LIT_STORE_TRUE, help="Add a matched Hybrid feedback-on trial; every ordinary route explicitly keeps feedback off.")
    parser.add_argument("--screen-steps", type=int, help="Exact matched SSR limit; default 16 for the qualified long-miss fixture, 96 otherwise.")
    parser.add_argument("--qualification", type=Path, help="Matching reflection_feedback_manifest.json; required for feedback_long_miss.")
    parser.add_argument("--blocks", type=int)
    parser.add_argument("--warmup-intervals", type=int, default=6)
    parser.add_argument("--sample-intervals", type=int, default=16)
    parser.add_argument("--minimum-frame-samples", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--width", type=int, default=960, help="Expected render width, verified from logs; does not resize the window.")
    parser.add_argument("--height", type=int, default=720, help="Expected render height, verified from logs; does not resize the window.")
    parser.add_argument("--ray-budget", type=int, default=2 * 960 * 720)
    parser.add_argument("--history-samples", type=int, default=16)
    parser.add_argument("--roughness", type=float, default=.4)
    parser.add_argument("--sampling-seed", type=int, default=0)
    parser.add_argument("--optical-queries", type=int, default=16)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--analysis-seed", type=int, default=0)
    parser.add_argument("--practical-ms", type=float, default=.02)
    parser.add_argument("--practical-fraction", type=float, default=.03)
    parser.add_argument("--control-floor-ms", type=float, default=.015)
    parser.add_argument("--namesym", type=Path)
    parser.add_argument("--require-hardware", action=LIT_STORE_TRUE)
    parser.add_argument("--logserver-executable")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE)
    parser.add_argument("--log-port", type=int, default=0)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    parser.add_argument("--software-vulkan", choices=(LIT_OFF,), default=LIT_OFF)
    args = parser.parse_args(argv)
    if args.screen_steps is None:
        args.screen_steps = 16 if args.family == LIT_FEEDBACK_LONG_MISS else 96
    for name in ("timeout", LIT_PRACTICAL_MS, LIT_PRACTICAL_FRACTION, LIT_CONTROL_FLOOR_MS):
        value = getattr(args, name)
        if not math.isfinite(value) or value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be finite and positive")
    for name in ("width", "height", LIT_SAMPLE_INTERVALS, LIT_MINIMUM_FRAME_SAMPLES):
        if getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.warmup_intervals < LIT_MINIMUM_WARMUP_INTERVALS or not 1 <= args.ray_budget <= 0xffffffff:
        parser.error("warmup intervals and nonzero u32 ray budget are required")
    if not 1 <= args.history_samples <= 256 or not 1 <= args.optical_queries <= 16:
        parser.error("history samples must be 1..256 and optical queries 1..16")
    if not 8 <= args.screen_steps <= 256:
        parser.error("screen steps must be 8..256")
    if args.include_feedback and args.minimum_frame_samples < 4 * FEEDBACK_PROBE_PERIOD:
        parser.error("feedback comparisons need at least four complete probe periods of GPU frame samples")
    if not math.isfinite(args.roughness) or not 0 < args.roughness <= 1 or not 0 <= args.sampling_seed <= 0xffffffff:
        parser.error("roughness must be finite in (0,1] and seed must be a u32")
    if any("gpudbg" in value.lower() or LIT_CAPTURE in value.lower() for value in args.application_arg):
        parser.error("validation/capture arguments are incompatible with timing")
    if args.family == LIT_ROUGH and args.include_screen:
        parser.error("--include-screen belongs to smooth route comparisons")
    for name in (LIT_EXECUTABLE, LIT_WORKING_DIRECTORY, "output_directory"):
        setattr(args, name, getattr(args, name).resolve())
    if args.namesym:
        args.namesym = args.namesym.resolve()
    if args.qualification:
        args.qualification = args.qualification.resolve()
    args.mip_count = max(args.width, args.height).bit_length()
    variants = variants_for(args.family, args.include_screen, args.include_feedback)
    cycle = len(variants) if len(variants) % 2 == 0 else 2 * len(variants)
    args.blocks = args.blocks if args.blocks is not None else max(1, math.ceil(5 / cycle)) * cycle
    PairedExperimentAnalysis(args.order_seed).balanced_orders(variants, args.blocks)
    return args


def run(args):
    if not args.executable.is_file() or not args.working_directory.is_dir():
        raise SmokeFailure("executable or packed runtime working directory does not exist")
    if args.output_directory.exists() and any(args.output_directory.iterdir()):
        raise SmokeFailure("output directory must be empty; prior benchmark artifacts are never overwritten")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    variants = variants_for(args.family, args.include_screen, args.include_feedback)
    orders = PairedExperimentAnalysis(args.order_seed).balanced_orders(variants, args.blocks)
    identity = file_identity(args.executable)
    qualification = ReflectionWorkloadQualification.validate_long_miss_qualification(args, identity)
    runtime_identity = runtime_asset_identity(args.working_directory)
    symbols = load_name_symbols(args.namesym, KNOWN_SCOPES)
    namesym_identity = {LIT_PATH: str(args.namesym), **file_identity(args.namesym)} if args.namesym else None
    plan = {"family": args.family, "dimensions": [args.width, args.height], LIT_BLOCKS: args.blocks, "workload_role": {LIT_FLOOR: "productive screen reflection", LIT_OFFSCREEN: "offscreen screen-miss control",
            LIT_FEEDBACK_LONG_MISS: "separately qualified costly screen misses", LIT_ROUGH: "rough estimator and filter route costs", LIT_OPTICAL_CLEAR: "bounded secondary transmission through an authored closed clear volume",
            LIT_OPTICAL_CSG_CAP: "retained CSG transmission through an analytic glass wall"}[args.family], "qualification": qualification, "orders": [[variant.name for variant in row] for row in orders], LIT_EXECUTABLE_IDENTITY: identity,
        LIT_RUNTIME_ASSET_IDENTITY: runtime_identity, "namesym_identity": namesym_identity, "authored_volume_hashes": authored_volume_hashes(args.working_directory), "runtime_pipeline_cache_initial": runtime_pipeline_cache_identity(args.working_directory),
        "runtime_resource_policy": "authored files immutable; only canonical runtime_pipeline_cache volume segments may change", "scope_decoding": "supplied namesym plus known scope hashes" if args.namesym else "known scope hashes only",
        "settings": {"ray_budget": args.ray_budget, LIT_OPTICAL_QUERIES: args.optical_queries, "history_samples": args.history_samples, LIT_ROUGHNESS: args.roughness if args.family == LIT_ROUGH else 0.0,
            "sampling_seed": args.sampling_seed, LIT_SCREEN_STEPS: args.screen_steps, "feedback_default": False, "feedback_pairs": args.include_feedback, "diagnostics": False, LIT_CAPTURE: False}, "variants": [asdict(variant) for variant in variants],
        "order_seed": args.order_seed, "analysis_seed": args.analysis_seed, "warmup_intervals": args.warmup_intervals, LIT_SAMPLE_INTERVALS: args.sample_intervals, "minimum_warmup_gpu_frames": 2 * FEEDBACK_PROBE_PERIOD if args.include_feedback else 0,
        LIT_MINIMUM_FRAME_SAMPLES: args.minimum_frame_samples, LIT_MIP_COUNT: args.mip_count, "timing_in_flight_ranges": TIMING_IN_FLIGHT_RANGES, "inference": "paired independent blocks; percentile bootstrap 95% CI of mean; no FPS inference",
        "multiplicity": "per-comparison intervals; no family-wide confidence claim", "correctness": "separate diagnostics-on reflection suites must pass for this build",
        "external_controls": "same physical GPU, power state and mesh route; no concurrent GPU jobs or builds", "feature_cost": "increment above current Disabled route, which retains shared infrastructure/classification",
        "primary_scope": FRAME, "control_scopes": CONTROLS, "observed_control_scopes": OBSERVED_CONTROLS, "depth_scope": "per-mip dispatch; kernel estimate multiplies by native mip count",
        "temporal_scope": "one dispatch per stable reused frame when history_samples > 1; zero at cap 1", LIT_PRACTICAL_MS: args.practical_ms, LIT_PRACTICAL_FRACTION: args.practical_fraction, LIT_CONTROL_FLOOR_MS: args.control_floor_ms}
    write_json(args.output_directory / "plan.json", plan)
    trials = []
    try:
        for block, order in enumerate(orders):
            for position, variant in enumerate(order):
                if runtime_asset_identity(args.working_directory) != runtime_identity:
                    raise SmokeFailure("packed runtime resources changed between trials")
                write_status(f"benchmark block {block + 1}/{args.blocks}: {variant.name}")
                trials.append(run_trial(args, variant, block, position, symbols, identity))
                write_json(args.output_directory / "trials.json", trials)
        comparisons = ReflectionExperimentAnalysis(FRAME, KERNELS, CONTROLS, args.practical_ms, args.practical_fraction, args.control_floor_ms, args.analysis_seed).compare(trials, variants)
        write_json(args.output_directory / LIT_REPORT_JSON, {"plan": plan, "trials": trials, "comparisons": comparisons})
        write_status(f"GPU benchmark complete: {args.output_directory / LIT_REPORT_JSON}")
        return 0
    except (SmokeFailure, SmokeSkip, OSError, subprocess.SubprocessError) as error:
        write_failure_report(write_json, args.output_directory / "failure.json", {"error": str(error), "completed_trials": len(trials)})
        raise


def main(argv=None):
    try:
        return run(parse_args(sys.argv[1:] if argv is None else argv))
    except SmokeSkip as error:
        write_status(f"SKIP: {error}")
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
        write_status(f"FAIL: {error}")
        return 1


if __name__ == LIT_MAIN:
    sys.exit(main())
