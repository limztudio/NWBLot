#!/usr/bin/env python3
"""Measure graph-owned soft-transparent shadow folding against the retained monolith."""

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


INTERVAL_RE = re.compile(r"^=== interval:\s+(?P<frames>\d+)\s+frames\s+/\s+(?P<seconds>[-+0-9.eE]+)s\s+===$")
SCOPE_RE = re.compile(
    r"^\s{2}(?P<scope>[^:]+):\s+avg=(?P<average>[-+0-9.eE]+)"
    r"\s+min=(?P<minimum>[-+0-9.eE]+)\s+max=(?P<maximum>[-+0-9.eE]+)"
    r"\s+samples=(?P<samples>\d+)\s*$"
)
FRAME_SCOPE = "render.frame"
OPTIONAL_SCOPES = (
    "render.shadow_visibility",
    "render.shadow_opaque_trace",
    "render.shadow_transparent_trace",
    "render.sw_bvh_sort",
)
FORBIDDEN_LOGS = (
    "[ERROR]",
    "VUID-",
    "Validation Error",
    "cannot safely continue after an unresolved frame recovery submission",
)
GRAPH_REQUIRED_LOGS = (
    "StressTestSmokeProject: enabled graph-owned soft-transparent shadow-fold benchmark",
    "RendererSystem: graph-owned soft-transparent shadow-fold benchmark path active",
)
MONOLITHIC_REQUIRED_LOGS = (
    "StressTestSmokeProject: enabled retained monolithic soft-transparent shadow-fold benchmark",
    "RendererSystem: retained monolithic soft-transparent shadow-fold benchmark path active",
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
    arm: str
    executable: str
    timing_file: str
    log_file: str
    capture_file: str
    scopes: Dict[str, ScopeSummary]
    missing_required_log_messages: List[str]
    forbidden_log_messages: List[str]
    opposite_path_messages: List[str]


def parse_timing_file(path: Path) -> List[Dict[str, float]]:
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
        if match and current is not None:
            try:
                current[match.group("scope")] = float(match.group("average"))
            except ValueError as error:
                raise SmokeFailure(f"invalid GPU timing line in {path}: {raw_line}") from error
    if current:
        intervals.append(current)
    return intervals


def summarize(values: Sequence[float]) -> ScopeSummary:
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
    return {scope: summarize(values) for scope, values in sorted(samples.items())}


def require_scope_samples(summaries: Mapping[str, ScopeSummary], scope: str, minimum: int, timing_file: Path) -> ScopeSummary:
    summary = summaries.get(scope)
    if summary and summary.sample_count >= minimum:
        return summary
    observed = ", ".join(summaries) or "none"
    raise SmokeFailure(
        f"required timing scope '{scope}' has fewer than {minimum} samples in {timing_file}; observed scopes: {observed}"
    )


def find_messages(text: str, messages: Sequence[str]) -> List[str]:
    return [message for message in messages if message in text]


def find_missing_messages(text: str, messages: Sequence[str]) -> List[str]:
    return [message for message in messages if message not in text]


def wait_for_log_message(process, log_directory: Path, baseline: Mapping[Path, int], pattern: str, message: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, f"while waiting for benchmark marker '{message}'")
        latest_log = collect_log_delta(log_directory, baseline, pattern)
        if message in latest_log:
            return
        time.sleep(0.1)
    raise SmokeFailure(f"timed out waiting for benchmark marker '{message}'\n{latest_log[-4000:]}")


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


def run_arm(
    args: argparse.Namespace,
    arm: str,
    executable: Path,
    required_logs: Sequence[str],
    opposite_path_logs: Sequence[str],
    capture_backend,
) -> RunResult:
    if not executable.is_file():
        raise SmokeFailure(f"{arm} executable does not exist: {executable}")
    timing_path = args.output_dir / f"{arm}.timing.txt"
    log_path = args.output_dir / f"{arm}.log"
    capture_path = args.output_dir / f"{arm}.bmp"
    launch_args = make_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment["NWB_GPU_TIMING_FILE"] = str(timing_path)
    environment["NWB_STRESS_TEST_SPIN_ANGLE"] = f"{args.frozen_yaw:.8g}"
    logserver_process = None
    app_process = None
    log_directory: Optional[Path] = None
    baseline: Mapping[Path, int] = {}
    pattern = ""
    window = None
    app_exit_code = None
    app_exit_tail = ""
    log_text = ""
    try:
        logserver_process, log_port, log_directory, baseline, pattern = launch_logserver(launch_args, executable, environment)
        if not log_directory:
            raise SmokeFailure("soft-transparent shadow-fold runner could not select a runtime-log directory")
        app_process = launch_testbed(launch_args, executable, environment, log_port)
        wait_for_log_message(app_process, log_directory, baseline, pattern, required_logs[0], args.startup_timeout)
        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"{arm} benchmark did not expose expected window '{args.window_title}'")
        capture_backend.prepare_window(window)
        time.sleep(0.1)

        wait_while_running(app_process, args.warmup_seconds, f"during {arm} warmup")
        wait_while_running(app_process, args.measure_seconds, f"during {arm} measurement")
        validate_capture_result(capture_backend.capture_window(window, capture_path))
    finally:
        if app_process:
            app_exit_code, app_exit_tail = terminate_process(app_process, f"{arm} benchmark", window)
        time.sleep(0.5)
        if log_directory:
            log_text = collect_log_delta(log_directory, baseline, pattern)
        terminate_process(logserver_process, "soft-transparent shadow-fold benchmark logserver")
    require_normal_testbed_exit(app_exit_code, app_exit_tail)
    if not log_text:
        raise SmokeFailure(f"{arm} benchmark produced no captured logger output")
    if not capture_path.is_file():
        raise SmokeFailure(f"{arm} benchmark did not write a BMP capture")
    log_path.write_text(log_text, encoding="utf-8")
    return RunResult(
        arm=arm,
        executable=str(executable),
        timing_file=str(timing_path),
        log_file=str(log_path),
        capture_file=str(capture_path),
        scopes=summarize_scopes(parse_timing_file(timing_path)),
        missing_required_log_messages=find_missing_messages(log_text, required_logs),
        forbidden_log_messages=find_messages(log_text, tuple(FORBIDDEN_LOGS) + tuple(args.reject_log)),
        opposite_path_messages=find_messages(log_text, opposite_path_logs),
    )


