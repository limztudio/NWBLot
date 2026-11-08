#!/usr/bin/env python3
"""Measure hardware transparent shadows against an opaque hardware-only baseline.

The same benchmark binary renders two fixed-yaw, ten-character animated stress scenes. The healthy arm records
the normal hardware transparent-shadow tail. The baseline arm uses a test-owned environment flag to make every
character opaque, naturally selecting hardware shadows without mutating renderer behavior. Their images are
diagnostic artifacts, not a pixel-parity gate: the material classes intentionally differ between arms.

The runner collects renderer GPU timestamp envelopes, verifies the arm-specific diagnostic markers,
captures both native windows, and writes a device-local report. ``--self-test`` exercises parsing
and report evaluation without a graphics device or visible window. Timing comparisons need more than ten positive
intervals per scope; otherwise a functionally clean report is inconclusive.

Both arms use hardware ray traversal, and only the healthy arm includes transparent volume shadows.
"""

from __future__ import annotations


import argparse
import math
import sys
import time
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence

LIT_WAIT_POLL_SECONDS = 0.25
LIT_TIMING_STABLE_SECONDS = 0.1

LIT_PROCESS_POLL_SECONDS = 0.1
LIT_TIMING_POLL_SECONDS = 0.05
LIT_TESTS_DIRECTORY = "tests"


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / LIT_TESTS_DIRECTORY / "ab"))
sys.path.insert(0, str(REPO / LIT_TESTS_DIRECTORY / "smoke"))

from gpu_timing_parse import (  # noqa: E402
    load_name_symbols as load_timing_name_symbols, parse_timing_file, require_non_negative, require_positive, summarize_scopes,)
from hardware_shadow_report import (  # noqa: E402
    HardwareShadowReport,
    RunResult,
    FRAME_SCOPE,
    LIT_BASELINE,
    LIT_FAIL,
    LIT_HEALTHY,
    LIT_INCONCLUSIVE,
    LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN,
    LIT_PASS,
    LIT_RENDER_SHADOW_VISIBILITY,
    LIT_UTF_8,
    LIT_VERDICT,
    OPAQUE_TRACE_SCOPE,
    OPTIONAL_COMPARISON_SCOPES,
    TRANSPARENT_RESOLVE_SCOPE,
    TRANSPARENT_TRACE_SCOPE,
)
from hardware_shadow_checks import HardwareShadowBoundaryChecks  # noqa: E402
from window_capture_environment import make_runtime_launch_args  # noqa: E402
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, )
from window_capture_validation import validate_capture_result  # noqa: E402

LIT_HEALTHY_EXECUTABLE = "--healthy-executable"
LIT_BASELINE_EXECUTABLE_2 = "--baseline-executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"

LIT_STRESSTESTSMOKEPROJECT_ENABLED_HEALTHY = "StressTestSmokeProject: enabled healthy hardware transparent-shadow benchmark"
LIT_STRESSTESTSMOKEPROJECT_RAYQUERY_CAPABL = "StressTestSmokeProject: RayQuery-capable hardware shadow route available"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_STRESSTESTSMOKEPROJECT_ENABLED_NATURAL = "StressTestSmokeProject: enabled natural opaque hardware-shadow baseline"
LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA = "RendererSystem: dispatched software shadow traversal"
LIT_HARDWARE_SHADOW_BENCHMARK_LOGSERVER = "hardware-shadow benchmark logserver"
LIT_SELF_TEST = "--self-test"
LIT_STORE_TRUE = "store_true"
LIT_BASELINE_EXECUTABLE = "baseline_executable"
LIT_WARMUP_SECONDS = "--warmup-seconds"
LIT_MEASURE_SECONDS = "--measure-seconds"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN_2 = "--maximum-transparent-frame-regression-percent"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


