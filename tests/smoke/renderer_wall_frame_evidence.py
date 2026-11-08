"""Validate paired wall-frame and GPU timing windows."""

import math
import re

from gpu_timing_evidence import HEADER
from window_capture_outcome import SmokeFailure

LIT_ELIGIBLE = "eligible"
LIT_REASON = "reason"
LIT_FPS = "fps"
LIT_POLICY = "policy"
LIT_SOURCE = "source"
LIT_RETAINED_REPORT_RANGE = "retained_report_range"
LIT_REPORTS = "reports"
LIT_MEAN_FRAME_MS = "mean_frame_ms"
LIT_MEAN_FPS = "mean_fps"
LIT_MILLISECONDS_PER_SECOND = 1000
LIT_AVERAGE = "average"
LIT_MINIMUM = "minimum"
LIT_MAXIMUM = "maximum"
LIT_SECONDS = "seconds"
LIT_FRAMES = "frames"
FPS_NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?|[-+]?(?:inf|nan)"
FPS_REPORT = re.compile(r"^ReflectionSmokeProject: fps avg=(?P<fps>" + FPS_NUMBER + r") frame_ms avg=(?P<average>" + FPS_NUMBER + r") min=(?P<minimum>" + FPS_NUMBER + r") max=(?P<maximum>" + FPS_NUMBER + r") frames=(?P<frames>\d+) seconds=(?P<seconds>"
    + FPS_NUMBER + r")$", re.IGNORECASE)
WALL_FRAME_POLICY = "finite positive callback deltas, including long frames; excludes invalid or nonpositive deltas; not CPU-only time or presentation cadence"
SYNTHETIC_FRAME_REASON = "omitted: this workload supplies fixed simulation delta to both FPS and GPU interval probes"


class RendererWallFrameEvidence:
    @staticmethod
    def summarize_wall_frames(text, timing_text, workload, report_range):
        if workload.reflection_policy is None:
            return {LIT_ELIGIBLE: False, LIT_REASON: SYNTHETIC_FRAME_REASON}
        start, stop = report_range
        if start < 0 or stop <= start:
            raise SmokeFailure("invalid retained wall-frame report range")
        reports = []
        for line in text.replace("\r\n", "\n").splitlines():
            if not line.startswith("ReflectionSmokeProject: fps "):
                continue
            match = FPS_REPORT.fullmatch(line)
            if match is None:
                raise SmokeFailure("malformed reflection FPS report")
            values = {field: float(match[field]) for field in (LIT_FPS, LIT_AVERAGE, LIT_MINIMUM, LIT_MAXIMUM, LIT_SECONDS)}
            frames = int(match[LIT_FRAMES])
            if frames <= 0 or any(not math.isfinite(value) or value <= 0 for value in values.values()):
                raise SmokeFailure("invalid reflection FPS report")
            average = values[LIT_SECONDS] * LIT_MILLISECONDS_PER_SECOND / frames
            if not (values[LIT_MINIMUM] <= values[LIT_AVERAGE] <= values[LIT_MAXIMUM]) \
                or not math.isclose(values[LIT_AVERAGE], average, rel_tol=1e-5, abs_tol=1e-6) \
                or not math.isclose(values[LIT_FPS], frames / values[LIT_SECONDS], rel_tol=1e-5, abs_tol=1e-6):
                raise SmokeFailure("inconsistent reflection FPS report")
            reports.append({**values, LIT_FRAMES: frames})
        headers = list(HEADER.finditer(timing_text.replace("\r\n", "\n")))
        if stop > len(reports) or stop > len(headers):
            raise SmokeFailure("retained GPU intervals lack matching reflection FPS reports")
        selected = reports[start:stop]
        for report, header in zip(selected, headers[start:stop]):
            if report[LIT_FRAMES] != int(header[1]) \
                or not math.isclose(report[LIT_SECONDS], float(header[2]), rel_tol=0, abs_tol=.000051):
                raise SmokeFailure("reflection FPS and GPU report windows do not match")
        frames = sum(report[LIT_FRAMES] for report in selected)
        seconds = sum(report[LIT_SECONDS] for report in selected)
        return {LIT_ELIGIBLE: True, LIT_SOURCE: "ReflectionSmokeProject FpsProbe callback delta", LIT_POLICY: WALL_FRAME_POLICY, LIT_RETAINED_REPORT_RANGE: [start, stop], LIT_REPORTS: len(selected), LIT_FRAMES: frames, LIT_SECONDS: seconds,
            LIT_MEAN_FRAME_MS: seconds * LIT_MILLISECONDS_PER_SECOND / frames, LIT_MEAN_FPS: frames / seconds, "min_frame_ms": min(report[LIT_MINIMUM] for report in selected), "max_frame_ms": max(report[LIT_MAXIMUM] for report in selected)}
