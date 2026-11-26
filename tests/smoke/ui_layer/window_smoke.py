#!/usr/bin/env python3
"""Exercise skinned window behavior through native input and displayed geometry."""
from __future__ import annotations

import platform
import sys
import time

from native_input import NativeInput
from probe_common import center
from window_probe import ACTION, observe_window, snapshot_from_logs
from window_capture_smoke import run_smoke_main, SmokeFailure
from smoke_arguments import parse_ui_smoke_args
from checkpoint_report import CHECKPOINT_SETTLE_SECONDS
from fixture_launch import UiFixtureRunBase, run_ui_fixture

LIT_SCALE = "scale"
LIT_RECTANGLES = "rectangles"
LIT_PASSED = "passed"
LIT_CAPTURE_CHANGE_CANCEL = "capture_change_cancel"
LIT_CAPTURE_CHANGE_RECOVERY = "capture_change_recovery"
LIT_POINTER_LEAVE_DRAG = "pointer_leave_drag"
LIT_MINIMUM = "minimum"
LIT_BUTTON = "button"
LIT_RESIZE = "resize"
LIT_COLLAPSE = "collapse"
LIT_LOCK = "lock"
LIT_LOCKED = "locked"
LIT_INCREASE = "increase"
LIT_UI_WINDOW_FIXTURE = "UI window fixture"


def parse_args(argv):
    return parse_ui_smoke_args(argv, description=__doc__, include_skin=True)


class WindowRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=NativeInput(backend, handle))

    def title_point(self):
        x, y, width, height = self.snapshot[LIT_RECTANGLES]["title"]
        return x + width * 0.6, y + height / 2.0

    def quick_drag(self, origin, dx, dy, *, leave=False):
        start = self.point(*origin)
        finish = self.point(origin[0] + dx, origin[1] + dy)
        self.native.button(True, *start)
        if leave:
            self.native._post(0x02A3)
        self.native.pointer(*finish)
        self.native.button(False, *finish)
        scale_x, scale_y = self.snapshot[LIT_SCALE]
        return (finish[0] - start[0]) / scale_x, (finish[1] - start[1]) / scale_y

    def checkpoint(self, name, bounds, count, collapsed=False, locked=False, *, extent=None):
        time.sleep(CHECKPOINT_SETTLE_SECONDS)
        self.poll_checkpoint(name, minimum_extent=(430.0, 270.0), minimum_extent_message="the window fixture needs a logical client of at least 430x270 for separate model markers",
            process_stage="during window UI capture", snapshot_reader=snapshot_from_logs,
            evaluate=lambda frame, snapshot: observe_window(frame, snapshot, bounds, count, collapsed, locked, extent=extent, skin=self.args.skin), failure_label="window")

    def write_report(self, passed):
        self.write_json_report("window.json", {LIT_PASSED: passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "input_path": "Win32 posted button/motion/leave/capture-change/activation messages" if self.native.windows else "X11 XSendEvent and XSetInputFocus",
            "runtime_limit": "No physical pointer grab or native Wayland input qualification",
            "omitted_platform_stages": [] if self.native.windows else [LIT_CAPTURE_CHANGE_CANCEL, LIT_CAPTURE_CHANGE_RECOVERY, LIT_POINTER_LEAVE_DRAG]})

    def execute(self):
        bounds = (40.0, 40.0, 300.0, 180.0)
        self.checkpoint("initial", bounds, 0)
        if tuple(self.snapshot[LIT_MINIMUM]) != (180.0, 110.0):
            raise SmokeFailure(f"window fixture minimum differs from declared 180x110: {self.snapshot[LIT_MINIMUM]}")
        self.click(LIT_BUTTON)
        self.checkpoint("first_button", bounds, 1)
        dx, dy = self.quick_drag(self.title_point(), 30.0, 10.0)
        bounds = (bounds[0] + dx, bounds[1] + dy, bounds[2], bounds[3])
        self.checkpoint("complete_title_drag", bounds, 1)
        dx, dy = self.quick_drag(center(self.snapshot[LIT_RECTANGLES][LIT_RESIZE]), 40.0, 10.0)
        bounds = (bounds[0], bounds[1], bounds[2] + dx, bounds[3] + dy)
        self.checkpoint("complete_corner_resize", bounds, 1)
        self.quick_drag(center(self.snapshot[LIT_RECTANGLES][LIT_RESIZE]), -240.0, -180.0)
        bounds = (bounds[0], bounds[1], 180.0, 110.0)
        self.checkpoint("minimum_resize", bounds, 1)
        self.click(LIT_COLLAPSE)
        self.checkpoint("collapsed", bounds, 1, collapsed=True)
        self.click(LIT_BUTTON)
        self.checkpoint("collapsed_body_inactive", bounds, 1, collapsed=True)
        self.click(LIT_COLLAPSE)
        self.checkpoint("expanded", bounds, 1)
        self.click(LIT_LOCK)
        self.checkpoint(LIT_LOCKED, bounds, 1, locked=True)
        self.quick_drag(self.title_point(), 24.0, 12.0)
        self.checkpoint("locked_move_barrier", bounds, 1, locked=True)
        self.quick_drag(center(self.snapshot[LIT_RECTANGLES][LIT_RESIZE]), 60.0, 40.0)
        self.checkpoint("locked_resize_barrier", bounds, 1, locked=True)
        self.click(LIT_LOCK)
        self.checkpoint("unlocked", bounds, 1)
        origin = self.title_point()
        self.native.button(True, *self.point(*origin))
        self.native.focus_loss_and_gain()
        self.native.button(False, *self.point(origin[0] + 25.0, origin[1] + 10.0))
        self.checkpoint("focus_loss_cancels_drag", bounds, 1)
        self.click(LIT_BUTTON)
        self.checkpoint("focus_loss_recovery_click", bounds, 2)
        count = 2
        if self.native.windows:
            origin = self.title_point()
            self.native.button(True, *self.point(*origin))
            self.native._post(0x0215)
            self.native.button(False, *self.point(origin[0] + 25.0, origin[1] + 10.0))
            self.checkpoint(LIT_CAPTURE_CHANGE_CANCEL, bounds, count)
            self.click(LIT_BUTTON)
            count += 1
            self.checkpoint(LIT_CAPTURE_CHANGE_RECOVERY, bounds, count)
            dx, dy = self.quick_drag(self.title_point(), 18.0, 8.0, leave=True)
            bounds = (bounds[0] + dx, bounds[1] + dy, bounds[2], bounds[3])
            self.checkpoint(LIT_POINTER_LEAVE_DRAG, bounds, count)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("client_resized", bounds, count, extent=(800, 600))
        dx, dy = self.quick_drag(self.title_point(), 24.0, 16.0)
        bounds = (bounds[0] + dx, bounds[1] + dy, bounds[2], bounds[3])
        self.checkpoint("resized_first_drag", bounds, count, extent=(800, 600))
        self.click(LIT_BUTTON)
        self.checkpoint("resized_button", bounds, count + 1, extent=(800, 600))


def expected_actions(windows):
    actions = [(LIT_INCREASE, 1, 0, 1), (LIT_LOCKED, 1, 1, 2), (LIT_LOCKED, 1, 0, 3), (LIT_INCREASE, 2, 0, 4)]
    if windows:
        actions += [(LIT_INCREASE, 3, 0, 5), (LIT_INCREASE, 4, 0, 6)]
    else:
        actions += [(LIT_INCREASE, 3, 0, 5)]
    return actions


def verify_window_actions(text, driver):
    observed = [(action, int(count), int(locked), int(sequence)) for action, count, locked, sequence in ACTION.findall(text)]
    expected = expected_actions(driver.native.windows)
    if observed != expected:
        raise SmokeFailure(f"unexpected window action sequence: {observed}; expected {expected}")


def run(args):
    return run_ui_fixture(args, fixture="WINDOW", display_marker="UiWindowSmoke: display", driver_type=WindowRun, log_name="window.log", label="UI window",
        window_label="window", process_label="window", after_collect=verify_window_actions,
        summary_formatter=lambda driver: f"UI window: {len(driver.stages)} displayed gates, {len(expected_actions(driver.native.windows))} exact actions")


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
