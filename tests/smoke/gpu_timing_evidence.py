"""Parse and summarize sealed GPU timing publication intervals."""

from dataclasses import dataclass
import math
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from gpu_timing_parse import SCOPE_RE
from window_capture_outcome import SmokeFailure

LIT_SCOPE = "scope"
LIT_TOTAL_MS = "total_ms"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_MEAN_MS = "mean_ms"
HEADER = re.compile(r"^=== interval: (\d+) frames / ([-+0-9.eE]+)s ===$", re.MULTILINE)


@dataclass(frozen=True)
class ScopeSample:
    total_ms: float
    gpu_samples: int


class GpuTimingEvidence:
    def __init__(self, symbols=None, frame="render.frame"):
        self.symbols = symbols or {}
        self.frame = frame

    def parse_intervals(self, text, finalized=False):
        """Only a following header seals a live report; the current tail may be partial."""
        text = text.replace("\r\n", "\n")
        headers = list(HEADER.finditer(text))
        result = []
        count = len(headers) if finalized else max(0, len(headers) - 1)
        for index in range(count):
            match = headers[index]
            seconds = float(match.group(2))
            if not math.isfinite(seconds) or seconds <= 0:
                raise SmokeFailure("invalid timing publication interval")
            end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
            samples = {}
            for line in text[match.end():end].splitlines():
                if not line.strip():
                    continue
                parsed = SCOPE_RE.fullmatch(line)
                if not parsed:
                    raise SmokeFailure(f"malformed completed GPU timing row: {line}")
                scope = self.symbols.get(parsed.group(LIT_SCOPE), parsed.group(LIT_SCOPE))
                values = tuple(float(parsed.group(field)) for field in ("average", "minimum", "maximum", "total", "sample_average"))
                gpu_samples = int(parsed.group(LIT_GPU_SAMPLES))
                published_windows = int(parsed.group("published_windows"))
                if (any(not math.isfinite(value) or value < 0.0 for value in values) or not 0 < published_windows < 2 ** 32 or not 0 < gpu_samples < 2 ** 64 or values[1] > values[0] or values[0] > values[2]):
                    raise SmokeFailure(f"invalid current GPU timing values: {line}")
                total = values[3]
                if scope in samples:
                    raise SmokeFailure(f"duplicate timing scope in one report: {scope}")
                samples[scope] = ScopeSample(total, gpu_samples)
            result.append(samples)
        return result

    def summarize_intervals(self, intervals):
        totals = {}
        for interval in intervals:
            for scope, value in interval.items():
                entry = totals.setdefault(scope, {LIT_TOTAL_MS: 0.0, LIT_GPU_SAMPLES: 0, LIT_REPORTS: 0})
                entry[LIT_TOTAL_MS] += value.total_ms
                entry[LIT_GPU_SAMPLES] += value.gpu_samples
                entry[LIT_REPORTS] += 1
        for entry in totals.values():
            entry[LIT_MEAN_MS] = entry[LIT_TOTAL_MS] / entry[LIT_GPU_SAMPLES]
        return totals

    def positive_frame_reports(self, reports):
        return sum(self.frame in report and report[self.frame].total_ms > 0.0 for report in reports)

    def retain_after_warmup(self, reports, warmup_intervals, minimum_warmup_frames=0):
        """Feedback warms up by completed GPU observations, not a presumed wall-clock frame rate."""
        start = min(warmup_intervals, len(reports))
        completed = sum(report[self.frame].gpu_samples for report in reports[:start] if self.frame in report)
        while start < len(reports) and completed < minimum_warmup_frames:
            completed += reports[start].get(self.frame, ScopeSample(0.0, 0)).gpu_samples
            start += 1
        return start, reports[start:]
