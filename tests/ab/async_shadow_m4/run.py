#!/usr/bin/env python3
"""Run the M4 async-shadow queue-validation and critical-path A/B benchmark.

The paired smoke executables are intentionally identical except that the synchronous baseline explicitly disables
the default AsyncCompute request before Vulkan device creation. This runner refuses a Graphics queue route, captures
fixed-yaw pixel output from both binaries, collects the renderer's
timestamp envelopes, and makes the M4 rollout gate explicit:

* a distinct Vulkan compute family must be active;
* render.async_shadow_effects_overlap must contain measurable positive overlap;
* render.frame (the Graphics critical path) must not regress beyond the configured tolerance; and
* the fixed-scene output and validation log must remain clean.

The actual benchmark needs a target GPU with a dedicated compute-only family and a visible native
window. `--self-test` exercises parsing, statistics, and image comparison without either requirement.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import struct
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Tuple


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    collect_log_delta,
    create_capture_backend,
    ensure_process_running,
    launch_logserver,
    launch_testbed,
    require_normal_testbed_exit,
    terminate_process,
    validate_capture_result,
)


LANE_RE = re.compile(
    r"Vulkan:\s+async compute lane\s+requested=(true|false|yes|no)\s+effective=(true|false|yes|no)"
    r"\s+graphicsFamily=(-?\d+)\s+computeFamily=(-?\d+)",
    re.IGNORECASE,
)
INTERVAL_RE = re.compile(
    r"^=== interval:\s+(?P<frames>\d+)\s+frames\s+/\s+(?P<seconds>[-+0-9.eE]+)s\s+===$"
)
SCOPE_RE = re.compile(
    r"^\s{2}(?P<scope>[^:]+):\s+avg=(?P<average>[-+0-9.eE]+)"
    r"\s+min=(?P<minimum>[-+0-9.eE]+)\s+max=(?P<maximum>[-+0-9.eE]+)"
    r"\s+samples=(?P<samples>\d+)\s*$"
)

REQUIRED_ASYNC_SCOPES = (
    "render.frame",
    "render.async_prefix",
    "render.async_shadow",
    "render.async_effects",
    "render.async_final",
    "render.async_shadow_effects_overlap",
)
DEFAULT_FORBIDDEN_LOGS = (
    "[ERROR]",
    "VUID-",
    "Validation Error",
    "async shadow ownership recovery failed",
    "cannot safely continue after an unresolved async shadow ownership release",
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
class ScopeSummary:
    sample_count: int
    positive_sample_count: int
    median_ms: float
    mean_ms: float
    min_ms: float
    max_ms: float


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
    if not path:
        return {}
    if not path.is_file():
        raise SmokeFailure(f"Name-symbol sidecar does not exist: {path}")

    decoded: Dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = raw_line.split("\t")
        if len(fields) >= 3 and fields[2]:
            decoded[fields[0]] = fields[2]
    return decoded


def parse_timing_file(path: Path, symbols: Mapping[str, str]) -> List[Dict[str, float]]:
    if not path.is_file():
        raise SmokeFailure(f"GPU timing file was not written: {path}")

    intervals: List[Dict[str, float]] = []
    current: Optional[Dict[str, float]] = None
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if INTERVAL_RE.match(raw_line):
            if current:
                intervals.append(current)
            current = {}
            continue

        match = SCOPE_RE.match(raw_line)
        if not match or current is None:
            continue

        raw_scope = match.group("scope")
        scope = symbols.get(raw_scope, raw_scope)
        try:
            current[scope] = float(match.group("average"))
        except ValueError as error:
            raise SmokeFailure(f"invalid GPU timing line in {path}: {raw_line}") from error

    if current:
        intervals.append(current)
    return intervals


def summarize_samples(values: Sequence[float]) -> ScopeSummary:
    if not values:
        return ScopeSummary(0, 0, 0.0, 0.0, 0.0, 0.0)
    return ScopeSummary(
        sample_count=len(values),
        positive_sample_count=sum(value > 1.0e-6 for value in values),
        median_ms=statistics.median(values),
        mean_ms=statistics.fmean(values),
        min_ms=min(values),
        max_ms=max(values),
    )


def summarize_scopes(intervals: Iterable[Mapping[str, float]]) -> Dict[str, ScopeSummary]:
    samples: Dict[str, List[float]] = {}
    for interval in intervals:
        for scope, value in interval.items():
            samples.setdefault(scope, []).append(value)
    return {scope: summarize_samples(values) for scope, values in sorted(samples.items())}


def require_scope_samples(
    summaries: Mapping[str, ScopeSummary],
    scope: str,
    minimum_samples: int,
    timing_file: Path,
) -> ScopeSummary:
    summary = summaries.get(scope)
    if summary and summary.sample_count >= minimum_samples:
        return summary

    observed = ", ".join(summaries) or "none"
    raise SmokeFailure(
        f"required timing scope '{scope}' has fewer than {minimum_samples} samples in {timing_file}; "
        f"observed scopes: {observed}. Build dbg/namesym or pass the matching --*-namesym sidecar."
    )


def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
    """Read the 24-bit BMPs written by the shared native-window capture helper."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise SmokeFailure(f"capture is not a BMP: {path}")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
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
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def format_ms(value: float) -> str:
    return f"{value:.4f} ms"