def percent_delta(candidate_ms: float, baseline_ms: float) -> float:
    if baseline_ms <= 0.0:
        raise SmokeFailure("monolithic render.frame median is not positive")
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
        "forbidden_log_messages": run.forbidden_log_messages,
        "opposite_path_messages": run.opposite_path_messages,
        "scopes": {name: asdict(summary) for name, summary in run.scopes.items()},
    }


def comparison_payload(graph: Mapping[str, ScopeSummary], monolithic: Mapping[str, ScopeSummary], scope: str) -> Dict[str, object]:
    graph_summary = graph.get(scope)
    monolithic_summary = monolithic.get(scope)
    delta = None
    if graph_summary and monolithic_summary and monolithic_summary.median_ms > 0.0:
        delta = percent_delta(graph_summary.median_ms, monolithic_summary.median_ms)
    return {
        "graph": asdict(graph_summary) if graph_summary else None,
        "monolithic": asdict(monolithic_summary) if monolithic_summary else None,
        "graph_delta_percent": delta,
    }


def arm_semantics_passed(run: RunResult) -> bool:
    return not run.missing_required_log_messages and not run.opposite_path_messages


def evaluate_runs(args: argparse.Namespace, graph: RunResult, monolithic: RunResult) -> Dict[str, object]:
    graph_frame = require_scope_samples(graph.scopes, FRAME_SCOPE, args.minimum_samples, Path(graph.timing_file))
    monolithic_frame = require_scope_samples(monolithic.scopes, FRAME_SCOPE, args.minimum_samples, Path(monolithic.timing_file))
    frame_delta = percent_delta(graph_frame.median_ms, monolithic_frame.median_ms)
    scopes = [FRAME_SCOPE] + [scope for scope in OPTIONAL_SCOPES if scope in graph.scopes or scope in monolithic.scopes]
    comparisons = {scope: comparison_payload(graph.scopes, monolithic.scopes, scope) for scope in scopes}
    graph_semantics = arm_semantics_passed(graph)
    monolithic_semantics = arm_semantics_passed(monolithic)
    logs_passed = not graph.forbidden_log_messages and not monolithic.forbidden_log_messages
    gates = [
        gate("target-scene timestamp telemetry", True, f"{FRAME_SCOPE}: graph {graph_frame.sample_count}, monolithic {monolithic_frame.sample_count} intervals"),
        gate("graph-owned fold arm", graph_semantics, "graph marker observed without monolithic marker" if graph_semantics else f"missing={graph.missing_required_log_messages}, unexpected={graph.opposite_path_messages}"),
        gate("retained monolithic arm", monolithic_semantics, "monolithic marker observed without graph marker" if monolithic_semantics else f"missing={monolithic.missing_required_log_messages}, unexpected={monolithic.opposite_path_messages}"),
        gate("runtime and validation logs", logs_passed, "no forbidden runtime or validation log messages" if logs_passed else f"graph={graph.forbidden_log_messages}, monolithic={monolithic.forbidden_log_messages}"),
    ]
    if args.maximum_graph_frame_regression_percent is not None:
        threshold = args.maximum_graph_frame_regression_percent
        gates.append(gate(
            "optional graph frame budget",
            frame_delta <= threshold,
            f"monolithic {monolithic_frame.median_ms:.4f} ms, graph {graph_frame.median_ms:.4f} ms, delta {frame_delta:+.3f}% (limit +{threshold:.3f}%)",
        ))
    graph_payload = raw_run_payload(graph)
    graph_payload.update(frame_median_ms=graph_frame.median_ms, frame_sample_count=graph_frame.sample_count)
    monolithic_payload = raw_run_payload(monolithic)
    monolithic_payload.update(frame_median_ms=monolithic_frame.median_ms, frame_sample_count=monolithic_frame.sample_count)
    return {
        "schema": "nwb.soft_transparent_shadow_fold.v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "verdict": "pass" if all(bool(item["passed"]) for item in gates) else "fail",
        "gates": gates,
        "graph": graph_payload,
        "monolithic": monolithic_payload,
        "graph_frame_delta_percent": frame_delta,
        "scope_comparisons": comparisons,
        "parameters": {
            "frozen_yaw": args.frozen_yaw,
            "warmup_seconds": args.warmup_seconds,
            "measure_seconds": args.measure_seconds,
            "minimum_samples": args.minimum_samples,
            "gpu_validation": args.gpu_validation,
            "maximum_graph_frame_regression_percent": args.maximum_graph_frame_regression_percent,
        },
    }


