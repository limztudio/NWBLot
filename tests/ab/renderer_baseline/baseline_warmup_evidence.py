"""Validate complex-GI capture warmup evidence."""

from __future__ import annotations

import math
import re
import struct
from typing import Mapping, Optional

from profiles import BaselineProfile
from window_capture_smoke import SmokeFailure

LIT_MINIMUM_COMPLEX_GI_SETTLE_SECONDS = 30.0
LIT_ENABLED_FLAG = "1"
LIT_NWB_GI_SMOKE_COMPLEX_SCENE = "NWB_GI_SMOKE_COMPLEX_SCENE"
LIT_COMPLEX_GI_PROFILE_REQUIRES_ITS_SELECT = "complex GI profile requires its selected complex scene"
LIT_F = "f"
LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS = "NWB_GI_SMOKE_MIN_SETTLE_SECONDS"
LIT_START = "start"
LIT_END = "end"
LIT_READBACK = "readback"
LIT_COMPLEX_GI_HAS_MALFORMED_LIVE_WARMUP_E = "complex GI has malformed live warmup evidence"
LIT_CAPTURE_SOURCE_FRAME = "capture_source_frame"
LIT_CAPTURE = "capture"


class BaselineWarmupEvidence:
    @staticmethod
    def validate(log_text: str, profile: BaselineProfile, frozen_environment: Mapping[str, str]) -> Optional[Mapping[str, object]]:
        if profile.frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) != LIT_ENABLED_FLAG:
            return None
        if frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) != LIT_ENABLED_FLAG:
            raise SmokeFailure(LIT_COMPLEX_GI_PROFILE_REQUIRES_ITS_SELECT)
        try:
            requested_seconds = struct.unpack(LIT_F, struct.pack(LIT_F, float(frozen_environment.get(LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS, "30"))))[0]
        except (ValueError, OverflowError, struct.error) as error:
            raise SmokeFailure("complex GI requires a finite live warmup of at least 30 seconds") from error
        if not math.isfinite(requested_seconds) or requested_seconds < LIT_MINIMUM_COMPLEX_GI_SETTLE_SECONDS or profile.capture_freeze_frame < 360:
            raise SmokeFailure("complex GI requires at least 360 presentations then 30 live warmup seconds")
        patterns = {
            LIT_START: r"GiTestSmokeProject: complex GI live warmup start graphics_frame=(\d+) presentations=(\d+) required_presentations=(\d+) required_seconds=(\S+)",
            LIT_END: r"GiTestSmokeProject: complex GI live warmup end graphics_frame=(\d+) presentations=(\d+) elapsed_seconds=(\S+)",
            LIT_READBACK: r"FramebufferCapture: graphics source frame (\d+)",
            LIT_CAPTURE: r"GiTestSmokeProject: complex GI capture source_frame=(\d+)",
        }
        matches = {}
        for name, pattern in patterns.items():
            records = list(re.finditer(pattern, log_text))
            if len(records) != 1:
                raise SmokeFailure(f"complex GI requires exactly one {name} live warmup evidence record")
            matches[name] = records[0]
        start_frame, start_presentations, required_presentations = map(int, matches[LIT_START].groups()[:3])
        end_frame, end_presentations = map(int, matches[LIT_END].groups()[:2])
        readback_frame = int(matches[LIT_READBACK].group(1))
        capture_frame = int(matches[LIT_CAPTURE].group(1))
        numeric_pattern = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
        for value in (matches[LIT_START].group(4), matches[LIT_END].group(3)):
            if not re.fullmatch(numeric_pattern, value):
                raise SmokeFailure(LIT_COMPLEX_GI_HAS_MALFORMED_LIVE_WARMUP_E)
        try:
            required_seconds = float(matches[LIT_START].group(4))
            elapsed_seconds = float(matches[LIT_END].group(3))
        except ValueError as error:
            raise SmokeFailure(LIT_COMPLEX_GI_HAS_MALFORMED_LIVE_WARMUP_E) from error
        if required_presentations != profile.capture_freeze_frame or start_presentations < required_presentations:
            raise SmokeFailure("complex GI live warmup began before the presentation warmup")
        if not math.isfinite(required_seconds) or required_seconds != requested_seconds:
            raise SmokeFailure("complex GI live warmup duration does not match its configured requirement")
        if not math.isfinite(elapsed_seconds) or elapsed_seconds < required_seconds:
            raise SmokeFailure("complex GI live warmup ended before its required duration")
        if end_frame <= start_frame or end_presentations <= start_presentations:
            raise SmokeFailure("complex GI live warmup has no rendered presentation progress")
        if capture_frame != readback_frame or capture_frame < end_frame or capture_frame >= 2**64 - 1:
            raise SmokeFailure("complex GI capture source does not match its completed readback and live warmup")
        if not (matches[LIT_START].start() < matches[LIT_END].start() < matches[LIT_READBACK].start() < matches[LIT_CAPTURE].start()):
            raise SmokeFailure("complex GI live warmup and readback evidence is out of order")
        return {"start_graphics_frame": start_frame, "start_presentations": start_presentations, "required_presentations": required_presentations, "required_live_seconds": required_seconds,
            "elapsed_live_seconds": elapsed_seconds, "warmup_end_graphics_frame": end_frame, "end_presentations": end_presentations, LIT_CAPTURE_SOURCE_FRAME: capture_frame}

