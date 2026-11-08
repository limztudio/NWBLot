from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Optional

from window_capture_smoke import SmokeFailure, SmokeSkip


LIT_ENABLED_FLAG = "1"
LANE_RE = re.compile(r"StressTestSmokeProject:\s+M4 async compute\s+requested=([01])\s+effective=([01])" r"\s+graphicsQueue=(\d+):(\d+)\s+computeQueue=(\d+):(\d+)" r"\s+computeDedicated=([01])\s+computeSupported=([01])\s+computeGraphics=([01])(?=\s|$)",)
INVALID_PHYSICAL_QUEUE_INDEX = 65535
MAX_DEVICE_GENERATION = 65535


class DedicatedComputeUnavailable(SmokeSkip):
    """The async binary requested a lane, but the adapter routed it through Graphics."""


@dataclass(frozen=True)
class PhysicalQueueId:
    index: int
    device_generation: int

    @property
    def valid(self) -> bool:
        return 0 <= self.index < INVALID_PHYSICAL_QUEUE_INDEX and 0 < self.device_generation <= MAX_DEVICE_GENERATION


@dataclass(frozen=True)
class LaneStatus:
    requested: bool
    effective: bool
    graphics_queue: PhysicalQueueId
    compute_queue: PhysicalQueueId
    compute_dedicated: bool
    compute_supported: bool
    compute_graphics: bool

    @property
    def dedicated_compute(self) -> bool:
        return (self.graphics_queue.valid and self.compute_queue.valid and self.compute_queue.device_generation == self.graphics_queue.device_generation and self.compute_queue != self.graphics_queue and self.compute_dedicated and self.compute_supported
            and not self.compute_graphics)

    @staticmethod
    def parse(log_text: str) -> Optional[LaneStatus]:
        matches = LANE_RE.findall(log_text)
        if not matches:
            return None

        requested, effective, graphics_index, graphics_generation, compute_index, compute_generation, dedicated, supported, graphics = matches[-1]
        return LaneStatus(requested=requested == LIT_ENABLED_FLAG, effective=effective == LIT_ENABLED_FLAG, graphics_queue=PhysicalQueueId(int(graphics_index), int(graphics_generation)), compute_queue=PhysicalQueueId(int(compute_index), int(compute_generation)),
            compute_dedicated=dedicated == LIT_ENABLED_FLAG, compute_supported=supported == LIT_ENABLED_FLAG, compute_graphics=graphics == LIT_ENABLED_FLAG,)

    def validate_for_mode(self, mode: str) -> None:
        if not self.graphics_queue.valid:
            raise SmokeFailure(f"{mode} benchmark reported an invalid primary Graphics queue: {self}")
        compute_absent = self.compute_queue == PhysicalQueueId(INVALID_PHYSICAL_QUEUE_INDEX, 0)
        if compute_absent:
            if self.compute_dedicated or self.compute_supported or self.compute_graphics:
                raise SmokeFailure(f"{mode} benchmark reported capabilities for an absent Compute queue: {self}")
        elif not self.compute_queue.valid or self.compute_queue.device_generation != self.graphics_queue.device_generation:
            raise SmokeFailure(f"{mode} benchmark reported an invalid or stale primary Compute queue: {self}")
        if self.effective != (self.requested and self.dedicated_compute):
            raise SmokeFailure(f"{mode} benchmark reported contradictory async-compute route evidence: {self}")
        if mode == "async":
            if not self.requested:
                raise SmokeFailure(f"async benchmark did not request an async compute lane: {self}")
            if not self.effective:
                raise DedicatedComputeUnavailable("async-shadow M4 skipped: the requested async lane did not resolve to a distinct dedicated compute-only queue " f"(Graphics {self.graphics_queue}, Compute {self.compute_queue})")
        if mode == "sync" and (self.requested or self.effective):
            raise SmokeFailure(f"synchronous baseline unexpectedly enabled async compute: {self}")