KNOWN_TIMING_SCOPES = (FRAME_SCOPE, *OPTIONAL_COMPARISON_SCOPES)
CAPABILITY_SKIP_LOG = "StressTestSmokeProject: hardware shadow boundary skipped because RayQuery-capable hardware is unavailable"
DEFAULT_FORBIDDEN_LOGS = (*STRICT_LOG_FAILURE_MESSAGES, "cannot safely continue after an unresolved frame recovery submission",)
HEALTHY_REQUIRED_LOGS = (LIT_STRESSTESTSMOKEPROJECT_ENABLED_HEALTHY, LIT_STRESSTESTSMOKEPROJECT_RAYQUERY_CAPABL, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA,)
BASELINE_REQUIRED_LOGS = (LIT_STRESSTESTSMOKEPROJECT_ENABLED_NATURAL, LIT_STRESSTESTSMOKEPROJECT_RAYQUERY_CAPABL,)
SHADOW_FAILURE_LOGS = ("RendererSystem: hardware transparent shadow resource preparation failed; colored shadows unavailable this frame", "RendererSystem: split opaque soft-shadow producer failed; retaining all-lit visibility",
    "RendererSystem: split opaque soft-shadow first wavelet failed; retaining all-lit visibility", "RendererSystem: split opaque soft-shadow resolve tail failed; retaining all-lit visibility",
    "RendererSystem: split transparent soft-shadow trace could not record; preserving opaque visibility", "RendererSystem: split transparent soft-shadow temporal merge failed; preserving opaque visibility",
    "RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope; preserving opaque visibility", "RendererSystem: split transparent soft-shadow first wavelet failed; preserving opaque visibility",
    "RendererSystem: split transparent soft-shadow resolve failed; preserving opaque visibility", "RendererSystem: split transparent soft-shadow trace or first wavelet failed; preserving opaque visibility",
    "RendererSystem: ray-traced shadow visibility pass failed", "RendererSystem: changed HW shadow material context has no graph-owned upload batch",)
HEALTHY_FORBIDDEN_LOGS = (LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA, LIT_STRESSTESTSMOKEPROJECT_ENABLED_NATURAL, *SHADOW_FAILURE_LOGS,)
BASELINE_FORBIDDEN_LOGS = (LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA, LIT_STRESSTESTSMOKEPROJECT_ENABLED_HEALTHY, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA, *SHADOW_FAILURE_LOGS,)


def load_name_symbols(path: Optional[Path]) -> Dict[str, str]:
    return load_timing_name_symbols(path, KNOWN_TIMING_SCOPES)


def find_log_messages(log_text: str, needles: Sequence[str]) -> List[str]:
    return [needle for needle in needles if needle in log_text]


def find_missing_log_messages(log_text: str, needles: Sequence[str]) -> List[str]:
    return [needle for needle in needles if needle not in log_text]


LOG_TAIL_CHAR_LIMIT = 4000


def wait_for_log_message(process, log_directory: Path, log_baseline: Mapping[Path, int], log_pattern: str, message: str, timeout_seconds: float, skip_message: Optional[str] = None,) -> Optional[str]:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, f"while waiting for benchmark log message '{message}'")
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        if skip_message and skip_message in latest_log:
            return skip_message
        if message in latest_log:
            return None
        time.sleep(LIT_PROCESS_POLL_SECONDS)

    raise SmokeFailure(f"timed out waiting for benchmark log message '{message}'\n{latest_log[-LOG_TAIL_CHAR_LIMIT:]}")


def wait_while_running(process, seconds: float, stage: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        time.sleep(min(LIT_WAIT_POLL_SECONDS, max(0.0, deadline - time.monotonic())))


def wait_for_measurement_timing_boundary(process, timing_path: Path, timeout_seconds: float) -> int:
    initial_size = timing_path.stat().st_size if timing_path.is_file() else 0
    latest_size = initial_size
    stable_since: Optional[float] = None
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, "while waiting for the post-warmup GPU timing boundary")
        current_size = timing_path.stat().st_size if timing_path.is_file() else 0
        now = time.monotonic()
        if current_size > initial_size:
            if current_size != latest_size:
                stable_since = now
            elif stable_since is not None and now - stable_since >= LIT_TIMING_STABLE_SECONDS:
                return current_size
        latest_size = current_size
        time.sleep(LIT_TIMING_POLL_SECONDS)

    raise SmokeFailure(f"GPU timing file did not publish a post-warmup interval boundary: {timing_path}")


