#!/usr/bin/env python3
"""Exercise keyed combo preview, commit, cancellation, and GPU popup composition."""
from __future__ import annotations

import json
import platform
import sys
import time

from combo_native import ComboNativeInput
from combo_probe_common import center
from combo_probe import COMBO_PROBE
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)
from fixture_environment import build_fixture_environment


class ComboRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = ComboNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.expected = {"selected": 0, "cursor": 0, "count": 100000, "focused": 0, "reversed": 0,
            "removed": 0, "visible": 1, "commits": 0, "underlying": 0, "open": 0, "enabled": 1,
            "bottom": 0, "source_generation": 1}

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def click(self, name):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][name])))

    def row(self, offset):
        x, y, width, height = self.snapshot["rectangles"]["viewport"]
        return self.point(x + min(width / 2.0, 120.0), y + (offset + 0.5) * 24.0)

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), **changes):
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        if self.snapshot is not None:
            self.native.pointer(*self.point(*center(self.snapshot["rectangles"]["counter"])))
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during combo UI capture")
            snapshot = COMBO_PROBE.snapshot(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 600.0 or snapshot["logical_extent"][1] < 400.0:
                raise SmokeSkip("the combo fixture needs a logical client of at least 600x400")
            self.backend.capture_client_window(self.handle, path)
            report = COMBO_PROBE.observe(read_bmp_24_rows(path), snapshot, self.expected,
                extent=extent, skin=self.args.skin, extra=extra)
            if report["passed"]:
                self.snapshot = snapshot
                for field in dynamic:
                    self.expected[field] = snapshot[field]
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        raise SmokeFailure(f"combo displayed-state gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "input_path": "Win32 posted keys/pointer/positioned WM_MOUSEWHEEL messages"
                if self.native.windows else "X11 XSendEvent keys/button4/button5 and XSetInputFocus",
            "runtime_limit": "No live IME, native Wayland, physical pointer-grab, or native Linux qualification."}
        (self.args.output_directory / "combo.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial_closed_combo", extra=lambda s: s["rectangles"]["trigger"][2] > 0.0)
        self.expected.pop("focused")
        self.click("trigger")
        self.checkpoint("pointer_opens_preview", open=1, dynamic=("cursor",),
            extra=lambda s: s["past"] - s["first"] <= 10)
        self.native.tap("Home")
        self.checkpoint("home_previews_first_without_commit", cursor=1)
        self.native.tap("Down")
        self.checkpoint("down_previews_second_without_commit", cursor=2)
        self.native.tap("Escape")
        self.checkpoint("escape_cancels_preview", open=0, dynamic=("cursor",))
        self.native.tap("space")
        self.checkpoint("space_opens_after_focus_return", open=1, dynamic=("cursor",))
        self.native.tap("Home")
        self.native.tap("Down")
        self.native.tap("Return")
        self.checkpoint("keyboard_commits_key2", selected=2, cursor=2, open=0, commits=1)
        self.native.tap("space")
        self.checkpoint("reopen_starts_from_committed_key", open=1)
        self.native.tap("End")
        self.checkpoint("end_previews100000_without_bulk_labels", cursor=100000,
            extra=lambda s: s["past"] == 100000 and s["first"] > 99980)
        self.native.tap("Escape")
        self.checkpoint("end_preview_cancel_preserves_committed_key", open=0, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("down_opens_and_advances_preview", open=1, cursor=3)
        self.native.tap("Down")
        self.checkpoint("down_reaches_before_disabled", cursor=4)
        self.native.tap("Down")
        self.checkpoint("down_skips_disabled_key5", cursor=6)
        self.native.tap("Up")
        self.checkpoint("up_skips_disabled_key5", cursor=4)
        self.native.tap("Return")
        self.checkpoint("keyboard_commits_key4", selected=4, cursor=4, open=0, commits=2)
        self.native.tap("F4")
        self.checkpoint("external_selection_updates_caption", selected=100000, dynamic=("cursor",))
        self.native.tap("space")
        self.checkpoint("reopen_ensures100000_visible", open=1, cursor=100000,
            extra=lambda s: s["past"] == 100000 and s["first"] > 99980)
        old_first = self.snapshot["first"]
        self.native.wheel(2, *self.point(*center(self.snapshot["rectangles"]["viewport"])))
        self.checkpoint("wheel_scrolls_without_committing", extra=lambda s: s["first"] < old_first)
        self.native.tap("Home")
        self.checkpoint("home_restores_preview_visibility", cursor=1, first=0)
        self.expected.pop("first")
        self.native.wheel(-2, *self.point(*center(self.snapshot["rectangles"]["viewport"])))
        self.checkpoint("wheel_down_virtualizes_preview_rows", extra=lambda s: s["first"] >= 5)
        thumb, track = self.snapshot["rectangles"]["thumb"], self.snapshot["rectangles"]["track"]
        self.native.drag(self.point(*center(thumb)), self.point(track[0] + track[2] / 2.0, track[1] + track[3] - thumb[3] / 2.0))
        self.checkpoint("thumb_drag_reaches_bottom_without_commit", extra=lambda s: s["past"] == 100000)
        thumb, track = self.snapshot["rectangles"]["thumb"], self.snapshot["rectangles"]["track"]
        self.native.drag(self.point(*center(thumb)), self.point(track[0] + track[2] / 2.0, track[1] + thumb[3] / 2.0))
        self.checkpoint("thumb_drag_reaches_top", first=0)
        self.expected.pop("first")
        self.native.click(*self.row(1))
        self.checkpoint("pointer_row_commits_and_closes", selected=2, cursor=2, open=0, commits=3)
        self.native.tap("F5")
        self.checkpoint("reorder_preserves_committed_key", reversed=1)
        self.native.tap("space")
        self.checkpoint("reordered_reopen_ensures_stable_key", open=1,
            extra=lambda s: s["first"] > 99980 and s["rectangles"]["cursor_row"][2] > 0.0)
        self.native.tap("Escape")
        self.checkpoint("reordered_cancel_keeps_selection", open=0, dynamic=("cursor",))
        self.native.tap("F6")
        self.checkpoint("removal_clears_committed_key", selected=0, count=99999, removed=2, dynamic=("cursor",))
        self.native.tap("space")
        self.checkpoint("reopen_after_source_removal", open=1, dynamic=("cursor",))
        self.native.tap("End")
        self.checkpoint("reversed_end_previews_smallest_key", cursor=1)
        self.native.tap("Return")
        self.checkpoint("commit_after_source_removal", selected=1, cursor=1, open=0, commits=4)
        self.native.tap("space")
        self.checkpoint("outside_dismissal_setup", open=1)
        self.click("counter")
        self.checkpoint("outside_press_dismisses_without_underlying_activation", open=0, dynamic=("cursor",))
        self.click("counter")
        self.checkpoint("next_outside_click_activates_counter", underlying=1)
        self.click("trigger")
        self.checkpoint("native_focus_loss_setup", open=1, dynamic=("cursor",))
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_cancels_preview", open=0, focused=0, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("unfocused_navigation_cannot_open")
        self.expected.pop("focused")
        self.native.tap("F9")
        self.click("trigger")
        self.checkpoint("disabled_trigger_blocks_pointer", enabled=0)
        self.native.tap("F9")
        self.checkpoint("enable_restores_trigger", enabled=1)
        self.native.tap("F8")
        self.checkpoint("empty_source_clears_selection", selected=0, count=0, dynamic=("cursor",))
        self.click("trigger")
        self.checkpoint("empty_enabled_combo_opens", open=1, cursor=0)
        self.native.tap("Return")
        self.checkpoint("empty_submit_cannot_commit")
        self.native.tap("Escape")
        self.checkpoint("empty_popup_cancels", open=0)
        self.native.tap("F8")
        self.checkpoint("restore_rows_after_empty", count=99999, dynamic=("cursor",))
        self.native.button(True, *self.point(*center(self.snapshot["rectangles"]["trigger"])))
        self.native.tap("F7")
        self.checkpoint("omission_retires_held_pointer", visible=0, focused=0)
        self.native.button(False, *self.point(*center(self.snapshot["rectangles"]["trigger"])))
        self.checkpoint("omitted_pointer_release_cannot_open")
        self.native.tap("F7")
        self.checkpoint("restore_after_pointer_omission", visible=1)
        self.expected.pop("focused")
        self.click("trigger")
        self.checkpoint("open_after_pointer_restore", open=1, dynamic=("cursor",))
        self.native.tap("Home")
        self.checkpoint("held_key_omission_setup", cursor=100000)
        self.native.key("Down", True)
        self.checkpoint("held_key_previews_once", cursor=99999)
        self.native.tap("F7")
        self.checkpoint("omission_retires_held_key", visible=0, open=0, focused=0, dynamic=("cursor",))
        self.native.key("Down", True, repeat=True)
        self.native.key("Down", False)
        self.checkpoint("omitted_key_repeat_and_release_cannot_navigate")
        self.native.tap("F7")
        self.checkpoint("restore_after_key_omission", visible=1, open=0, dynamic=("cursor",))
        self.expected.pop("focused")
        self.click("trigger")
        self.checkpoint("open_after_key_restore", open=1, dynamic=("cursor",))
        self.native.tap("Home")
        self.checkpoint("source_lifetime_fence_setup", cursor=100000)
        self.native.key("Down", True)
        self.checkpoint("source_lifetime_held_preview", cursor=99999)
        self.native.tap("F12")
        self.checkpoint("source_replacement_cancels_popup", open=0, source_generation=2, dynamic=("cursor",))
        self.native.key("Down", True, repeat=True)
        self.native.key("Down", False)
        self.checkpoint("stale_source_key_owner_cannot_reopen")
        self.native.tap("F10")
        self.checkpoint("anchor_moves_near_bottom", bottom=1)
        self.click("trigger")
        self.checkpoint("bottom_anchor_flips_popup_above", open=1, dynamic=("cursor",))
        self.native.tap("Escape")
        self.checkpoint("bottom_popup_cancels", open=0, dynamic=("cursor",))
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_committed_model", extent=(800, 600))
        self.click("trigger")
        self.checkpoint("first_open_after_resize_uses_accepted_geometry", open=1, dynamic=("cursor",), extent=(800, 600))
        self.native.tap("Home")
        self.checkpoint("home_after_resize_previews_first_key", cursor=100000, extent=(800, 600))
        self.native.click(*self.row(0))
        self.checkpoint("pointer_commit_after_resize", selected=100000, cursor=100000, open=0, commits=5, extent=(800, 600))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), "COMBO", skin=args.skin, force_x11=True)
    backend = create_capture_backend()
    logserver = application = combo_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the combo fixture appeared")
            raise SmokeFailure("custom combo fixture did not appear")
        backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiComboSmoke: display", min(args.timeout, 15.0))
        combo_run = ComboRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        combo_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI combo fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI combo fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "combo.log").write_text(text, encoding="utf-8")
            finally:
                if logserver is not None:
                    terminate_process(logserver, "UI combo logserver")
                backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    combo_run.write_report(True)
    print(f"UI combo: {len(combo_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0


def main(argv):
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
