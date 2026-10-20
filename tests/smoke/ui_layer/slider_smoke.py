#!/usr/bin/env python3
"""Qualify continuous sliders through native input and post-end displayed snapshots."""
from __future__ import annotations

import json
import platform
import sys
import time

from slider_native import SliderNativeInput
from slider_probe import bits_value, f32, observe_slider, snapshot_from_logs, value_bits
from probe_common import center
from window_smoke import parse_args
from window_capture_smoke import (run_smoke_main, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, wait_for_log_message,)
from fixture_environment import build_fixture_environment
from checkpoint_report import accept_checkpoint, fail_checkpoint

LIT_FOCUS_CODE = "focus_code"
LIT_CHANGES = "changes"
LIT_POPUP_CHANGES = "popup_changes"
LIT_RANGE_CODE = "range_code"
LIT_MAIN_BITS = "main_bits"
LIT_POPUP_BITS = "popup_bits"
LIT_SCALE = "scale"
LIT_RECTANGLES = "rectangles"
LIT_MAIN = "main"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_PASSED = "passed"
LIT_UTF_8 = "utf-8"
LIT_TAB = "Tab"
LIT_RIGHT = "Right"
LIT_END = "End"
LIT_HOME = "Home"
LIT_PRESSED = "pressed"
LIT_MAIN_CENTER = "main_center"
LIT_FENCE_VALUE = "fence_value"
LIT_TOGGLE_ENABLED = "toggle_enabled"
LIT_OPEN = "open"
LIT_POPUP = "popup"
LIT_UI_SLIDER_FIXTURE = "UI slider fixture"


class SliderRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = SliderNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None
        self.expected = { LIT_FOCUS_CODE: 0, LIT_CHANGES: 0, LIT_POPUP_CHANGES: 0, "before_clicks": 0, "after_clicks": 0, "enabled": 1, LIT_RANGE_CODE: 0, "step_code": 0, "parent": 0, "popup_count": 0, "focus_scope": 0,
            "main_valid": 1, "main_dragging": 0, "popup_valid": 0, "popup_dragging": 0, "external_intents": 0, LIT_MAIN_BITS: value_bits(0.25), "disabled_bits": value_bits(0.75), "constant_bits": value_bits(0.5), LIT_POPUP_BITS: value_bits(0.5), }

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot[LIT_SCALE]
        return round(x * scale_x), round(y * scale_y)

    def rectangle_point(self, name):
        rectangle = self.snapshot[LIT_RECTANGLES][name]
        if rectangle[2] <= 0.0 or rectangle[3] <= 0.0:
            raise SmokeFailure(f"slider fixture has no accepted placement for '{name}'")
        return self.point(*center(rectangle))

    def click(self, name):
        self.native.click(*self.rectangle_point(name))

    def maximum(self, prefix):
        return 0.5 if prefix == LIT_MAIN and self.expected[LIT_RANGE_CODE] else 1.0

    def track_point(self, prefix, fraction):
        x, y, width, height = self.snapshot[LIT_RECTANGLES][f"{prefix}_center"]
        return self.point(x + fraction * width, y + height / 2.0)

    def thumb_point(self, prefix, fraction=0.75):
        x, y, width, height = self.snapshot[LIT_RECTANGLES][f"{prefix}_thumb"]
        return self.point(x + fraction * width, y + height / 2.0)

    def logical_x(self, position):
        return f32(position[0] / self.snapshot[LIT_SCALE][0])

    def seek_value(self, prefix, position):
        centers = self.snapshot[LIT_RECTANGLES][f"{prefix}_center"]
        fraction = (self.logical_x(position) - f32(centers[0])) / f32(centers[2])
        return self.maximum(prefix) * min(1.0, max(0.0, fraction))

    def drag_value(self, prefix, origin, position, baseline, rectangles):
        travel = f32(rectangles[f"{prefix}_travel"][2]) - f32(rectangles[f"{prefix}_thumb"][2])
        delta = self.logical_x(position) - self.logical_x(origin)
        normalized = min(1.0, max(0.0, baseline / self.maximum(prefix))) + delta / travel
        return self.maximum(prefix) * min(1.0, max(0.0, normalized))

    def value_changes(self, prefix, value):
        name = LIT_CHANGES if prefix == LIT_MAIN else LIT_POPUP_CHANGES
        bits_name = f"{prefix}_bits"
        bits = value_bits(value)
        return {bits_name: bits, name: self.expected[name] + int(self.expected[bits_name] != bits)}

    def navigate(self, key, value, name):
        self.native.tap(key)
        self.checkpoint(name, focus_code=2, **self.value_changes(LIT_MAIN, value))

    def checkpoint(self, name, *, extent=None, extra=None, neutral=True, modes=None, dynamic=(), **changes):
        self.native.record("checkpoint_begin", name=name)
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        if neutral:
            self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during slider UI capture")
            self.native.maintain_pointer()
            self.native.record("capture_before", name=name)
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot[LIT_LOGICAL_EXTENT][0] < 740.0 or snapshot[LIT_LOGICAL_EXTENT][1] < 480.0:
                raise SmokeSkip("the slider fixture needs a logical client of at least 740x480")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_slider(read_bmp_24_rows(path), snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra, modes=modes)
            self.native.record("capture_after", name=name, sequence=snapshot["sequence"], passed=report[LIT_PASSED])
            if report[LIT_PASSED]:
                accept_checkpoint(self, name, path, snapshot, report, dynamic)
                return
            time.sleep(0.1)
        fail_checkpoint(self, name, path, report, "slider")

    def write_report(self, passed):
        report = { LIT_PASSED: passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages, "failure": self.failure_report, "native_actions": self.native.actions,
            "input_path": "Win32 ClipCursor-isolated physical cursor positioning with posted pointer/key messages" if self.native.windows else "X11 XSendEvent keys/button1 and XSetInputFocus",
            "runtime_limit": "No native Wayland, physical pointer-grab, or live IME qualification.", }
        (self.args.output_directory / "slider.json").write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)

    def execute(self):
        self.checkpoint("initial_exact_values_and_atlas")
        self.native.pointer(*self.thumb_point(LIT_MAIN, 0.5))
        self.checkpoint("hover_uses_authored_thumb_region_and_tint", neutral=False, modes={LIT_MAIN: "hover"})
        self.click("before")
        self.native.tap(LIT_TAB)
        self.checkpoint("tab_enters_one_slider_host", before_clicks=1, focus_code=2)
        self.native.tap(LIT_TAB)
        self.checkpoint("next_tab_skips_track_and_thumb_parts", focus_code=3)
        self.native.tap(LIT_TAB, shift=True)
        self.navigate(LIT_RIGHT, 0.375, "right_adds_exact_key_step")
        self.navigate("Up", 0.5, "up_adds_exact_key_step")
        self.navigate("Down", 0.375, "down_subtracts_exact_key_step")
        self.navigate("Left", 0.25, "left_subtracts_exact_key_step")
        self.navigate("Page_Up", 1.0, "page_up_saturates_at_exact_maximum")
        self.navigate("Page_Down", 0.0, "page_down_saturates_at_exact_minimum")
        self.navigate(LIT_END, 1.0, "end_chooses_exact_maximum")
        prior_changes = self.expected[LIT_CHANGES]
        self.native.tap(LIT_HOME)
        self.native.tap(LIT_RIGHT, repeat_count=3)
        self.checkpoint("home_then_accepted_arrow_repeats", main_bits=value_bits(0.5), dynamic=(LIT_CHANGES,), extra=lambda snapshot: prior_changes + 1 <= snapshot[LIT_CHANGES] <= prior_changes + 5)
        seek = self.track_point(LIT_MAIN, 0.75)
        sought = self.seek_value(LIT_MAIN, seek)
        self.native.button(True, *seek)
        self.checkpoint("track_press_seeks_absolute_value_while_held", neutral=False, modes={LIT_MAIN: LIT_PRESSED}, main_dragging=1, **self.value_changes(LIT_MAIN, sought))
        self.native.button(False, *seek)
        origin = self.thumb_point(LIT_MAIN)
        baseline = bits_value(self.expected[LIT_MAIN_BITS])
        original_rectangles = dict(self.snapshot[LIT_RECTANGLES])
        self.native.button(True, *origin)
        self.checkpoint("offcenter_thumb_press_preserves_exact_value", neutral=False, modes={LIT_MAIN: LIT_PRESSED}, main_dragging=1)
        travel = self.snapshot[LIT_RECTANGLES][LIT_MAIN_CENTER][2]
        scale = self.snapshot[LIT_SCALE][0]
        moved = origin[0] + round(travel * 0.125 * scale), origin[1]
        self.native.pointer(*moved)
        value = self.drag_value(LIT_MAIN, origin, moved, baseline, original_rectangles)
        self.checkpoint("held_thumb_move_preserves_grab_offset", neutral=False, modes={LIT_MAIN: LIT_PRESSED}, **self.value_changes(LIT_MAIN, value))
        moved_again = origin[0] + round(travel * 0.375 * scale), origin[1]
        self.native.pointer(*moved_again)
        value = self.drag_value(LIT_MAIN, origin, moved_again, baseline, original_rectangles)
        self.checkpoint("later_move_uses_original_baseline_and_clamps_maximum", neutral=False, modes={LIT_MAIN: LIT_PRESSED}, **self.value_changes(LIT_MAIN, value))
        self.native.pointer(*self.point(4, 4))
        time.sleep(0.15)
        self.native.button(False, *self.point(4, 4))
        self.checkpoint("outside_capture_clamps_minimum_and_release_finishes", main_dragging=0, **self.value_changes(LIT_MAIN, 0.0))
        self.click("disabled_thumb")
        self.native.tap(LIT_END)
        self.click("constant_thumb")
        self.native.tap(LIT_RIGHT)
        self.checkpoint("disabled_and_constant_controls_cannot_change_or_focus", focus_code=0)
        self.click("main_thumb")
        self.native.key(LIT_RIGHT, True)
        self.checkpoint("held_key_first_step_is_presented", focus_code=2, **self.value_changes(LIT_MAIN, 0.125))
        self.native.command(LIT_FENCE_VALUE)
        self.native.key(LIT_RIGHT, True, repeat=True)
        self.native.key(LIT_RIGHT, False)
        self.checkpoint("same_value_external_setter_retires_held_key", external_intents=1)
        self.native.key(LIT_RIGHT, True)
        self.checkpoint("fresh_key_press_after_external_fence", **self.value_changes(LIT_MAIN, 0.25))
        self.native.command("toggle_step")
        self.native.key(LIT_RIGHT, True, repeat=True)
        self.native.key(LIT_RIGHT, False)
        self.checkpoint("changed_step_retires_old_repeat", step_code=1)
        prior_changes = self.expected[LIT_CHANGES]
        self.native.key(LIT_RIGHT, True)
        time.sleep(0.15)
        self.native.key(LIT_RIGHT, True, repeat=True)
        self.checkpoint("fresh_coarse_step_and_repeat_use_new_policy", main_bits=value_bits(0.75), dynamic=(LIT_CHANGES,), extra=lambda snapshot: prior_changes + 1 <= snapshot[LIT_CHANGES] <= prior_changes + 2)
        self.native.command("toggle_range")
        self.native.key(LIT_RIGHT, True, repeat=True)
        self.native.key(LIT_RIGHT, False)
        self.checkpoint("changed_range_retires_repeat_and_preserves_authoritative_value", range_code=1)
        old_thumb = self.thumb_point(LIT_MAIN)
        self.native.button(True, *old_thumb)
        self.native.command(LIT_TOGGLE_ENABLED)
        self.native.pointer(old_thumb[0] - 40, old_thumb[1])
        self.native.button(False, old_thumb[0] - 40, old_thumb[1])
        self.native.tap(LIT_HOME)
        self.click("main_track")
        self.checkpoint("disable_retires_capture_and_blocks_fresh_input", enabled=0, focus_code=0, main_dragging=0)
        self.native.command(LIT_TOGGLE_ENABLED)
        self.native.command("set_quarter")
        self.checkpoint("reenable_and_external_value_publish_fresh_state", enabled=1, external_intents=2, main_bits=value_bits(0.25))
        old_thumb = self.thumb_point(LIT_MAIN)
        self.native.button(True, *old_thumb)
        self.checkpoint("fresh_thumb_press_is_published_before_external_fence", neutral=False, modes={LIT_MAIN: LIT_PRESSED}, focus_code=2, main_dragging=1)
        self.native.command(LIT_FENCE_VALUE)
        self.native.pointer(old_thumb[0] - 40, old_thumb[1])
        self.native.button(False, old_thumb[0] - 40, old_thumb[1])
        self.checkpoint("same_value_external_setter_retires_thumb_capture", external_intents=3, main_dragging=0)
        old_thumb = self.thumb_point(LIT_MAIN)
        old_width = self.snapshot[LIT_RECTANGLES][LIT_MAIN_CENTER][2]
        old_extent = self.snapshot[LIT_LOGICAL_EXTENT]
        target_extent = round(old_extent[0]) + 96, round(old_extent[1]) + 64
        physical_extent = self.point(*target_extent)
        self.native.button(True, *old_thumb)
        self.backend.resize_client(self.handle, *physical_extent)
        time.sleep(0.3)
        self.native.pointer(old_thumb[0] + 40, old_thumb[1])
        self.native.button(False, old_thumb[0] + 40, old_thumb[1])
        self.checkpoint("resize_changes_admission_and_retires_old_drag", extent=physical_extent, main_dragging=0, dynamic=(LIT_FOCUS_CODE,), extra=lambda snapshot: snapshot[LIT_FOCUS_CODE] in (0, 2) and snapshot[LIT_RECTANGLES][LIT_MAIN_CENTER][2] != old_width
                and abs(snapshot[LIT_LOGICAL_EXTENT][0] - target_extent[0]) < 1.0 and abs(snapshot[LIT_LOGICAL_EXTENT][1] - target_extent[1]) < 1.0)
        fresh = self.track_point(LIT_MAIN, 0.75)
        value = self.seek_value(LIT_MAIN, fresh)
        self.native.click(*fresh)
        self.checkpoint("fresh_seek_uses_resized_geometry_and_current_range", focus_code=2, **self.value_changes(LIT_MAIN, value))
        old_thumb = self.thumb_point(LIT_MAIN)
        self.native.button(True, *old_thumb)
        self.native.focus_loss_and_gain()
        self.native.pointer(old_thumb[0] - 40, old_thumb[1])
        self.native.button(False, old_thumb[0] - 40, old_thumb[1])
        self.checkpoint("native_focus_loss_retires_drag_without_value_change", focus_code=0, main_dragging=0)
        self.click(LIT_OPEN)
        self.checkpoint("parent_popup_publishes_slider_focus_and_geometry", parent=1, popup_count=1, focus_code=5, focus_scope=1, popup_valid=1)
        popup_seek = self.track_point(LIT_POPUP, 0.75)
        value = self.seek_value(LIT_POPUP, popup_seek)
        self.native.button(True, *popup_seek)
        self.checkpoint("popup_track_seek_publishes_held_value", neutral=False, modes={LIT_POPUP: LIT_PRESSED}, popup_dragging=1, **self.value_changes(LIT_POPUP, value))
        self.native.command("close_parent")
        self.checkpoint("ancestor_close_retires_popup_drag_and_hidden_diagnostics", neutral=False, parent=0, popup_count=0, focus_code=4, focus_scope=0, popup_valid=0, popup_dragging=0)
        self.native.pointer(popup_seek[0] - 60, popup_seek[1])
        self.native.button(False, popup_seek[0] - 60, popup_seek[1])
        self.checkpoint("stale_popup_motion_and_release_preserve_closed_value")
        self.click(LIT_OPEN)
        self.checkpoint("reopened_popup_publishes_before_fresh_thumb_input", parent=1, popup_count=1, focus_code=5, focus_scope=1, popup_valid=1)
        origin = self.thumb_point(LIT_POPUP)
        baseline = bits_value(self.expected[LIT_POPUP_BITS])
        original_rectangles = dict(self.snapshot[LIT_RECTANGLES])
        delta = round(self.snapshot[LIT_RECTANGLES]["popup_center"][2] * -0.25 * self.snapshot[LIT_SCALE][0])
        destination = origin[0] + delta, origin[1]
        value = self.drag_value(LIT_POPUP, origin, destination, baseline, original_rectangles)
        self.native.drag(origin, destination)
        self.checkpoint("fresh_popup_thumb_drag_after_ancestor_reopen", **self.value_changes(LIT_POPUP, value))
        self.click("popup_close")
        self.checkpoint("popup_close_button_returns_root_focus", parent=0, popup_count=0, focus_code=4, focus_scope=0, popup_valid=0, popup_dragging=0)


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), "SLIDER", skin=args.skin, force_x11=True)
    backend = create_capture_backend()
    logserver = application = slider_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the slider fixture appeared")
            raise SmokeFailure("custom slider fixture did not appear")
        if platform.system() == "Windows":
            backend.prepare_raw_client_window(handle)
        else:
            backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiSliderSmoke: display", min(args.timeout, 15.0))
        slider_run = SliderRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        slider_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, LIT_UI_SLIDER_FIXTURE, handle)
                require_normal_process_exit(exit_code, tail, LIT_UI_SLIDER_FIXTURE)
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "slider.log").write_text(text, encoding=LIT_UTF_8)
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI slider logserver")
                finally:
                    try:
                        if slider_run is not None:
                            slider_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    slider_run.write_report(True)
    print(f"UI slider: {len(slider_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0


def main(argv):
    return run_smoke_main(argv, parse_args, run, default_timeout=180)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
