#!/usr/bin/env python3
"""Measure the retained hybrid HW -> SW transparent-shadow fallback boundary.

The two benchmark binaries render the same fixed-yaw, ten-character animated stress scene.
The healthy arm records the normal hybrid transparent-shadow tail. The fallback arm deliberately
misses that optional tail every frame, then restores the already-captured immutable hardware
material context and continues with opaque hardware shadows. Their images are diagnostic artifacts,
not a pixel-parity gate: transparent shadows are intentionally absent in the fallback arm.

The runner collects renderer GPU timestamp envelopes, verifies the arm-specific diagnostic markers,
captures both native windows, and writes a device-local report. ``--self-test`` exercises parsing
and report evaluation without a Vulkan device or visible window.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, Iterable, List, Mapping, Optional, Sequence


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


INTERVAL_RE = re.compile(
    r"^=== interval:\s+(?P<frames>\d+)\s+frames\s+/\s+(?P<seconds>[-+0-9.eE]+)s\s+===$"
)
SCOPE_RE = re.compile(
    r"^\s{2}(?P<scope>[^:]+):\s+avg=(?P<average>[-+0-9.eE]+)"
    r"\s+min=(?P<minimum>[-+0-9.eE]+)\s+max=(?P<maximum>[-+0-9.eE]+)"
    r"\s+samples=(?P<samples>\d+)\s*$"
)

FRAME_SCOPE = "render.frame"
OPTIONAL_COMPARISON_SCOPES = (
    "render.shadow_visibility",
    "render.shadow_opaque_trace",
    "render.shadow_transparent_trace",
    "render.sw_bvh_sort",
)
DEFAULT_FORBIDDEN_LOGS = (
    "[ERROR]",
    "VUID-",
    "Validation Error",
    "cannot safely continue after an unresolved frame recovery submission",
)
HEALTHY_REQUIRED_LOGS = (
    "StressTestSmokeProject: enabled healthy hybrid transparent-shadow benchmark",
)
FALLBACK_REQUIRED_LOGS = (
    "StressTestSmokeProject: enabled persistent hybrid transparent-shadow fallback benchmark",
    "RendererSystem: test forced hybrid software traversal fallback",
    "RendererSystem: hybrid transparent software shadow recording failed; transparent shadows absent this frame",
    "RendererSystem: restored frozen hybrid hardware material context",
)
HEALTHY_FORBIDDEN_LOGS = FALLBACK_REQUIRED_LOGS
FALLBACK_FORBIDDEN_LOGS = (
    "RendererSystem: test hybrid software traversal recovered",
    "RendererSystem: hybrid hardware material-context fallback failed",
    "RendererSystem: frozen hybrid hardware material-context restore failed; rejecting shadow preparation packet",
)


@dataclass(frozen=True)
class ScopeSummary:
    sample_count: int
    positive_sample_count: int
    median_ms: float
    mean_ms: float
    min_ms: float
    max_ms: float


@dataclass
class RunResult:
    mode: str
    executable: str
    timing_file: str
    log_file: str
    capture_file: str
    scopes: Dict[str, ScopeSummary]
    missing_required_log_messages: List[str]
    runtime_forbidden_log_messages: List[str]
    semantic_forbidden_log_messages: List[str]


def load_name_symbols(path: Optional[Path]) -> Dict[str, str]:
    if not path:
        return {}
    if not path.is_file():
        raise SmokeFailure(f"name-symbol sidecar does not exist: {path}")

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


def find_log_messages(log_text: str, needles: Sequence[str]) -> List[str]:
    return [needle for needle in needles if needle in log_text]


def find_missing_log_messages(log_text: str, needles: Sequence[str]) -> List[str]:
    return [needle for needle in needles if needle not in log_text]


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

    raise SmokeFailure(f"timed out waiting for benchmark log message '{message}'\n{latest_log[-4000:]}")


def wait_while_running(process, seconds: float, stage: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))


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


def run_single_arm(
    args: argparse.Namespace,
    mode: str,
    executable: Path,
    symbols: Mapping[str, str],
    required_logs: Sequence[str],
    semantic_forbidden_logs: Sequence[str],
    capture_backend,
) -> RunResult:
    if not executable.is_file():
        raise SmokeFailure(f"{mode} executable does not exist: {executable}")

    timing_path = args.output_dir / f"{mode}.timing.txt"
    log_path = args.output_dir / f"{mode}.log"
    capture_path = args.output_dir / f"{mode}.bmp"
    launch_args = make_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment["NWB_GPU_TIMING_FILE"] = str(timing_path)
    environment["NWB_STRESS_TEST_SPIN_ANGLE"] = f"{args.frozen_yaw:.8g}"

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
            raise SmokeFailure("hybrid-shadow runner could not select a runtime-log directory")
        app_process = launch_testbed(launch_args, executable, environment, log_port)

        wait_for_log_message(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            required_logs[0],
            args.startup_timeout,
        )
        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"{mode} benchmark did not expose the expected window '{args.window_title}'")

        wait_while_running(app_process, args.warmup_seconds, f"during {mode} warmup")
        wait_while_running(app_process, args.measure_seconds, f"during {mode} measurement")
        capture_result = capture_backend.capture_window(window, capture_path)
        validate_capture_result(capture_result)
    finally:
        if app_process:
            app_exit_code, app_exit_tail = terminate_process(app_process, f"{mode} benchmark", window)
        # Let normal client shutdown messages reach the server before collecting this arm's final delta.
        time.sleep(0.5)
        if log_directory:
            log_text = collect_log_delta(log_directory, log_baseline, log_pattern)
        terminate_process(logserver_process, "hybrid-shadow benchmark logserver")

    require_normal_testbed_exit(app_exit_code, app_exit_tail)
    if not log_text:
        raise SmokeFailure(f"{mode} benchmark produced no captured logger output")
    if not capture_path.is_file():
        raise SmokeFailure(f"{mode} benchmark did not write a BMP capture")

    log_path.write_text(log_text, encoding="utf-8")
    runtime_forbidden = find_log_messages(log_text, tuple(DEFAULT_FORBIDDEN_LOGS) + tuple(args.reject_log))
    semantic_forbidden = find_log_messages(log_text, semantic_forbidden_logs)
    return RunResult(
        mode=mode,
        executable=str(executable),
        timing_file=str(timing_path),
        log_file=str(log_path),
        capture_file=str(capture_path),
        scopes=summarize_scopes(parse_timing_file(timing_path, symbols)),
        missing_required_log_messages=find_missing_log_messages(log_text, required_logs),
        runtime_forbidden_log_messages=runtime_forbidden,
        semantic_forbidden_log_messages=semantic_forbidden,
    )


def percent_delta(candidate_ms: float, baseline_ms: float) -> float:
    if baseline_ms <= 0.0:
        raise SmokeFailure("healthy render.frame median is not positive, so the boundary percentage is undefined")
    return (candidate_ms - baseline_ms) * 100.0 / baseline_ms


def gate(name: str, passed: bool, detail: str) -> Dict[str, object]:
    return {"name": name, "passed": passed, "detail": detail}


def raw_run_payload(run: RunResult) -> Dict[str, object]:
    return {
        "executable": run.executable,
        "timing_file": run.timing_file,
        "log_file": run.log_file,
        "capture_file": run.capture_file,
        "missing_required_log_messages": run.missing_required_log_messages,
        "runtime_forbidden_log_messages": run.runtime_forbidden_log_messages,
        "semantic_forbidden_log_messages": run.semantic_forbidden_log_messages,
        "scopes": {name: asdict(summary) for name, summary in run.scopes.items()},
    }


def comparison_scope_payload(
    healthy: Mapping[str, ScopeSummary], fallback: Mapping[str, ScopeSummary], scope: str
) -> Dict[str, object]:
    healthy_summary = healthy.get(scope)
    fallback_summary = fallback.get(scope)
    delta = None
    if healthy_summary and fallback_summary and healthy_summary.median_ms > 0.0:
        delta = percent_delta(fallback_summary.median_ms, healthy_summary.median_ms)
    return {
        "healthy": asdict(healthy_summary) if healthy_summary else None,
        "fallback": asdict(fallback_summary) if fallback_summary else None,
        "fallback_delta_percent": delta,
    }


def evaluate_runs(args: argparse.Namespace, healthy: RunResult, fallback: RunResult) -> Dict[str, object]:
    healthy_frame = require_scope_samples(healthy.scopes, FRAME_SCOPE, args.minimum_samples, Path(healthy.timing_file))
    fallback_frame = require_scope_samples(fallback.scopes, FRAME_SCOPE, args.minimum_samples, Path(fallback.timing_file))
    frame_delta_percent = percent_delta(fallback_frame.median_ms, healthy_frame.median_ms)

    comparison_scopes = [FRAME_SCOPE]
    for scope in OPTIONAL_COMPARISON_SCOPES:
        if scope in healthy.scopes or scope in fallback.scopes:
            comparison_scopes.append(scope)
    scope_comparisons = {
        scope: comparison_scope_payload(healthy.scopes, fallback.scopes, scope) for scope in comparison_scopes
    }

    healthy_semantics_passed = not healthy.missing_required_log_messages and not healthy.semantic_forbidden_log_messages
    fallback_semantics_passed = not fallback.missing_required_log_messages and not fallback.semantic_forbidden_log_messages
    runtime_logs_passed = not healthy.runtime_forbidden_log_messages and not fallback.runtime_forbidden_log_messages
    gates = [
        gate(
            "target-scene timestamp telemetry",
            True,
            f"{FRAME_SCOPE}: healthy {healthy_frame.sample_count} intervals, fallback {fallback_frame.sample_count} intervals",
        ),
        gate(
            "healthy hybrid arm",
            healthy_semantics_passed,
            "healthy marker present and no fallback marker"
            if healthy_semantics_passed
            else f"missing={healthy.missing_required_log_messages}, unexpected={healthy.semantic_forbidden_log_messages}",
        ),
        gate(
            "persistent opaque-HW fallback arm",
            fallback_semantics_passed,
            "forced traversal miss, transparent-shadow absence, and frozen hardware restore observed"
            if fallback_semantics_passed
            else f"missing={fallback.missing_required_log_messages}, unexpected={fallback.semantic_forbidden_log_messages}",
        ),
        gate(
            "runtime and validation logs",
            runtime_logs_passed,
            "no forbidden runtime or validation log messages"
            if runtime_logs_passed
            else f"healthy={healthy.runtime_forbidden_log_messages}, fallback={fallback.runtime_forbidden_log_messages}",
        ),
    ]
    if args.maximum_fallback_frame_regression_percent is not None:
        threshold = args.maximum_fallback_frame_regression_percent
        gates.append(
            gate(
                "optional fallback frame budget",
                frame_delta_percent <= threshold,
                f"healthy {healthy_frame.median_ms:.4f} ms, fallback {fallback_frame.median_ms:.4f} ms, "
                f"delta {frame_delta_percent:+.3f}% (limit +{threshold:.3f}%)",
            )
        )

    verdict = "pass" if all(bool(item["passed"]) for item in gates) else "fail"
    healthy_payload = raw_run_payload(healthy)
    healthy_payload.update(
        frame_median_ms=healthy_frame.median_ms,
        frame_sample_count=healthy_frame.sample_count,
    )
    fallback_payload = raw_run_payload(fallback)
    fallback_payload.update(
        frame_median_ms=fallback_frame.median_ms,
        frame_sample_count=fallback_frame.sample_count,
    )
    return {
        "schema": "nwb.hybrid_shadow_boundary.v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "verdict": verdict,
        "gates": gates,
        "healthy": healthy_payload,
        "fallback": fallback_payload,
        "frame_fallback_delta_percent": frame_delta_percent,
        "scope_comparisons": scope_comparisons,
        "parameters": {
            "frozen_yaw": args.frozen_yaw,
            "warmup_seconds": args.warmup_seconds,
            "measure_seconds": args.measure_seconds,
            "minimum_samples": args.minimum_samples,
            "gpu_validation": args.gpu_validation,
            "maximum_fallback_frame_regression_percent": args.maximum_fallback_frame_regression_percent,
        },
    }


def format_scope_value(summary: Optional[Mapping[str, object]]) -> str:
    if not summary:
        return "not published"
    return f"{float(summary['median_ms']):.4f} ms ({int(summary['sample_count'])} intervals)"


def format_delta(value: Optional[object]) -> str:
    return "not comparable" if value is None else f"{float(value):+.3f}%"


def write_json(path: Path, payload: Mapping[str, object]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown_report(path: Path, report: Mapping[str, object]) -> None:
    lines = [
        "# Hybrid transparent-shadow fallback boundary",
        "",
        f"Verdict: **{str(report['verdict']).upper()}**",
        "",
        "The fallback arm intentionally removes transparent shadows after its optional software traversal tail misses. "
        "The captures are therefore diagnostic artifacts, not a pixel-parity comparison.",
        "",
    ]
    if report.get("collection_error"):
        lines.extend((
            "## Incomplete telemetry",
            "",
            str(report["collection_error"]),
            "",
        ))
    else:
        gates = report["gates"]
        assert isinstance(gates, Sequence)
        lines.extend((
            "| Gate | Result | Detail |",
            "| --- | --- | --- |",
        ))
        for entry in gates:
            assert isinstance(entry, Mapping)
            lines.append(f"| {entry['name']} | {'PASS' if entry['passed'] else 'FAIL'} | {entry['detail']} |")

        comparisons = report["scope_comparisons"]
        assert isinstance(comparisons, Mapping)
        lines.extend((
            "",
            "## GPU timestamp comparison",
            "",
            "| Scope | Healthy hybrid | Persistent opaque-HW fallback | Fallback delta |",
            "| --- | ---: | ---: | ---: |",
        ))
        for scope, comparison in comparisons.items():
            assert isinstance(comparison, Mapping)
            healthy = comparison.get("healthy")
            fallback = comparison.get("fallback")
            lines.append(
                f"| `{scope}` | {format_scope_value(healthy if isinstance(healthy, Mapping) else None)} | "
                f"{format_scope_value(fallback if isinstance(fallback, Mapping) else None)} | "
                f"{format_delta(comparison.get('fallback_delta_percent'))} |"
            )
        lines.extend((
            "",
            "A missing optional scope means that arm did not publish the scope; it is not treated as a zero-duration pass.",
            "",
        ))

    lines.extend(("## Artifacts", ""))
    for label in ("healthy", "fallback"):
        arm = report.get(label)
        if not isinstance(arm, Mapping):
            continue
        lines.extend((
            f"- {label.title()} timing: `{arm.get('timing_file', 'not collected')}`",
            f"- {label.title()} log: `{arm.get('log_file', 'not collected')}`",
            f"- {label.title()} capture: `{arm.get('capture_file', 'not collected')}`",
        ))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_failure_report(error: SmokeFailure, healthy: Optional[RunResult], fallback: Optional[RunResult]) -> Dict[str, object]:
    return {
        "schema": "nwb.hybrid_shadow_boundary.v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "verdict": "fail",
        "collection_error": str(error),
        "gates": [gate("target-scene collection", False, str(error))],
        "healthy": raw_run_payload(healthy) if healthy else None,
        "fallback": raw_run_payload(fallback) if fallback else None,
    }


def require_positive(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value <= 0.0:
        parser.error(f"{option} must be positive")


def require_non_negative(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value < 0.0:
        parser.error(f"{option} must not be negative")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Run parser and report checks without Vulkan.")
    parser.add_argument("--healthy-executable", type=Path, help="Path to nwb_hybrid_shadow_boundary_healthy_benchmark.")
    parser.add_argument("--fallback-executable", type=Path, help="Path to nwb_hybrid_shadow_boundary_fallback_benchmark.")
    parser.add_argument("--runtime-dir", type=Path, help="Cooked stress-scene runtime root used as process working directory.")
    parser.add_argument("--output-dir", type=Path, help="Fresh artifact directory for timing files, logs, captures, and reports.")
    parser.add_argument("--logserver-executable", type=Path, help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs rather than a logserver.")
    parser.add_argument("--healthy-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin healthy binary.")
    parser.add_argument("--fallback-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin fallback binary.")
    parser.add_argument(
        "--window-title",
        default="NWB Hybrid Shadow Boundary Benchmark",
        help="Native window title used for capture and graceful exit.",
    )
    parser.add_argument("--frozen-yaw", type=float, default=0.6, help="Fixed NWB_STRESS_TEST_SPIN_ANGLE in radians.")
    parser.add_argument("--warmup-seconds", type=float, default=4.0, help="Settling time before each timed arm.")
    parser.add_argument("--measure-seconds", type=float, default=12.0, help="Timestamp collection duration per arm after warmup.")
    parser.add_argument("--startup-timeout", type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument("--minimum-samples", type=int, default=6, help="Minimum render.frame timing intervals required per arm.")
    parser.add_argument(
        "--maximum-fallback-frame-regression-percent",
        type=float,
        help="Optional explicit frame-budget gate; unset records the measured boundary without classifying its cost.",
    )
    parser.add_argument("--gpu-validation", action="store_true", help="Pass --gpudbg to both benchmark processes.")
    parser.add_argument("--reject-log", action="append", default=[], help="Additional log substring that fails either arm.")
    parser.add_argument("--report-only", action="store_true", help="Write the report but return success if a measurement gate fails.")
    args = parser.parse_args(argv)

    if args.self_test:
        return args

    required = ("healthy_executable", "fallback_executable", "runtime_dir", "output_dir")
    missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join(missing)}")
    if not math.isfinite(args.frozen_yaw):
        parser.error("--frozen-yaw must be finite")
    require_non_negative(parser, "--warmup-seconds", args.warmup_seconds)
    require_positive(parser, "--measure-seconds", args.measure_seconds)
    require_positive(parser, "--startup-timeout", args.startup_timeout)
    if args.minimum_samples <= 0:
        parser.error("--minimum-samples must be positive")
    if args.maximum_fallback_frame_regression_percent is not None:
        require_non_negative(
            parser,
            "--maximum-fallback-frame-regression-percent",
            args.maximum_fallback_frame_regression_percent,
        )

    args.healthy_executable = args.healthy_executable.resolve()
    args.fallback_executable = args.fallback_executable.resolve()
    args.runtime_dir = args.runtime_dir.resolve()
    args.output_dir = args.output_dir.resolve()
    args.logserver_executable = args.logserver_executable.resolve() if args.logserver_executable else None
    args.healthy_namesym = args.healthy_namesym.resolve() if args.healthy_namesym else None
    args.fallback_namesym = args.fallback_namesym.resolve() if args.fallback_namesym else None
    return args


def run_self_test() -> int:
    defaults = parse_args(["--self-test"])
    assert defaults.gpu_validation is False
    assert defaults.maximum_fallback_frame_regression_percent is None
    assert find_missing_log_messages("healthy", ("healthy", "fallback")) == ["fallback"]
    assert find_log_messages("Validation Error", DEFAULT_FORBIDDEN_LOGS) == ["Validation Error"]

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        timing = root / "timing.txt"
        timing.write_text(
            "=== interval: 20 frames / 0.5s ===\n"
            "  render.frame: avg=4.0000 min=3.0 max=5.0 samples=20\n"
            "  render.shadow_visibility: avg=1.2500 min=1.0 max=1.5 samples=20\n"
            "=== interval: 20 frames / 0.5s ===\n"
            "  render.frame: avg=5.0000 min=4.0 max=6.0 samples=20\n"
            "  render.shadow_visibility: avg=1.7500 min=1.0 max=2.0 samples=20\n",
            encoding="utf-8",
        )
        scopes = summarize_scopes(parse_timing_file(timing, {}))
        assert scopes[FRAME_SCOPE].median_ms == 4.5
        assert scopes["render.shadow_visibility"].positive_sample_count == 2

        healthy = RunResult(
            mode="healthy",
            executable="healthy",
            timing_file=str(timing),
            log_file="healthy.log",
            capture_file="healthy.bmp",
            scopes=scopes,
            missing_required_log_messages=[],
            runtime_forbidden_log_messages=[],
            semantic_forbidden_log_messages=[],
        )
        fallback_scopes = dict(scopes)
        fallback_scopes[FRAME_SCOPE] = ScopeSummary(2, 2, 4.95, 5.0, 4.9, 5.1)
        fallback = RunResult(
            mode="fallback",
            executable="fallback",
            timing_file=str(timing),
            log_file="fallback.log",
            capture_file="fallback.bmp",
            scopes=fallback_scopes,
            missing_required_log_messages=[],
            runtime_forbidden_log_messages=[],
            semantic_forbidden_log_messages=[],
        )
        args = SimpleNamespace(
            minimum_samples=2,
            maximum_fallback_frame_regression_percent=None,
            frozen_yaw=0.6,
            warmup_seconds=4.0,
            measure_seconds=12.0,
            gpu_validation=False,
        )
        report = evaluate_runs(args, healthy, fallback)
        assert report["verdict"] == "pass"
        assert abs(float(report["frame_fallback_delta_percent"]) - 10.0) < 1.0e-9
        args.maximum_fallback_frame_regression_percent = 5.0
        threshold_report = evaluate_runs(args, healthy, fallback)
        assert threshold_report["verdict"] == "fail"
        assert threshold_report["gates"][-1]["name"] == "optional fallback frame budget"
        markdown = root / "report.md"
        write_markdown_report(markdown, report)
        assert "not a pixel-parity comparison" in markdown.read_text(encoding="utf-8")

    print("hybrid-shadow boundary harness self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"runtime directory does not exist: {args.runtime_dir}")
    try:
        args.output_dir.mkdir(parents=True, exist_ok=False)
    except FileExistsError as error:
        raise SmokeFailure(f"artifact directory already exists: {args.output_dir}") from error

    capture_backend = None
    healthy: Optional[RunResult] = None
    fallback: Optional[RunResult] = None
    try:
        capture_backend = create_capture_backend()
        healthy = run_single_arm(
            args,
            "healthy",
            args.healthy_executable,
            load_name_symbols(args.healthy_namesym),
            HEALTHY_REQUIRED_LOGS,
            HEALTHY_FORBIDDEN_LOGS,
            capture_backend,
        )
        fallback = run_single_arm(
            args,
            "fallback",
            args.fallback_executable,
            load_name_symbols(args.fallback_namesym),
            FALLBACK_REQUIRED_LOGS,
            FALLBACK_FORBIDDEN_LOGS,
            capture_backend,
        )
        report = evaluate_runs(args, healthy, fallback)
    except SmokeFailure as error:
        report = make_failure_report(error, healthy, fallback)
    finally:
        if capture_backend:
            capture_backend.close()

    json_path = args.output_dir / "hybrid_shadow_boundary_report.json"
    markdown_path = args.output_dir / "hybrid_shadow_boundary_report.md"
    write_json(json_path, report)
    write_markdown_report(markdown_path, report)
    print(f"Hybrid-shadow boundary report: {markdown_path}")
    if report["verdict"] != "pass" and not args.report_only:
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


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
