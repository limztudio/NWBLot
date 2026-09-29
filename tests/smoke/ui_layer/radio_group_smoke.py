#!/usr/bin/env python3
"""Qualify one-host radio selection, keyed input lifetime and skin pixels."""
from __future__ import annotations

import json
import platform
import sys
import time

from radio_group_native import RadioGroupNativeInput
from radio_group_probe import center, observe_radio_group, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


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
        self.expected = {
            "main_selected": 10, "main_cursor": 10, "focus_code": 0, "changes": 0, "activations": 0,
            "before_clicks": 0, "after_clicks": 0, "enabled": 1, "source_revision": 1, "source_generation": 1701,
            "reversed": 0, "removed": 0, "parent": 0, "popup_selected": 10, "popup_cursor": 10,
            "popup_changes": 0, "popup_activations": 0, "popup_count": 0, "focus_scope": 0, "label_reads": 10,
            "disabled_selected": 30, "choice_count": 5,
        }

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
        changed = int(self.expected["main_selected"] != selected)
        self.native.tap(key)
        self.checkpoint(name, main_selected=selected, main_cursor=selected, focus_code=2,
            **self.increments(changes=changed))

    def checkpoint(self, name, *, extent=None, extra=None, neutral=True, pressed=None, **changes):
        self.expected.update(changes)
        self.expected["label_reads"] = self.expected["choice_count"] * (2 + self.expected["parent"])
        if neutral:
            self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during radio group UI capture")
            self.native.maintain_pointer()
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 700.0 or snapshot["logical_extent"][1] < 400.0:
                raise SmokeSkip("the radio fixture needs a logical client of at least 700x400")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_radio_group(read_bmp_24_rows(path), snapshot, self.expected,
                extent=extent, skin=self.args.skin, extra=extra, pressed=pressed)
            if report["passed"]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path), "native_window": self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        self.failure_report = {"stage": name, "capture": str(path), "native_window": self.native.observe_window(),
            "observation": report}
        self.write_report(False)
        raise SmokeFailure(f"radio displayed-state gate '{name}' failed: {self.failure_report}")

    def write_report(self, passed):
        report = {
            "passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "failure": self.failure_report,
            "input_path": "Win32 physical cursor positioning with posted pointer/key messages"
                if self.native.windows else "X11 XSendEvent keys/button1 and XSetInputFocus",
            "runtime_limit": "No native Wayland, physical pointer-grab, or live IME qualification.",
        }
        (self.args.output_directory / "radio_group.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial_checked_disabled_and_atlas_state")
        self.click("before")
        self.native.tap("Tab")
        self.checkpoint("tab_enters_one_radio_host", before_clicks=1, focus_code=2)
        self.native.tap("Tab")
        self.checkpoint("next_tab_skips_owned_choices", focus_code=3)
        self.native.tap("Tab", shift=True)
        self.navigate("Right", 20, "shift_tab_and_right_select_second")
        self.navigate("Down", 40, "down_skips_disabled_choice")
        self.navigate("Left", 20, "left_selects_previous_enabled_choice")
        self.navigate("Up", 10, "up_selects_first_choice")
        self.navigate("Up", 50, "up_wraps_to_last_enabled_choice")
        self.navigate("Right", 10, "right_wraps_to_first_enabled_choice")
        self.navigate("End", 50, "end_selects_last_enabled_choice")
        self.navigate("Home", 10, "home_selects_first_enabled_choice")
        self.native.tap("Return", repeat_count=3)
        self.checkpoint("held_enter_activates_once_without_selection_change", **self.increments(activations=1))
        self.native.tap("space", repeat_count=3)
        self.checkpoint("held_space_activates_once_without_selection_change", **self.increments(activations=1))
        self.click("row30")
        self.native.drag(self.rectangle_point("row20"), self.point(4, 4))
        self.checkpoint("disabled_choice_and_drag_out_cannot_activate", focus_code=2)
        pressed = self.rectangle_point("row40")
        self.native.button(True, *pressed)
        self.checkpoint("pointer_press_keeps_selection_until_release", neutral=False, pressed=("", 40))
        self.native.button(False, *pressed)
        self.checkpoint("pointer_release_selects_and_activates_once", main_selected=40, main_cursor=40,
            **self.increments(changes=1, activations=1))
        self.native.command("select_disabled")
        self.checkpoint("external_disabled_selection_keeps_check_and_repairs_cursor", main_selected=30, main_cursor=40)
        self.native.command("toggle_enabled")
        self.native.tap("Return")
        self.click("row50")
        self.checkpoint("disabled_group_blocks_keyboard_and_pointer", enabled=0, focus_code=0)
        self.native.command("toggle_enabled")
        self.click("row20")
        self.checkpoint("fresh_pointer_sequence_after_enable", enabled=1, focus_code=2, main_selected=20, main_cursor=20,
            **self.increments(changes=1, activations=1))
        old_row = self.rectangle_point("row40")
        self.native.button(True, *old_row)
        self.native.command("reverse")
        self.native.button(False, *old_row)
        self.checkpoint("source_reorder_preserves_key_and_retires_old_release", reversed=1, source_revision=2)
        self.native.command("remove_selected")
        self.checkpoint("removal_clears_selection_and_repairs_cursor", removed=20, source_revision=3,
            main_selected=0, main_cursor=50, choice_count=4, **self.increments(changes=1))
        self.click("row50")
        self.checkpoint("fresh_keyed_release_after_removal", main_selected=50, main_cursor=50,
            **self.increments(changes=1, activations=1))
        self.native.key("Return", True)
        time.sleep(0.15)
        self.native.command("replace_source")
        self.native.key("Return", True, repeat=True)
        self.native.key("Return", False)
        self.checkpoint("replacement_resets_selection_and_retires_held_submit", source_generation=1702, source_revision=4,
            reversed=0, removed=0, choice_count=5, main_selected=0, main_cursor=10, disabled_selected=0,
            **self.increments(changes=1, activations=1))
        physical_extent = self.point(840, 640)
        self.backend.resize_client(self.handle, *physical_extent)
        self.native.pointer(4, 4)
        time.sleep(0.3)
        self.click("row40")
        self.checkpoint("resized_display_accepts_fresh_owned_release", extent=physical_extent,
            extra=lambda snapshot: abs(snapshot["logical_extent"][0] - 840.0) < 1.0
                and abs(snapshot["logical_extent"][1] - 640.0) < 1.0,
            main_selected=40, main_cursor=40, **self.increments(changes=1, activations=1))
        self.click("open")
        self.checkpoint("parent_popup_focuses_its_single_radio_host", parent=1, popup_count=1, focus_code=5, focus_scope=1)
        self.native.tap("Down")
        self.native.tap("Return", repeat_count=3)
        self.checkpoint("popup_navigation_selects_and_held_enter_activates_once", popup_selected=20, popup_cursor=20,
            popup_changes=1, popup_activations=1)
        self.native.tap("Tab")
        self.checkpoint("popup_next_tab_skips_all_owned_choices", focus_code=6)
        self.native.tap("Tab", shift=True)
        old_popup_row = self.rectangle_point("popup_row40")
        self.native.button(True, *old_popup_row)
        self.native.command("close_parent")
        self.native.button(False, *old_popup_row)
        self.checkpoint("ancestor_close_retires_popup_capture_before_release", parent=0, popup_count=0,
            focus_code=4, focus_scope=0)
        self.click("open")
        self.checkpoint("reopened_popup_publishes_focus_before_held_submit", parent=1, popup_count=1,
            focus_code=5, focus_scope=1)
        self.native.key("Return", True)
        time.sleep(0.15)
        self.native.focus_loss_and_gain()
        self.native.key("Return", True, repeat=True)
        self.native.key("Return", False)
        self.checkpoint("native_focus_loss_closes_reopened_popup_and_retires_key", parent=0, popup_count=0,
            focus_code=0, focus_scope=0, popup_activations=2)
        self.click("open")
        self.checkpoint("reopened_popup_publishes_geometry_before_fresh_release", parent=1, popup_count=1,
            focus_code=5, focus_scope=1)
        self.click("popup_row50")
        self.checkpoint("fresh_popup_release_after_focus_loss", parent=1, popup_count=1, focus_code=5, focus_scope=1,
            popup_selected=50, popup_cursor=50, popup_changes=2, popup_activations=3)
        self.click("popup_close")
        self.checkpoint("popup_close_button_restores_root_focus", parent=0, popup_count=0, focus_code=4, focus_scope=0)


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    for fixture in ("WINDOW", "POPUP", "POPUP_TOOLS", "NESTED_POPUP", "LIST", "COMBO", "SEARCH_COMBO",
            "NUMERIC_EDIT", "TEXT_AREA"):
        environment[f"NWB_UI_LAYER_{fixture}"] = "0"
        environment[f"NWB_UI_LAYER_{fixture}_SKIN"] = "0"
    environment.update({
        "NWB_UI_LAYER_RADIO_GROUP": "1",
        "NWB_UI_LAYER_RADIO_GROUP_SKIN": "1" if args.skin == "alternate" else "0",
        "NWB_UI_LAYER_EDIT": "0", "NWB_UI_LAYER_INTERACTIVE": "0",
    })
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
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
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiRadioGroupSmoke: display", min(args.timeout, 15.0))
        radio_run = RadioGroupRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        radio_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI radio fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI radio fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "radio_group.log").write_text(text, encoding="utf-8")
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
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"],
        STRICT_LOG_FAILURE_MESSAGES)
    radio_run.write_report(True)
    print(f"UI radio group: {len(radio_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0


def main(argv):
    if not any(argument == "--timeout" or argument.startswith("--timeout=") for argument in argv):
        argv = [*argv, "--timeout", "180"]
    args = parse_args(argv)
    try:
        return run(args)
    except SmokeSkip as error:
        print(f"SKIP: {error}", flush=True)
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, ValueError, TimeoutError) as error:
        print(f"FAIL: {error}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
