#!/usr/bin/env python3
"""Qualify one-host radio selection, keyed input lifetime and skin pixels."""
from __future__ import annotations

import json
import platform
import sys
import time

from radio_group_native import RadioGroupNativeInput
from radio_group_probe import observe_radio_group, snapshot_from_logs
from probe_common import center
from window_smoke import parse_args
from window_capture_smoke import (run_smoke_main, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, wait_for_log_message,)
from fixture_environment import build_fixture_environment
from checkpoint_report import CHECKPOINT_POLL_SECONDS, CHECKPOINT_SETTLE_SECONDS, CHECKPOINT_STAGE_TIMEOUT_SECONDS
from fixture_launch import DISPLAY_MARKER_TIMEOUT_SECONDS

LIT_MAIN_SELECTED = "main_selected"
LIT_MAIN_CURSOR = "main_cursor"
LIT_PARENT = "parent"
LIT_POPUP_CURSOR = "popup_cursor"
LIT_LABEL_READS = "label_reads"
LIT_CHOICE_COUNT = "choice_count"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_PASSED = "passed"
LIT_STAGE = "stage"
LIT_CAPTURE = "capture"
LIT_NATIVE_WINDOW = "native_window"
LIT_UTF_8 = "utf-8"
LIT_TAB = "Tab"
LIT_RIGHT = "Right"
LIT_DOWN = "Down"
LIT_UP = "Up"
LIT_RETURN = "Return"
LIT_ROW20 = "row20"
LIT_ROW40 = "row40"
LIT_TOGGLE_ENABLED = "toggle_enabled"
LIT_ROW50 = "row50"
LIT_OPEN = "open"
LIT_UI_RADIO_FIXTURE = "UI radio fixture"

FIXTURE_TIMEOUT_SECONDS = 180
RESIZE_SETTLE_SECONDS = 0.3


class RadioGroupRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = RadioGroupNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None
        self.expected = { LIT_MAIN_SELECTED: 10, LIT_MAIN_CURSOR: 10, "focus_code": 0, "changes": 0, "activations": 0, "before_clicks": 0, "after_clicks": 0, "enabled": 1, "source_revision": 1, "source_generation": 1701,
            "reversed": 0, "removed": 0, LIT_PARENT: 0, "popup_selected": 10, LIT_POPUP_CURSOR: 10, "popup_changes": 0, "popup_activations": 0, "popup_count": 0, "focus_scope": 0, LIT_LABEL_READS: 10, "disabled_selected": 30, LIT_CHOICE_COUNT: 5, }

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def rectangle_point(self, name):
        rectangle = self.snapshot["rectangles"][name]
        if rectangle[2] <= 0.0 or rectangle[3] <= 0.0:
            raise SmokeFailure(f"radio fixture has no accepted placement for '{name}'")
        return self.point(*center(rectangle))

    def click(self, name):
        self.native.click(*self.rectangle_point(name))

    def increments(self, **counts):
        return {name: self.expected[name] + delta for name, delta in counts.items()}

    def navigate(self, key, selected, name):
        changed = int(self.expected[LIT_MAIN_SELECTED] != selected)
        self.native.tap(key)
        self.checkpoint(name, main_selected=selected, main_cursor=selected, focus_code=2, **self.increments(changes=changed))

    def checkpoint(self, name, *, extent=None, extra=None, neutral=True, pressed=None, **changes):
        self.expected.update(changes)
        self.expected[LIT_LABEL_READS] = self.expected[LIT_CHOICE_COUNT] * (2 + self.expected[LIT_PARENT])
        if neutral:
            self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(CHECKPOINT_SETTLE_SECONDS)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + CHECKPOINT_STAGE_TIMEOUT_SECONDS)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during radio group UI capture")
            self.native.maintain_pointer()
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(CHECKPOINT_POLL_SECONDS)
                continue
            if snapshot[LIT_LOGICAL_EXTENT][0] < 700.0 or snapshot[LIT_LOGICAL_EXTENT][1] < 400.0:
                raise SmokeSkip("the radio fixture needs a logical client of at least 700x400")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_radio_group(read_bmp_24_rows(path), snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra, pressed=pressed)
            if report[LIT_PASSED]:
                self.snapshot = snapshot
                report.update({LIT_STAGE: name, LIT_CAPTURE: str(path), LIT_NATIVE_WINDOW: self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(CHECKPOINT_POLL_SECONDS)
        self.failure_report = {LIT_STAGE: name, LIT_CAPTURE: str(path), LIT_NATIVE_WINDOW: self.native.observe_window(), "observation": report}
        self.write_report(False)
        raise SmokeFailure(f"radio displayed-state gate '{name}' failed: {self.failure_report}")

    def write_report(self, passed):
        report = { LIT_PASSED: passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages, "failure": self.failure_report, "input_path": "Win32 physical cursor positioning with posted pointer/key messages"
                if self.native.windows else "X11 XSendEvent keys/button1 and XSetInputFocus", "runtime_limit": "No native Wayland, physical pointer-grab, or live IME qualification.", }
        (self.args.output_directory / "radio_group.json").write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)

    def execute(self):
        self.checkpoint("initial_checked_disabled_and_atlas_state")
        self.click("before")
        self.native.tap(LIT_TAB)
        self.checkpoint("tab_enters_one_radio_host", before_clicks=1, focus_code=2)
        self.native.tap(LIT_TAB)
        self.checkpoint("next_tab_skips_owned_choices", focus_code=3)
        self.native.tap(LIT_TAB, shift=True)
        self.navigate(LIT_RIGHT, 20, "shift_tab_and_right_select_second")
        self.navigate(LIT_DOWN, 40, "down_skips_disabled_choice")
        self.navigate("Left", 20, "left_selects_previous_enabled_choice")
        self.navigate(LIT_UP, 10, "up_selects_first_choice")
        self.navigate(LIT_UP, 50, "up_wraps_to_last_enabled_choice")
        self.navigate(LIT_RIGHT, 10, "right_wraps_to_first_enabled_choice")
        self.navigate("End", 50, "end_selects_last_enabled_choice")
        self.navigate("Home", 10, "home_selects_first_enabled_choice")
        self.native.tap(LIT_RETURN, repeat_count=3)
        self.checkpoint("held_enter_activates_once_without_selection_change", **self.increments(activations=1))
        self.native.tap("space", repeat_count=3)
        self.checkpoint("held_space_activates_once_without_selection_change", **self.increments(activations=1))
        self.click("row30")
        self.native.drag(self.rectangle_point(LIT_ROW20), self.point(4, 4))
        self.checkpoint("disabled_choice_and_drag_out_cannot_activate", focus_code=2)
        pressed = self.rectangle_point(LIT_ROW40)
        self.native.button(True, *pressed)
        self.checkpoint("pointer_press_keeps_selection_until_release", neutral=False, pressed=("", 40))
        self.native.button(False, *pressed)
        self.checkpoint("pointer_release_selects_and_activates_once", main_selected=40, main_cursor=40, **self.increments(changes=1, activations=1))
        self.native.command("select_disabled")
        self.checkpoint("external_disabled_selection_keeps_check_and_repairs_cursor", main_selected=30, main_cursor=40)
        self.native.command(LIT_TOGGLE_ENABLED)
        self.native.tap(LIT_RETURN)
        self.click(LIT_ROW50)
        self.checkpoint("disabled_group_blocks_keyboard_and_pointer", enabled=0, focus_code=0)
        self.native.command(LIT_TOGGLE_ENABLED)
        self.click(LIT_ROW20)
        self.checkpoint("fresh_pointer_sequence_after_enable", enabled=1, focus_code=2, main_selected=20, main_cursor=20, **self.increments(changes=1, activations=1))
        old_row = self.rectangle_point(LIT_ROW40)
        self.native.button(True, *old_row)
        self.native.command("reverse")
        self.native.button(False, *old_row)
        self.checkpoint("source_reorder_preserves_key_and_retires_old_release", reversed=1, source_revision=2)
        self.native.command("remove_selected")
        self.checkpoint("removal_clears_selection_and_repairs_cursor", removed=20, source_revision=3, main_selected=0, main_cursor=50, choice_count=4, **self.increments(changes=1))
        self.click(LIT_ROW50)
        self.checkpoint("fresh_keyed_release_after_removal", main_selected=50, main_cursor=50, **self.increments(changes=1, activations=1))
        self.native.key(LIT_RETURN, True)
        self.checkpoint("radio_submit_press_waits_for_release", pressed=("", self.expected[LIT_MAIN_CURSOR]))
        self.native.command("replace_source")
        self.native.key(LIT_RETURN, True, repeat=True)
        self.native.key(LIT_RETURN, False)
        self.checkpoint("replacement_resets_selection_and_retires_held_submit", source_generation=1702, source_revision=4, reversed=0, removed=0, choice_count=5, main_selected=0, main_cursor=10, disabled_selected=0, **self.increments(changes=1))
        physical_extent = self.point(840, 640)
        self.backend.resize_client(self.handle, *physical_extent)
        self.native.pointer(4, 4)
        time.sleep(RESIZE_SETTLE_SECONDS)
        self.click(LIT_ROW40)
        self.checkpoint("resized_display_accepts_fresh_owned_release", extent=physical_extent, extra=lambda snapshot: abs(snapshot[LIT_LOGICAL_EXTENT][0] - 840.0) < 1.0 and abs(snapshot[LIT_LOGICAL_EXTENT][1] - 640.0) < 1.0,
            main_selected=40, main_cursor=40, **self.increments(changes=1, activations=1))
        self.click(LIT_OPEN)
        self.checkpoint("parent_popup_focuses_its_single_radio_host", parent=1, popup_count=1, focus_code=5, focus_scope=1)
        self.native.tap(LIT_DOWN)
        self.native.tap(LIT_RETURN, repeat_count=3)
        self.checkpoint("popup_navigation_selects_and_held_enter_activates_once", popup_selected=20, popup_cursor=20, popup_changes=1, popup_activations=1)
        self.native.tap(LIT_TAB)
        self.checkpoint("popup_next_tab_skips_all_owned_choices", focus_code=6)
        self.native.tap(LIT_TAB, shift=True)
        old_popup_row = self.rectangle_point("popup_row40")
        self.native.button(True, *old_popup_row)
        self.native.command("close_parent")
        self.native.button(False, *old_popup_row)
        self.checkpoint("ancestor_close_retires_popup_capture_before_release", parent=0, popup_count=0, focus_code=4, focus_scope=0)
        self.click(LIT_OPEN)
        self.checkpoint("reopened_popup_publishes_focus_before_held_submit", parent=1, popup_count=1, focus_code=5, focus_scope=1)
        self.native.key(LIT_RETURN, True)
        self.checkpoint("popup_radio_submit_press_waits_for_release", pressed=("popup_", self.expected[LIT_POPUP_CURSOR]))
        self.native.focus_loss_and_gain()
        self.native.key(LIT_RETURN, True, repeat=True)
        self.native.key(LIT_RETURN, False)
        self.checkpoint("native_focus_loss_closes_reopened_popup_and_retires_key", parent=0, popup_count=0, focus_code=0, focus_scope=0)
        self.click(LIT_OPEN)
        self.checkpoint("reopened_popup_publishes_geometry_before_fresh_release", parent=1, popup_count=1, focus_code=5, focus_scope=1)
        self.click("popup_row50")
        self.checkpoint("fresh_popup_release_after_focus_loss", parent=1, popup_count=1, focus_code=5, focus_scope=1, popup_selected=50, popup_cursor=50, popup_changes=2, popup_activations=2)
        self.click("popup_close")
        self.checkpoint("popup_close_button_restores_root_focus", parent=0, popup_count=0, focus_code=4, focus_scope=0)


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), "RADIO_GROUP", skin=args.skin, force_x11=True)
    backend = create_capture_backend()
    logserver = application = radio_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the radio fixture appeared")
            raise SmokeFailure("custom radio fixture did not appear")
        if platform.system() == "Windows":
            backend.prepare_raw_client_window(handle)
        else:
            backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiRadioGroupSmoke: display", min(args.timeout, DISPLAY_MARKER_TIMEOUT_SECONDS))
        radio_run = RadioGroupRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        radio_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, LIT_UI_RADIO_FIXTURE, handle)
                require_normal_process_exit(exit_code, tail, LIT_UI_RADIO_FIXTURE)
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "radio_group.log").write_text(text, encoding=LIT_UTF_8)
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI radio logserver")
                finally:
                    try:
                        if radio_run is not None:
                            radio_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    radio_run.write_report(True)
    print(f"UI radio group: {len(radio_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0


def main(argv):
    return run_smoke_main(argv, parse_args, run, default_timeout=FIXTURE_TIMEOUT_SECONDS)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