def write_markdown_report(path: Path, report: Mapping[str, object]) -> None:
    if report.get("collection_error"):
        sync = report["sync"]
        async_run = report["async"]
        assert isinstance(sync, Mapping)
        assert isinstance(async_run, Mapping)
        lines = (
            "# Async-shadow M4 benchmark report",
            "",
            "Verdict: **FAIL**",
            "",
            "## Incomplete telemetry",
            "",
            str(report["collection_error"]),
            "",
            "## Artifacts",
            "",
            f"- Sync timing: `{sync['timing_file']}`",
            f"- Async timing: `{async_run['timing_file']}`",
            f"- Sync log: `{sync['log_file']}`",
            f"- Async log: `{async_run['log_file']}`",
        )
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return

    sync = report["sync"]
    async_run = report["async"]
    assert isinstance(sync, Mapping)
    assert isinstance(async_run, Mapping)
    gates = report["gates"]
    assert isinstance(gates, Sequence)

    lines = [
        "# Async-shadow M4 benchmark report",
        "",
        f"Verdict: **{report['verdict'].upper()}**",
        "",
        "| Gate | Result | Detail |",
        "| --- | --- | --- |",
    ]
    for gate in gates:
        assert isinstance(gate, Mapping)
        result = "PASS" if gate["passed"] else "FAIL"
        lines.append(f"| {gate['name']} | {result} | {gate['detail']} |")

    lines.extend((
        "",
        "## Critical path",
        "",
        f"- Synchronous `render.frame`: {sync['frame_median_ms']:.4f} ms",
        f"- Async `render.frame`: {async_run['frame_median_ms']:.4f} ms",
        f"- Delta: {report['frame_regression_percent']:+.3f}%",
        f"- Async overlap median: {async_run['overlap_median_ms']:.4f} ms",
        f"- Async overlap positive samples: {async_run['overlap_positive_sample_count']}/{async_run['overlap_sample_count']}",
        "",
        "## Artifacts",
        "",
        f"- Sync timing: `{sync['timing_file']}`",
        f"- Async timing: `{async_run['timing_file']}`",
        f"- Sync log: `{sync['log_file']}`",
        f"- Async log: `{async_run['log_file']}`",
    ))
    if report.get("pixel_diff"):
        pixel_diff = report["pixel_diff"]
        assert isinstance(pixel_diff, Mapping)
        lines.extend((
            f"- Pixel comparison: max abs {pixel_diff['max_abs']}, mean abs {pixel_diff['mean_abs']:.6f}, "
            f"changed {pixel_diff['changed_fraction'] * 100.0:.4f}%",
            f"- Sync capture: `{sync['capture_file']}`",
            f"- Async capture: `{async_run['capture_file']}`",
        ))

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_launch_args(args: argparse.Namespace) -> SimpleNamespace:
    return SimpleNamespace(
        no_logserver=args.no_logserver,
        logserver_executable=args.logserver_executable,
        log_port=0,
        working_directory=args.runtime_dir,
        timeout=args.startup_timeout,
        application_arg=["--gpudbg"] if args.gpu_validation else [],
        software_vulkan="off",
    )


def wait_while_running(process, seconds: float, stage: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))


def find_forbidden_log_messages(log_text: str, needles: Sequence[str]) -> List[str]:
    return [needle for needle in needles if needle in log_text]