def write_json(path: Path, payload: Mapping[str, object]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def format_scope(summary: Optional[Mapping[str, object]]) -> str:
    if not summary:
        return "not published"
    return f"{float(summary['median_ms']):.4f} ms ({int(summary['sample_count'])} intervals)"


def format_delta(value: Optional[object]) -> str:
    return "not comparable" if value is None else f"{float(value):+.3f}%"


def write_markdown_report(path: Path, report: Mapping[str, object]) -> None:
    lines = [
        "# Graph-owned soft-transparent shadow fold",
        "",
        f"Verdict: **{str(report['verdict']).upper()}**",
        "",
        "Both arms render the same frozen target scene. Captures are inspection artifacts; temporal soft-shadow sampling is not a byte-exact pixel gate.",
        "",
    ]
    if report.get("collection_error"):
        lines.extend(("## Incomplete telemetry", "", str(report["collection_error"]), ""))
    else:
        lines.extend(("| Gate | Result | Detail |", "| --- | --- | --- |"))
        for entry in report["gates"]:
            assert isinstance(entry, Mapping)
            lines.append(f"| {entry['name']} | {'PASS' if entry['passed'] else 'FAIL'} | {entry['detail']} |")
        lines.extend(("", "## GPU timestamp comparison", "", "| Scope | Retained monolithic | Graph-owned fold | Graph delta |", "| --- | ---: | ---: | ---: |"))
        comparisons = report["scope_comparisons"]
        assert isinstance(comparisons, Mapping)
        for scope, comparison in comparisons.items():
            assert isinstance(comparison, Mapping)
            monolithic = comparison.get("monolithic")
            graph = comparison.get("graph")
            lines.append(
                f"| `{scope}` | {format_scope(monolithic if isinstance(monolithic, Mapping) else None)} | "
                f"{format_scope(graph if isinstance(graph, Mapping) else None)} | {format_delta(comparison.get('graph_delta_percent'))} |"
            )
        lines.append("")
    lines.extend(("## Artifacts", ""))
    for label in ("graph", "monolithic"):
        arm = report.get(label)
        if isinstance(arm, Mapping):
            lines.extend((
                f"- {label.title()} timing: `{arm.get('timing_file', 'not collected')}`",
                f"- {label.title()} log: `{arm.get('log_file', 'not collected')}`",
                f"- {label.title()} capture: `{arm.get('capture_file', 'not collected')}`",
            ))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def failure_report(error: SmokeFailure, graph: Optional[RunResult], monolithic: Optional[RunResult]) -> Dict[str, object]:
    return {
        "schema": "nwb.soft_transparent_shadow_fold.v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "verdict": "fail",
        "collection_error": str(error),
        "gates": [gate("target-scene collection", False, str(error))],
        "graph": raw_run_payload(graph) if graph else None,
        "monolithic": raw_run_payload(monolithic) if monolithic else None,
    }


def positive(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value <= 0.0:
        parser.error(f"{option} must be positive")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--graph-executable", type=Path)
    parser.add_argument("--monolithic-executable", type=Path)
    parser.add_argument("--runtime-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--no-logserver", action="store_true")
    parser.add_argument("--window-title", default="NWB Soft Transparent Shadow Fold Benchmark")
    parser.add_argument("--frozen-yaw", type=float, default=0.6)
    parser.add_argument("--warmup-seconds", type=float, default=4.0)
    parser.add_argument("--measure-seconds", type=float, default=12.0)
    parser.add_argument("--startup-timeout", type=float, default=45.0)
    parser.add_argument("--minimum-samples", type=int, default=6)
    parser.add_argument("--maximum-graph-frame-regression-percent", type=float)
    parser.add_argument("--gpu-validation", action="store_true")
    parser.add_argument("--reject-log", action="append", default=[])
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return args
    missing = [f"--{name.replace('_', '-')}" for name in ("graph_executable", "monolithic_executable", "runtime_dir", "output_dir") if getattr(args, name) is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join(missing)}")
    if not math.isfinite(args.frozen_yaw):
        parser.error("--frozen-yaw must be finite")
    if args.warmup_seconds < 0.0:
        parser.error("--warmup-seconds must not be negative")
    positive(parser, "--measure-seconds", args.measure_seconds)
    positive(parser, "--startup-timeout", args.startup_timeout)
    if args.minimum_samples <= 0:
        parser.error("--minimum-samples must be positive")
    if args.maximum_graph_frame_regression_percent is not None and args.maximum_graph_frame_regression_percent < 0.0:
        parser.error("--maximum-graph-frame-regression-percent must not be negative")
    args.graph_executable = args.graph_executable.resolve()
    args.monolithic_executable = args.monolithic_executable.resolve()
    args.runtime_dir = args.runtime_dir.resolve()
    args.output_dir = args.output_dir.resolve()
    args.logserver_executable = args.logserver_executable.resolve() if args.logserver_executable else None
    return args


def run_self_test() -> int:
    assert parse_args(["--self-test"]).gpu_validation is False
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        timing = root / "timing.txt"
        timing.write_text(
            "=== interval: 20 frames / 0.5s ===\n  render.frame: avg=4.0000 min=3.0 max=5.0 samples=20\n"
            "=== interval: 20 frames / 0.5s ===\n  render.frame: avg=5.0000 min=4.0 max=6.0 samples=20\n",
            encoding="utf-8",
        )
        scopes = summarize_scopes(parse_timing_file(timing))
        graph_scopes = dict(scopes)
        graph_scopes[FRAME_SCOPE] = ScopeSummary(2, 2, 4.95, 5.0, 4.9, 5.1)
        graph = RunResult("graph", "graph", str(timing), "graph.log", "graph.bmp", graph_scopes, [], [], [])
        monolithic = RunResult("monolithic", "monolithic", str(timing), "monolithic.log", "monolithic.bmp", scopes, [], [], [])
        args = SimpleNamespace(
            minimum_samples=2, maximum_graph_frame_regression_percent=None, frozen_yaw=0.6,
            warmup_seconds=4.0, measure_seconds=12.0, gpu_validation=False,
        )
        report = evaluate_runs(args, graph, monolithic)
        assert report["verdict"] == "pass"
        assert abs(float(report["graph_frame_delta_percent"]) - 10.0) < 1.0e-9
        args.maximum_graph_frame_regression_percent = 5.0
        assert evaluate_runs(args, graph, monolithic)["verdict"] == "fail"
        markdown = root / "report.md"
        write_markdown_report(markdown, report)
        assert "Graph-owned fold" in markdown.read_text(encoding="utf-8")
    print("soft-transparent shadow-fold harness self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"runtime directory does not exist: {args.runtime_dir}")
    try:
        args.output_dir.mkdir(parents=True, exist_ok=False)
    except FileExistsError as error:
        raise SmokeFailure(f"artifact directory already exists: {args.output_dir}") from error
    capture_backend = None
    graph: Optional[RunResult] = None
    monolithic: Optional[RunResult] = None
    try:
        capture_backend = create_capture_backend()
        graph = run_arm(args, "graph", args.graph_executable, GRAPH_REQUIRED_LOGS, MONOLITHIC_REQUIRED_LOGS, capture_backend)
        monolithic = run_arm(args, "monolithic", args.monolithic_executable, MONOLITHIC_REQUIRED_LOGS, GRAPH_REQUIRED_LOGS, capture_backend)
        report = evaluate_runs(args, graph, monolithic)
    except SmokeFailure as error:
        report = failure_report(error, graph, monolithic)
    finally:
        if capture_backend:
            capture_backend.close()
    json_path = args.output_dir / "soft_transparent_shadow_fold_report.json"
    markdown_path = args.output_dir / "soft_transparent_shadow_fold_report.md"
    write_json(json_path, report)
    write_markdown_report(markdown_path, report)
    print(f"Soft-transparent shadow-fold report: {markdown_path}")
    return 0 if report["verdict"] == "pass" or args.report_only else 1


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

