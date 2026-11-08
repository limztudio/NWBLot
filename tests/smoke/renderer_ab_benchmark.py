#!/usr/bin/env python3
"""Balanced GPU A/B acquisition for two explicitly frozen renderer builds.

Workloads cover transparent-multi and fixed reflection routes. Source manifests contain a nonempty
revision and a files object mapping paths (relative to the manifest, or absolute)
to SHA256 values. Keep physical source snapshots available throughout acquisition.
Correctness qualification is separate; this runner never captures framebuffers.
"""

import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import subprocess
import sys
import time
from types import SimpleNamespace

LIT_MINIMUM_WARMUP_INTERVALS = 2

LIT_WINDOW_TIMEOUT_SECONDS = 30.0
LIT_PROCESS_POLL_SECONDS = .1

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from gpu_timing_parse import MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS, load_name_symbols
from experiment_statistics import PairedExperimentAnalysis
import reflection_benchmark as reflection
from reflection_gpu_coverage import ReflectionGpuCoverage
import caustic_timing_qualification as caustic
from caustic_workload import CausticWorkload
from reflection_benchmark import LIT_POSITIVE_FRAME_REPORTS
from gpu_timing_evidence import GpuTimingEvidence
from renderer_wall_frame_evidence import WALL_FRAME_POLICY, SYNTHETIC_FRAME_REASON, RendererWallFrameEvidence
from renderer_workload_log_policy import RendererWorkloadLogPolicy
from renderer_trial_comparison import RendererTrialComparison
from renderer_workload_registry import FRAME, LIT_TRANSPARENT_MULTI, RendererWorkloadRegistry
from smoke_volume_identity import file_identity
from renderer_benchmark_identity import RendererBenchmarkIdentity
from smoke_failure_reporting import write_failure_report
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (SmokeFailure, SmokeSkip, build_launch_environment, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    require_normal_process_exit, shutdown_logserver_and_collect, window_capture_source_paths, write_status,)

LIT_SOURCE = "source"
LIT_RETAINED_REPORT_RANGE = "retained_report_range"
LIT_WALL_FRAME = "wall_frame"
LIT_COMPARISON = "comparison"
LIT_NATIVE_HARDWARE_DISPATCHES_PER_RANGE = "native_hardware_dispatches_per_range"
LIT_REPORT_JSON = "report.json"

LIT_HARDWARE = "hardware"
LIT_HYBRID = "hybrid"
LIT_SOFTWARE = "software"
LIT_TIMING_IN_FLIGHT_RANGES = "timing_in_flight_ranges"
LIT_REFLECTION_POLICY = "reflection_policy"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_MEAN_MS = "mean_ms"
LIT_REPORTS = "reports"
LIT_UTF_8 = "utf-8"
LIT_EXECUTABLE = "executable"
LIT_RUNTIME = "runtime"
LIT_BLOCK = "block"
LIT_POSITION = "position"
LIT_ARM = "arm"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_SCOPES = "scopes"
LIT_SECONDARY_SCOPE = "secondary_scope"
LIT_CONTROLS = "controls"
LIT_COMPLETED_TRIALS = "completed_trials"
LIT_APPLICATION_ARGS = "application_args"
LIT_RENDERER_A_B_BENCHMARK = "renderer A/B benchmark"
LIT_RUNTIME_LOG = "runtime.log"
LIT_RUNTIME_SIGNATURE = "runtime_signature"
LIT_ARTIFACTS = "artifacts"
LIT_STORE_TRUE = "store_true"
LIT_PRACTICAL_MS = "practical_ms"
LIT_PRACTICAL_FRACTION = "practical_fraction"
LIT_CONTROL_FLOOR_MS = "control_floor_ms"
LIT_CAPTURE = "capture"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


@dataclass(frozen=True)
class Arm:
    name: str
    executable: Path
    runtime: Path
    source_manifest: Path


def configure_environment(base, workload, timing_file):
    env = dict(base)
    # Preserve a requested validation override as an error, never silently mute it.
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if env.get(key, "").strip():
            raise SmokeFailure(f"explicit validation layer override is incompatible with timing: {key}")
    for key in tuple(env):
        if (key.startswith("NWB_") and ("SMOKE" in key or key.startswith("NWB_TRANSPARENT_"))) \
            or key.startswith("NWB_RENDERER_BASELINE_") or key.startswith("NWB_SOFT_SHADOW_TEST_") or key == LIT_NWB_GPU_TIMING_FILE:
            del env[key]
    if workload.reflection_policy is not None:
        return reflection.timed_environment(env, RendererWorkloadLogPolicy.reflection_arguments(workload), workload.reflection_policy.variant, timing_file)
    overrides = dict(workload.environment_overrides)
    overrides[LIT_NWB_GPU_TIMING_FILE] = str(timing_file)
    env.update(overrides)
    return env, overrides