def validate_lane_for_mode(mode: str, lane: LaneStatus) -> None:
    if mode == "async" and not lane.effective:
        raise DedicatedComputeUnavailable(
            "async-shadow M4 skipped: the requested async lane did not resolve to a distinct compute-only family "
            f"(graphics family {lane.graphics_family}, compute family {lane.compute_family})"
        )
    if mode == "async" and (not lane.requested or lane.graphics_family == lane.compute_family):
        raise SmokeFailure(f"async benchmark did not create a distinct requested compute lane: {lane}")
    if mode == "sync" and (lane.requested or lane.effective):
        raise SmokeFailure(f"synchronous baseline unexpectedly enabled async compute: {lane}")


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

    launch_args = make_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment["NWB_STRESS_TEST_SPIN_ANGLE"] = args.frozen_yaw
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
            raise SmokeFailure("benchmark runner could not select a runtime-log directory")
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

        wait_for_log_message(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            M4_PIXEL_CAPTURE_SUBMISSION_PAUSED_LOG,
            args.startup_timeout,
        )
        wait_while_running(app_process, args.pixel_capture_settle_seconds, f"while settling {mode} frame-locked capture")
        capture_result = capture_backend.capture_window(window, capture_path)
        validate_capture_result(capture_result)
    finally:
        if app_process:
            app_exit_code, app_exit_tail = terminate_process(app_process, f"{mode} frame-locked capture", window)
        # Let client messages written during normal teardown land before stopping the server and reading its delta.
        time.sleep(0.5)
        if log_directory:
            log_text = collect_log_delta(log_directory, log_baseline, log_pattern)
            capture_log_path.write_text(log_text, encoding="utf-8")
        terminate_process(logserver_process, "benchmark logserver")

    require_normal_testbed_exit(app_exit_code, app_exit_tail)
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

    launch_args = make_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment["NWB_GPU_TIMING_FILE"] = str(timing_path)
    environment["NWB_STRESS_TEST_SPIN_ANGLE"] = args.frozen_yaw

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
            raise SmokeFailure("benchmark runner could not select a runtime-log directory")
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
    finally:
        if app_process:
            app_exit_code, app_exit_tail = terminate_process(app_process, f"{mode} benchmark", window)
        # Let client messages written during normal teardown land before stopping the server and reading its delta.
        time.sleep(0.5)
        if log_directory:
            measurement_log_text = collect_log_delta(log_directory, log_baseline, log_pattern)
        terminate_process(logserver_process, "benchmark logserver")

    require_normal_testbed_exit(app_exit_code, app_exit_tail)
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
    log_path.write_text(log_text, encoding="utf-8")
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
    return {"name": name, "passed": passed, "detail": detail}


