#!/usr/bin/env python3
"""Run the M4 async-shadow queue-validation and critical-path A/B benchmark.

The paired smoke executables are intentionally identical except that the synchronous baseline explicitly disables
the default AsyncCompute request before Vulkan device creation. This runner refuses a Graphics queue route, captures
fixed-yaw pixel output from both binaries, collects the renderer's
timestamp envelopes, and makes the M4 rollout gate explicit:

* a distinct Vulkan compute family must be active;
* the graph-owned render.async_shadow scope must contain measurable work;
* render.frame (the Graphics critical path) must not regress beyond the configured tolerance; and
* the fixed-scene output and validation log must remain clean.

The actual benchmark needs a target GPU with a dedicated compute-only family and a visible native
window. `--self-test` checks capture failures, cleanup ordering, and incomplete telemetry without either requirement.
"""

from __future__ import annotations


import argparse
import json
import math
import re
import struct
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, List, Mapping, Optional, Sequence, Tuple
from unittest import mock


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "ab"))
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from gpu_timing_parse import (  # noqa: E402
    ScopeSummary,
    load_name_symbols as load_timing_name_symbols,
    parse_timing_file,
    require_scope_samples,
    summarize_scopes,
)
from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    STRICT_LOG_FAILURE_MESSAGES,
    SmokeFailure,
    SmokeSkip,
    WindowsCapture,
    build_launch_environment,
    make_runtime_launch_args,
    collect_log_delta,
    create_capture_backend,
    ensure_process_running,
    launch_logserver,
    launch_testbed,
    require_normal_process_exit,
    shutdown_logserver_and_collect,
    terminate_process,
    validate_capture_result,
)

LIT_RENDER_FRAME = "render.frame"
LIT_RENDER_ASYNC_SHADOW = "render.async_shadow"
LIT_RENDER_ASYNC_FINAL = "render.async_final"
LIT_CANNOT_SAFELY_CONTINUE_AFTER_AN_UNRESO = "cannot safely continue after an unresolved frame recovery submission"
LIT_I = "<I"
LIT_UTF_8 = "utf-8"
LIT_COLLECTION_ERROR = "collection_error"
LIT_SYNC = "sync"
LIT_ASYNC = "async"
LIT_ASYNC_SHADOW_M4_BENCHMARK_REPORT = "# Async-shadow M4 benchmark report"
LIT_ARTIFACTS = "## Artifacts"
LIT_GATES = "gates"
LIT_PASSED = "passed"
LIT_PIXEL_DIFF = "pixel_diff"
LIT_NWB_RENDER_UNFOCUSED = "NWB_RENDER_UNFOCUSED"
LIT_NWB_STRESS_TEST_SPIN_ANGLE = "NWB_STRESS_TEST_SPIN_ANGLE"
LIT_BENCHMARK_RUNNER_COULD_NOT_SELECT_A_RU = "benchmark runner could not select a runtime-log directory"
LIT_BENCHMARK_LOGSERVER = "benchmark logserver"
LIT_TESTBED = "testbed"
LIT_TIMING_FILE = "timing_file"
LIT_LOG_FILE = "log_file"
LIT_PASS = "pass"
LIT_FAIL = "fail"
LIT_SCHEMA = "schema"
LIT_NWB_ASYNC_SHADOW_M4_V2 = "nwb.async_shadow_m4.v2"
LIT_VERDICT = "verdict"
LIT_SELF_TEST = "--self-test"
LIT_STORE_TRUE = "store_true"
LIT_WARMUP_SECONDS = "--warmup-seconds"
LIT_MEASURE_SECONDS = "--measure-seconds"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_PIXEL_CAPTURE_SETTLE_SECONDS = "--pixel-capture-settle-seconds"
LIT_MINIMUM_SHADOW_MS = "--minimum-shadow-ms"
LIT_MAXIMUM_FRAME_REGRESSION_PERCENT = "--maximum-frame-regression-percent"
LIT_MAXIMUM_PIXEL_MEAN_ABS = "--maximum-pixel-mean-abs"
LIT_PREPARE = "prepare"
LIT_DWMSETWINDOWATTRIBUTE = "DwmSetWindowAttribute"
LIT_DWMFLUSH = "DwmFlush"
LIT_APP_STOP = "app-stop"
LIT_CLEANUP_NONE = "cleanup-none"
LIT_LOGSERVER_LOG = "logserver_*.log"
LIT_LOGSERVER_HELPER = "logserver-helper"
LIT_EXIT_7 = "exit 7"
LIT_SYNC_FRAME_LOCKED_CAPTURE = "sync frame-locked capture"
LIT_SYNC_BENCHMARK = "sync benchmark"
LIT_ASYNC_LOG = "async.log"
LIT_SYNC_LOG = "sync.log"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


LANE_RE = re.compile(
    r"Vulkan:\s+async compute lane\s+requested=(true|false|yes|no)\s+effective=(true|false|yes|no)"
    r"\s+graphicsFamily=(-?\d+)\s+computeFamily=(-?\d+)",
    re.IGNORECASE,
)
REQUIRED_ASYNC_SCOPES = (
    LIT_RENDER_FRAME,
    LIT_RENDER_ASYNC_SHADOW,
    LIT_RENDER_ASYNC_FINAL,
)
DEFAULT_FORBIDDEN_LOGS = (
    *STRICT_LOG_FAILURE_MESSAGES,
    LIT_CANNOT_SAFELY_CONTINUE_AFTER_AN_UNRESO,
)
M4_PIXEL_CAPTURE_READY_LOG = "StressTestSmokeProject: M4 pixel capture ready after"
M4_PIXEL_CAPTURE_SUBMISSION_PAUSED_LOG = "render submission suspended"


