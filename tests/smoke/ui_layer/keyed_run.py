from __future__ import annotations

import time

from checkpoint_report import CHECKPOINT_SETTLE_SECONDS
from fixture_launch import UiFixtureRunBase, run_ui_fixture
from window_capture_smoke import SmokeFailure
from window_capture_termination import LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS

LIT_RECTANGLES = "rectangles"


class KeyedRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern, *, native_type, snapshot_reader, observer, minimum_height, fixture, capture_stage):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=native_type(backend, handle))
        self.snapshot_reader = snapshot_reader
        self.observer = observer
        self.minimum_height = minimum_height
        self.fixture = fixture
        self.capture_stage = capture_stage

    def row(self, offset):
        x, y, width, height = self.snapshot[LIT_RECTANGLES]["first_row"]
        vx, vy, vw, vh = self.snapshot[LIT_RECTANGLES]["viewport"]
        left, right = max(vx, x), min(vx + vw, x + width)
        top, bottom = max(vy, y + offset * height), min(vy + vh, y + (offset + 1) * height)
        if height <= 0.0 or right <= left or not 0 <= offset < self.snapshot["past"] - self.snapshot["first"] or bottom <= top:
            raise SmokeFailure(f"{self.fixture} requested a row outside its accepted visible interval")
        return self.point(left + min((right - left) / 2.0, 120.0), (top + bottom) / 2.0)

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), **changes):
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        if self.snapshot is not None:
            self.native.pointer(*self.rectangle_point("counter"))
        time.sleep(CHECKPOINT_SETTLE_SECONDS)
        self.poll_checkpoint(name, minimum_extent=(600.0, self.minimum_height),
            minimum_extent_message=f"the {self.fixture} fixture needs a logical client of at least 600x{self.minimum_height:g}",
            process_stage=self.capture_stage, snapshot_reader=self.snapshot_reader,
            evaluate=lambda frame, snapshot: self.observer(frame, snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra),
            raw_windows=False, failure_label=self.fixture, dynamic=dynamic)


def run_keyed_fixture(args, *, fixture, display_marker, driver_type, log_name, label, window_label, process_label, graceful_exit_timeout=LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS):
    return run_ui_fixture(args, fixture=fixture, display_marker=display_marker, driver_type=driver_type, log_name=log_name, label=label,
        window_label=window_label, process_label=process_label, raw_windows=False, restore_pointer=False, graceful_exit_timeout=graceful_exit_timeout)
