#!/usr/bin/env python3
"""Exercise custom controls through Win32/X11 input and validate displayed model markers.

Windows uses native window messages; Shift+Tab also uses a real Shift modifier
while the fixture is foreground. X11 uses XSendEvent and actual focus transfers.
This harness does not qualify physical pointer grabs or Wayland input.
"""
from __future__ import annotations

import math
from pathlib import Path
import re
import sys
import time

SMOKE_DIRECTORY = Path(__file__).resolve().parent.parent
if str(SMOKE_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SMOKE_DIRECTORY))

from window_capture_image_codec import Bmp24Codec  # noqa: E402
from window_capture_logs import wait_for_log_message  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    run_smoke_main, SmokeFailure, )
import native_input
from smoke_arguments import parse_ui_smoke_args
from fixture_launch import DISPLAY_MARKER_TIMEOUT_SECONDS, UiFixtureRunBase, run_ui_fixture
from checkpoint_report import CHECKPOINT_POLL_SECONDS, CHECKPOINT_SETTLE_SECONDS, CHECKPOINT_STAGE_TIMEOUT_SECONDS
from probe_reference import linear_rgb_bytes

LIT_ENABLED = "enabled"
LIT_PASSED = "passed"
LIT_ESCAPE = "Escape"
LIT_SPACE = "space"
LIT_TAB = "Tab"
LIT_RETURN = "Return"

DISPLAY_PATTERN = re.compile(r"UiInteractiveSmoke: display logical=([0-9.eE+-]+)x([0-9.eE+-]+) scale=([0-9.eE+-]+)x([0-9.eE+-]+)")
ACTION_PATTERN = re.compile(r"UiInteractiveSmoke: action=(increase|enabled|reset) count=(\d+) enabled=([01]) actions=(\d+)")
EXPECTED_ACTIONS = [("increase", 1, 1, 1), ("increase", 2, 1, 2), ("increase", 3, 1, 3), ("enabled", 3, 0, 4), ("enabled", 3, 1, 5), ("increase", 4, 1, 6), ("increase", 5, 1, 7), ("increase", 6, 1, 8),]
OFF = (0.08, 0.01, 0.01)
ON = (0.02, 0.70, 0.12)
MARKER_TOLERANCE = 4
COUNT_MARKER_BITS = 8
BUTTON_POINT = (91, 76)
RESIZED_CLIENT_EXTENT = (800, 600)
HIT_LAYOUT_SETTLE_SECONDS = 0.2


def parse_args(argv, *, description=__doc__):
    return parse_ui_smoke_args(argv, description=description, include_many_roots=True)


def observe_markers(frame, scale_x, scale_y, expected_count, enabled):
    width, height, rows = frame
    probes = []
    for bit in range(COUNT_MARKER_BITS + 1):
        logical_x = 25.0 + bit * 20.0 if bit < COUNT_MARKER_BITS else 224.0
        x = int(round(logical_x * scale_x))
        y = int(round(height - 14.0 * scale_y))
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"model marker falls outside {width}x{height} at ({x},{y})")
        wanted = bool(expected_count & (1 << bit)) if bit < COUNT_MARKER_BITS else enabled
        expected = linear_rgb_bytes(ON if wanted else OFF)
        sample = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in sample)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, expected))
        probes.append({"bit": bit if bit < COUNT_MARKER_BITS else LIT_ENABLED, "position": [x, y], "expected": list(expected), "observed": list(observed), "error": error, LIT_PASSED: error <= MARKER_TOLERANCE})
    return {"extent": [width, height], "count": expected_count, LIT_ENABLED: enabled, "probes": probes, LIT_PASSED: all(probe[LIT_PASSED] for probe in probes)}


class InteractionRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=native_input.NativeInput(backend, handle))
        self.scale_x = 1.0
        self.scale_y = 1.0

    def on_after_display(self):
        self.native.wait_for_foreground(self.process, min(self.args.timeout, 15.0))

    def update_display(self):
        matches = list(DISPLAY_PATTERN.finditer(self.logs()))
        if not matches:
            raise SmokeFailure("the interactive fixture did not publish display metrics")
        logical_width, logical_height, self.scale_x, self.scale_y = map(float, matches[-1].groups())
        if not all(math.isfinite(value) and value > 0.0 for value in (logical_width, logical_height, self.scale_x, self.scale_y)):
            raise SmokeFailure("interactive fixture display metrics are invalid")

    def point(self, x, y):
        return round(x * self.scale_x), round(y * self.scale_y)

    def checkpoint(self, name, count, enabled, *, extent=None):
        time.sleep(CHECKPOINT_SETTLE_SECONDS)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + CHECKPOINT_STAGE_TIMEOUT_SECONDS)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during interactive UI capture")
            self.update_display()
            self.backend.capture_client_window(self.handle, path)
            frame = Bmp24Codec.read_bmp_24_rows(path)
            report = observe_markers(frame, self.scale_x, self.scale_y, count, enabled)
            if extent is not None and tuple(frame[:2]) != extent:
                report[LIT_PASSED] = False
            if report[LIT_PASSED]:
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(CHECKPOINT_POLL_SECONDS)
        raise SmokeFailure(f"interactive marker gate '{name}' failed: {report}")

    def write_report(self, passed):
        self.write_smoke_report("interaction.json", passed, include_skin=False,
            input_path="Win32 posted messages with native Shift modifier; posted activation loss/gain" if self.native.windows else "X11 XSendEvent and XSetInputFocus",
            runtime_limit="No physical pointer grab or Wayland input qualification")

    def execute(self):
        self.checkpoint("initial", 0, True)
        time.sleep(HIT_LAYOUT_SETTLE_SECONDS)
        self.native.button(True, *self.point(*BUTTON_POINT))
        self.checkpoint("pointer_press_has_no_submit", 0, True)
        self.native.tap(LIT_ESCAPE)
        self.native.button(False, *self.point(*BUTTON_POINT))
        self.checkpoint("escape_cancels_pointer_before_release", 0, True)
        self.native.click(*self.point(*BUTTON_POINT))
        self.checkpoint("first_click", 1, True)
        self.native.key(LIT_SPACE, True)
        self.native.key(LIT_SPACE, True, repeat=True)
        self.checkpoint("keyboard_press_and_repeat_have_no_submit", 1, True)
        self.native.tap(LIT_ESCAPE)
        self.native.key(LIT_SPACE, True, repeat=True)
        self.native.key(LIT_SPACE, False)
        self.checkpoint("escape_cancels_keyboard_before_release", 1, True)
        self.native.tap(LIT_TAB)
        self.native.key(LIT_SPACE, True)
        self.checkpoint("fresh_keyboard_press_waits_for_release", 1, True)
        self.native.key(LIT_SPACE, False)
        self.checkpoint("space_single_activation", 2, True)
        self.native.key(LIT_RETURN, True)
        self.native.key(LIT_RETURN, True, repeat=True)
        self.checkpoint("enter_hold_waits_for_release", 2, True)
        self.native.key(LIT_RETURN, False)
        self.checkpoint("enter_single_activation", 3, True)
        self.native.tap(LIT_TAB)
        self.native.tap(LIT_SPACE)
        self.checkpoint("keyboard_checkbox_disabled", 3, False)
        self.native.click(*self.point(*BUTTON_POINT))
        self.native.tap(LIT_SPACE)
        self.checkpoint("disabled_button_barrier", 3, False)
        self.native.tap(LIT_TAB)
        self.native.tap(LIT_TAB)
        self.native.tap(LIT_TAB, shift=True)
        self.native.tap(LIT_SPACE)
        self.checkpoint("tab_shift_tab_checkbox_enabled", 3, True)
        self.native.click(*self.point(*BUTTON_POINT))
        self.checkpoint("enabled_click", 4, True)
        self.native.button(True, *self.point(*BUTTON_POINT))
        time.sleep(native_input.INPUT_SETTLE_SECONDS)
        self.native.button(False, *self.point(400, 260))
        self.checkpoint("drag_out_cancels", 4, True)
        self.native.tap(LIT_SPACE)
        self.checkpoint("release_outside_cleanup", 5, True)
        self.native.button(True, *self.point(*BUTTON_POINT))
        time.sleep(native_input.INPUT_SETTLE_SECONDS)
        self.native.focus_loss_and_gain()
        self.native.button(False, *self.point(*BUTTON_POINT))
        self.checkpoint("focus_loss_cancels_hold", 5, True)
        self.backend.resize_client(self.handle, *RESIZED_CLIENT_EXTENT)
        self.checkpoint("resized", 5, True, extent=RESIZED_CLIENT_EXTENT)
        time.sleep(HIT_LAYOUT_SETTLE_SECONDS)
        self.native.click(*self.point(*BUTTON_POINT))
        self.checkpoint("resized_first_click", 6, True, extent=RESIZED_CLIENT_EXTENT)


def verify_interactive_actions(text, _driver):
    observed = [(action, int(count), int(enabled), int(sequence)) for action, count, enabled, sequence in ACTION_PATTERN.findall(text)]
    if observed != EXPECTED_ACTIONS:
        raise SmokeFailure(f"unexpected interactive action sequence: {observed}; expected {EXPECTED_ACTIONS}")


def run(args):
    def after_display(launch):
        if args.many_roots:
            wait_for_log_message(launch.log_directory, launch.log_baseline, launch.log_pattern, "UiLayerSmokeProject: root beyond target limit painted", min(args.timeout, DISPLAY_MARKER_TIMEOUT_SECONDS))
            wait_for_log_message(launch.log_directory, launch.log_baseline, launch.log_pattern, "UiLayerSmokeProject: root removed during paint", min(args.timeout, DISPLAY_MARKER_TIMEOUT_SECONDS))

    return run_ui_fixture(args, fixture="INTERACTIVE", display_marker="UiInteractiveSmoke: display", driver_type=InteractionRun, log_name="interaction.log", label="UI interaction",
        window_label="interactive window", process_label="interaction", missing_window_message="interactive UI window did not appear",
        environment_kwargs={"force_x11": True}, extra_env={"NWB_UI_LAYER_MANY_ROOTS": "1" if args.many_roots else "0"},
        after_display=after_display, after_collect=verify_interactive_actions,
        summary_formatter=lambda driver: f"UI interaction: {len(driver.stages)} displayed gates, {len(EXPECTED_ACTIONS)} exact actions; no replay")


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
