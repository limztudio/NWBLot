from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence

from gpu_timing_parse import MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS, ScopeSummary, require_scope_samples
from window_capture_smoke import SmokeFailure


LIT_DETAIL = "detail"
LIT_TIMING_FILE = "timing_file"
LIT_LOG_FILE = "log_file"
LIT_CAPTURE_FILE = "capture_file"
LIT_TRANSPARENT_DELTA_PERCENT = "transparent_delta_percent"
LIT_NOT_COLLECTED = 'not collected'
LIT_RENDER_SHADOW_VISIBILITY = "render.shadow_visibility"
LIT_BASELINE = "baseline"
LIT_UTF_8 = "utf-8"
LIT_NAME = "name"
LIT_PASSED = "passed"
LIT_HEALTHY = "healthy"
LIT_OPTIONAL_HARDWARE_TRANSPARENT_FRAME_BU = "optional hardware transparent frame budget"
LIT_PASS = "pass"
LIT_FAIL = "fail"
LIT_INCONCLUSIVE = "inconclusive"
LIT_SCHEMA = "schema"
LIT_NWB_HARDWARE_SHADOW_BOUNDARY_V1 = "nwb.hardware_shadow_boundary.v1"
LIT_GENERATED_UTC = "generated_utc"
LIT_VERDICT = "verdict"
LIT_GATES = "gates"
LIT_FRAME_TRANSPARENT_DELTA_PERCENT = "frame_transparent_delta_percent"
LIT_SCOPE_COMPARISONS = "scope_comparisons"
LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN = "maximum_transparent_frame_regression_percent"
LIT_COLLECTION_ERROR = "collection_error"
FRAME_SCOPE = "render.frame"
OPAQUE_TRACE_SCOPE = "render.shadow_opaque_trace"
TRANSPARENT_TRACE_SCOPE = "render.shadow_transparent_trace"
TRANSPARENT_RESOLVE_SCOPE = "render.shadow_transparent_resolve"
OPTIONAL_COMPARISON_SCOPES = (LIT_RENDER_SHADOW_VISIBILITY, OPAQUE_TRACE_SCOPE, TRANSPARENT_TRACE_SCOPE, TRANSPARENT_RESOLVE_SCOPE, "render.sw_bvh_sort",)


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


