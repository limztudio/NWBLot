from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence

from async_shadow_m4_lane import LaneStatus
from bitmap_image import BitmapImage, PixelDiff
from gpu_timing_parse import MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS, ScopeSummary, require_scope_samples

LIT_NAME = 'name'
LIT_DETAIL = 'detail'
LIT_FRAME_MEDIAN_MS = 'frame_median_ms'
LIT_FRAME_REGRESSION_PERCENT = 'frame_regression_percent'
LIT_CHANGED_FRACTION = 'changed_fraction'
LIT_CAPTURE_FILE = 'capture_file'
LIT_RENDER_FRAME = "render.frame"
LIT_RENDER_ASYNC_SHADOW = "render.async_shadow"
LIT_RENDER_ASYNC_FINAL = "render.async_final"
LIT_UTF_8 = "utf-8"
LIT_COLLECTION_ERROR = "collection_error"
LIT_SYNC = "sync"
LIT_ASYNC = "async"
LIT_ASYNC_SHADOW_M4_BENCHMARK_REPORT = "# Async-shadow M4 benchmark report"
LIT_ARTIFACTS = "## Artifacts"
LIT_GATES = "gates"
LIT_PASSED = "passed"
LIT_PIXEL_DIFF = "pixel_diff"
LIT_TIMING_FILE = "timing_file"
LIT_LOG_FILE = "log_file"
LIT_PASS = "pass"
LIT_FAIL = "fail"
LIT_INCONCLUSIVE = "inconclusive"
LIT_SCHEMA = "schema"
LIT_NWB_ASYNC_SHADOW_M4_V3 = "nwb.async_shadow_m4.v3"
LIT_VERDICT = "verdict"
REQUIRED_ASYNC_SCOPES = (LIT_RENDER_FRAME, LIT_RENDER_ASYNC_SHADOW, LIT_RENDER_ASYNC_FINAL,)


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
    measurement_start_byte_offset: int = 0


