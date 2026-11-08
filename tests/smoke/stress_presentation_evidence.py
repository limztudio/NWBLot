"""Validation of accepted stress presentation measurements."""

import math
import re

from stress_reflection_diagnostics import ReflectionDiagnostics
from stress_workload import StressWorkload
from window_capture_smoke import STRICT_LOG_FAILURE_MESSAGES, require_normal_process_exit
from window_capture_outcome import SmokeFailure
from window_capture_validation import validate_expected_log_text


LIT_MINIMUM_CAPTURE_SECONDS = 30.0
LIT_MILLISECONDS_PER_SECOND = 1000.0
LIT_STRESSTESTSMOKEPROJECT_DEVICE_CAPABILI = "StressTestSmokeProject: device capability "
LIT_FPS = "fps"
LIT_PRESENTATIONS = "presentations"
LIT_SECONDS = "seconds"
LIT_FIRST = "first"
LIT_LAST = "last"
LIT_STRESS_PRESENTATION_TIMING = "stress presentation timing"
LIT_STRESSTESTSMOKEPROJECT_PRESENTATION_PA = "StressTestSmokeProject: presentation pacing "
LIT_MEASUREMENT = "measurement"
LIT_INTERVALS = "intervals"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_WORKLOAD = "workload"
LIT_OPTICAL_REFLECTION = "optical_reflection"
START = "StressTestSmokeProject: presentation timing warmup_seconds=5 measure_seconds=30 clock=steady accepted_native_present=1"
DONE = "StressTestSmokeProject: presentation measurement complete "
INTERVAL = "StressTestSmokeProject: presentation fps "
SHUTDOWN = "StressTestSmokeProject: shutdown"
GPU_DEBUG = ("Loader: GPU debug validation enabled",)
REQUIRED = (START, SHUTDOWN, "AvboitTimingProbe: in-flight ranges 32", "AvboitTimingProbe: render unfocused 1", "AvboitTimingProbe: caustic in-flight ranges 32")