def run_single_arm(args: argparse.Namespace, mode: str, executable: Path, symbols: Mapping[str, str], required_logs: Sequence[str], semantic_forbidden_logs: Sequence[str], capture_backend,) -> RunResult:
    if not executable.is_file():
        raise SmokeFailure(f"{mode} executable does not exist: {executable}")

    timing_path = args.output_dir / f"{mode}.timing.txt"
    log_path = args.output_dir / f"{mode}.log"
    capture_path = args.output_dir / f"{mode}.bmp"
    launch_args = make_runtime_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_GPU_TIMING_FILE"] = str(timing_path)
    environment["NWB_STRESS_TEST_SPIN_ANGLE"] = f"{args.frozen_yaw:.8g}"
    environment["NWB_STRESS_CHARACTERS_PER_CLASS"] = "5"
    environment["NWB_HARDWARE_SHADOW_BOUNDARY_OPAQUE_BASELINE"] = "1" if mode == LIT_BASELINE else "0"

    logserver_process = None
    app_process = None
    log_directory: Optional[Path] = None
    log_baseline: Mapping[Path, int] = {}
    log_pattern = ""
    app_exit_code = None
    app_exit_tail = ""
    log_text = ""
    window = None
    skip_reason = None
    measurement_start_byte_offset = 0
    try:
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(launch_args, executable, environment)
        if not log_directory:
            raise SmokeFailure("hardware-shadow runner could not select a runtime-log directory")
        app_process = launch_testbed(launch_args, executable, environment, log_port)

        skip_reason = wait_for_log_message(app_process, log_directory, log_baseline, log_pattern, required_logs[0], args.startup_timeout, CAPABILITY_SKIP_LOG,)
        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"{mode} benchmark did not expose the expected window '{args.window_title}'")
        if skip_reason is None:
            wait_while_running(app_process, args.warmup_seconds, f"during {mode} warmup")
            measurement_start_byte_offset = wait_for_measurement_timing_boundary(app_process, timing_path, args.startup_timeout,)
            wait_while_running(app_process, args.measure_seconds, f"during {mode} measurement")
            capture_result = capture_backend.capture_window(window, capture_path)
            validate_capture_result(capture_result)
        app_exit_code, app_exit_tail = CaptureProcessTermination.terminate_process(app_process, f"{mode} benchmark", window)
        app_process = None
        log_text = shutdown_logserver_and_collect(logserver_process, log_directory, log_baseline, log_pattern, LIT_HARDWARE_SHADOW_BENCHMARK_LOGSERVER,)
        logserver_process = None
    finally:
        CaptureProcessTermination.terminate_process(app_process, f"{mode} benchmark", window)
        CaptureProcessTermination.terminate_process(logserver_process, LIT_HARDWARE_SHADOW_BENCHMARK_LOGSERVER)

    require_normal_process_exit(app_exit_code, app_exit_tail, "testbed")
    if not log_text:
        raise SmokeFailure(f"{mode} benchmark produced no captured logger output")
    runtime_forbidden = find_log_messages(log_text, tuple(DEFAULT_FORBIDDEN_LOGS) + tuple(args.reject_log))
    if skip_reason:
        if runtime_forbidden:
            raise SmokeFailure(f"{mode} capability skip also emitted forbidden runtime logs: {runtime_forbidden}")
        raise SmokeSkip(skip_reason)
    if not capture_path.is_file():
        raise SmokeFailure(f"{mode} benchmark did not write a BMP capture")

    log_path.write_text(log_text, encoding=LIT_UTF_8)
    semantic_forbidden = find_log_messages(log_text, semantic_forbidden_logs)
    return RunResult(mode=mode, executable=str(executable), timing_file=str(timing_path), log_file=str(log_path), capture_file=str(capture_path), scopes=summarize_scopes(parse_timing_file(timing_path, symbols, measurement_start_byte_offset)),
        missing_required_log_messages=find_missing_log_messages(log_text, required_logs), runtime_forbidden_log_messages=runtime_forbidden, semantic_forbidden_log_messages=semantic_forbidden, measurement_start_byte_offset=measurement_start_byte_offset,)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Run parser and report checks without starting a graphics application.")
    parser.add_argument(LIT_HEALTHY_EXECUTABLE, type=Path, help="Path to nwb_hardware_shadow_boundary_healthy_benchmark.")
    parser.add_argument(LIT_BASELINE_EXECUTABLE_2, dest=LIT_BASELINE_EXECUTABLE, type=Path, help="Path to the benchmark binary reused for the opaque baseline.",)
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Cooked stress-scene runtime root used as process working directory.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Fresh artifact directory for timing files, logs, captures, and reports.")
    parser.add_argument("--logserver-executable", type=Path, help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Use standalone loader logs rather than a logserver.")
    parser.add_argument("--healthy-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin healthy binary.")
    parser.add_argument("--baseline-namesym", dest="baseline_namesym", type=Path, help="Optional name-symbol sidecar for the opaque-baseline binary.",)
    parser.add_argument("--window-title", default="NWB Hardware Shadow Boundary Benchmark", help="Native window title used for capture and graceful exit.",)
    parser.add_argument("--frozen-yaw", type=float, default=0.6, help="Fixed NWB_STRESS_TEST_SPIN_ANGLE in radians.")
    parser.add_argument(LIT_WARMUP_SECONDS, type=float, default=4.0, help="Settling time before each timed arm.")
    parser.add_argument(LIT_MEASURE_SECONDS, type=float, default=12.0, help="Timestamp collection duration per arm after warmup.")
    parser.add_argument(LIT_STARTUP_TIMEOUT, type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument("--minimum-samples", type=int, default=6, help="Minimum render.frame timing intervals required per arm.")
    parser.add_argument(LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN_2, dest=LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN, type=float, help="Optional maximum hardware-transparent frame overhead versus the opaque baseline.",)
    parser.add_argument("--gpu-validation", action=LIT_STORE_TRUE, help="Pass --gpudbg to both benchmark processes.")
    parser.add_argument("--reject-log", action=LIT_APPEND, default=[], help="Additional log substring that fails either arm.")
    parser.add_argument("--report-only", action=LIT_STORE_TRUE, help="Write the report but return success if a measurement gate fails.")
    args = parser.parse_args(argv)

    if args.self_test:
        return args

    required = ("healthy_executable", LIT_BASELINE_EXECUTABLE, "runtime_dir", "output_dir")
    missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join(missing)}")
    if not math.isfinite(args.frozen_yaw):
        parser.error("--frozen-yaw must be finite")
    require_non_negative(parser, LIT_WARMUP_SECONDS, args.warmup_seconds)
    require_positive(parser, LIT_MEASURE_SECONDS, args.measure_seconds)
    require_positive(parser, LIT_STARTUP_TIMEOUT, args.startup_timeout)
    if args.minimum_samples <= 0:
        parser.error("--minimum-samples must be positive")
    if args.maximum_transparent_frame_regression_percent is not None:
        require_non_negative(parser, LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN_2, args.maximum_transparent_frame_regression_percent,)

    args.healthy_executable = args.healthy_executable.resolve()
    args.baseline_executable = args.baseline_executable.resolve()
    args.runtime_dir = args.runtime_dir.resolve()
    args.output_dir = args.output_dir.resolve()
    args.logserver_executable = args.logserver_executable.resolve() if args.logserver_executable else None
    args.healthy_namesym = args.healthy_namesym.resolve() if args.healthy_namesym else None
    args.baseline_namesym = args.baseline_namesym.resolve() if args.baseline_namesym else None
    if args.baseline_namesym is None and args.baseline_executable == args.healthy_executable:
        args.baseline_namesym = args.healthy_namesym
    return args


def run_self_test() -> int:
    checks = HardwareShadowBoundaryChecks(sys.modules[__name__])
    defaults = checks.check_cli()
    checks.check_process_and_report(defaults)
    print("hardware-shadow boundary harness self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"runtime directory does not exist: {args.runtime_dir}")
    try:
        args.output_dir.mkdir(parents=True, exist_ok=False)
    except FileExistsError as error:
        raise SmokeFailure(f"artifact directory already exists: {args.output_dir}") from error

    reporter = HardwareShadowReport(args)
    capture_backend = None
    healthy: Optional[RunResult] = None
    baseline: Optional[RunResult] = None
    try:
        capture_backend = create_capture_backend()
        healthy = run_single_arm(args, LIT_HEALTHY, args.healthy_executable, load_name_symbols(args.healthy_namesym), HEALTHY_REQUIRED_LOGS, HEALTHY_FORBIDDEN_LOGS, capture_backend,)
        baseline = run_single_arm(args, LIT_BASELINE, args.baseline_executable, load_name_symbols(args.baseline_namesym), BASELINE_REQUIRED_LOGS, BASELINE_FORBIDDEN_LOGS, capture_backend,)
        report = reporter.evaluate_runs(healthy, baseline)
    except SmokeFailure as error:
        report = reporter.make_failure_report(error, healthy, baseline)
    finally:
        if capture_backend:
            capture_backend.close()

    json_path = args.output_dir / "hardware_shadow_boundary_report.json"
    markdown_path = args.output_dir / "hardware_shadow_boundary_report.md"
    reporter.write_json(json_path, report)
    reporter.write_markdown_report(markdown_path, report)
    print(f"Hardware-shadow boundary report: {markdown_path}")
    if report[LIT_VERDICT] != LIT_PASS and not args.report_only:
        return 1
    return 0


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    try:
        return run(args)
    except SmokeSkip as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