class AsyncShadowM4Report:
    @staticmethod
    def write_json(path: Path, payload: Mapping[str, object]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)

    @staticmethod
    def format_ms(value: float) -> str:
        return f"{value:.4f} ms"

    @staticmethod
    def write_markdown_report(path: Path, report: Mapping[str, object]) -> None:
        if report.get(LIT_COLLECTION_ERROR):
            sync = report[LIT_SYNC]
            async_run = report[LIT_ASYNC]
            assert isinstance(sync, Mapping)
            assert isinstance(async_run, Mapping)
            lines = (LIT_ASYNC_SHADOW_M4_BENCHMARK_REPORT, "", "Verdict: **FAIL**", "", "## Incomplete telemetry", "", str(report[LIT_COLLECTION_ERROR]), "", LIT_ARTIFACTS, "", f"- Sync timing: `{sync[LIT_TIMING_FILE]}`",
                f"- Async timing: `{async_run[LIT_TIMING_FILE]}`", f"- Sync log: `{sync[LIT_LOG_FILE]}`", f"- Async log: `{async_run[LIT_LOG_FILE]}`",)
            path.write_text("\n".join(lines) + "\n", encoding=LIT_UTF_8)
            return

        sync = report[LIT_SYNC]
        async_run = report[LIT_ASYNC]
        assert isinstance(sync, Mapping)
        assert isinstance(async_run, Mapping)
        gates = report[LIT_GATES]
        assert isinstance(gates, Sequence)

        lines = [LIT_ASYNC_SHADOW_M4_BENCHMARK_REPORT, "", f"Verdict: **{report[LIT_VERDICT].upper()}**", "", "| Gate | Result | Detail |", "| --- | --- | --- |",]
        for gate in gates:
            assert isinstance(gate, Mapping)
            result = "PASS" if gate[LIT_PASSED] else "FAIL"
            lines.append(f"| {gate[LIT_NAME]} | {result} | {gate[LIT_DETAIL]} |")

        lines.extend(("", "## Critical path", "", f"- Synchronous `render.frame`: {sync[LIT_FRAME_MEDIAN_MS]:.4f} ms", f"- Async `render.frame`: {async_run[LIT_FRAME_MEDIAN_MS]:.4f} ms", f"- Delta: {report[LIT_FRAME_REGRESSION_PERCENT]:+.3f}%",
            f"- Async graph-owned shadow median: {async_run['shadow_median_ms']:.4f} ms", f"- Async graph-owned shadow positive samples: {async_run['shadow_positive_sample_count']}/{async_run['shadow_sample_count']}", "", LIT_ARTIFACTS, "",
            f"- Sync timing: `{sync[LIT_TIMING_FILE]}`", f"- Async timing: `{async_run[LIT_TIMING_FILE]}`", f"- Sync log: `{sync[LIT_LOG_FILE]}`", f"- Async log: `{async_run[LIT_LOG_FILE]}`",))
        if report.get(LIT_PIXEL_DIFF):
            pixel_diff = report[LIT_PIXEL_DIFF]
            assert isinstance(pixel_diff, Mapping)
            lines.extend((f"- Pixel comparison: max abs {pixel_diff['max_abs']}, mean abs {pixel_diff['mean_abs']:.6f}, " f"changed {pixel_diff[LIT_CHANGED_FRACTION] * 100.0:.4f}%", f"- Sync capture: `{sync[LIT_CAPTURE_FILE]}`",
                f"- Async capture: `{async_run[LIT_CAPTURE_FILE]}`",))

        path.write_text("\n".join(lines) + "\n", encoding=LIT_UTF_8)

    @staticmethod
    def gate(name: str, passed: bool, detail: str) -> Dict[str, object]:
        return {LIT_NAME: name, LIT_PASSED: passed, LIT_DETAIL: detail}

    @staticmethod
    def raw_run_payload(run: RunResult) -> Dict[str, object]:
        return { "executable": run.executable, LIT_TIMING_FILE: run.timing_file, LIT_LOG_FILE: run.log_file, LIT_CAPTURE_FILE: run.capture_file, "measurement_start_byte_offset": run.measurement_start_byte_offset, "lane": asdict(run.lane),
            "forbidden_log_messages": run.forbidden_log_messages, "scopes": {name: asdict(summary) for name, summary in run.scopes.items()}, }

    @staticmethod
    def raw_pixel_diff_payload(pixel_diff: PixelDiff) -> Dict[str, object]:
        payload = asdict(pixel_diff)
        payload[LIT_CHANGED_FRACTION] = pixel_diff.changed_fraction
        return payload

    def evaluate_runs(self, args: argparse.Namespace, sync: RunResult, async_run: RunResult) -> Dict[str, object]:
        sync_frame = require_scope_samples(sync.scopes, LIT_RENDER_FRAME, args.minimum_samples, Path(sync.timing_file))
        async_frame = require_scope_samples(async_run.scopes, LIT_RENDER_FRAME, args.minimum_samples, Path(async_run.timing_file))
        shadow = require_scope_samples(async_run.scopes, LIT_RENDER_ASYNC_SHADOW, args.minimum_samples, Path(async_run.timing_file),)
        for scope in REQUIRED_ASYNC_SCOPES:
            require_scope_samples(async_run.scopes, scope, args.minimum_samples, Path(async_run.timing_file))

        regression_percent = ((async_frame.median_ms - sync_frame.median_ms) * 100.0 / sync_frame.median_ms if sync_frame.median_ms > 0.0 else math.inf)
        shadow_scope_passed = shadow.median_ms >= args.minimum_shadow_ms
        timing_passed = regression_percent <= args.maximum_frame_regression_percent

        pixel_diff: Optional[PixelDiff] = None
        pixel_passed = True
        pixel_detail = "pixel parity disabled by --skip-pixel-parity"
        if not args.skip_pixel_parity:
            assert sync.capture_file and async_run.capture_file
            pixel_diff = BitmapImage(Path(sync.capture_file)).compare(BitmapImage(Path(async_run.capture_file)))
            pixel_passed = (pixel_diff.max_abs <= args.maximum_pixel_max_abs and pixel_diff.mean_abs <= args.maximum_pixel_mean_abs and (args.maximum_pixel_changed_fraction is None or pixel_diff.changed_fraction <= args.maximum_pixel_changed_fraction))
            pixel_detail = (f"max abs {pixel_diff.max_abs}/{args.maximum_pixel_max_abs}, mean abs " f"{pixel_diff.mean_abs:.6f}/{args.maximum_pixel_mean_abs:.6f}, changed " f"{pixel_diff.changed_fraction * 100.0:.4f}%")

        logs_passed = not sync.forbidden_log_messages and not async_run.forbidden_log_messages
        lane_passed = async_run.lane.requested and async_run.lane.effective and async_run.lane.dedicated_compute
        intervals_passed = min(sync_frame.positive_sample_count, async_frame.positive_sample_count, shadow.positive_sample_count) > MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS
        gates = [self.gate("dedicated async lane", lane_passed, f"requested={async_run.lane.requested}, effective={async_run.lane.effective}, "
                f"queues={async_run.lane.graphics_queue}/{async_run.lane.compute_queue}, " f"dedicated={async_run.lane.compute_dedicated}, compute={async_run.lane.compute_supported}, " f"graphics={async_run.lane.compute_graphics}",), self.gate(
                "positive timing intervals", intervals_passed, f"sync frame {sync_frame.positive_sample_count}, async frame {async_frame.positive_sample_count}, async shadow {shadow.positive_sample_count} "
                f"(more than {MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS} required)",), self.gate("graph-owned shadow timing", shadow_scope_passed, f"median {self.format_ms(shadow.median_ms)} (min {args.minimum_shadow_ms:.4f} ms), " f"positive {shadow.positive_sample_count}/{shadow.sample_count}",), self.gate("critical path", timing_passed,
                f"sync {self.format_ms(sync_frame.median_ms)}, async {self.format_ms(async_frame.median_ms)}, " f"delta {regression_percent:+.3f}% (limit +{args.maximum_frame_regression_percent:.3f}%)",), self.gate("pixel parity", pixel_passed, pixel_detail), self.gate(
                "runtime and validation logs", logs_passed, "no forbidden log messages" if logs_passed else f"sync={sync.forbidden_log_messages}, async={async_run.forbidden_log_messages}",),]
        # Functional failures still fail when positive timing intervals are too few.
        if lane_passed and pixel_passed and logs_passed and not intervals_passed:
            verdict = LIT_INCONCLUSIVE
        else:
            verdict = LIT_PASS if all(bool(item[LIT_PASSED]) for item in gates) else LIT_FAIL

        def compact_run(run: RunResult, frame: ScopeSummary, shadow_scope: Optional[ScopeSummary] = None) -> Dict[str, object]:
            result = self.raw_run_payload(run)
            result.update({ LIT_FRAME_MEDIAN_MS: frame.median_ms, "frame_sample_count": frame.sample_count, })
            if shadow_scope:
                result.update(shadow_median_ms=shadow_scope.median_ms, shadow_sample_count=shadow_scope.sample_count, shadow_positive_sample_count=shadow_scope.positive_sample_count,)
            return result

        return { LIT_SCHEMA: LIT_NWB_ASYNC_SHADOW_M4_V3, LIT_VERDICT: verdict, LIT_GATES: gates, LIT_FRAME_REGRESSION_PERCENT: regression_percent, LIT_SYNC: compact_run(sync, sync_frame), LIT_ASYNC: compact_run(async_run, async_frame, shadow),
            LIT_PIXEL_DIFF: self.raw_pixel_diff_payload(pixel_diff) if pixel_diff else None, }
