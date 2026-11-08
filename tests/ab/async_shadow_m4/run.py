#!/usr/bin/env python3
"""Run the M4 async-shadow queue-validation and critical-path A/B benchmark.

The paired smoke executables are intentionally identical except that the synchronous baseline explicitly disables
the default async-compute lane request before selected-backend device creation. This runner refuses a Graphics queue route, captures
fixed-yaw pixel output from both binaries, collects the renderer's
timestamp envelopes, and makes the M4 rollout gate explicit:

* a distinct dedicated compute queue must be active;
* the graph-owned render.async_shadow scope must contain measurable work;
* render.frame (the Graphics critical path) must not regress beyond the configured tolerance; and
* the fixed-scene output and validation log must remain clean.

Timing gates need more than ten positive intervals per compared scope; otherwise a functionally clean run is inconclusive.

The actual benchmark needs a target GPU with a dedicated compute-only queue and a visible native
window. `--self-test` checks capture failures, cleanup ordering, and incomplete telemetry without either requirement.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
import time
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence

LIT_WAIT_POLL_SECONDS = 0.25

LIT_PROCESS_POLL_SECONDS = 0.1
LIT_TIMING_POLL_SECONDS = 0.05
LIT_TESTS_DIRECTORY = "tests"

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / LIT_TESTS_DIRECTORY / "ab"))
sys.path.insert(0, str(REPO / LIT_TESTS_DIRECTORY / "smoke"))

from bitmap_image import BitmapImage  # noqa: E402
from gpu_timing_parse import (  # noqa: E402
    INTERVAL_RE, ScopeSummary, load_name_symbols as load_timing_name_symbols, parse_timing_file, require_non_negative, require_positive, summarize_scopes,)
from window_capture_environment import make_runtime_launch_args  # noqa: E402
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (  # noqa: E402
    run_smoke_main, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, WindowsCapture, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    require_normal_process_exit, shutdown_logserver_and_collect, )
from window_capture_validation import validate_capture_result  # noqa: E402
import async_shadow_m4_self_test  # noqa: E402
from async_shadow_m4_lane import DedicatedComputeUnavailable, LaneStatus, PhysicalQueueId  # noqa: E402
from async_shadow_m4_report import (  # noqa: E402
    AsyncShadowM4Report, RunResult, LIT_ASYNC, LIT_COLLECTION_ERROR, LIT_FAIL, LIT_GATES, LIT_NWB_ASYNC_SHADOW_M4_V3, LIT_PASS, LIT_PIXEL_DIFF, LIT_SCHEMA, LIT_SYNC, LIT_UTF_8, LIT_VERDICT, REQUIRED_ASYNC_SCOPES,
)

LIT_SYNC_EXECUTABLE = "--sync-executable"
LIT_ASYNC_EXECUTABLE = "--async-executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_FROZEN_YAW_MUST_BE_A_FINITE_FLOAT32_DE = "--frozen-yaw must be a finite float32 decimal"

LIT_CANNOT_SAFELY_CONTINUE_AFTER_AN_UNRESO = "cannot safely continue after an unresolved frame recovery submission"
LIT_NWB_STRESS_TEST_SPIN_ANGLE = "NWB_STRESS_TEST_SPIN_ANGLE"
LIT_BENCHMARK_RUNNER_COULD_NOT_SELECT_A_RU = "benchmark runner could not select a runtime-log directory"
LIT_BENCHMARK_LOGSERVER = "benchmark logserver"
LIT_TESTBED = "testbed"
LIT_SELF_TEST = "--self-test"
LIT_STORE_TRUE = "store_true"
LIT_WARMUP_SECONDS = "--warmup-seconds"
LIT_MEASURE_SECONDS = "--measure-seconds"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_PIXEL_CAPTURE_SETTLE_SECONDS = "--pixel-capture-settle-seconds"
LIT_MINIMUM_SHADOW_MS = "--minimum-shadow-ms"
LIT_MAXIMUM_FRAME_REGRESSION_PERCENT = "--maximum-frame-regression-percent"
LIT_MAXIMUM_PIXEL_MEAN_ABS = "--maximum-pixel-mean-abs"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


DEFAULT_FORBIDDEN_LOGS = (*STRICT_LOG_FAILURE_MESSAGES, LIT_CANNOT_SAFELY_CONTINUE_AFTER_AN_UNRESO,)
M4_PIXEL_CAPTURE_READY_LOG = "StressTestSmokeProject: M4 pixel capture ready after"
M4_PIXEL_CAPTURE_SUBMISSION_PAUSED_LOG = "render submission suspended"
M4_RENDER_UNFOCUSED_LOG = "AvboitTimingProbe: render unfocused 1"


@dataclass(frozen=True)
class FrameLockedCapture:
    capture_file: str
    log_text: str
    lane: LaneStatus


LOG_TAIL_CHAR_LIMIT = 4000


def wait_for_lane_status(process, log_directory: Path, log_baseline: Mapping[Path, int], log_pattern: str, timeout_seconds: float,) -> LaneStatus:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, "while waiting for the async-lane capability log")
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        status = LaneStatus.parse(latest_log)
        if status:
            return status
        time.sleep(LIT_PROCESS_POLL_SECONDS)

    detail = latest_log[-LOG_TAIL_CHAR_LIMIT:]
    raise SmokeFailure(f"timed out waiting for selected async-lane capability log\n{detail}")


def wait_for_log_message(process, log_directory: Path, log_baseline: Mapping[Path, int], log_pattern: str, message: str, timeout_seconds: float,) -> None:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, f"while waiting for benchmark log message '{message}'")
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        if message in latest_log:
            return
        time.sleep(LIT_PROCESS_POLL_SECONDS)

    detail = latest_log[-LOG_TAIL_CHAR_LIMIT:]
    raise SmokeFailure(f"timed out waiting for benchmark log message '{message}'\n{detail}")


def wait_for_measurement_timing_boundary(process, timing_path: Path, timeout_seconds: float) -> int:
    initial_size = timing_path.stat().st_size if timing_path.is_file() else 0
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, "while waiting for the post-warmup GPU timing boundary")
        raw_timing = timing_path.read_bytes() if timing_path.is_file() else b""
        if len(raw_timing) < initial_size:
            raise SmokeFailure(f"GPU timing file was truncated during measurement admission: {timing_path}")
        header_count = 0
        for match in re.finditer(rb"(?m)^=== interval:[^\r\n]*\r?\n", raw_timing):
            if match.start() < initial_size:
                continue
            header = INTERVAL_RE.fullmatch(match.group().decode(LIT_UTF_8).rstrip("\r\n"))
            if not header or not 0 < int(header.group("frames")) < 2 ** 32:
                raise SmokeFailure(f"invalid post-warmup GPU timing interval in {timing_path}")
            seconds = float(header.group("seconds"))
            if not math.isfinite(seconds) or seconds <= 0.0:
                raise SmokeFailure(f"invalid post-warmup GPU timing interval in {timing_path}")
            header_count += 1
            # A second header proves the first warmup report closed.
            if header_count == 2:
                return match.start()
        time.sleep(LIT_TIMING_POLL_SECONDS)

    raise SmokeFailure(f"GPU timing file did not publish a post-warmup interval boundary: {timing_path}")


def load_name_symbols(path: Optional[Path]) -> Dict[str, str]:
    return load_timing_name_symbols(path, REQUIRED_ASYNC_SCOPES, missing_file_message="Name-symbol sidecar does not exist: {path}",)


def wait_while_running(process, seconds: float, stage: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        time.sleep(min(LIT_WAIT_POLL_SECONDS, max(0.0, deadline - time.monotonic())))


def find_forbidden_log_messages(log_text: str, needles: Sequence[str]) -> List[str]:
    return [needle for needle in needles if needle in log_text]


def capture_m4_client_area(capture_backend, window, capture_path):
    """Capture application pixels only; Windows excludes the compositor-owned non-client frame."""
    if isinstance(capture_backend, WindowsCapture):
        return capture_backend.capture_prepared_raw_client_window(window, capture_path)
    return capture_backend.capture_client_window(window, capture_path)


def prepare_m4_client_area(capture_backend, window):
    """Stabilize the Windows M4 client capture before the held-frame marker."""
    if isinstance(capture_backend, WindowsCapture):
        capture_backend.prepare_raw_client_window(window)
        client_rect = capture_backend._client_rect(window)
        if client_rect is None or client_rect.right <= client_rect.left or client_rect.bottom <= client_rect.top:
            raise SmokeFailure("M4 cursor preparation requires a positive screen client rectangle")
        center_x = (client_rect.left + client_rect.right) // 2
        center_y = (client_rect.top + client_rect.bottom) // 2
        if not capture_backend.user32.SetCursorPos(center_x, center_y):
            raise SmokeFailure("M4 cursor preparation SetCursorPos failed")


@dataclass
class BenchmarkStartup:
    # Keep each acquired resource visible to the caller's finally block before the next step can fail.
    logserver_process: Optional[object] = None
    app_process: Optional[object] = None
    log_directory: Optional[Path] = None
    log_baseline: Mapping[Path, int] = field(default_factory=dict)
    log_pattern: str = ""
    window: Optional[object] = None


def start_benchmark(args: argparse.Namespace, mode: str, executable: Path, capture_backend, launch_args, environment, startup: BenchmarkStartup) -> None:
    startup.logserver_process, log_port, startup.log_directory, startup.log_baseline, startup.log_pattern = launch_logserver(launch_args, executable, environment)
    if not startup.log_directory:
        raise SmokeFailure(LIT_BENCHMARK_RUNNER_COULD_NOT_SELECT_A_RU)
    startup.app_process = launch_testbed(launch_args, executable, environment, log_port)

    lane = wait_for_lane_status(startup.app_process, startup.log_directory, startup.log_baseline, startup.log_pattern, args.startup_timeout,)
    lane.validate_for_mode(mode)
    wait_for_log_message(startup.app_process, startup.log_directory, startup.log_baseline, startup.log_pattern, M4_RENDER_UNFOCUSED_LOG, args.startup_timeout,)

    startup.window = capture_backend.wait_for_window(startup.app_process.pid, args.startup_timeout, args.window_title)
    if not startup.window:
        raise SmokeFailure(f"{mode} benchmark did not expose the expected window '{args.window_title}'")


def run_frame_locked_capture(args: argparse.Namespace, mode: str, executable: Path, capture_backend,) -> FrameLockedCapture:
    """Capture a settled temporal frame after an identical number of world ticks in each mode."""
    if not executable.is_file():
        raise SmokeFailure(f"{mode} executable does not exist: {executable}")

    capture_path = args.output_dir / f"{mode}.bmp"
    capture_log_path = args.output_dir / f"{mode}.capture.log"
    for path in (capture_path, capture_log_path):
        if path.exists():
            path.unlink()

    launch_args = make_runtime_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment[LIT_NWB_STRESS_TEST_SPIN_ANGLE] = args.frozen_yaw
    environment["NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME"] = str(args.pixel_capture_frames)

    startup = BenchmarkStartup()
    app_exit_code = None
    app_exit_tail = ""
    log_text = ""
    try:
        start_benchmark(args, mode, executable, capture_backend, launch_args, environment, startup)

        prepare_m4_client_area(capture_backend, startup.window)
        wait_for_log_message(startup.app_process, startup.log_directory, startup.log_baseline, startup.log_pattern, M4_PIXEL_CAPTURE_SUBMISSION_PAUSED_LOG, args.startup_timeout,)
        wait_while_running(startup.app_process, args.pixel_capture_settle_seconds, f"while settling {mode} frame-locked capture")
        capture_result = capture_m4_client_area(capture_backend, startup.window, capture_path)
        validate_capture_result(capture_result)
        app_exit_code, app_exit_tail = CaptureProcessTermination.terminate_process(startup.app_process, f"{mode} frame-locked capture", startup.window)
        startup.app_process = None
        log_text = shutdown_logserver_and_collect(startup.logserver_process, startup.log_directory, startup.log_baseline, startup.log_pattern, LIT_BENCHMARK_LOGSERVER,)
        startup.logserver_process = None
        capture_log_path.write_text(log_text, encoding=LIT_UTF_8)
    finally:
        CaptureProcessTermination.terminate_process(startup.app_process, f"{mode} frame-locked capture", startup.window)
        CaptureProcessTermination.terminate_process(startup.logserver_process, LIT_BENCHMARK_LOGSERVER)

    require_normal_process_exit(app_exit_code, app_exit_tail, LIT_TESTBED)
    if not log_text:
        raise SmokeFailure(f"{mode} frame-locked capture produced no captured logger output")

    lane = LaneStatus.parse(log_text)
    if not lane:
        raise SmokeFailure(f"{mode} frame-locked capture lost its async-lane capability log during collection")
    lane.validate_for_mode(mode)
    if M4_PIXEL_CAPTURE_READY_LOG not in log_text:
        raise SmokeFailure(f"{mode} frame-locked capture ended before its capture-ready marker was logged")
    if M4_PIXEL_CAPTURE_SUBMISSION_PAUSED_LOG not in log_text:
        raise SmokeFailure(f"{mode} frame-locked capture did not suspend submission at its capture-ready marker")
    if not capture_path.is_file():
        raise SmokeFailure(f"{mode} frame-locked capture did not write a BMP")
    return FrameLockedCapture(str(capture_path), log_text, lane)


def run_single_mode(args: argparse.Namespace, mode: str, executable: Path, symbols: Mapping[str, str], capture_backend, frame_locked_capture: Optional[FrameLockedCapture],) -> RunResult:
    if not executable.is_file():
        raise SmokeFailure(f"{mode} executable does not exist: {executable}")

    timing_path = args.output_dir / f"{mode}.timing.txt"
    log_path = args.output_dir / f"{mode}.log"
    for path in (timing_path, log_path):
        if path.exists():
            path.unlink()

    launch_args = make_runtime_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_GPU_TIMING_FILE"] = str(timing_path)
    environment[LIT_NWB_STRESS_TEST_SPIN_ANGLE] = args.frozen_yaw

    startup = BenchmarkStartup()
    app_exit_code = None
    app_exit_tail = ""
    measurement_log_text = ""
    try:
        start_benchmark(args, mode, executable, capture_backend, launch_args, environment, startup)

        wait_while_running(startup.app_process, args.warmup_seconds, f"during {mode} warmup")
        measurement_start_byte_offset = wait_for_measurement_timing_boundary(startup.app_process, timing_path, args.startup_timeout)
        wait_while_running(startup.app_process, args.measure_seconds, f"during {mode} measurement")
        app_exit_code, app_exit_tail = CaptureProcessTermination.terminate_process(startup.app_process, f"{mode} benchmark", startup.window)
        startup.app_process = None
        measurement_log_text = shutdown_logserver_and_collect(startup.logserver_process, startup.log_directory, startup.log_baseline, startup.log_pattern, LIT_BENCHMARK_LOGSERVER,)
        startup.logserver_process = None
    finally:
        CaptureProcessTermination.terminate_process(startup.app_process, f"{mode} benchmark", startup.window)
        CaptureProcessTermination.terminate_process(startup.logserver_process, LIT_BENCHMARK_LOGSERVER)

    require_normal_process_exit(app_exit_code, app_exit_tail, LIT_TESTBED)
    if not measurement_log_text:
        raise SmokeFailure(f"{mode} benchmark produced no captured logger output")

    lane = LaneStatus.parse(measurement_log_text)
    if not lane:
        raise SmokeFailure(f"{mode} benchmark lost its async-lane capability log during collection")
    lane.validate_for_mode(mode)
    if frame_locked_capture:
        log_text = ("=== frame-locked pixel capture ===\n" f"{frame_locked_capture.log_text}\n" "=== timed benchmark ===\n" f"{measurement_log_text}")
    else:
        log_text = measurement_log_text
    log_path.write_text(log_text, encoding=LIT_UTF_8)
    summaries = summarize_scopes(parse_timing_file(timing_path, symbols, measurement_start_byte_offset))
    forbidden = find_forbidden_log_messages(log_text, tuple(DEFAULT_FORBIDDEN_LOGS) + tuple(args.reject_log))
    return RunResult(mode=mode, executable=str(executable), timing_file=str(timing_path), log_file=str(log_path), capture_file=frame_locked_capture.capture_file if frame_locked_capture else None, lane=lane, scopes=summaries, forbidden_log_messages=forbidden,
        measurement_start_byte_offset=measurement_start_byte_offset,)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Run parser/verdict checks without launching the selected backend.")
    parser.add_argument(LIT_SYNC_EXECUTABLE, type=Path, help="Path to nwb_async_shadow_m4_sync_benchmark.")
    parser.add_argument(LIT_ASYNC_EXECUTABLE, type=Path, help="Path to nwb_async_shadow_m4_async_benchmark.")
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Cooked smoke runtime root used as both process working directory.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Directory for timing files, logs, captures, and reports.")
    parser.add_argument("--logserver-executable", help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Use standalone loader logs rather than a logserver.")
    parser.add_argument("--sync-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin sync binary.")
    parser.add_argument("--async-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin async binary.")
    parser.add_argument("--window-title", default="NWB Async Shadow M4 Benchmark", help="Native window title used for capture and graceful exit.")
    parser.add_argument("--frozen-yaw", default="0.6", help="NWB_STRESS_TEST_SPIN_ANGLE used for deterministic A/B captures.")
    parser.add_argument(LIT_WARMUP_SECONDS, type=float, default=4.0, help="Settling time before each timed measurement.")
    parser.add_argument(LIT_MEASURE_SECONDS, type=float, default=12.0, help="Timing collection time per mode after warmup.")
    parser.add_argument(LIT_STARTUP_TIMEOUT, type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument("--pixel-capture-frames", type=int, default=96, help="World ticks to render before the benchmark-only held-frame pixel capture.",)
    parser.add_argument(LIT_PIXEL_CAPTURE_SETTLE_SECONDS, type=float, default=0.75, help="Additional time to let the held capture frame present before it is read back.",)
    parser.add_argument("--minimum-samples", type=int, default=6, help="Minimum captured timing intervals required per rollout scope.")
    parser.add_argument(LIT_MINIMUM_SHADOW_MS, type=float, default=0.01, help="Minimum median graph-owned async shadow time.")
    parser.add_argument(LIT_MAXIMUM_FRAME_REGRESSION_PERCENT, type=float, default=3.0, help="Maximum allowed async render.frame median regression versus sync.",)
    parser.add_argument("--skip-pixel-parity", action=LIT_STORE_TRUE, help="Do not capture or compare native window pixels.")
    parser.add_argument("--maximum-pixel-max-abs", type=int, default=16, help="Maximum allowed per-channel pixel delta.")
    parser.add_argument(LIT_MAXIMUM_PIXEL_MEAN_ABS, type=float, default=0.75, help="Maximum allowed mean per-channel pixel delta.")
    parser.add_argument("--maximum-pixel-changed-fraction", type=float, help="Optional maximum fraction of changed pixels; unset tolerates widespread sub-threshold temporal noise.",)
    parser.add_argument("--gpu-validation", action=LIT_STORE_TRUE, help="Pass --gpudbg to both loader processes.")
    parser.add_argument("--reject-log", action=LIT_APPEND, default=[], help="Additional log substring that fails the run.")
    parser.add_argument("--report-only", action=LIT_STORE_TRUE, help="Write a report but return success if a rollout gate fails.")
    args = parser.parse_args(argv)

    if args.self_test:
        return args

    required = ("sync_executable", "async_executable", "runtime_dir", "output_dir")
    missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join(missing)}")
    # The native environment reader uses full-consumption std::from_chars<f32>.
    yaw_token = re.fullmatch(r"-?([0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?", args.frozen_yaw)
    if yaw_token is None:
        parser.error(LIT_FROZEN_YAW_MUST_BE_A_FINITE_FLOAT32_DE)
    if any(digit not in "0." for digit in yaw_token[1]):
        try:
            frozen_yaw = Decimal(args.frozen_yaw).copy_abs()
        except InvalidOperation:
            parser.error(LIT_FROZEN_YAW_MUST_BE_A_FINITE_FLOAT32_DE)
        # Nonzero values rounding to zero or infinity are native range errors.
        if not Decimal.from_float(2.0 ** -150) < frozen_yaw < Decimal(2 ** 128 - 2 ** 103):
            parser.error(LIT_FROZEN_YAW_MUST_BE_A_FINITE_FLOAT32_DE)
    require_non_negative(parser, LIT_WARMUP_SECONDS, args.warmup_seconds)
    require_positive(parser, LIT_MEASURE_SECONDS, args.measure_seconds)
    require_positive(parser, LIT_STARTUP_TIMEOUT, args.startup_timeout)
    if args.pixel_capture_frames <= 0:
        parser.error("--pixel-capture-frames must be positive")
    require_non_negative(parser, LIT_PIXEL_CAPTURE_SETTLE_SECONDS, args.pixel_capture_settle_seconds)
    if args.minimum_samples <= 0:
        parser.error("--minimum-samples must be positive")
    require_non_negative(parser, LIT_MINIMUM_SHADOW_MS, args.minimum_shadow_ms)
    require_non_negative(parser, LIT_MAXIMUM_FRAME_REGRESSION_PERCENT, args.maximum_frame_regression_percent)
    if args.maximum_pixel_max_abs < 0 or args.maximum_pixel_max_abs > 255:
        parser.error("--maximum-pixel-max-abs must be in [0, 255]")
    require_non_negative(parser, LIT_MAXIMUM_PIXEL_MEAN_ABS, args.maximum_pixel_mean_abs)
    if args.maximum_pixel_changed_fraction is not None and not 0.0 <= args.maximum_pixel_changed_fraction <= 1.0:
        parser.error("--maximum-pixel-changed-fraction must be in [0, 1]")

    args.sync_executable = args.sync_executable.resolve()
    args.async_executable = args.async_executable.resolve()
    args.runtime_dir = args.runtime_dir.resolve()
    args.output_dir = args.output_dir.resolve()
    args.sync_namesym = args.sync_namesym.resolve() if args.sync_namesym else None
    args.async_namesym = args.async_namesym.resolve() if args.async_namesym else None
    return args


def run_self_test() -> int:
    return async_shadow_m4_self_test.run_self_test(sys.modules[__name__])


def run(args: argparse.Namespace) -> int:
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"runtime directory does not exist: {args.runtime_dir}")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    capture_backend = None
    try:
        # Run async first so hosts without dedicated compute exit 77 early.
        capture_backend = create_capture_backend()
        async_capture = (run_frame_locked_capture(args, LIT_ASYNC, args.async_executable, capture_backend) if not args.skip_pixel_parity else None)
        async_run = run_single_mode(args, LIT_ASYNC, args.async_executable, load_name_symbols(args.async_namesym), capture_backend, async_capture,)
        sync_capture = (run_frame_locked_capture(args, LIT_SYNC, args.sync_executable, capture_backend) if not args.skip_pixel_parity else None)
        sync_run = run_single_mode(args, LIT_SYNC, args.sync_executable, load_name_symbols(args.sync_namesym), capture_backend, sync_capture,)
        json_path = args.output_dir / "m4_report.json"
        markdown_path = args.output_dir / "m4_report.md"
        reporter = AsyncShadowM4Report()
        try:
            report = reporter.evaluate_runs(args, sync_run, async_run)
        except SmokeFailure as error:
            report = { LIT_SCHEMA: LIT_NWB_ASYNC_SHADOW_M4_V3, LIT_VERDICT: LIT_FAIL, LIT_COLLECTION_ERROR: str(error), LIT_GATES: [reporter.gate("required timestamp telemetry", False, str(error))], LIT_SYNC: reporter.raw_run_payload(sync_run),
                LIT_ASYNC: reporter.raw_run_payload(async_run), LIT_PIXEL_DIFF: None, }
        reporter.write_json(json_path, report)
        reporter.write_markdown_report(markdown_path, report)
        print(f"M4 report: {markdown_path}")
        if report[LIT_VERDICT] != LIT_PASS and not args.report_only:
            return 1
        return 0
    finally:
        if capture_backend:
            capture_backend.close()


def main(argv: Sequence[str]) -> int:
    return run_smoke_main(argv, parse_args, run, self_test=run_self_test, output_stream=sys.stderr, failure_types=(SmokeFailure,))


if __name__ == LIT_MAIN:
    sys.exit(main(sys.argv[1:]))
