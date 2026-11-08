"""Validate reflection CPU timing lifecycle and publication coverage."""

import math
import re
from types import SimpleNamespace

from renderer_workload_log_policy import RendererWorkloadLogPolicy, DIMENSIONS
import reflection_benchmark as reflection
import smoke_cpu_gpu_timing as timing
from window_capture_smoke import SmokeFailure, STRICT_LOG_FAILURE_MESSAGES
from window_capture_validation import validate_expected_log_text

LIT_MINIMUM_WARMUP_SECONDS = 5.0
LIT_MILLISECONDS_PER_SECOND = 1000
LIT_REFLECTIONSMOKEPROJECT_HARDWARE_AVAILA = "ReflectionSmokeProject: hardware available"
LIT_DIAGNOSTIC = "diagnostic"
LIT_HARDWARE = "hardware"
LIT_PRESENTATIONS = "presentations"
LIT_SECONDS = "seconds"
LIT_FIRST_SOURCE_FRAME = "first_source_frame"
LIT_FIRST_SOURCE = "first_source"
LIT_END_SOURCE = "end_source"
LIT_SAMPLES = "samples"
LIT_LAST_SOURCE_FRAME = "last_source_frame"
CASES = ("optical_csg_reference", "optical_csg_cap")
REQUIRED_GPU = ("render.frame", "render.reflection_hardware")
REQUIRED_CPU = ("graphics.frame", "graphics.prepare_resources", "graphics.render_passes")
MINIMUM_INTERVALS = 11
MINIMUM_SAMPLES = 100
MINIMUM_SECONDS = 30.0
NUMBER = r"[^ ]+"


