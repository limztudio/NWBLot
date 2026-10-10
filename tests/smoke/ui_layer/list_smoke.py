#!/usr/bin/env python3
"""Exercise a 100000-row virtual list through native input and accepted GPU presentation."""
from __future__ import annotations

import json
import platform
import sys
import time

from list_native import ListNativeInput
from list_probe import center, observe_list, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)
from fixture_environment import build_fixture_environment


class ListRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = ListNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.expected = {"selected": 0, "cursor": 0, "count": 100000, "focused": 0, "reversed": 0,
            "removed": 0, "visible": 1, "commits": 0, "underlying": 0}

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
            ensure_process_running(self.process, "during virtual-list UI capture")
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 600.0 or snapshot["logical_extent"][1] < 400.0:
                raise SmokeSkip("the list fixture needs a logical client of at least 600x400")
            self.backend.capture_client_window(self.handle, path)
            report = observe_list(read_bmp_24_rows(path), snapshot, self.expected,
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
        raise SmokeFailure(f"list displayed-state gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "input_path": "Win32 posted keys/pointer/positioned WM_MOUSEWHEEL messages"
                if self.native.windows else "X11 XSendEvent keys/button4/button5 and XSetInputFocus",
            "runtime_limit": "No live IME, native Wayland, physical pointer-grab, or native Linux qualification."}
        (self.args.output_directory / "list.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial_bounded_rows", first=0)
        self.expected.pop("first")
        self.native.click(*self.row(1))
        self.checkpoint("pointer_selects_keyed_row", selected=2, cursor=2, focused=1, commits=1)
        self.native.tap("Down")
        self.checkpoint("down_selects_next_key", selected=3, cursor=3)
        self.native.tap("Down")
        self.checkpoint("down_reaches_before_disabled", selected=4, cursor=4)
        self.native.tap("Down")
        self.checkpoint("down_skips_disabled_key5", selected=6, cursor=6)
        self.native.tap("Up")
        self.checkpoint("up_skips_disabled_key5", selected=4, cursor=4)
        self.native.tap("Page_Down")
        self.checkpoint("page_down_ensures_selected_visible", dynamic=("selected", "cursor"),
            extra=lambda s: s["selected"] == s["cursor"] and s["selected"] > 4 and s["rectangles"]["selected_row"][2] > 0.0)
        self.native.tap("Home")
        self.checkpoint("home_selects_first", selected=1, cursor=1, first=0)
        self.expected.pop("first")
        self.native.tap("End")
        self.checkpoint("end_reaches100000_without_bulk_labels", selected=100000, cursor=100000,
            extra=lambda s: s["past"] == 100000 and s["first"] > 99980)
        self.native.tap("Return")
        self.checkpoint("enter_activates_current_key", commits=2)
        self.native.tap("Page_Up")
        self.checkpoint("page_up_ensures_selected_visible", dynamic=("selected", "cursor"),
            extra=lambda s: s["selected"] == s["cursor"] and 99980 < s["selected"] < 100000
                and s["rectangles"]["selected_row"][2] > 0.0)
        old_first = self.snapshot["first"]
        self.native.wheel(2, *self.point(*center(self.snapshot["rectangles"]["viewport"])))
        self.checkpoint("wheel_scrolls_preserving_selected_key", extra=lambda s: s["first"] < old_first)
        self.native.tap("Home")
        self.checkpoint("home_after_wheel_ensures_cursor", selected=1, cursor=1, first=0)
        self.expected.pop("first")
        self.native.wheel(-2, *self.point(*center(self.snapshot["rectangles"]["viewport"])))
        self.checkpoint("wheel_down_virtualizes_next_rows", extra=lambda s: s["first"] >= 5)
        thumb = self.snapshot["rectangles"]["thumb"]
        track = self.snapshot["rectangles"]["track"]
        self.native.drag(self.point(*center(thumb)), self.point(track[0] + track[2] / 2.0, track[1] + track[3] - thumb[3] / 2.0))
        self.checkpoint("thumb_drag_reaches_bottom_without_changing_selection", extra=lambda s: s["past"] == 100000)
        thumb = self.snapshot["rectangles"]["thumb"]
        track = self.snapshot["rectangles"]["track"]
        self.native.drag(self.point(*center(thumb)), self.point(track[0] + track[2] / 2.0, track[1] + thumb[3] / 2.0))
        self.checkpoint("thumb_drag_reaches_top", first=0)
        self.expected.pop("first")
        self.native.click(*self.row(3))
        self.checkpoint("pointer_reselects_known_key", selected=4, cursor=4, commits=3)
        self.native.tap("F5")
        self.checkpoint("reorder_keeps_selected_key_and_scrolls_to_new_index", reversed=1,
            extra=lambda s: s["first"] > 99980 and s["rectangles"]["selected_row"][2] > 0.0)
        self.native.tap("F6")
        self.checkpoint("remove_selected_key_clears_selection", selected=0, cursor=0, count=99999, removed=4)
        self.native.tap("Home")
        self.checkpoint("reversed_home_selects_largest_key", selected=100000, cursor=100000, first=0)
        self.expected.pop("first")
        self.native.button(True, *self.row(2))
        self.native.tap("F7")
        self.checkpoint("omission_retires_held_pointer", visible=0, focused=0)
        self.native.button(False, *self.row(2))
        self.checkpoint("omitted_pointer_release_cannot_activate")
        self.native.tap("F7")
        self.checkpoint("restore_requires_new_focus", visible=1)
        self.native.click(*self.row(0))
        self.checkpoint("restored_list_accepts_new_pointer_sequence", focused=1, commits=4)
        self.native.key("Down", True)
        self.checkpoint("held_navigation_selects_once", selected=99999, cursor=99999)
        self.native.tap("F7")
        self.checkpoint("omission_retires_held_key", visible=0, focused=0)
        self.native.key("Down", True, repeat=True)
        self.native.key("Down", False)
        self.checkpoint("omitted_key_repeat_and_release_cannot_navigate")
        self.native.tap("F7")
        self.checkpoint("restore_after_held_key", visible=1)
        self.native.click(*self.row(1))
        self.checkpoint("restored_keyboard_owner", focused=1, commits=5)
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_retires_list_focus", focused=0)
        self.native.tap("Down")
        self.checkpoint("unfocused_navigation_cannot_change_model")
        self.click("counter")
        self.checkpoint("outside_counter_is_independent", underlying=1)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_keyed_model", extent=(800, 600))
        self.native.click(*self.row(0))
        self.checkpoint("first_pointer_after_resize_uses_accepted_geometry", selected=100000, cursor=100000,
            focused=1, commits=6, extent=(800, 600))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), "LIST", skin=args.skin, force_x11=True)
    backend = create_capture_backend()
    logserver = application = list_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the list fixture appeared")
            raise SmokeFailure("custom virtual-list fixture did not appear")
        backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiListSmoke: display", min(args.timeout, 15.0))
        list_run = ListRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        list_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI virtual-list fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI virtual-list fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "list.log").write_text(text, encoding="utf-8")
            finally:
                if logserver is not None:
                    terminate_process(logserver, "UI virtual-list logserver")
                backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    list_run.write_report(True)
    print(f"UI virtual list: {len(list_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
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