def raw_run_payload(run: RunResult) -> Dict[str, object]:
    return {
        "executable": run.executable,
        "timing_file": run.timing_file,
        "log_file": run.log_file,
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
    sync_frame = require_scope_samples(sync.scopes, "render.frame", args.minimum_samples, Path(sync.timing_file))
    async_frame = require_scope_samples(async_run.scopes, "render.frame", args.minimum_samples, Path(async_run.timing_file))
    overlap = require_scope_samples(
        async_run.scopes,
        "render.async_shadow_effects_overlap",
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
    positive_overlap_fraction = overlap.positive_sample_count / overlap.sample_count if overlap.sample_count else 0.0
    overlap_passed = (
        overlap.median_ms >= args.minimum_overlap_ms
        and positive_overlap_fraction >= args.minimum_positive_overlap_fraction
    )
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
            "timestamp overlap",
            overlap_passed,
            f"median {format_ms(overlap.median_ms)} (min {args.minimum_overlap_ms:.4f} ms), "
            f"positive {overlap.positive_sample_count}/{overlap.sample_count} "
            f"({positive_overlap_fraction * 100.0:.1f}%; min {args.minimum_positive_overlap_fraction * 100.0:.1f}%)",
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
    verdict = "pass" if all(bool(item["passed"]) for item in gates) else "fail"

    def compact_run(run: RunResult, frame: ScopeSummary, overlap_scope: Optional[ScopeSummary] = None) -> Dict[str, object]:
        result = raw_run_payload(run)
        result.update({
            "frame_median_ms": frame.median_ms,
            "frame_sample_count": frame.sample_count,
        })
        if overlap_scope:
            result.update(
                overlap_median_ms=overlap_scope.median_ms,
                overlap_sample_count=overlap_scope.sample_count,
                overlap_positive_sample_count=overlap_scope.positive_sample_count,
            )
        return result

    return {
        "schema": "nwb.async_shadow_m4.v1",
        "verdict": verdict,
        "gates": gates,
        "frame_regression_percent": regression_percent,
        "sync": compact_run(sync, sync_frame),
        "async": compact_run(async_run, async_frame, overlap),
        "pixel_diff": raw_pixel_diff_payload(pixel_diff) if pixel_diff else None,
    }


def require_positive(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value <= 0.0:
        parser.error(f"{option} must be positive")


def require_non_negative(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value < 0.0:
        parser.error(f"{option} must not be negative")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Run parser/verdict checks without launching Vulkan.")
    parser.add_argument("--sync-executable", type=Path, help="Path to nwb_async_shadow_m4_sync_benchmark.")
    parser.add_argument("--async-executable", type=Path, help="Path to nwb_async_shadow_m4_async_benchmark.")
    parser.add_argument("--runtime-dir", type=Path, help="Cooked smoke runtime root used as both process working directory.")
    parser.add_argument("--output-dir", type=Path, help="Directory for timing files, logs, captures, and reports.")
    parser.add_argument("--logserver-executable", help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs rather than a logserver.")
    parser.add_argument("--sync-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin sync binary.")
    parser.add_argument("--async-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin async binary.")
    parser.add_argument("--window-title", default="NWB Async Shadow M4 Benchmark", help="Native window title used for capture and graceful exit.")
    parser.add_argument("--frozen-yaw", default="0.6", help="NWB_STRESS_TEST_SPIN_ANGLE used for deterministic A/B captures.")
    parser.add_argument("--warmup-seconds", type=float, default=4.0, help="Settling time before each timed measurement.")
    parser.add_argument("--measure-seconds", type=float, default=12.0, help="Timing collection time per mode after warmup.")
    parser.add_argument("--startup-timeout", type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument(
        "--pixel-capture-frames",
        type=int,
        default=96,
        help="World ticks to render before the benchmark-only held-frame pixel capture.",
    )
    parser.add_argument(
        "--pixel-capture-settle-seconds",
        type=float,
        default=0.75,
        help="Additional time to let the held capture frame present before it is read back.",
    )
    parser.add_argument("--minimum-samples", type=int, default=6, help="Minimum captured timing intervals required per rollout scope.")
    parser.add_argument("--minimum-overlap-ms", type=float, default=0.01, help="Minimum median shadow/effects overlap.")
    parser.add_argument(
        "--minimum-positive-overlap-fraction",
        type=float,
        default=0.50,
        help="Minimum fraction of overlap samples that must be positive.",
    )
    parser.add_argument(
        "--maximum-frame-regression-percent",
        type=float,
        default=3.0,
        help="Maximum allowed async render.frame median regression versus sync.",
    )
    parser.add_argument("--skip-pixel-parity", action="store_true", help="Do not capture or compare native window pixels.")
    parser.add_argument("--maximum-pixel-max-abs", type=int, default=16, help="Maximum allowed per-channel pixel delta.")
    parser.add_argument("--maximum-pixel-mean-abs", type=float, default=0.75, help="Maximum allowed mean per-channel pixel delta.")
    parser.add_argument(
        "--maximum-pixel-changed-fraction",
        type=float,
        help="Optional maximum fraction of changed pixels; unset tolerates widespread sub-threshold temporal noise.",
    )
    parser.add_argument("--gpu-validation", action="store_true", help="Pass --gpudbg to both loader processes.")
    parser.add_argument("--reject-log", action="append", default=[], help="Additional log substring that fails the run.")
    parser.add_argument("--report-only", action="store_true", help="Write a report but return success if a rollout gate fails.")
    args = parser.parse_args(argv)

    if args.self_test:
        return args

    required = ("sync_executable", "async_executable", "runtime_dir", "output_dir")
    missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join(missing)}")
    require_non_negative(parser, "--warmup-seconds", args.warmup_seconds)
    require_positive(parser, "--measure-seconds", args.measure_seconds)
    require_positive(parser, "--startup-timeout", args.startup_timeout)
    if args.pixel_capture_frames <= 0:
        parser.error("--pixel-capture-frames must be positive")
    require_non_negative(parser, "--pixel-capture-settle-seconds", args.pixel_capture_settle_seconds)
    if args.minimum_samples <= 0:
        parser.error("--minimum-samples must be positive")
    require_non_negative(parser, "--minimum-overlap-ms", args.minimum_overlap_ms)
    if not 0.0 <= args.minimum_positive_overlap_fraction <= 1.0:
        parser.error("--minimum-positive-overlap-fraction must be in [0, 1]")
    require_non_negative(parser, "--maximum-frame-regression-percent", args.maximum_frame_regression_percent)
    if args.maximum_pixel_max_abs < 0 or args.maximum_pixel_max_abs > 255:
        parser.error("--maximum-pixel-max-abs must be in [0, 255]")
    require_non_negative(parser, "--maximum-pixel-mean-abs", args.maximum_pixel_mean_abs)
    if args.maximum_pixel_changed_fraction is not None and not 0.0 <= args.maximum_pixel_changed_fraction <= 1.0:
        parser.error("--maximum-pixel-changed-fraction must be in [0, 1]")

    args.sync_executable = args.sync_executable.resolve()
    args.async_executable = args.async_executable.resolve()
    args.runtime_dir = args.runtime_dir.resolve()
    args.output_dir = args.output_dir.resolve()
    args.sync_namesym = args.sync_namesym.resolve() if args.sync_namesym else None
    args.async_namesym = args.async_namesym.resolve() if args.async_namesym else None
    return args


def build_test_bmp(path: Path, pixels: Sequence[Tuple[int, int, int]]) -> None:
    """Write a one-row 24-bit BMP used only by --self-test."""
    width = len(pixels)
    stride = ((width * 3 + 3) // 4) * 4
    payload = bytearray(stride)
    for index, (red, green, blue) in enumerate(pixels):
        payload[index * 3:index * 3 + 3] = bytes((blue, green, red))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(payload), 0, 0, 54)
    dib = struct.pack("<IIIHHIIIIII", 40, width, 1, 1, 24, 0, len(payload), 0, 0, 0, 0)
    path.write_bytes(header + dib + payload)


def run_self_test() -> int:
    capture_args = parse_args(["--self-test"])
    assert capture_args.pixel_capture_frames == 96
    assert capture_args.pixel_capture_settle_seconds == 0.75

    lane = parse_lane_status(
        "Vulkan: async compute lane requested=true effective=true graphicsFamily=1 computeFamily=3 "
        "(dedicated compute family selected)"
    )
    assert lane == LaneStatus(True, True, 1, 3)

    graphics_route_lane = parse_lane_status(
        "Vulkan: async compute lane requested=yes effective=no graphicsFamily=0 computeFamily=-1 "
        "(no dedicated compute-only family)"
    )
    assert graphics_route_lane == LaneStatus(True, False, 0, -1)

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        timing = root / "timing.txt"
        timing.write_text(
            "=== interval: 20 frames / 0.5s ===\n"
            "  render.frame: avg=4.0000 min=3.0000 max=5.0000 samples=20\n"
            "  render.async_shadow_effects_overlap: avg=1.2500 min=0.0 max=2.0 samples=20\n"
            "=== interval: 20 frames / 0.5s ===\n"
            "  render.frame: avg=5.0000 min=4.0000 max=6.0000 samples=20\n"
            "  render.async_shadow_effects_overlap: avg=1.7500 min=0.0 max=2.0 samples=20\n",
            encoding="utf-8",
        )
        summaries = summarize_scopes(parse_timing_file(timing, {}))
        assert summaries["render.frame"].median_ms == 4.5
        assert summaries["render.async_shadow_effects_overlap"].positive_sample_count == 2

        first = root / "first.bmp"
        second = root / "second.bmp"
        build_test_bmp(first, ((0, 0, 0), (10, 20, 30)))
        build_test_bmp(second, ((0, 0, 0), (13, 18, 35)))
        diff = compare_bmp_rgb(first, second)
        assert diff.width == 2 and diff.height == 1
        assert diff.max_abs == 5
        assert diff.changed_pixels == 1
        assert abs(diff.mean_abs - (10.0 / 6.0)) < 1.0e-9

        failure_markdown = root / "failure.md"
        write_markdown_report(
            failure_markdown,
            {
                "collection_error": "render.async_shadow was not published",
                "sync": {"timing_file": "sync.txt", "log_file": "sync.log"},
                "async": {"timing_file": "async.txt", "log_file": "async.log"},
            },
        )
        assert "Incomplete telemetry" in failure_markdown.read_text(encoding="utf-8")

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
            run_frame_locked_capture(args, "async", args.async_executable, capture_backend)
            if not args.skip_pixel_parity
            else None
        )
        async_run = run_single_mode(
            args,
            "async",
            args.async_executable,
            load_name_symbols(args.async_namesym),
            capture_backend,
            async_capture,
        )
        sync_capture = (
            run_frame_locked_capture(args, "sync", args.sync_executable, capture_backend)
            if not args.skip_pixel_parity
            else None
        )
        sync_run = run_single_mode(
            args,
            "sync",
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
                "schema": "nwb.async_shadow_m4.v1",
                "verdict": "fail",
                "collection_error": str(error),
                "gates": [gate("required timestamp telemetry", False, str(error))],
                "sync": raw_run_payload(sync_run),
                "async": raw_run_payload(async_run),
                "pixel_diff": None,
            }
        write_json(json_path, report)
        write_markdown_report(markdown_path, report)
        print(f"M4 report: {markdown_path}")
        if report["verdict"] != "pass" and not args.report_only:
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


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