def validate_inactive_scopes(scopes, workload):
    unexpected = [scope for scope in workload.inactive_scopes if scope in scopes]
    if unexpected:
        raise SmokeFailure("inactive reflection scopes recorded GPU work: " + ", ".join(unexpected))


def validate_coverage(scopes, workload, report_count, minimum_frames):
    validate_inactive_scopes(scopes, workload)
    if workload.reflection_policy is not None:
        policy = workload.reflection_policy
        ReflectionGpuCoverage.validate_coverage(scopes, policy.variant, report_count, minimum_frames, max(workload.width, workload.height).bit_length(), policy.history_samples)
    missing = [scope for scope in workload.scopes if scope not in scopes]
    if missing:
        raise SmokeFailure("missing completed GPU scopes: " + ", ".join(missing))
    frames = scopes[FRAME][LIT_GPU_SAMPLES]
    if frames < minimum_frames or scopes[FRAME][LIT_MEAN_MS] <= 0:
        raise SmokeFailure("insufficient completed GPU frame samples")
    for name, multiplier in workload.scope_multipliers:
        scope = scopes[name]
        if scope[LIT_REPORTS] < math.ceil(report_count * .75):
            raise SmokeFailure(f"scope {name} is absent from too many publications")
        expected = frames * multiplier
        if abs(scope[LIT_GPU_SAMPLES] - expected) > max(2 * multiplier, expected * .02):
            raise SmokeFailure(f"scope {name} sample ratio differs from {multiplier} range(s) per frame")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding=LIT_UTF_8)