class HardwareShadowReport:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args

    def require_positive_scope_samples(self, summaries: Mapping[str, ScopeSummary], scope: str, minimum_samples: int, timing_file: Path, mode: str,) -> ScopeSummary:
        summary = require_scope_samples(summaries, scope, minimum_samples, timing_file)
        if summary.positive_sample_count >= minimum_samples and summary.median_ms > 0.0:
            return summary

        raise SmokeFailure(f"{mode} required timing scope '{scope}' did not publish at least {minimum_samples} positive intervals " f"in {timing_file}; positive={summary.positive_sample_count}, total={summary.sample_count}, " f"median={summary.median_ms:.6f} ms")

    def percent_delta(self, candidate_ms: float, baseline_ms: float) -> float:
        if baseline_ms <= 0.0:
            raise SmokeFailure("opaque baseline median is not positive, so the boundary percentage is undefined")
        return (candidate_ms - baseline_ms) * 100.0 / baseline_ms

    def gate(self, name: str, passed: bool, detail: str) -> Dict[str, object]:
        return {LIT_NAME: name, LIT_PASSED: passed, LIT_DETAIL: detail}

    def raw_run_payload(self, run: RunResult) -> Dict[str, object]:
        return { "executable": run.executable, LIT_TIMING_FILE: run.timing_file, LIT_LOG_FILE: run.log_file, LIT_CAPTURE_FILE: run.capture_file, "missing_required_log_messages": run.missing_required_log_messages,
            "runtime_forbidden_log_messages": run.runtime_forbidden_log_messages, "semantic_forbidden_log_messages": run.semantic_forbidden_log_messages, "measurement_start_byte_offset": run.measurement_start_byte_offset,
            "scopes": {name: asdict(summary) for name, summary in run.scopes.items()}, }

    def comparison_scope_payload(self, healthy: Mapping[str, ScopeSummary], baseline: Mapping[str, ScopeSummary], scope: str) -> Dict[str, object]:
        healthy_summary = healthy.get(scope)
        baseline_summary = baseline.get(scope)
        delta = None
        if healthy_summary and baseline_summary and baseline_summary.median_ms > 0.0:
            delta = self.percent_delta(healthy_summary.median_ms, baseline_summary.median_ms)
        return { LIT_HEALTHY: asdict(healthy_summary) if healthy_summary else None, LIT_BASELINE: asdict(baseline_summary) if baseline_summary else None, LIT_TRANSPARENT_DELTA_PERCENT: delta, }

    def evaluate_runs(self, healthy: RunResult, baseline: RunResult) -> Dict[str, object]:
        healthy_timing_file = Path(healthy.timing_file)
        baseline_timing_file = Path(baseline.timing_file)
        healthy_frame = self.require_positive_scope_samples(healthy.scopes, FRAME_SCOPE, self.args.minimum_samples, healthy_timing_file, LIT_HEALTHY)
        baseline_frame = self.require_positive_scope_samples(baseline.scopes, FRAME_SCOPE, self.args.minimum_samples, baseline_timing_file, LIT_BASELINE)
        healthy_opaque_trace = self.require_positive_scope_samples(healthy.scopes, OPAQUE_TRACE_SCOPE, self.args.minimum_samples, healthy_timing_file, LIT_HEALTHY)
        baseline_opaque_trace = self.require_positive_scope_samples(baseline.scopes, OPAQUE_TRACE_SCOPE, self.args.minimum_samples, baseline_timing_file, LIT_BASELINE)
        healthy_transparent_trace = self.require_positive_scope_samples(healthy.scopes, TRANSPARENT_TRACE_SCOPE, self.args.minimum_samples, healthy_timing_file, LIT_HEALTHY)
        healthy_transparent_resolve = self.require_positive_scope_samples(healthy.scopes, TRANSPARENT_RESOLVE_SCOPE, self.args.minimum_samples, healthy_timing_file, LIT_HEALTHY)
        frame_delta_percent = self.percent_delta(healthy_frame.median_ms, baseline_frame.median_ms)

        baseline_unexpected_transparent_scopes = [scope for scope in (TRANSPARENT_TRACE_SCOPE, TRANSPARENT_RESOLVE_SCOPE) if baseline.scopes.get(scope) and baseline.scopes[scope].positive_sample_count > 0]

        comparison_scopes = [FRAME_SCOPE]
        for scope in OPTIONAL_COMPARISON_SCOPES:
            if scope in healthy.scopes or scope in baseline.scopes:
                comparison_scopes.append(scope)
        scope_comparisons = { scope: self.comparison_scope_payload(healthy.scopes, baseline.scopes, scope) for scope in comparison_scopes }

        healthy_semantics_passed = not healthy.missing_required_log_messages and not healthy.semantic_forbidden_log_messages
        baseline_semantics_passed = (not baseline.missing_required_log_messages and not baseline.semantic_forbidden_log_messages and not baseline_unexpected_transparent_scopes)
        runtime_logs_passed = not healthy.runtime_forbidden_log_messages and not baseline.runtime_forbidden_log_messages
        timed_scopes = (healthy_frame, baseline_frame, healthy_opaque_trace, baseline_opaque_trace, healthy_transparent_trace, healthy_transparent_resolve)
        intervals_passed = min(summary.positive_sample_count for summary in timed_scopes) > MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS
        gates = [self.gate("target-scene timestamp telemetry", intervals_passed, f"positive intervals: frame healthy={healthy_frame.positive_sample_count}, " f"frame baseline={baseline_frame.positive_sample_count}, opaque healthy={healthy_opaque_trace.positive_sample_count}, "
                f"opaque baseline={baseline_opaque_trace.positive_sample_count}, " f"transparent trace healthy={healthy_transparent_trace.positive_sample_count}, " f"transparent resolve healthy={healthy_transparent_resolve.positive_sample_count} "
                f"(more than {MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS} required)",), self.gate(
                "healthy hardware transparent arm", healthy_semantics_passed, "RayQuery plus accepted opaque and transparent GPU timing scopes were observed" if healthy_semantics_passed
                else f"missing={healthy.missing_required_log_messages}, unexpected={healthy.semantic_forbidden_log_messages}",), self.gate("natural opaque hardware-shadow baseline", baseline_semantics_passed,
                "opaque trace timing was observed without positive transparent timing or hardware transparent traversal diagnostics" if baseline_semantics_passed
                else f"missing={baseline.missing_required_log_messages}, unexpected_logs={baseline.semantic_forbidden_log_messages}, " f"unexpected_timing={baseline_unexpected_transparent_scopes}",), self.gate("runtime and validation logs", runtime_logs_passed,
                "no forbidden runtime or validation log messages" if runtime_logs_passed else f"healthy={healthy.runtime_forbidden_log_messages}, baseline={baseline.runtime_forbidden_log_messages}",),]
        if self.args.maximum_transparent_frame_regression_percent is not None:
            threshold = self.args.maximum_transparent_frame_regression_percent
            gates.append(self.gate(LIT_OPTIONAL_HARDWARE_TRANSPARENT_FRAME_BU, frame_delta_percent <= threshold, f"healthy {healthy_frame.median_ms:.4f} ms, baseline {baseline_frame.median_ms:.4f} ms, " f"delta {frame_delta_percent:+.3f}% (limit +{threshold:.3f}%)",))

        # Functional failures still fail when positive timing intervals are too few.
        if healthy_semantics_passed and baseline_semantics_passed and runtime_logs_passed and not intervals_passed:
            verdict = LIT_INCONCLUSIVE
        else:
            verdict = LIT_PASS if all(bool(item[LIT_PASSED]) for item in gates) else LIT_FAIL
        healthy_payload = self.raw_run_payload(healthy)
        healthy_payload.update(frame_median_ms=healthy_frame.median_ms, frame_sample_count=healthy_frame.sample_count,)
        baseline_payload = self.raw_run_payload(baseline)
        baseline_payload.update(frame_median_ms=baseline_frame.median_ms, frame_sample_count=baseline_frame.sample_count,)
        return { LIT_SCHEMA: LIT_NWB_HARDWARE_SHADOW_BOUNDARY_V1, LIT_GENERATED_UTC: datetime.now(timezone.utc).isoformat(), LIT_VERDICT: verdict, LIT_GATES: gates, LIT_HEALTHY: healthy_payload, LIT_BASELINE: baseline_payload,
            LIT_FRAME_TRANSPARENT_DELTA_PERCENT: frame_delta_percent, LIT_SCOPE_COMPARISONS: scope_comparisons, "parameters": { "frozen_yaw": self.args.frozen_yaw, "warmup_seconds": self.args.warmup_seconds, "measure_seconds": self.args.measure_seconds,
                "minimum_samples": self.args.minimum_samples, "gpu_validation": self.args.gpu_validation, LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN: self.args.maximum_transparent_frame_regression_percent, }, }

    def format_scope_value(self, summary: Optional[Mapping[str, object]]) -> str:
        if not summary:
            return "not published"
        return f"{float(summary['median_ms']):.4f} ms ({int(summary['sample_count'])} intervals)"

    def format_delta(self, value: Optional[object]) -> str:
        return "not comparable" if value is None else f"{float(value):+.3f}%"

    def write_json(self, path: Path, payload: Mapping[str, object]) -> None:
        path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)

    def write_markdown_report(self, path: Path, report: Mapping[str, object]) -> None:
        lines = ["# Hardware transparent versus opaque shadows", "", f"Verdict: **{str(report[LIT_VERDICT]).upper()}**", "", "The baseline arm uses opaque materials so it naturally omits the transparent shadow tail. "
            "The captures are therefore diagnostic artifacts, not a pixel-parity comparison.", "",]
        if report.get(LIT_COLLECTION_ERROR):
            lines.extend(("## Incomplete telemetry", "", str(report[LIT_COLLECTION_ERROR]), "",))
        else:
            gates = report[LIT_GATES]
            assert isinstance(gates, Sequence)
            lines.extend(("| Gate | Result | Detail |", "| --- | --- | --- |",))
            for entry in gates:
                assert isinstance(entry, Mapping)
                lines.append(f"| {entry[LIT_NAME]} | {'PASS' if entry[LIT_PASSED] else 'FAIL'} | {entry[LIT_DETAIL]} |")

            comparisons = report[LIT_SCOPE_COMPARISONS]
            assert isinstance(comparisons, Mapping)
            lines.extend(("", "## GPU timestamp comparison", "", "| Scope | Hardware transparent | Opaque HW baseline | Transparent delta vs opaque baseline |", "| --- | ---: | ---: | ---: |",))
            for scope, comparison in comparisons.items():
                assert isinstance(comparison, Mapping)
                healthy = comparison.get(LIT_HEALTHY)
                baseline = comparison.get(LIT_BASELINE)
                lines.append(f"| `{scope}` | {self.format_scope_value(healthy if isinstance(healthy, Mapping) else None)} | " f"{self.format_scope_value(baseline if isinstance(baseline, Mapping) else None)} | "
                    f"{self.format_delta(comparison.get(LIT_TRANSPARENT_DELTA_PERCENT))} |")
            lines.extend(("", "A missing optional scope means that arm did not publish the scope; it is not treated as a zero-duration pass.", "",))

        lines.extend(("## Artifacts", ""))
        for label in (LIT_HEALTHY, LIT_BASELINE):
            arm = report.get(label)
            if not isinstance(arm, Mapping):
                continue
            lines.extend((f"- {label.title()} timing: `{arm.get(LIT_TIMING_FILE, LIT_NOT_COLLECTED)}`", f"- {label.title()} log: `{arm.get(LIT_LOG_FILE, LIT_NOT_COLLECTED)}`", f"- {label.title()} capture: `{arm.get(LIT_CAPTURE_FILE, LIT_NOT_COLLECTED)}`",))
        path.write_text("\n".join(lines) + "\n", encoding=LIT_UTF_8)

    def make_failure_report(self, error: SmokeFailure, healthy: Optional[RunResult], baseline: Optional[RunResult]) -> Dict[str, object]:
        return { LIT_SCHEMA: LIT_NWB_HARDWARE_SHADOW_BOUNDARY_V1, LIT_GENERATED_UTC: datetime.now(timezone.utc).isoformat(), LIT_VERDICT: LIT_FAIL, LIT_COLLECTION_ERROR: str(error), LIT_GATES: [self.gate("target-scene collection", False, str(error))],
            LIT_HEALTHY: self.raw_run_payload(healthy) if healthy else None, LIT_BASELINE: self.raw_run_payload(baseline) if baseline else None, }

