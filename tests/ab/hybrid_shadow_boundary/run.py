#!/usr/bin/env python3
"""Measure the natural hybrid HW+SW shadow route against an opaque hardware-only baseline.

The same benchmark binary renders two fixed-yaw, ten-character animated stress scenes. The healthy arm records
the normal hybrid transparent-shadow tail. The baseline arm uses a test-owned environment flag to make every
character opaque, naturally selecting hardware shadows without mutating renderer behavior. Their images are
diagnostic artifacts, not a pixel-parity gate: the material classes intentionally differ between arms.

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
from name_symbols import debug_name_hash_token  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    STRICT_LOG_FAILURE_MESSAGES,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
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


FRAME_SCOPE = "render.frame"
OPAQUE_TRACE_SCOPE = "render.shadow_opaque_trace"
TRANSPARENT_TRACE_SCOPE = "render.shadow_transparent_trace"
TRANSPARENT_RESOLVE_SCOPE = "render.shadow_transparent_resolve"
OPTIONAL_COMPARISON_SCOPES = (
    "render.shadow_visibility",
    OPAQUE_TRACE_SCOPE,
    TRANSPARENT_TRACE_SCOPE,
    TRANSPARENT_RESOLVE_SCOPE,
    "render.sw_bvh_sort",
)
KNOWN_TIMING_SCOPES = (FRAME_SCOPE, *OPTIONAL_COMPARISON_SCOPES)
CAPABILITY_SKIP_LOG = "StressTestSmokeProject: hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable"
DEFAULT_FORBIDDEN_LOGS = (
    *STRICT_LOG_FAILURE_MESSAGES,
    "cannot safely continue after an unresolved frame recovery submission",
)
HEALTHY_REQUIRED_LOGS = (
    "StressTestSmokeProject: enabled healthy hybrid transparent-shadow benchmark",
    "StressTestSmokeProject: RayQuery-capable hybrid shadow hardware available",
    "RendererSystem: dispatched software shadow traversal",
)
BASELINE_REQUIRED_LOGS = (
    "StressTestSmokeProject: enabled natural opaque hardware-shadow baseline",
    "StressTestSmokeProject: RayQuery-capable hybrid shadow hardware available",
)
HYBRID_FAILURE_LOGS = (
    "RendererSystem: split opaque soft-shadow producer failed; retaining all-lit visibility",
    "RendererSystem: split opaque soft-shadow first wavelet failed; retaining all-lit visibility",
    "RendererSystem: split opaque soft-shadow resolve tail failed; retaining all-lit visibility",
    "RendererSystem: split transparent soft-shadow trace could not record; preserving opaque visibility",
    "RendererSystem: split transparent soft-shadow temporal merge failed; preserving opaque visibility",
    "RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope; preserving opaque visibility",
    "RendererSystem: split transparent soft-shadow first wavelet failed; preserving opaque visibility",
    "RendererSystem: split transparent soft-shadow resolve failed; preserving opaque visibility",
    "RendererSystem: split transparent soft-shadow trace or first wavelet failed; preserving opaque visibility",
    "RendererSystem: ray-traced shadow visibility pass failed",
    "RendererSystem: healthy hybrid tail requires a complete graph-owned hardware material fallback",
    "RendererSystem: could not declare hybrid software shadow-preparation tail",
    "RendererSystem: changed HW shadow material context has no graph-owned upload batch",
    "RendererSystem: could not retain frozen hybrid hardware material fallback",
    "RendererSystem: changed software scene BVH has no graph-owned upload pair",
    "RendererSystem: changed SW shadow material context has no graph-owned upload batch",
    "RendererSystem: could not freeze hybrid transparent software BVH build plan",
    "RendererSystem: frozen hybrid hardware material fallback could not retain graph uploads",
    "RendererSystem: hybrid transparent shadow software BVH resource preparation failed",
    "RendererSystem: hybrid transparent software shadow preparation failed; transparent shadows absent this frame",
    "RendererSystem: hybrid transparent shadow per-mesh software BVH build failed",
    "RendererSystem: hybrid transparent software shadow pass failed",
    "RendererSystem: hybrid transparent software shadow recording failed; transparent shadows absent this frame",
    "RendererSystem: hybrid hardware fallback inputs changed after graph preflight",
    "RendererSystem: frozen hybrid hardware material-context restore failed; rejecting shadow preparation packet",
)
HEALTHY_FORBIDDEN_LOGS = (
    "StressTestSmokeProject: enabled natural opaque hardware-shadow baseline",
    "RendererSystem: restored frozen hybrid hardware material context",
    "RendererSystem: test forced hybrid software traversal fallback",
    *HYBRID_FAILURE_LOGS,
)
BASELINE_FORBIDDEN_LOGS = (
    "StressTestSmokeProject: enabled healthy hybrid transparent-shadow benchmark",
    "RendererSystem: dispatched software shadow traversal",
    "RendererSystem: restored frozen hybrid hardware material context",
    "RendererSystem: test forced hybrid software traversal fallback",
    "RendererSystem: test hybrid software traversal recovered",
    "RendererSystem: hybrid hardware material-context fallback failed",
    *HYBRID_FAILURE_LOGS,
)


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
    measurement_start_byte_offset: int = 0


def load_name_symbols(path: Optional[Path]) -> Dict[str, str]:
    return load_timing_name_symbols(path, KNOWN_TIMING_SCOPES)


def require_positive_scope_samples(
    summaries: Mapping[str, ScopeSummary],
    scope: str,
    minimum_samples: int,
    timing_file: Path,
    mode: str,
) -> ScopeSummary:
    summary = require_scope_samples(summaries, scope, minimum_samples, timing_file)
    if summary.positive_sample_count >= minimum_samples and summary.median_ms > 0.0:
        return summary

    raise SmokeFailure(
        f"{mode} required timing scope '{scope}' did not publish at least {minimum_samples} positive intervals "
        f"in {timing_file}; positive={summary.positive_sample_count}, total={summary.sample_count}, "
        f"median={summary.median_ms:.6f} ms"
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
    skip_message: Optional[str] = None,
) -> Optional[str]:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, f"while waiting for benchmark log message '{message}'")
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        if skip_message and skip_message in latest_log:
            return skip_message
        if message in latest_log:
            return None
        time.sleep(0.1)

    raise SmokeFailure(f"timed out waiting for benchmark log message '{message}'\n{latest_log[-4000:]}")


def wait_while_running(process, seconds: float, stage: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))


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
            elif stable_since is not None and now - stable_since >= 0.1:
                return current_size
        latest_size = current_size
        time.sleep(0.05)

    raise SmokeFailure(f"GPU timing file did not publish a post-warmup interval boundary: {timing_path}")


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
    environment["NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE"] = "1" if mode == "baseline" else "0"

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
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(
            launch_args, executable, environment
        )
        if not log_directory:
            raise SmokeFailure("hybrid-shadow runner could not select a runtime-log directory")
        app_process = launch_testbed(launch_args, executable, environment, log_port)

        skip_reason = wait_for_log_message(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            required_logs[0],
            args.startup_timeout,
            CAPABILITY_SKIP_LOG,
        )
        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"{mode} benchmark did not expose the expected window '{args.window_title}'")
        if skip_reason is None:
            wait_while_running(app_process, args.warmup_seconds, f"during {mode} warmup")
            measurement_start_byte_offset = wait_for_measurement_timing_boundary(
                app_process,
                timing_path,
                args.startup_timeout,
            )
            wait_while_running(app_process, args.measure_seconds, f"during {mode} measurement")
            capture_result = capture_backend.capture_window(window, capture_path)
            validate_capture_result(capture_result)
        app_exit_code, app_exit_tail = terminate_process(app_process, f"{mode} benchmark", window)
        app_process = None
        log_text = shutdown_logserver_and_collect(
            logserver_process,
            log_directory,
            log_baseline,
            log_pattern,
            "hybrid-shadow benchmark logserver",
        )
        logserver_process = None
    finally:
        terminate_process(app_process, f"{mode} benchmark", window)
        terminate_process(logserver_process, "hybrid-shadow benchmark logserver")

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

    log_path.write_text(log_text, encoding="utf-8")
    semantic_forbidden = find_log_messages(log_text, semantic_forbidden_logs)
    return RunResult(
        mode=mode,
        executable=str(executable),
        timing_file=str(timing_path),
        log_file=str(log_path),
        capture_file=str(capture_path),
        scopes=summarize_scopes(parse_timing_file(timing_path, symbols, measurement_start_byte_offset)),
        missing_required_log_messages=find_missing_log_messages(log_text, required_logs),
        runtime_forbidden_log_messages=runtime_forbidden,
        semantic_forbidden_log_messages=semantic_forbidden,
        measurement_start_byte_offset=measurement_start_byte_offset,
    )


def percent_delta(candidate_ms: float, baseline_ms: float) -> float:
    if baseline_ms <= 0.0:
        raise SmokeFailure("opaque baseline median is not positive, so the boundary percentage is undefined")
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
        "measurement_start_byte_offset": run.measurement_start_byte_offset,
        "scopes": {name: asdict(summary) for name, summary in run.scopes.items()},
    }


def comparison_scope_payload(
    healthy: Mapping[str, ScopeSummary], baseline: Mapping[str, ScopeSummary], scope: str
) -> Dict[str, object]:
    healthy_summary = healthy.get(scope)
    baseline_summary = baseline.get(scope)
    delta = None
    if healthy_summary and baseline_summary and baseline_summary.median_ms > 0.0:
        delta = percent_delta(healthy_summary.median_ms, baseline_summary.median_ms)
    return {
        "healthy": asdict(healthy_summary) if healthy_summary else None,
        "baseline": asdict(baseline_summary) if baseline_summary else None,
        "hybrid_delta_percent": delta,
    }


def evaluate_runs(args: argparse.Namespace, healthy: RunResult, baseline: RunResult) -> Dict[str, object]:
    healthy_timing_file = Path(healthy.timing_file)
    baseline_timing_file = Path(baseline.timing_file)
    healthy_frame = require_positive_scope_samples(
        healthy.scopes, FRAME_SCOPE, args.minimum_samples, healthy_timing_file, "healthy"
    )
    baseline_frame = require_positive_scope_samples(
        baseline.scopes, FRAME_SCOPE, args.minimum_samples, baseline_timing_file, "baseline"
    )
    healthy_opaque_trace = require_positive_scope_samples(
        healthy.scopes, OPAQUE_TRACE_SCOPE, args.minimum_samples, healthy_timing_file, "healthy"
    )
    baseline_opaque_trace = require_positive_scope_samples(
        baseline.scopes, OPAQUE_TRACE_SCOPE, args.minimum_samples, baseline_timing_file, "baseline"
    )
    healthy_transparent_trace = require_positive_scope_samples(
        healthy.scopes, TRANSPARENT_TRACE_SCOPE, args.minimum_samples, healthy_timing_file, "healthy"
    )
    healthy_transparent_resolve = require_positive_scope_samples(
        healthy.scopes, TRANSPARENT_RESOLVE_SCOPE, args.minimum_samples, healthy_timing_file, "healthy"
    )
    frame_delta_percent = percent_delta(healthy_frame.median_ms, baseline_frame.median_ms)

    baseline_unexpected_transparent_scopes = [
        scope
        for scope in (TRANSPARENT_TRACE_SCOPE, TRANSPARENT_RESOLVE_SCOPE)
        if baseline.scopes.get(scope) and baseline.scopes[scope].positive_sample_count > 0
    ]

    comparison_scopes = [FRAME_SCOPE]
    for scope in OPTIONAL_COMPARISON_SCOPES:
        if scope in healthy.scopes or scope in baseline.scopes:
            comparison_scopes.append(scope)
    scope_comparisons = {
        scope: comparison_scope_payload(healthy.scopes, baseline.scopes, scope) for scope in comparison_scopes
    }

    healthy_semantics_passed = not healthy.missing_required_log_messages and not healthy.semantic_forbidden_log_messages
    baseline_semantics_passed = (
        not baseline.missing_required_log_messages
        and not baseline.semantic_forbidden_log_messages
        and not baseline_unexpected_transparent_scopes
    )
    runtime_logs_passed = not healthy.runtime_forbidden_log_messages and not baseline.runtime_forbidden_log_messages
    gates = [
        gate(
            "target-scene timestamp telemetry",
            True,
            f"positive intervals: frame healthy={healthy_frame.positive_sample_count}, "
            f"frame baseline={baseline_frame.positive_sample_count}, opaque healthy={healthy_opaque_trace.positive_sample_count}, "
            f"opaque baseline={baseline_opaque_trace.positive_sample_count}, "
            f"transparent trace healthy={healthy_transparent_trace.positive_sample_count}, "
            f"transparent resolve healthy={healthy_transparent_resolve.positive_sample_count}",
        ),
        gate(
            "healthy hybrid arm",
            healthy_semantics_passed,
            "RayQuery plus accepted opaque and transparent GPU timing scopes were observed"
            if healthy_semantics_passed
            else f"missing={healthy.missing_required_log_messages}, unexpected={healthy.semantic_forbidden_log_messages}",
        ),
        gate(
            "natural opaque hardware-shadow baseline",
            baseline_semantics_passed,
            "opaque trace timing was observed without positive transparent timing or software traversal diagnostics"
            if baseline_semantics_passed
            else f"missing={baseline.missing_required_log_messages}, unexpected_logs={baseline.semantic_forbidden_log_messages}, "
            f"unexpected_timing={baseline_unexpected_transparent_scopes}",
        ),
        gate(
            "runtime and validation logs",
            runtime_logs_passed,
            "no forbidden runtime or validation log messages"
            if runtime_logs_passed
            else f"healthy={healthy.runtime_forbidden_log_messages}, baseline={baseline.runtime_forbidden_log_messages}",
        ),
    ]
    if args.maximum_hybrid_frame_regression_percent is not None:
        threshold = args.maximum_hybrid_frame_regression_percent
        gates.append(
            gate(
                "optional hybrid frame budget",
                frame_delta_percent <= threshold,
                f"healthy {healthy_frame.median_ms:.4f} ms, baseline {baseline_frame.median_ms:.4f} ms, "
                f"delta {frame_delta_percent:+.3f}% (limit +{threshold:.3f}%)",
            )
        )

    verdict = "pass" if all(bool(item["passed"]) for item in gates) else "fail"
    healthy_payload = raw_run_payload(healthy)
    healthy_payload.update(
        frame_median_ms=healthy_frame.median_ms,
        frame_sample_count=healthy_frame.sample_count,
    )
    baseline_payload = raw_run_payload(baseline)
    baseline_payload.update(
        frame_median_ms=baseline_frame.median_ms,
        frame_sample_count=baseline_frame.sample_count,
    )
    return {
        "schema": "nwb.hybrid_shadow_boundary.v2",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "verdict": verdict,
        "gates": gates,
        "healthy": healthy_payload,
        "baseline": baseline_payload,
        "frame_hybrid_delta_percent": frame_delta_percent,
        "scope_comparisons": scope_comparisons,
        "parameters": {
            "frozen_yaw": args.frozen_yaw,
            "warmup_seconds": args.warmup_seconds,
            "measure_seconds": args.measure_seconds,
            "minimum_samples": args.minimum_samples,
            "gpu_validation": args.gpu_validation,
            "maximum_hybrid_frame_regression_percent": args.maximum_hybrid_frame_regression_percent,
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
        "# Hybrid versus opaque shadow boundary",
        "",
        f"Verdict: **{str(report['verdict']).upper()}**",
        "",
        "The baseline arm uses opaque materials so it naturally omits the transparent software tail. "
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
            "| Scope | Healthy hybrid | Opaque HW baseline | Hybrid delta vs opaque baseline |",
            "| --- | ---: | ---: | ---: |",
        ))
        for scope, comparison in comparisons.items():
            assert isinstance(comparison, Mapping)
            healthy = comparison.get("healthy")
            baseline = comparison.get("baseline")
            lines.append(
                f"| `{scope}` | {format_scope_value(healthy if isinstance(healthy, Mapping) else None)} | "
                f"{format_scope_value(baseline if isinstance(baseline, Mapping) else None)} | "
                f"{format_delta(comparison.get('hybrid_delta_percent'))} |"
            )
        lines.extend((
            "",
            "A missing optional scope means that arm did not publish the scope; it is not treated as a zero-duration pass.",
            "",
        ))

    lines.extend(("## Artifacts", ""))
    for label in ("healthy", "baseline"):
        arm = report.get(label)
        if not isinstance(arm, Mapping):
            continue
        lines.extend((
            f"- {label.title()} timing: `{arm.get('timing_file', 'not collected')}`",
            f"- {label.title()} log: `{arm.get('log_file', 'not collected')}`",
            f"- {label.title()} capture: `{arm.get('capture_file', 'not collected')}`",
        ))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_failure_report(error: SmokeFailure, healthy: Optional[RunResult], baseline: Optional[RunResult]) -> Dict[str, object]:
    return {
        "schema": "nwb.hybrid_shadow_boundary.v2",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "verdict": "fail",
        "collection_error": str(error),
        "gates": [gate("target-scene collection", False, str(error))],
        "healthy": raw_run_payload(healthy) if healthy else None,
        "baseline": raw_run_payload(baseline) if baseline else None,
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
    parser.add_argument(
        "--baseline-executable",
        dest="baseline_executable",
        type=Path,
        help="Path to the benchmark binary reused for the opaque baseline.",
    )
    parser.add_argument("--runtime-dir", type=Path, help="Cooked stress-scene runtime root used as process working directory.")
    parser.add_argument("--output-dir", type=Path, help="Fresh artifact directory for timing files, logs, captures, and reports.")
    parser.add_argument("--logserver-executable", type=Path, help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs rather than a logserver.")
    parser.add_argument("--healthy-namesym", type=Path, help="Optional name-symbol sidecar for an opt/fin healthy binary.")
    parser.add_argument(
        "--baseline-namesym",
        dest="baseline_namesym",
        type=Path,
        help="Optional name-symbol sidecar for the opaque-baseline binary.",
    )
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
        "--maximum-hybrid-frame-regression-percent",
        dest="maximum_hybrid_frame_regression_percent",
        type=float,
        help="Optional maximum healthy-hybrid frame overhead versus the opaque baseline.",
    )
    parser.add_argument("--gpu-validation", action="store_true", help="Pass --gpudbg to both benchmark processes.")
    parser.add_argument("--reject-log", action="append", default=[], help="Additional log substring that fails either arm.")
    parser.add_argument("--report-only", action="store_true", help="Write the report but return success if a measurement gate fails.")
    args = parser.parse_args(argv)

    if args.self_test:
        return args

    required = ("healthy_executable", "baseline_executable", "runtime_dir", "output_dir")
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
    if args.maximum_hybrid_frame_regression_percent is not None:
        require_non_negative(
            parser,
            "--maximum-hybrid-frame-regression-percent",
            args.maximum_hybrid_frame_regression_percent,
        )

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
    defaults = parse_args(["--self-test"])
    assert DEFAULT_FORBIDDEN_LOGS[:len(STRICT_LOG_FAILURE_MESSAGES)] == STRICT_LOG_FAILURE_MESSAGES
    assert defaults.gpu_validation is False
    assert defaults.maximum_hybrid_frame_regression_percent is None
    assert find_missing_log_messages("healthy", ("healthy", "baseline")) == ["baseline"]
    assert find_log_messages("Validation Error", DEFAULT_FORBIDDEN_LOGS) == ["Validation Error"]
    healthy_log = "\n".join(HEALTHY_REQUIRED_LOGS)
    baseline_log = "\n".join(BASELINE_REQUIRED_LOGS)
    assert find_missing_log_messages(healthy_log, HEALTHY_REQUIRED_LOGS) == []
    assert find_log_messages(healthy_log, HEALTHY_FORBIDDEN_LOGS) == []
    assert find_missing_log_messages(baseline_log, BASELINE_REQUIRED_LOGS) == []
    assert find_log_messages(baseline_log, BASELINE_FORBIDDEN_LOGS) == []

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        module = sys.modules[__name__]
        executable = root / "orchestration.exe"
        executable.write_bytes(b"exe")
        defaults.output_dir = root
        defaults.runtime_dir = root
        app = SimpleNamespace(pid=4321, poll=lambda: None)
        logserver = object()
        baseline = {root / "old.log": 7}
        backend = mock.Mock()
        backend.wait_for_window.return_value = 17
        events = []

        def terminate(process, name, window_handle=None):
            if process is app:
                events.append(("app-stop", name, window_handle))
                return 7, "simulated abnormal exit"
            assert process is None
            events.append(("cleanup-none", name, window_handle))
            return None, ""

        def shutdown(process, log_directory, received_baseline, pattern, shutdown_name="logserver"):
            assert process is logserver
            assert log_directory == root
            assert received_baseline == baseline
            assert pattern == "logserver_*.log"
            assert events == [("app-stop", "healthy benchmark", 17)]
            events.append(("logserver-helper", shutdown_name))
            return CAPABILITY_SKIP_LOG

        with mock.patch.object(module, "build_launch_environment", return_value={}), \
             mock.patch.object(module, "launch_logserver", return_value=(logserver, 49152, root, baseline, "logserver_*.log")), \
             mock.patch.object(module, "launch_testbed", return_value=app), \
             mock.patch.object(module, "wait_for_log_message", return_value=CAPABILITY_SKIP_LOG), \
             mock.patch.object(module, "terminate_process", side_effect=terminate) as terminate_mock, \
             mock.patch.object(module, "shutdown_logserver_and_collect", side_effect=shutdown) as shutdown_mock:
            try:
                run_single_arm(defaults, "healthy", executable, {}, ("ready",), (), backend)
            except SmokeFailure as error:
                assert "exit 7" in str(error)
            else:
                raise AssertionError("hybrid orchestration accepted an abnormal Testbed exit")

        assert events == [
            ("app-stop", "healthy benchmark", 17),
            ("logserver-helper", "hybrid-shadow benchmark logserver"),
            ("cleanup-none", "healthy benchmark", 17),
            ("cleanup-none", "hybrid-shadow benchmark logserver", None),
        ]
        assert terminate_mock.mock_calls == [
            mock.call(app, "healthy benchmark", 17),
            mock.call(None, "healthy benchmark", 17),
            mock.call(None, "hybrid-shadow benchmark logserver"),
        ]
        shutdown_mock.assert_called_once_with(
            logserver, root, baseline, "logserver_*.log", "hybrid-shadow benchmark logserver"
        )

        timing = root / "timing.txt"
        timing_symbols = load_name_symbols(None)
        frame_token = debug_name_hash_token(FRAME_SCOPE)
        visibility_token = debug_name_hash_token("render.shadow_visibility")
        opaque_token = debug_name_hash_token(OPAQUE_TRACE_SCOPE)
        transparent_trace_token = debug_name_hash_token(TRANSPARENT_TRACE_SCOPE)
        transparent_resolve_token = debug_name_hash_token(TRANSPARENT_RESOLVE_SCOPE)
        assert timing_symbols[frame_token] == FRAME_SCOPE
        warmup_timing = (
            "=== interval: 20 frames / 0.5s ===\n"
            f"  {frame_token}: avg=4.0000 min=3.0 max=5.0 samples=20\n"
            f"  {visibility_token}: avg=1.2500 min=1.0 max=1.5 samples=20\n"
            f"  {opaque_token}: avg=0.5000 min=0.4 max=0.6 samples=20\n"
            f"  {transparent_trace_token}: avg=0.3000 min=0.2 max=0.4 samples=20\n"
            f"  {transparent_resolve_token}: avg=0.2000 min=0.1 max=0.3 samples=20\n"
        )
        measurement_timing = (
            "=== interval: 20 frames / 0.5s ===\n"
            f"  {frame_token}: avg=5.0000 min=4.0 max=6.0 samples=20\n"
            f"  {visibility_token}: avg=1.7500 min=1.0 max=2.0 samples=20\n"
            f"  {opaque_token}: avg=0.6000 min=0.5 max=0.7 samples=20\n"
            f"  {transparent_trace_token}: avg=0.4000 min=0.3 max=0.5 samples=20\n"
            f"  {transparent_resolve_token}: avg=0.3000 min=0.2 max=0.4 samples=20\n"
        )
        timing.write_text(warmup_timing + measurement_timing, encoding="utf-8")
        scopes = summarize_scopes(parse_timing_file(timing, timing_symbols))
        assert scopes[FRAME_SCOPE].median_ms == 4.5
        assert scopes["render.shadow_visibility"].positive_sample_count == 2
        assert scopes[TRANSPARENT_RESOLVE_SCOPE].positive_sample_count == 2
        measurement_offset = len(warmup_timing.encode("utf-8"))
        measurement_scopes = summarize_scopes(parse_timing_file(timing, timing_symbols, measurement_offset))
        assert measurement_scopes[FRAME_SCOPE].median_ms == 5.0
        assert measurement_scopes[FRAME_SCOPE].sample_count == 1

        capability_log = root / "capability.log"
        capability_log.write_text(CAPABILITY_SKIP_LOG, encoding="utf-8")
        running_process = SimpleNamespace(poll=lambda: None)
        assert wait_for_log_message(
            running_process,
            root,
            {},
            "capability.log",
            HEALTHY_REQUIRED_LOGS[0],
            0.1,
            CAPABILITY_SKIP_LOG,
        ) == CAPABILITY_SKIP_LOG

        healthy_scopes = dict(scopes)
        healthy_scopes[FRAME_SCOPE] = ScopeSummary(2, 2, 4.95, 4.95, 4.9, 5.0)
        healthy = RunResult(
            mode="healthy",
            executable="healthy",
            timing_file=str(timing),
            log_file="healthy.log",
            capture_file="healthy.bmp",
            scopes=healthy_scopes,
            missing_required_log_messages=[],
            runtime_forbidden_log_messages=[],
            semantic_forbidden_log_messages=[],
        )
        baseline_scopes = {
            FRAME_SCOPE: ScopeSummary(2, 2, 4.5, 4.5, 4.4, 4.6),
            OPAQUE_TRACE_SCOPE: ScopeSummary(2, 2, 0.55, 0.55, 0.5, 0.6),
        }
        baseline = RunResult(
            mode="baseline",
            executable="baseline",
            timing_file=str(timing),
            log_file="baseline.log",
            capture_file="baseline.bmp",
            scopes=baseline_scopes,
            missing_required_log_messages=[],
            runtime_forbidden_log_messages=[],
            semantic_forbidden_log_messages=[],
        )
        args = SimpleNamespace(
            minimum_samples=2,
            maximum_hybrid_frame_regression_percent=None,
            frozen_yaw=0.6,
            warmup_seconds=4.0,
            measure_seconds=12.0,
            gpu_validation=False,
        )
        report = evaluate_runs(args, healthy, baseline)
        assert report["verdict"] == "pass"
        assert report["schema"] == "nwb.hybrid_shadow_boundary.v2"
        assert abs(float(report["frame_hybrid_delta_percent"]) - 10.0) < 1.0e-9
        args.maximum_hybrid_frame_regression_percent = 5.0
        threshold_report = evaluate_runs(args, healthy, baseline)
        assert threshold_report["verdict"] == "fail"
        assert threshold_report["gates"][-1]["name"] == "optional hybrid frame budget"

        baseline_with_transparent_timing = RunResult(
            mode="baseline",
            executable="baseline",
            timing_file=str(timing),
            log_file="baseline.log",
            capture_file="baseline.bmp",
            scopes={
                **baseline_scopes,
                TRANSPARENT_TRACE_SCOPE: ScopeSummary(2, 2, 0.2, 0.2, 0.1, 0.3),
            },
            missing_required_log_messages=[],
            runtime_forbidden_log_messages=[],
            semantic_forbidden_log_messages=[],
        )
        assert evaluate_runs(args, healthy, baseline_with_transparent_timing)["verdict"] == "fail"

        healthy_without_resolve = RunResult(
            mode="healthy",
            executable="healthy",
            timing_file=str(timing),
            log_file="healthy.log",
            capture_file="healthy.bmp",
            scopes={name: summary for name, summary in healthy_scopes.items() if name != TRANSPARENT_RESOLVE_SCOPE},
            missing_required_log_messages=[],
            runtime_forbidden_log_messages=[],
            semantic_forbidden_log_messages=[],
        )
        try:
            evaluate_runs(args, healthy_without_resolve, baseline)
        except SmokeFailure as error:
            assert TRANSPARENT_RESOLVE_SCOPE in str(error)
        else:
            raise AssertionError("healthy hybrid evidence must require transparent resolve timing")

        try:
            require_positive_scope_samples(
                {OPAQUE_TRACE_SCOPE: ScopeSummary(2, 0, 0.0, 0.0, 0.0, 0.0)},
                OPAQUE_TRACE_SCOPE,
                2,
                timing,
                "healthy",
            )
        except SmokeFailure as error:
            assert "did not publish at least 2 positive intervals" in str(error)
        else:
            raise AssertionError("zero-duration route timing must not satisfy hybrid evidence")
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
    baseline: Optional[RunResult] = None
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
        baseline = run_single_arm(
            args,
            "baseline",
            args.baseline_executable,
            load_name_symbols(args.baseline_namesym),
            BASELINE_REQUIRED_LOGS,
            BASELINE_FORBIDDEN_LOGS,
            capture_backend,
        )
        report = evaluate_runs(args, healthy, baseline)
    except SmokeFailure as error:
        report = make_failure_report(error, healthy, baseline)
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
