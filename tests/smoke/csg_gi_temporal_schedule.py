"""Validate CSG temporal capture schedule and runtime log evidence."""

import math
import re

from csg_gi_temporal_contract import (
    EVENT_DATA_FLAGS_MARKER,
    EVENT_ORDER_MARKER,
    LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN,
    LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP,
    LIT_EVENT_DATA,
    LIT_EVENT_ORDER,
    LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE,
    LIT_SPAN_STATE,
    LIT_TEMPORAL_SERIES_COMPLETE,
    LIT_TEMPORAL_WARMUP_COMPLETE,
    MINIMUM_FRAME,
    OVERLAPPING_MARKER,
    SAMPLE_COUNT,
    SPAN_STATE_MARKER,
    WARMUP_SECONDS,
)
from window_capture_smoke import STRICT_LOG_FAILURE_MESSAGES, SmokeFailure
from window_capture_validation import validate_expected_log_text


class CsgTemporalSchedule:
    @staticmethod
    def parse(log, frame_interval):
        validate_expected_log_text(log, (LIT_TEMPORAL_SERIES_COMPLETE, LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN, LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE), STRICT_LOG_FAILURE_MESSAGES)
        setups = re.findall(r"CsgVisibleSmokeProject: temporal scene animated=([01]) view=(full|indirect|direct|normal|position|cap_state|interval_state|event_state|event_data|event_order|span_state) minimum_frame=(\d+) warmup_seconds=(\d+) interval=(\d+) samples=(\d+)", log)
        if len(setups) != 1:
            raise SmokeFailure("temporal series must have exactly one logged scene setup")
        animated, view, minimum_frame, warmup_seconds, logged_interval, sample_count = setups[0]
        if view == LIT_EVENT_DATA and log.count(EVENT_DATA_FLAGS_MARKER) != 1:
            raise SmokeFailure("event-data series must identify its missing-span flag encoding")
        if view == LIT_EVENT_ORDER and log.count(EVENT_ORDER_MARKER) != 1:
            raise SmokeFailure("event-order series must identify its validated raw face encoding")
        if view == LIT_SPAN_STATE and log.count(SPAN_STATE_MARKER) != 1:
            raise SmokeFailure("span-state series must identify its count and flag encoding")
        overlap_count = log.count(OVERLAPPING_MARKER)
        if overlap_count > 1 or (overlap_count and animated != "0"):
            raise SmokeFailure("overlapping receiver series requires exactly one static duplicate")
        if (int(minimum_frame), int(warmup_seconds), int(logged_interval), int(sample_count)) != (MINIMUM_FRAME, WARMUP_SECONDS, frame_interval, SAMPLE_COUNT):
            raise SmokeFailure("logged temporal setup differs from the requested sample schedule")
        warmup_starts = re.findall(r"CsgVisibleSmokeProject: temporal warmup started graphics frame (\d+)", log)
        warmup_completions = re.findall(r"CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds=(\d+\.\d+) first_frame=(\d+)", log)
        if len(warmup_starts) != 1 or len(warmup_completions) != 1:
            raise SmokeFailure("temporal series must identify one live warmup start and completion")
        warmup_start_frame = int(warmup_starts[0])
        elapsed, first_frame = warmup_completions[0]
        warmup_elapsed_seconds = float(elapsed)
        first_frame = int(first_frame)
        if (warmup_start_frame < MINIMUM_FRAME or first_frame <= warmup_start_frame or not math.isfinite(warmup_elapsed_seconds) or warmup_elapsed_seconds < WARMUP_SECONDS):
            raise SmokeFailure("temporal series must render at least 360 frames before a 30-second live warmup")
        captured = [(int(sample), int(frame)) for sample, frame in re.findall(r"CsgVisibleSmokeProject: temporal sample (\d+) graphics frame (\d+)", log)]
        expected = [(sample, first_frame + frame_interval * sample) for sample in range(SAMPLE_COUNT)]
        if captured != expected:
            raise SmokeFailure(f"temporal source-frame sequence differs from its deterministic schedule: {captured}")
        if not (log.index(LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP) < log.index(LIT_TEMPORAL_WARMUP_COMPLETE) < log.index("CsgVisibleSmokeProject: temporal sample 0 graphics frame")):
            raise SmokeFailure("temporal capture must follow the completed live warmup")
        if len(re.findall(r"FramebufferCapture: capture ready", log)) != SAMPLE_COUNT:
            raise SmokeFailure("temporal series did not complete all accepted framebuffer readbacks")
        bounded_motion = "CsgVisibleSmokeProject: front-facing triangle pose from graphics frame" in log
        return animated, view, overlap_count, warmup_start_frame, warmup_elapsed_seconds, first_frame, captured, bounded_motion