class PresentationEvidence:
    def __init__(self, application_args=(), reflection_diagnostics=False, characters_per_class=10):
        self._application_args = application_args
        self._reflection_diagnostics = reflection_diagnostics
        self._characters_per_class = characters_per_class

    def _parse_sample(self, line, prefix, rate_key, positive_count):
        match = re.fullmatch(re.escape(prefix) + re.escape(rate_key) + r"=(\S+) presentations=(\d+) seconds=(\S+) first=(\d+) last=(\d+)", line)
        if not match:
            raise SmokeFailure("malformed presentation measurement record")
        try:
            fps, frames, seconds, first, last = (float(match[1]), int(match[2]), float(match[3]), int(match[4]), int(match[5]))
        except ValueError as error:
            raise SmokeFailure("invalid presentation measurement number") from error
        if not math.isfinite(fps) or not math.isfinite(seconds) or fps < 0 or seconds <= 0:
            raise SmokeFailure("presentation rate and elapsed time must be finite and nonnegative")
        if not 0 <= first <= last < 2 ** 64 or frames != last - first or (positive_count and frames == 0):
            raise SmokeFailure("presentation count must equal last minus first and be positive for completion")
        if not math.isclose(fps, frames / seconds, rel_tol=1e-9, abs_tol=1e-9):
            raise SmokeFailure("presentation FPS must be count divided by actual wall seconds")
        return {LIT_FPS: fps, LIT_PRESENTATIONS: frames, LIT_SECONDS: seconds, LIT_FIRST: first, LIT_LAST: last}

    def parse_runtime_log(self, text, exit_code):
        application_args = self._application_args
        reflection_diagnostics = self._reflection_diagnostics
        characters_per_class = self._characters_per_class
        require_normal_process_exit(exit_code, "", LIT_STRESS_PRESENTATION_TIMING)
        validate_expected_log_text(text, list(REQUIRED), list(STRICT_LOG_FAILURE_MESSAGES) + ["presentation measurement incomplete", "render submission suspended", "render pass skipped", "device recreation"])
        lines = [line.strip() for line in text.splitlines()]
        if any(lines.count(marker) != 1 for marker in REQUIRED):
            raise SmokeFailure("one exact startup, reservation, fixture and shutdown sequence is required")
        workload = StressWorkload(characters_per_class, START, INTERVAL).parse(lines)
        if sum(line.startswith("StressTestSmokeProject: presentation timing ") for line in lines) != 1:
            raise SmokeFailure("multiple or conflicting presentation timing configurations")
        if any(value == "--gpudbg" or value.startswith("--gpudbg=") for value in application_args):
            if any(lines.count(marker) != 1 for marker in GPU_DEBUG):
                raise SmokeFailure("requested GPU validation requires exactly one Loader activation marker")
        completions = [line for line in lines if line.startswith(DONE)]
        intervals = [self._parse_sample(line, INTERVAL, "avg", False) for line in lines if line.startswith(INTERVAL)]
        if len(completions) != 1 or not intervals:
            raise SmokeFailure("exactly one completion and its interval evidence are required")
        total = self._parse_sample(completions[0], DONE, LIT_FPS, True)
        if total[LIT_SECONDS] < LIT_MINIMUM_CAPTURE_SECONDS:
            raise SmokeFailure("measurement must span at least 30 actual wall seconds")
        if not lines.index(START) < lines.index(completions[0]) < lines.index(SHUTDOWN):
            raise SmokeFailure("startup, completion and shutdown order is invalid")
        interval_lines = [index for index, line in enumerate(lines) if line.startswith(INTERVAL)]
        if not lines.index(START) < min(interval_lines) <= max(interval_lines) < lines.index(completions[0]):
            raise SmokeFailure("intervals must occur inside the completed measurement")
        previous = total[LIT_FIRST]
        for interval in intervals:
            if interval[LIT_FIRST] != previous:
                raise SmokeFailure("presentation intervals must form a contiguous count chain")
            previous = interval[LIT_LAST]
        if previous != total[LIT_LAST] or sum(row[LIT_PRESENTATIONS] for row in intervals) != total[LIT_PRESENTATIONS]:
            raise SmokeFailure("interval counts do not match completion")
        if not math.isclose(sum(row[LIT_SECONDS] for row in intervals), total[LIT_SECONDS], rel_tol=1e-9, abs_tol=1e-9):
            raise SmokeFailure("interval wall times do not match completion")
        pacing = [line for line in lines if line.startswith(LIT_STRESSTESTSMOKEPROJECT_PRESENTATION_PA)]
        if len(pacing) != 1:
            raise SmokeFailure("exactly one current presentation pacing summary is required")
        if not max(interval_lines) < lines.index(pacing[0]) < lines.index(completions[0]):
            raise SmokeFailure("presentation pacing must follow the measured intervals before completion")
        pace = re.fullmatch(re.escape(LIT_STRESSTESTSMOKEPROJECT_PRESENTATION_PA) + r"samples=(\d+) p50ms=(\S+) p95ms=(\S+) maxms=(\S+) stalls50ms=(\d+)", pacing[0])
        if not pace:
            raise SmokeFailure("malformed presentation pacing summary")
        try:
            samples, p50, p95, maxms, stalls = (int(pace[1]), float(pace[2]), float(pace[3]), float(pace[4]), int(pace[5]))
        except ValueError as error:
            raise SmokeFailure("invalid presentation pacing number") from error
        if samples < 0 or not all(math.isfinite(v) for v in (p50, p95, maxms)) or stalls < 0 or p50 < 0 or p95 < 0 or maxms < 0:
            raise SmokeFailure("presentation pacing values must be finite and nonnegative")
        if not p50 <= p95 <= maxms:
            raise SmokeFailure("presentation pacing percentiles must be ordered p50 <= p95 <= max")
        pacing_summary = {"samples": samples, "p50ms": p50, "p95ms": p95, "maxms": maxms, "stalls50ms": stalls}
        capability = [line for line in lines if line.startswith(LIT_STRESSTESTSMOKEPROJECT_DEVICE_CAPABILI)]
        if len(capability) > 1:
            raise SmokeFailure("exactly one device capability report is allowed")
        extents = re.findall(r"deferred rendering targets ready \((\d+)x(\d+),", text)
        if not extents or any(pair != ("1280", "900") for pair in extents):
            raise SmokeFailure("actual stress extent must be 1280x900")
        return {LIT_MEASUREMENT: total | {"frame_ms": LIT_MILLISECONDS_PER_SECOND * total[LIT_SECONDS] / total[LIT_PRESENTATIONS]}, LIT_INTERVALS: intervals, LIT_WIDTH: 1280, LIT_HEIGHT: 900, "warmup_seconds": 5,
            "requested_measurement_seconds": 30, "clock": "steady", "count": "accepted_native_present", LIT_WORKLOAD: workload, "pacing": pacing_summary, LIT_OPTICAL_REFLECTION: ReflectionDiagnostics(lines, reflection_diagnostics, SHUTDOWN).parse()}

    def parse_measurement(self, log_text):
        """Replay the complete raw measurement log without requiring a process launch."""
        parsed = self.parse_runtime_log(log_text, 0)
        return parsed[LIT_MEASUREMENT] | {LIT_INTERVALS: parsed[LIT_INTERVALS]}