class ReflectionCpuTimingEvidence:
    @staticmethod
    def _duration(text):
        try:
            value = float(text)
        except ValueError as error:
            raise SmokeFailure("reflection diagnostic duration is malformed") from error
        if not math.isfinite(value) or value <= 0:
            raise SmokeFailure("reflection diagnostic duration must be finite and positive")
        return value


    @staticmethod
    def _one(lines, prefix, pattern):
        matches = [line for line in lines if line.startswith(prefix)]
        if len(matches) != 1 or not (match := re.fullmatch(pattern, matches[0])):
            raise SmokeFailure(f"reflection diagnostic needs one valid {prefix} record")
        return match, lines.index(matches[0])


    @staticmethod
    def parse_runtime_log(text, case):
        if case not in CASES:
            raise SmokeFailure("unsupported reflection CPU diagnostic fixture")
        validate_expected_log_text(text, [f"ReflectionSmokeProject: case {case} created", LIT_REFLECTIONSMOKEPROJECT_HARDWARE_AVAILA, "ReflectionSmokeProject: shutdown"], [*STRICT_LOG_FAILURE_MESSAGES, "FramebufferCapture:", "ReflectionSmokeStatistics:",
             "ReflectionSmokeHistory:", "ReflectionSmokeOptics:", "ReflectionSmokeSlicePackets:", "ReflectionSmokeProject: fps avg=", "render submission suspended"])
        args = SimpleNamespace(family=case, ray_budget=1382400, optical_queries=16, screen_steps=96, mip_count=10, width=960, height=720, require_hardware=True)
        reflection.validate_trial_log(text, args, reflection.Variant(LIT_DIAGNOSTIC, LIT_HARDWARE, temporal=False, spatial=False, feedback=False))
        lines = text.replace("\r\n", "\n").splitlines()
        extents = set(DIMENSIONS.findall(text))
        if extents != {("960", "720")}:
            raise SmokeFailure("reflection diagnostic extent differs from 960x720")
        availability = [line for line in lines if line.startswith("ReflectionSmokeProject: hardware ") and line.endswith((" available", " unavailable"))]
        if availability != [LIT_REFLECTIONSMOKEPROJECT_HARDWARE_AVAILA]:
            raise SmokeFailure("reflection diagnostic has contradictory hardware availability")
        configured, configured_index = ReflectionCpuTimingEvidence._one(lines, "ReflectionCpuTiming: configured ", re.escape(f"ReflectionCpuTiming: configured case={case} preparation_presentations=64 warmup_seconds=5 "
            "minimum_seconds=30 minimum_presentations=100 minimum_positive_intervals=11 diagnostic_only=1"))
        preparation, preparation_index = ReflectionCpuTimingEvidence._one(lines, "ReflectionCpuTiming: preparation complete ", r"ReflectionCpuTiming: preparation complete presentations=(\d+) source_frame=(\d+)")
        begin, begin_index = ReflectionCpuTimingEvidence._one(lines, "ReflectionCpuTiming: measurement begin ", r"ReflectionCpuTiming: measurement begin presentations=(\d+) source_frame=(\d+) warmup_seconds=(" + NUMBER + r")")
        end, end_index = ReflectionCpuTimingEvidence._one(lines, "ReflectionCpuTiming: measurement complete ", r"ReflectionCpuTiming: measurement complete presentations=(\d+) seconds=(" + NUMBER + r") first=(\d+) last=(\d+) " r"positive_intervals=(\d+) end_source=(\d+) diagnostic_only=1")
        if not configured_index < preparation_index < begin_index < end_index:
            raise SmokeFailure("reflection diagnostic lifecycle order is invalid")
        prepared, prepared_source = map(int, preparation.groups())
        first, source_first = map(int, begin.groups()[:2])
        warmup = ReflectionCpuTimingEvidence._duration(begin[3])
        count, seconds, end_first, last, positive, source_end = end.groups()
        count, end_first, last, positive, source_end = map(int, (count, end_first, last, positive, source_end))
        seconds = ReflectionCpuTimingEvidence._duration(seconds)
        if prepared < 64 or first < prepared or source_first < prepared_source or warmup < LIT_MINIMUM_WARMUP_SECONDS:
            raise SmokeFailure("reflection diagnostic preparation or actual warmup is insufficient")
        if end_first != first or count != last - first or count < MINIMUM_SAMPLES or seconds < MINIMUM_SECONDS:
            raise SmokeFailure("reflection diagnostic measurement count or duration is insufficient")
        if source_end <= source_first or positive < MINIMUM_INTERVALS:
            raise SmokeFailure("reflection diagnostic measured source range or intervals are insufficient")
        intervals = []
        previous_presentation, previous_source = first, source_first
        for index, line in enumerate(lines):
            if not line.startswith("ReflectionCpuTiming: interval"):
                continue
            match = re.fullmatch(r"ReflectionCpuTiming: interval presentations=(\d+) seconds=(" + NUMBER + r") first=(\d+) last=(\d+) first_source=(\d+) end_source=(\d+)", line)
            if match is None or not begin_index < index < end_index:
                raise SmokeFailure("reflection diagnostic interval is malformed or outside measurement")
            admitted, wall, start, finish, frame_start, frame_end = match.groups()
            admitted, start, finish, frame_start, frame_end = map(int, (admitted, start, finish, frame_start, frame_end))
            wall = ReflectionCpuTimingEvidence._duration(wall)
            if (start, frame_start) != (previous_presentation, previous_source) or finish < start or frame_end <= frame_start:
                raise SmokeFailure("reflection diagnostic interval endpoints do not form one continuous window")
            if admitted != finish - start:
                raise SmokeFailure("reflection diagnostic interval count differs from its endpoints")
            intervals.append(dict(presentations=admitted, seconds=wall, first=start, last=finish, first_source=frame_start, end_source=frame_end))
            previous_presentation, previous_source = finish, frame_end
        actual_positive = sum(row[LIT_PRESENTATIONS] > 0 for row in intervals)
        if (previous_presentation, previous_source) != (last, source_end) or positive != actual_positive:
            raise SmokeFailure("reflection diagnostic final endpoints or positive interval count disagree")
        if not math.isclose(sum(row[LIT_SECONDS] for row in intervals), seconds, rel_tol=1e-7, abs_tol=1e-6):
            raise SmokeFailure("reflection diagnostic interval duration sum differs from the actual window")
        return dict(case=case, first=first, last=last, seconds=seconds, presentations=count, first_source=source_first, end_source=source_end, warmup_seconds=warmup, preparation_presentations=prepared,
            positive_intervals=positive, intervals=intervals, accepted_wall_frame_ms=LIT_MILLISECONDS_PER_SECOND * seconds / count, diagnostic_only=True, performance_qualification=False, runtime_signature=RendererWorkloadLogPolicy.device_material_signature(text))


    @staticmethod
    def validate_publications(log_text, raw_path, measurement):
        result = timing.verify_capture(log_text, raw_path, measurement, True, "reflection")
        window = result["window"]
        if (window[LIT_FIRST_SOURCE_FRAME], window["end_source_frame_exclusive"]) != (measurement[LIT_FIRST_SOURCE], measurement[LIT_END_SOURCE]):
            raise SmokeFailure("reflection publication source window disagrees with native measurement")
        coverage = {}
        for domain, names in (("cpu", REQUIRED_CPU), ("gpu", REQUIRED_GPU)):
            for name in names:
                rows = [row for row in result["publications"] if row["included"] and row["domain"] == domain and row["scope"] == name]
                if sum(row[LIT_SAMPLES] for row in rows) < MINIMUM_SAMPLES:
                    raise SmokeFailure(f"reflection diagnostic has fewer than 100 completed {domain} samples: {name}")
                seen_intervals = set()
                last_end = None
                for row in rows:
                    if row[LIT_SECONDS] <= 0:
                        raise SmokeFailure(f"reflection diagnostic target publication is not positive: {name}")
                    if last_end is not None and row[LIT_FIRST_SOURCE_FRAME] <= last_end:
                        raise SmokeFailure(f"reflection diagnostic target publication source ranges overlap or regress: {name}")
                    if row[LIT_SAMPLES] > row[LIT_LAST_SOURCE_FRAME] - row[LIT_FIRST_SOURCE_FRAME] + 1:
                        raise SmokeFailure(f"reflection diagnostic target scope has more than one sample per source frame: {name}")
                    last_end = row[LIT_LAST_SOURCE_FRAME]
                    for index, interval in enumerate(measurement["intervals"]):
                        if interval[LIT_PRESENTATIONS] > 0 and interval[LIT_FIRST_SOURCE] < row["observation_frame"] <= interval[LIT_END_SOURCE]:
                            seen_intervals.add(index)
                            break
                if len(seen_intervals) < MINIMUM_INTERVALS:
                    raise SmokeFailure(f"reflection diagnostic has fewer than 11 positive completed publication intervals: {name}")
                coverage[f"{domain}:{name}"] = dict(positive_publication_intervals=len(seen_intervals), completed_samples=sum(row[LIT_SAMPLES] for row in rows), first_source=rows[0][LIT_FIRST_SOURCE_FRAME], last_source=rows[-1][LIT_LAST_SOURCE_FRAME])
        result["required_publication_coverage"] = coverage
        return result