class DedicatedComputeUnavailable(SmokeSkip):
    """The async binary requested a lane, but the adapter routed it through Graphics."""


@dataclass(frozen=True)
class LaneStatus:
    requested: bool
    effective: bool
    graphics_family: int
    compute_family: int


@dataclass(frozen=True)
class PixelDiff:
    width: int
    height: int
    max_abs: int
    mean_abs: float
    changed_pixels: int
    total_pixels: int

    @property
    def changed_fraction(self) -> float:
        return self.changed_pixels / self.total_pixels if self.total_pixels else 0.0


@dataclass
class RunResult:
    mode: str
    executable: str
    timing_file: str
    log_file: str
    capture_file: Optional[str]
    lane: LaneStatus
    scopes: Dict[str, ScopeSummary]
    forbidden_log_messages: List[str]


@dataclass(frozen=True)
class FrameLockedCapture:
    capture_file: str
    log_text: str
    lane: LaneStatus


def bool_from_log(value: str) -> bool:
    return value.lower() in ("true", "yes")


def parse_lane_status(log_text: str) -> Optional[LaneStatus]:
    matches = LANE_RE.findall(log_text)
    if not matches:
        return None

    requested, effective, graphics_family, compute_family = matches[-1]
    return LaneStatus(
        requested=bool_from_log(requested),
        effective=bool_from_log(effective),
        graphics_family=int(graphics_family),
        compute_family=int(compute_family),
    )


def wait_for_lane_status(
    process,
    log_directory: Path,
    log_baseline: Mapping[Path, int],
    log_pattern: str,
    timeout_seconds: float,
) -> LaneStatus:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, "while waiting for the async-lane capability log")
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        status = parse_lane_status(latest_log)
        if status:
            return status
        time.sleep(0.1)

    detail = latest_log[-4000:]
    raise SmokeFailure(f"timed out waiting for Vulkan async-lane capability log\n{detail}")


def wait_for_log_message(
    process,
    log_directory: Path,
    log_baseline: Mapping[Path, int],
    log_pattern: str,
    message: str,
    timeout_seconds: float,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, f"while waiting for benchmark log message '{message}'")
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        if message in latest_log:
            return
        time.sleep(0.1)

    detail = latest_log[-4000:]
    raise SmokeFailure(f"timed out waiting for benchmark log message '{message}'\n{detail}")


def load_name_symbols(path: Optional[Path]) -> Dict[str, str]:
    return load_timing_name_symbols(
        path,
        REQUIRED_ASYNC_SCOPES,
        missing_file_message="Name-symbol sidecar does not exist: {path}",
    )