def acquire_trial(args, arm, workload, block, position, symbols):
    timing_evidence = GpuTimingEvidence(symbols)
    directory = args.output_directory / f"block_{block:02d}_{position:02d}_{arm.name}"
    directory.mkdir(parents=True, exist_ok=False)
    timing_file = directory / "gpu_timing.txt"
    launch_args = SimpleNamespace(**vars(args))
    launch_args.working_directory = arm.runtime
    launch_args.executable = arm.executable
    env, overrides = configure_environment(build_launch_environment(launch_args), workload, timing_file)
    write_json(directory / "launch.json", {LIT_ARM: arm.name, LIT_BLOCK: block, LIT_POSITION: position, LIT_EXECUTABLE: str(arm.executable), LIT_RUNTIME: str(arm.runtime), LIT_APPLICATION_ARGS: args.application_arg,
        "environment_overrides": overrides, "cache_before": RendererBenchmarkIdentity.cache_identity(arm.runtime), "gpu_environment": {key: value for key, value in env.items() if key.startswith("VK_") or key == "NWB_LINUX_BACKEND"}})
    backend = logserver = process = handle = log_directory = None
    baseline = {}
    pattern = ""
    logs_collected = False
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch_args, arm.executable, env)
        process = launch_testbed(launch_args, arm.executable, env, port)
        handle = backend.wait_for_window(process.pid, min(args.timeout, LIT_WINDOW_TIMEOUT_SECONDS))
        if not handle:
            raise SmokeFailure("benchmark render window did not appear")
        backend.focus_window(handle)
        deadline = time.monotonic() + args.timeout
        selected = None
        problem = "timing reports have not arrived"
        while time.monotonic() < deadline:
            ensure_process_running(process, "during A/B benchmark")
            if timing_file.is_file():
                intervals = timing_evidence.parse_intervals(timing_file.read_text(encoding=LIT_UTF_8))
                validate_inactive_scopes(timing_evidence.summarize_intervals(intervals), workload)
                if intervals:
                    problem = "waiting for warm-up/measured reports" if any(FRAME in value for value in intervals) \
                        else "timing reports contain no completed GPU frame samples"
                retained = intervals[args.warmup_intervals:]
                if timing_evidence.positive_frame_reports(retained) >= args.sample_intervals:
                    summaries = timing_evidence.summarize_intervals(retained)
                    if workload.validate_log is RendererWorkloadLogPolicy.caustic_log:
                        CausticWorkload.validate_warmup(timing_evidence.summarize_intervals(intervals[:args.warmup_intervals]))
                    try:
                        validate_coverage(summaries, workload, len(retained), args.minimum_frame_samples)
                        selected = retained
                        break
                    except SmokeFailure as error:
                        problem = str(error)
            time.sleep(LIT_PROCESS_POLL_SECONDS)
        if selected is None:
            raise SmokeFailure(f"GPU acquisition timed out: {problem}")
        exit_code, tail = CaptureProcessTermination.terminate_process(process, LIT_RENDERER_A_B_BENCHMARK, handle)
        process = None
        (directory / "process_tail.txt").write_text(tail, encoding=LIT_UTF_8)
        require_normal_process_exit(exit_code, tail, LIT_RENDERER_A_B_BENCHMARK)
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        logs_collected = True
        (directory / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
        signature = workload.validate_log(text, workload, args.require_hardware)
        timing_text = timing_file.read_text(encoding=LIT_UTF_8)
        finalized = timing_evidence.parse_intervals(timing_text, finalized=True)
        validate_inactive_scopes(timing_evidence.summarize_intervals(finalized), workload)
        report_range = [args.warmup_intervals, args.warmup_intervals + len(selected)]
        return {LIT_ARM: arm.name, LIT_BLOCK: block, LIT_POSITION: position, LIT_REPORTS: len(selected), LIT_POSITIVE_FRAME_REPORTS: timing_evidence.positive_frame_reports(selected), LIT_RETAINED_REPORT_RANGE: report_range, LIT_NATIVE_HARDWARE_DISPATCHES_PER_RANGE: getattr(args, arm.name + "_hardware_dispatches_per_range")
                if reflection.HARDWARE in workload.scopes else None, LIT_WALL_FRAME: RendererWallFrameEvidence.summarize_wall_frames(text, timing_text, workload, report_range),
            "caustic_warmup_scopes": timing_evidence.summarize_intervals(finalized[:args.warmup_intervals]) if workload.validate_log is RendererWorkloadLogPolicy.caustic_log else None, LIT_SCOPES: timing_evidence.summarize_intervals(selected), LIT_RUNTIME_SIGNATURE: signature,
            LIT_ARTIFACTS: str(directory), "cache_after": RendererBenchmarkIdentity.cache_identity(arm.runtime)}
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        try:
            CaptureProcessTermination.terminate_process(process, LIT_RENDERER_A_B_BENCHMARK, handle)
        except Exception as error:
            errors.append(f"application cleanup: {error}")
        if log_directory is not None and not logs_collected:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (directory / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
                logserver = None
            except Exception as error:
                errors.append(f"log collection: {error}")
        try:
            CaptureProcessTermination.terminate_process(logserver, "logserver")
        except Exception as error:
            errors.append(f"logserver cleanup: {error}")
        if backend is not None:
            try:
                backend.close()
            except Exception as error:
                errors.append(f"window backend cleanup: {error}")
        if errors:
            write_status("; ".join(errors))
            write_failure_report(write_json, directory / "cleanup_error.json", {"errors": errors})
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for arm in (LIT_BASELINE, LIT_CANDIDATE):
        for field in (LIT_EXECUTABLE, LIT_RUNTIME, "source-manifest"):
            parser.add_argument(f"--{arm}-{field}", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--workload", choices=tuple(RendererWorkloadRegistry.workloads()), default=LIT_TRANSPARENT_MULTI)
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--warmup-intervals", type=int, default=2)
    parser.add_argument("--sample-intervals", type=int, default=MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS + 1, help="Positive measured frame reports per trial; performance needs more than ten.")
    parser.add_argument("--minimum-frame-samples", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--analysis-seed", type=int, default=0)
    parser.add_argument("--practical-ms", type=float, default=.02)
    parser.add_argument("--practical-fraction", type=float, default=.03)
    parser.add_argument("--control-floor-ms", type=float, default=.015)
    parser.add_argument("--baseline-hardware-dispatches-per-range", type=int, default=1, help="Submitted native calls per complete hardware timing range; does not change range coverage")
    parser.add_argument("--candidate-hardware-dispatches-per-range", type=int, default=1, help="Submitted native calls per complete hardware timing range; does not change range coverage")
    parser.add_argument("--namesym", type=Path)
    parser.add_argument("--require-hardware", action=LIT_STORE_TRUE)
    parser.add_argument("--plan-only", action=LIT_STORE_TRUE)
    parser.add_argument("--baseline-caustic-qualification", type=Path)
    parser.add_argument("--candidate-caustic-qualification", type=Path)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    args = parser.parse_args(argv)
    workload = RendererWorkloadRegistry.workloads()[args.workload]
    native_counts = (args.baseline_hardware_dispatches_per_range, args.candidate_hardware_dispatches_per_range)
    maximum_calls = max(1, min(workload.reflection_policy.ray_budget, 2 * workload.width * workload.height)) \
        if workload.reflection_policy is not None else 1
    if any(count <= 0 or count > maximum_calls for count in native_counts):
        parser.error("native hardware dispatch counts must be positive and cannot exceed the frozen ray population")
    optical = workload.reflection_policy is not None and workload.reflection_policy.family.startswith("optical_")
    if not optical and (args.baseline_hardware_dispatches_per_range != 1 or args.candidate_hardware_dispatches_per_range != 1):
        parser.error("multiple native hardware dispatches per timing range are supported only for optical workloads")
    if workload.reflection_policy is not None and workload.reflection_policy.variant.mode in (LIT_HARDWARE, LIT_HYBRID):
        args.require_hardware = True
    if workload.validate_log in (RendererWorkloadLogPolicy.soft_shadow_log, RendererWorkloadLogPolicy.caustic_log):
        args.require_hardware = True
    qualifications = (args.baseline_caustic_qualification, args.candidate_caustic_qualification)
    if workload.validate_log is RendererWorkloadLogPolicy.caustic_log and not all(qualifications):
        parser.error("caustic workloads require separate baseline/candidate footprint qualification reports")
    if workload.validate_log is not RendererWorkloadLogPolicy.caustic_log and any(qualifications):
        parser.error("caustic qualification applies only to caustic workloads")
    if args.blocks < 8 or args.blocks % 2:
        parser.error("blocks must be even and at least8, retaining complete AB/BA balance")
    if args.warmup_intervals < LIT_MINIMUM_WARMUP_INTERVALS or args.sample_intervals <= MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS or args.minimum_frame_samples < 100:
        parser.error(f"at least 2 warm-up reports, {MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS + 1} positive measured reports and 100 completed frames are required")
    for field in ("timeout", LIT_PRACTICAL_MS, LIT_PRACTICAL_FRACTION, LIT_CONTROL_FLOOR_MS):
        if not math.isfinite(getattr(args, field)) or getattr(args, field) <= 0:
            parser.error(f"{field} must be finite and positive")
    if any(any(word in value.lower() for word in ("gpudbg", LIT_CAPTURE, "benchmark")) for value in args.application_arg):
        parser.error("validation/capture or nested benchmark arguments are incompatible with timing")
    for field, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, field, value.resolve())
    args.no_logserver = False
    args.log_port = 0
    args.software_vulkan = "off"
    return args


def run(args):
    workload = RendererWorkloadRegistry.workloads()[args.workload]
    arms = tuple(Arm(name, getattr(args, name + "_executable"), getattr(args, name + "_runtime"), getattr(args, name + "_source_manifest")) for name in (LIT_BASELINE, LIT_CANDIDATE))
    RendererBenchmarkIdentity.validate_arm_separation(arms)
    if not args.logserver_executable.is_file():
        raise SmokeFailure("the explicit shared logserver executable is missing")
    if args.output_directory.exists() and any(args.output_directory.iterdir()):
        raise SmokeFailure("output directory must be empty; previous plans/trials are never overwritten")
    for arm in arms:
        if not arm.executable.is_file() or not arm.runtime.is_dir() or not arm.source_manifest.is_file():
            raise SmokeFailure(f"missing executable, runtime, or source manifest for {arm.name}")
    identities = {arm.name: RendererBenchmarkIdentity.freeze_arm(arm) for arm in arms}
    qualification = {}
    qualification_paths = []
    if workload.validate_log is RendererWorkloadLogPolicy.caustic_log:
        for arm in arms:
            path = getattr(args, arm.name + "_caustic_qualification")
            qualification[arm.name], paths = caustic.validate_report(path, identities[arm.name])
            qualification_paths.extend(paths)
    local_modules = ("renderer_trial_comparison", "renderer_workload_registry", "renderer_workload_log_policy", "renderer_wall_frame_evidence", "gpu_timing_evidence", "renderer_benchmark_identity", "reflection_benchmark", "reflection_workload_qualification", "reflection_gpu_coverage", "experiment_statistics", "gpu_timing_parse", "name_symbols", "smoke_volume_identity", "caustic_timing_qualification", "caustic_footprint", "caustic_workload")
    shared_paths = [Path(__file__), args.logserver_executable, *qualification_paths, *window_capture_source_paths()]
    shared_paths.extend(Path(sys.modules[name].__file__) for name in local_modules)
    if args.namesym:
        shared_paths.append(args.namesym)
    shared = {str(path.resolve()): file_identity(path) for path in shared_paths}
    orders = [[arm.name for arm in row] for row in PairedExperimentAnalysis(args.order_seed).balanced_orders(arms, args.blocks)]
    symbols = load_name_symbols(args.namesym, workload.observed_scopes)
    plan = {"workload": workload.name, "dimensions": [workload.width, workload.height], "orders": orders, "arms": identities, "shared_files": shared, "blocks": args.blocks, "planned_trials": args.blocks * 2,
        "settings": dict(workload.environment_overrides), "diagnostics": False, LIT_CAPTURE: False, "warmup_intervals": args.warmup_intervals, "sample_intervals": args.sample_intervals,
        "minimum_frame_samples": args.minimum_frame_samples, "timeout_seconds": args.timeout, LIT_TIMING_IN_FLIGHT_RANGES: 32, "scope_multipliers": dict(workload.scope_multipliers), "inactive_scopes": list(workload.inactive_scopes),
        "wall_frame_policy": WALL_FRAME_POLICY if workload.reflection_policy is not None else SYNTHETIC_FRAME_REASON, LIT_REFLECTION_POLICY: asdict(workload.reflection_policy) if workload.reflection_policy is not None else None,
        "caustic_qualification": qualification, LIT_NATIVE_HARDWARE_DISPATCHES_PER_RANGE: { LIT_BASELINE: args.baseline_hardware_dispatches_per_range, LIT_CANDIDATE: args.candidate_hardware_dispatches_per_range,
        } if reflection.HARDWARE in workload.scopes else None,
        "hardware_range_policy": "one completed range per frame containing every submitted native call; sliced optical work may include zero-work indirect records beyond the admitted queue; declared submitted counts require separate build qualification and never divide measured time",
        "depth_range_policy": "native mip count ranges per frame; per-range means describe one mip, with total measured mip work per frame reported separately",
        "inactive_scope_policy": "no completed inactive scope in warm-up, retained acquisition, or final shutdown reports", "scope_coverage": "completed timing ranges; publication skew bounded by max(2 ranges, 2% of expected), scaled by multiplicity",
        "primary_scope": FRAME, LIT_SECONDARY_SCOPE: workload.secondary_scope, LIT_CONTROLS: list(workload.control_scopes), "order_seed": args.order_seed, "analysis_seed": args.analysis_seed, "bootstrap_draws": 10000,
        LIT_PRACTICAL_MS: args.practical_ms, LIT_PRACTICAL_FRACTION: args.practical_fraction, LIT_CONTROL_FLOOR_MS: args.control_floor_ms, "require_hardware": args.require_hardware,
        LIT_APPLICATION_ARGS: args.application_arg, "normalization": "sum(total_ms)/sum(gpu_samples)", "resource_policy": "only canonical contiguous runtime_pipeline_cache segments may mutate",
        "correctness": "qualify each frozen build separately before timing; no image correctness claim from timings"}
    args.output_directory.mkdir(parents=True, exist_ok=True)
    write_json(args.output_directory / "plan.json", plan)
    if args.plan_only:
        write_status("Frozen A/B plan written; use a new output directory for acquisition")
        return 0
    trials = []
    signature = None
    by_name = {arm.name: arm for arm in arms}
    try:
        for block, row in enumerate(orders):
            for position, name in enumerate(row):
                RendererBenchmarkIdentity.verify_frozen(arms, identities, shared)
                write_status(f"renderer A/B block {block + 1}/{args.blocks}: {name}")
                trial = acquire_trial(args, by_name[name], workload, block, position, symbols)
                RendererBenchmarkIdentity.verify_frozen(arms, identities, shared)
                if signature is not None and trial[LIT_RUNTIME_SIGNATURE] != signature:
                    raise SmokeFailure("physical device, material route, shadow route or extent changed between arms/trials")
                signature = trial[LIT_RUNTIME_SIGNATURE]
                write_json(Path(trial[LIT_ARTIFACTS]) / "summary.json", trial)
                trials.append(trial)
                write_json(args.output_directory / "trials.json", trials)
        comparison = RendererTrialComparison.compare_trials(trials, orders, workload, args.analysis_seed, args.practical_ms, args.practical_fraction, args.control_floor_ms)
        write_json(args.output_directory / LIT_REPORT_JSON, {"plan": plan, "trials": trials, LIT_COMPARISON: comparison})
        write_status(f"Renderer A/B complete: {args.output_directory / LIT_REPORT_JSON}")
        return 0
    except Exception as error:
        write_failure_report(write_json, args.output_directory / "failure.json", {"error": str(error), LIT_COMPLETED_TRIALS: len(trials)})
        raise


def main(argv=None):
    try:
        return run(parse_args(argv))
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        write_status(f"FAIL: {error}")
        return 1


if __name__ == LIT_MAIN:
    sys.exit(main())