def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
    """Read the 24-bit BMPs written by the shared native-window capture helper."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise SmokeFailure(f"capture is not a BMP: {path}")

    pixel_offset = struct.unpack_from(LIT_I, data, 10)[0]
    dib_size = struct.unpack_from(LIT_I, data, 14)[0]
    if dib_size < 40 or len(data) < 14 + dib_size:
        raise SmokeFailure(f"capture has an unsupported DIB header: {path}")

    width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
    if width <= 0 or signed_height == 0 or planes != 1 or bits_per_pixel != 24 or compression != 0:
        raise SmokeFailure(f"capture must be an uncompressed 24-bit BMP: {path}")

    height = abs(signed_height)
    source_stride = ((width * 3 + 3) // 4) * 4
    required_bytes = pixel_offset + source_stride * height
    if required_bytes > len(data):
        raise SmokeFailure(f"capture pixel data is truncated: {path}")

    rows: List[bytes] = []
    bottom_up = signed_height > 0
    for logical_row in range(height):
        source_row = height - 1 - logical_row if bottom_up else logical_row
        row_offset = pixel_offset + source_row * source_stride
        source = data[row_offset:row_offset + width * 3]
        rgb = bytearray(width * 3)
        for pixel in range(width):
            source_offset = pixel * 3
            rgb[source_offset] = source[source_offset + 2]
            rgb[source_offset + 1] = source[source_offset + 1]
            rgb[source_offset + 2] = source[source_offset]
        rows.append(bytes(rgb))
    return width, height, b"".join(rows)


def compare_bmp_rgb(first: Path, second: Path) -> PixelDiff:
    first_width, first_height, first_rgb = read_bmp_rgb(first)
    second_width, second_height, second_rgb = read_bmp_rgb(second)
    if (first_width, first_height) != (second_width, second_height):
        raise SmokeFailure(
            f"pixel-parity captures have different dimensions: {first_width}x{first_height} vs "
            f"{second_width}x{second_height}"
        )

    total_abs = 0
    max_abs = 0
    changed_pixels = 0
    for pixel_offset in range(0, len(first_rgb), 3):
        pixel_changed = False
        for channel in range(3):
            delta = abs(first_rgb[pixel_offset + channel] - second_rgb[pixel_offset + channel])
            total_abs += delta
            max_abs = max(max_abs, delta)
            pixel_changed = pixel_changed or delta != 0
        if pixel_changed:
            changed_pixels += 1

    total_pixels = first_width * first_height
    return PixelDiff(
        width=first_width,
        height=first_height,
        max_abs=max_abs,
        mean_abs=total_abs / len(first_rgb),
        changed_pixels=changed_pixels,
        total_pixels=total_pixels,
    )


def write_json(path: Path, payload: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)


def format_ms(value: float) -> str:
    return f"{value:.4f} ms"


def write_markdown_report(path: Path, report: Mapping[str, object]) -> None:
    if report.get(LIT_COLLECTION_ERROR):
        sync = report[LIT_SYNC]
        async_run = report[LIT_ASYNC]
        assert isinstance(sync, Mapping)
        assert isinstance(async_run, Mapping)
        lines = (
            LIT_ASYNC_SHADOW_M4_BENCHMARK_REPORT,
            "",
            "Verdict: **FAIL**",
            "",
            "## Incomplete telemetry",
            "",
            str(report[LIT_COLLECTION_ERROR]),
            "",
            LIT_ARTIFACTS,
            "",
            f"- Sync timing: `{sync['timing_file']}`",
            f"- Async timing: `{async_run['timing_file']}`",
            f"- Sync log: `{sync['log_file']}`",
            f"- Async log: `{async_run['log_file']}`",
        )
        path.write_text("\n".join(lines) + "\n", encoding=LIT_UTF_8)
        return

    sync = report[LIT_SYNC]
    async_run = report[LIT_ASYNC]
    assert isinstance(sync, Mapping)
    assert isinstance(async_run, Mapping)
    gates = report[LIT_GATES]
    assert isinstance(gates, Sequence)

    lines = [
        LIT_ASYNC_SHADOW_M4_BENCHMARK_REPORT,
        "",
        f"Verdict: **{report['verdict'].upper()}**",
        "",
        "| Gate | Result | Detail |",
        "| --- | --- | --- |",
    ]
    for gate in gates:
        assert isinstance(gate, Mapping)
        result = "PASS" if gate[LIT_PASSED] else "FAIL"
        lines.append(f"| {gate['name']} | {result} | {gate['detail']} |")

    lines.extend((
        "",
        "## Critical path",
        "",
        f"- Synchronous `render.frame`: {sync['frame_median_ms']:.4f} ms",
        f"- Async `render.frame`: {async_run['frame_median_ms']:.4f} ms",
        f"- Delta: {report['frame_regression_percent']:+.3f}%",
        f"- Async graph-owned shadow median: {async_run['shadow_median_ms']:.4f} ms",
        f"- Async graph-owned shadow positive samples: {async_run['shadow_positive_sample_count']}/{async_run['shadow_sample_count']}",
        "",
        LIT_ARTIFACTS,
        "",
        f"- Sync timing: `{sync['timing_file']}`",
        f"- Async timing: `{async_run['timing_file']}`",
        f"- Sync log: `{sync['log_file']}`",
        f"- Async log: `{async_run['log_file']}`",
    ))
    if report.get(LIT_PIXEL_DIFF):
        pixel_diff = report[LIT_PIXEL_DIFF]
        assert isinstance(pixel_diff, Mapping)
        lines.extend((
            f"- Pixel comparison: max abs {pixel_diff['max_abs']}, mean abs {pixel_diff['mean_abs']:.6f}, "
            f"changed {pixel_diff['changed_fraction'] * 100.0:.4f}%",
            f"- Sync capture: `{sync['capture_file']}`",
            f"- Async capture: `{async_run['capture_file']}`",
        ))

    path.write_text("\n".join(lines) + "\n", encoding=LIT_UTF_8)


def wait_while_running(process, seconds: float, stage: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))


def find_forbidden_log_messages(log_text: str, needles: Sequence[str]) -> List[str]:
    return [needle for needle in needles if needle in log_text]


def validate_lane_for_mode(mode: str, lane: LaneStatus) -> None:
    if mode == LIT_ASYNC and not lane.effective:
        raise DedicatedComputeUnavailable(
            "async-shadow M4 skipped: the requested async lane did not resolve to a distinct compute-only family "
            f"(graphics family {lane.graphics_family}, compute family {lane.compute_family})"
        )
    if mode == LIT_ASYNC and (not lane.requested or lane.graphics_family == lane.compute_family):
        raise SmokeFailure(f"async benchmark did not create a distinct requested compute lane: {lane}")
    if mode == LIT_SYNC and (lane.requested or lane.effective):
        raise SmokeFailure(f"synchronous baseline unexpectedly enabled async compute: {lane}")


def capture_m4_client_area(capture_backend, window, capture_path):
    """Capture application pixels only; Windows excludes the compositor-owned non-client frame."""
    if isinstance(capture_backend, WindowsCapture):
        return capture_backend.capture_prepared_raw_client_window(window, capture_path)
    return capture_backend.capture_client_window(window, capture_path)


def prepare_m4_client_area(capture_backend, window):
    """Stabilize the Windows M4 client capture before the held-frame marker."""
    if isinstance(capture_backend, WindowsCapture):
        capture_backend.prepare_raw_client_window(window)


def run_frame_locked_capture(
    args: argparse.Namespace,
    mode: str,
    executable: Path,
    capture_backend,
) -> FrameLockedCapture:
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
    environment[LIT_NWB_RENDER_UNFOCUSED] = "1"
    environment[LIT_NWB_STRESS_TEST_SPIN_ANGLE] = args.frozen_yaw
    environment["NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME"] = str(args.pixel_capture_frames)

    logserver_process = None
    app_process = None
    log_directory: Optional[Path] = None
    log_baseline: Mapping[Path, int] = {}
    log_pattern = ""
    app_exit_code = None
    app_exit_tail = ""
    log_text = ""
    window = None
    try:
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(
            launch_args, executable, environment
        )
        if not log_directory:
            raise SmokeFailure(LIT_BENCHMARK_RUNNER_COULD_NOT_SELECT_A_RU)
        app_process = launch_testbed(launch_args, executable, environment, log_port)

        lane = wait_for_lane_status(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            args.startup_timeout,
        )
        validate_lane_for_mode(mode, lane)

        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"{mode} benchmark did not expose the expected window '{args.window_title}'")

        prepare_m4_client_area(capture_backend, window)
        wait_for_log_message(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            M4_PIXEL_CAPTURE_SUBMISSION_PAUSED_LOG,
            args.startup_timeout,
        )
        wait_while_running(app_process, args.pixel_capture_settle_seconds, f"while settling {mode} frame-locked capture")
        capture_result = capture_m4_client_area(capture_backend, window, capture_path)
        validate_capture_result(capture_result)
        app_exit_code, app_exit_tail = terminate_process(app_process, f"{mode} frame-locked capture", window)
        app_process = None
        log_text = shutdown_logserver_and_collect(
            logserver_process,
            log_directory,
            log_baseline,
            log_pattern,
            LIT_BENCHMARK_LOGSERVER,
        )
        logserver_process = None
        capture_log_path.write_text(log_text, encoding=LIT_UTF_8)
    finally:
        terminate_process(app_process, f"{mode} frame-locked capture", window)
        terminate_process(logserver_process, LIT_BENCHMARK_LOGSERVER)

    require_normal_process_exit(app_exit_code, app_exit_tail, LIT_TESTBED)
    if not log_text:
        raise SmokeFailure(f"{mode} frame-locked capture produced no captured logger output")

    lane = parse_lane_status(log_text)
    if not lane:
        raise SmokeFailure(f"{mode} frame-locked capture lost its async-lane capability log during collection")
    validate_lane_for_mode(mode, lane)
    if M4_PIXEL_CAPTURE_READY_LOG not in log_text:
        raise SmokeFailure(f"{mode} frame-locked capture ended before its capture-ready marker was logged")
    if M4_PIXEL_CAPTURE_SUBMISSION_PAUSED_LOG not in log_text:
        raise SmokeFailure(f"{mode} frame-locked capture did not suspend submission at its capture-ready marker")
    if not capture_path.is_file():
        raise SmokeFailure(f"{mode} frame-locked capture did not write a BMP")
    return FrameLockedCapture(str(capture_path), log_text, lane)


def run_single_mode(
    args: argparse.Namespace,
    mode: str,
    executable: Path,
    symbols: Mapping[str, str],
    capture_backend,
    frame_locked_capture: Optional[FrameLockedCapture],
) -> RunResult:
    if not executable.is_file():
        raise SmokeFailure(f"{mode} executable does not exist: {executable}")

    timing_path = args.output_dir / f"{mode}.timing.txt"
    log_path = args.output_dir / f"{mode}.log"
    for path in (timing_path, log_path):
        if path.exists():
            path.unlink()

    launch_args = make_runtime_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment[LIT_NWB_RENDER_UNFOCUSED] = "1"
    environment["NWB_GPU_TIMING_FILE"] = str(timing_path)
    environment[LIT_NWB_STRESS_TEST_SPIN_ANGLE] = args.frozen_yaw

    logserver_process = None
    app_process = None
    log_directory: Optional[Path] = None
    log_baseline: Mapping[Path, int] = {}
    log_pattern = ""
    app_exit_code = None
    app_exit_tail = ""
    measurement_log_text = ""
    window = None
    try:
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(
            launch_args, executable, environment
        )
        if not log_directory:
            raise SmokeFailure(LIT_BENCHMARK_RUNNER_COULD_NOT_SELECT_A_RU)
        app_process = launch_testbed(launch_args, executable, environment, log_port)

        lane = wait_for_lane_status(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            args.startup_timeout,
        )
        validate_lane_for_mode(mode, lane)

        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"{mode} benchmark did not expose the expected window '{args.window_title}'")

        wait_while_running(app_process, args.warmup_seconds, f"during {mode} warmup")
        wait_while_running(app_process, args.measure_seconds, f"during {mode} measurement")
        app_exit_code, app_exit_tail = terminate_process(app_process, f"{mode} benchmark", window)
        app_process = None
        measurement_log_text = shutdown_logserver_and_collect(
            logserver_process,
            log_directory,
            log_baseline,
            log_pattern,
            LIT_BENCHMARK_LOGSERVER,
        )
        logserver_process = None
    finally:
        terminate_process(app_process, f"{mode} benchmark", window)
        terminate_process(logserver_process, LIT_BENCHMARK_LOGSERVER)

    require_normal_process_exit(app_exit_code, app_exit_tail, LIT_TESTBED)
    if not measurement_log_text:
        raise SmokeFailure(f"{mode} benchmark produced no captured logger output")

    lane = parse_lane_status(measurement_log_text)
    if not lane:
        raise SmokeFailure(f"{mode} benchmark lost its async-lane capability log during collection")
    validate_lane_for_mode(mode, lane)
    if frame_locked_capture:
        log_text = (
            "=== frame-locked pixel capture ===\n"
            f"{frame_locked_capture.log_text}\n"
            "=== timed benchmark ===\n"
            f"{measurement_log_text}"
        )
    else:
        log_text = measurement_log_text
    log_path.write_text(log_text, encoding=LIT_UTF_8)
    summaries = summarize_scopes(parse_timing_file(timing_path, symbols))
    forbidden = find_forbidden_log_messages(log_text, tuple(DEFAULT_FORBIDDEN_LOGS) + tuple(args.reject_log))
    return RunResult(
        mode=mode,
        executable=str(executable),
        timing_file=str(timing_path),
        log_file=str(log_path),
        capture_file=frame_locked_capture.capture_file if frame_locked_capture else None,
        lane=lane,
        scopes=summaries,
        forbidden_log_messages=forbidden,
    )


def gate(name: str, passed: bool, detail: str) -> Dict[str, object]:
    return {"name": name, LIT_PASSED: passed, "detail": detail}


def raw_run_payload(run: RunResult) -> Dict[str, object]:
    return {
        "executable": run.executable,
        LIT_TIMING_FILE: run.timing_file,
        LIT_LOG_FILE: run.log_file,
        "capture_file": run.capture_file,
        "lane": asdict(run.lane),
        "forbidden_log_messages": run.forbidden_log_messages,
        "scopes": {name: asdict(summary) for name, summary in run.scopes.items()},
    }


def raw_pixel_diff_payload(pixel_diff: PixelDiff) -> Dict[str, object]:
    payload = asdict(pixel_diff)
    payload["changed_fraction"] = pixel_diff.changed_fraction
    return payload


def evaluate_runs(args: argparse.Namespace, sync: RunResult, async_run: RunResult) -> Dict[str, object]:
    sync_frame = require_scope_samples(sync.scopes, LIT_RENDER_FRAME, args.minimum_samples, Path(sync.timing_file))
    async_frame = require_scope_samples(async_run.scopes, LIT_RENDER_FRAME, args.minimum_samples, Path(async_run.timing_file))
    shadow = require_scope_samples(
        async_run.scopes,
        LIT_RENDER_ASYNC_SHADOW,
        args.minimum_samples,
        Path(async_run.timing_file),
    )
    for scope in REQUIRED_ASYNC_SCOPES:
        require_scope_samples(async_run.scopes, scope, args.minimum_samples, Path(async_run.timing_file))

    regression_percent = (
        (async_frame.median_ms - sync_frame.median_ms) * 100.0 / sync_frame.median_ms
        if sync_frame.median_ms > 0.0
        else math.inf
    )
    shadow_scope_passed = shadow.median_ms >= args.minimum_shadow_ms
    timing_passed = regression_percent <= args.maximum_frame_regression_percent

    pixel_diff: Optional[PixelDiff] = None
    pixel_passed = True
    pixel_detail = "pixel parity disabled by --skip-pixel-parity"
    if not args.skip_pixel_parity:
        assert sync.capture_file and async_run.capture_file
        pixel_diff = compare_bmp_rgb(Path(sync.capture_file), Path(async_run.capture_file))
        pixel_passed = (
            pixel_diff.max_abs <= args.maximum_pixel_max_abs
            and pixel_diff.mean_abs <= args.maximum_pixel_mean_abs
            and (
                args.maximum_pixel_changed_fraction is None
                or pixel_diff.changed_fraction <= args.maximum_pixel_changed_fraction
            )
        )
        pixel_detail = (
            f"max abs {pixel_diff.max_abs}/{args.maximum_pixel_max_abs}, mean abs "
            f"{pixel_diff.mean_abs:.6f}/{args.maximum_pixel_mean_abs:.6f}, changed "
            f"{pixel_diff.changed_fraction * 100.0:.4f}%"
        )

    logs_passed = not sync.forbidden_log_messages and not async_run.forbidden_log_messages
    gates = [
        gate(
            "dedicated async lane",
            async_run.lane.requested
            and async_run.lane.effective
            and async_run.lane.graphics_family != async_run.lane.compute_family,
            f"requested={async_run.lane.requested}, effective={async_run.lane.effective}, "
            f"families={async_run.lane.graphics_family}/{async_run.lane.compute_family}",
        ),
        gate(
            "graph-owned shadow timing",
            shadow_scope_passed,
            f"median {format_ms(shadow.median_ms)} (min {args.minimum_shadow_ms:.4f} ms), "
            f"positive {shadow.positive_sample_count}/{shadow.sample_count}",
        ),
        gate(
            "critical path",
            timing_passed,
            f"sync {format_ms(sync_frame.median_ms)}, async {format_ms(async_frame.median_ms)}, "
            f"delta {regression_percent:+.3f}% (limit +{args.maximum_frame_regression_percent:.3f}%)",
        ),
        gate("pixel parity", pixel_passed, pixel_detail),
        gate(
            "runtime and validation logs",
            logs_passed,
            "no forbidden log messages"
            if logs_passed
            else f"sync={sync.forbidden_log_messages}, async={async_run.forbidden_log_messages}",
        ),
    ]
    verdict = LIT_PASS if all(bool(item[LIT_PASSED]) for item in gates) else LIT_FAIL

    def compact_run(run: RunResult, frame: ScopeSummary, shadow_scope: Optional[ScopeSummary] = None) -> Dict[str, object]:
        result = raw_run_payload(run)
        result.update({
            "frame_median_ms": frame.median_ms,
            "frame_sample_count": frame.sample_count,
        })
        if shadow_scope:
            result.update(
                shadow_median_ms=shadow_scope.median_ms,
                shadow_sample_count=shadow_scope.sample_count,
                shadow_positive_sample_count=shadow_scope.positive_sample_count,
            )
        return result

    return {
        LIT_SCHEMA: LIT_NWB_ASYNC_SHADOW_M4_V2,
        LIT_VERDICT: verdict,
        LIT_GATES: gates,
        "frame_regression_percent": regression_percent,
        LIT_SYNC: compact_run(sync, sync_frame),
        LIT_ASYNC: compact_run(async_run, async_frame, shadow),
        LIT_PIXEL_DIFF: raw_pixel_diff_payload(pixel_diff) if pixel_diff else None,
    }


def require_positive(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value <= 0.0:
        parser.error(f"{option} must be positive")


def require_non_negative(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value < 0.0:
        parser.error(f"{option} must not be negative")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Run parser/verdict checks without launching Vulkan.")
    parser.add_argument("--sync-executable", type=Path, help="Path to nwb_async_shadow_m4_sync_benchmark.")
    parser.add_argument("--async-executable", type=Path, help="Path to nwb_async_shadow_m4_async_benchmark.")
    parser.add_argument("--runtime-dir", type=Path, help="Cooked smoke runtime root used as both process working directory.")
    parser.add_argument("--output-dir", type=Path, help="Directory for timing files, logs, captures, and reports.")
    parser.add_argument("--logserver-executable", help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Use standalone loader logs rather than a logserver.")
    parser.add_argument("--sync-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin sync binary.")
    parser.add_argument("--async-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin async binary.")
    parser.add_argument("--window-title", default="NWB Async Shadow M4 Benchmark", help="Native window title used for capture and graceful exit.")
    parser.add_argument("--frozen-yaw", default="0.6", help="NWB_STRESS_TEST_SPIN_ANGLE used for deterministic A/B captures.")
    parser.add_argument(LIT_WARMUP_SECONDS, type=float, default=4.0, help="Settling time before each timed measurement.")
    parser.add_argument(LIT_MEASURE_SECONDS, type=float, default=12.0, help="Timing collection time per mode after warmup.")
    parser.add_argument(LIT_STARTUP_TIMEOUT, type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument(
        "--pixel-capture-frames",
        type=int,
        default=96,
        help="World ticks to render before the benchmark-only held-frame pixel capture.",
    )
    parser.add_argument(
        LIT_PIXEL_CAPTURE_SETTLE_SECONDS,
        type=float,
        default=0.75,
        help="Additional time to let the held capture frame present before it is read back.",
    )
    parser.add_argument("--minimum-samples", type=int, default=6, help="Minimum captured timing intervals required per rollout scope.")
    parser.add_argument(LIT_MINIMUM_SHADOW_MS, type=float, default=0.01, help="Minimum median graph-owned async shadow time.")
    parser.add_argument(
        LIT_MAXIMUM_FRAME_REGRESSION_PERCENT,
        type=float,
        default=3.0,
        help="Maximum allowed async render.frame median regression versus sync.",
    )
    parser.add_argument("--skip-pixel-parity", action=LIT_STORE_TRUE, help="Do not capture or compare native window pixels.")
    parser.add_argument("--maximum-pixel-max-abs", type=int, default=16, help="Maximum allowed per-channel pixel delta.")
    parser.add_argument(LIT_MAXIMUM_PIXEL_MEAN_ABS, type=float, default=0.75, help="Maximum allowed mean per-channel pixel delta.")
    parser.add_argument(
        "--maximum-pixel-changed-fraction",
        type=float,
        help="Optional maximum fraction of changed pixels; unset tolerates widespread sub-threshold temporal noise.",
    )
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
    capture_args = parse_args([LIT_SELF_TEST])

    class ClientRectUser32:
        def __init__(self, get_client_rect_result=True, client_to_screen_result=True):
            self.get_client_rect_result = get_client_rect_result
            self.client_to_screen_result = client_to_screen_result
            self.get_client_rect_calls = 0
            self.client_to_screen_calls = 0

        def GetClientRect(self, hwnd, rect_pointer):
            del hwnd
            self.get_client_rect_calls += 1
            if not self.get_client_rect_result:
                return 0
            rect = rect_pointer._obj
            rect.left = 0
            rect.top = 0
            rect.right = 1280
            rect.bottom = 900
            return 1

        def ClientToScreen(self, hwnd, point_pointer):
            del hwnd
            self.client_to_screen_calls += 1
            if not self.client_to_screen_result:
                return 0
            point = point_pointer._obj
            point.x += 104
            point.y += 73
            return 1

    client_rect_failure_user32 = ClientRectUser32(get_client_rect_result=False)
    client_rect_failure_capture = object.__new__(WindowsCapture)
    client_rect_failure_capture.user32 = client_rect_failure_user32
    assert client_rect_failure_capture._client_rect(17) is None
    assert client_rect_failure_user32.get_client_rect_calls == 1
    assert client_rect_failure_user32.client_to_screen_calls == 0

    client_origin_failure_user32 = ClientRectUser32(client_to_screen_result=False)
    client_origin_failure_capture = object.__new__(WindowsCapture)
    client_origin_failure_capture.user32 = client_origin_failure_user32
    assert client_origin_failure_capture._client_rect(17) is None
    assert client_origin_failure_user32.get_client_rect_calls == 1
    assert client_origin_failure_user32.client_to_screen_calls == 1

    class DwmApi:
        def __init__(self, calls, set_result=0, flush_result=0):
            self.calls = calls
            self.set_result = set_result
            self.flush_result = flush_result

        def DwmSetWindowAttribute(self, hwnd, attribute, preference_pointer, preference_size):
            self.calls.append((LIT_DWMSETWINDOWATTRIBUTE, hwnd.value, attribute, preference_pointer._obj.value, preference_size))
            return self.set_result

        def DwmFlush(self):
            self.calls.append((LIT_DWMFLUSH,))
            return self.flush_result

    unsupported_calls = []
    unsupported_capture = object.__new__(WindowsCapture)
    unsupported_capture.dwmapi = DwmApi(unsupported_calls, -2147024809)
    unsupported_capture._prepare_capture_window = lambda hwnd: unsupported_calls.append((LIT_PREPARE, hwnd))
    try:
        unsupported_capture.prepare_raw_client_window(17)
    except SmokeSkip as error:
        assert "Windows 11 build 22000 or later" in str(error)
        assert "0x80070057" in str(error)
    else:
        raise AssertionError("M4 DWM setup accepted an unsupported corner-preference attribute")
    assert unsupported_calls == [
        (LIT_PREPARE, 17),
        (LIT_DWMSETWINDOWATTRIBUTE, 17, 33, 1, 4),
    ]

    negative_failure_calls = []
    negative_failure_capture = object.__new__(WindowsCapture)
    negative_failure_capture.dwmapi = DwmApi(negative_failure_calls, -1)
    negative_failure_capture._prepare_capture_window = lambda hwnd: negative_failure_calls.append((LIT_PREPARE, hwnd))
    try:
        negative_failure_capture.prepare_raw_client_window(17)
    except SmokeFailure as error:
        assert LIT_DWMSETWINDOWATTRIBUTE in str(error)
        assert "HRESULT 0xFFFFFFFF" in str(error)
    else:
        raise AssertionError("M4 DWM setup accepted a negative DwmSetWindowAttribute failure")
    assert len(negative_failure_calls) == 2

    for set_result, flush_result, expected_operation, expected_calls in (
        (1, 0, LIT_DWMSETWINDOWATTRIBUTE, 2),
        (0, 1, LIT_DWMFLUSH, 3),
    ):
        failure_calls = []
        failure_capture = object.__new__(WindowsCapture)
        failure_capture.dwmapi = DwmApi(failure_calls, set_result, flush_result)
        failure_capture._prepare_capture_window = lambda hwnd: failure_calls.append((LIT_PREPARE, hwnd))
        try:
            failure_capture.prepare_raw_client_window(17)
        except SmokeFailure as error:
            assert expected_operation in str(error)
            assert "HRESULT 0x00000001" in str(error)
        else:
            raise AssertionError(f"M4 DWM setup accepted non-S_OK {expected_operation} result")
        assert len(failure_calls) == expected_calls

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        module = sys.modules[__name__]
        executable = root / "orchestration.exe"
        executable.write_bytes(b"exe")
        capture_args.output_dir = root
        capture_args.runtime_dir = root
        app = SimpleNamespace(pid=4321, poll=lambda: None)
        logserver = object()
        baseline = {root / "old.log": 7}
        backend = mock.Mock()
        backend.wait_for_window.return_value = 17
        backend.capture_client_window.return_value = SimpleNamespace(
            appears_empty_or_white=False,
            has_pixel_variation=True,
        )
        orchestration_lane = LaneStatus(False, False, 0, 1)
        events = []

        def terminate(process, name, window_handle=None):
            if process is app:
                events.append((LIT_APP_STOP, name, window_handle))
                return 7, "simulated abnormal exit"
            assert process is None
            events.append((LIT_CLEANUP_NONE, name, window_handle))
            return None, ""

        def shutdown(process, log_directory, received_baseline, pattern, shutdown_name="logserver"):
            assert process is logserver
            assert events[-1][0] == LIT_APP_STOP
            events.append((LIT_LOGSERVER_HELPER, shutdown_name))
            return "captured runtime evidence"

        with mock.patch.object(module, "build_launch_environment", return_value={}), \
             mock.patch.object(module, "launch_logserver", return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
             mock.patch.object(module, "launch_testbed", return_value=app), \
             mock.patch.object(module, "wait_for_lane_status", return_value=orchestration_lane), \
             mock.patch.object(module, "wait_for_log_message"), \
             mock.patch.object(module, "wait_while_running"), \
             mock.patch.object(module, "terminate_process", side_effect=terminate) as terminate_mock, \
             mock.patch.object(module, "shutdown_logserver_and_collect", side_effect=shutdown):
            try:
                run_frame_locked_capture(capture_args, LIT_SYNC, executable, backend)
            except SmokeFailure as error:
                assert LIT_EXIT_7 in str(error)
            else:
                raise AssertionError("frame-locked orchestration accepted an abnormal Testbed exit")

            try:
                run_single_mode(capture_args, LIT_SYNC, executable, {}, backend, None)
            except SmokeFailure as error:
                assert LIT_EXIT_7 in str(error)
            else:
                raise AssertionError("timed orchestration accepted an abnormal Testbed exit")

        assert events == [
            (LIT_APP_STOP, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17),
            (LIT_LOGSERVER_HELPER, LIT_BENCHMARK_LOGSERVER),
            (LIT_CLEANUP_NONE, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17),
            (LIT_CLEANUP_NONE, LIT_BENCHMARK_LOGSERVER, None),
            (LIT_APP_STOP, LIT_SYNC_BENCHMARK, 17),
            (LIT_LOGSERVER_HELPER, LIT_BENCHMARK_LOGSERVER),
            (LIT_CLEANUP_NONE, LIT_SYNC_BENCHMARK, 17),
            (LIT_CLEANUP_NONE, LIT_BENCHMARK_LOGSERVER, None),
        ]
        assert terminate_mock.mock_calls == [
            mock.call(app, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17),
            mock.call(None, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17),
            mock.call(None, LIT_BENCHMARK_LOGSERVER),
            mock.call(app, LIT_SYNC_BENCHMARK, 17),
            mock.call(None, LIT_SYNC_BENCHMARK, 17),
            mock.call(None, LIT_BENCHMARK_LOGSERVER),
        ]

        # The prefix envelope is diagnostic-only: it is absent when the compiler splits its endpoints into separate
        # submissions. The rollout gate must still accept complete frame/shadow/final timing in that valid topology.
        stable_scope = ScopeSummary(6, 6, 1.0, 1.0, 1.0, 1.0)
        async_run = RunResult(
            mode=LIT_ASYNC,
            executable="async.exe",
            timing_file="async.timing.txt",
            log_file=LIT_ASYNC_LOG,
            capture_file=None,
            lane=LaneStatus(True, True, 0, 1),
            scopes={
                LIT_RENDER_FRAME: stable_scope,
                LIT_RENDER_ASYNC_SHADOW: stable_scope,
                LIT_RENDER_ASYNC_FINAL: stable_scope,
            },
            forbidden_log_messages=[],
        )
        sync_run = RunResult(
            mode=LIT_SYNC,
            executable="sync.exe",
            timing_file="sync.timing.txt",
            log_file=LIT_SYNC_LOG,
            capture_file=None,
            lane=LaneStatus(False, False, 0, 1),
            scopes={LIT_RENDER_FRAME: stable_scope},
            forbidden_log_messages=[],
        )
        capture_args.skip_pixel_parity = True
        assert evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == LIT_PASS

    print("async-shadow M4 harness self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"runtime directory does not exist: {args.runtime_dir}")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    capture_backend = None
    try:
        # Run async first. A no-dedicated-compute host exits 77 before spending time on an A/B whose asynchronous half
        # would only exercise the intentional Graphics queue route.
        capture_backend = create_capture_backend()
        async_capture = (
            run_frame_locked_capture(args, LIT_ASYNC, args.async_executable, capture_backend)
            if not args.skip_pixel_parity
            else None
        )
        async_run = run_single_mode(
            args,
            LIT_ASYNC,
            args.async_executable,
            load_name_symbols(args.async_namesym),
            capture_backend,
            async_capture,
        )
        sync_capture = (
            run_frame_locked_capture(args, LIT_SYNC, args.sync_executable, capture_backend)
            if not args.skip_pixel_parity
            else None
        )
        sync_run = run_single_mode(
            args,
            LIT_SYNC,
            args.sync_executable,
            load_name_symbols(args.sync_namesym),
            capture_backend,
            sync_capture,
        )
        json_path = args.output_dir / "m4_report.json"
        markdown_path = args.output_dir / "m4_report.md"
        try:
            report = evaluate_runs(args, sync_run, async_run)
        except SmokeFailure as error:
            report = {
                LIT_SCHEMA: LIT_NWB_ASYNC_SHADOW_M4_V2,
                LIT_VERDICT: LIT_FAIL,
                LIT_COLLECTION_ERROR: str(error),
                LIT_GATES: [gate("required timestamp telemetry", False, str(error))],
                LIT_SYNC: raw_run_payload(sync_run),
                LIT_ASYNC: raw_run_payload(async_run),
                LIT_PIXEL_DIFF: None,
            }
        write_json(json_path, report)
        write_markdown_report(markdown_path, report)
        print(f"M4 report: {markdown_path}")
        if report[LIT_VERDICT] != LIT_PASS and not args.report_only:
            return 1
        return 0
    finally:
        if capture_backend:
            capture_backend.close()


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    try:
        return run(args)
    except DedicatedComputeUnavailable as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeSkip as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    sys.exit(main(sys.argv[1:]))
