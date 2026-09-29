#!/usr/bin/env python3
"""Exercise popup focus, dismissal, placement and modal composition through native input."""
from __future__ import annotations

import json
import platform
import sys
import time

from edit_native import EditNativeInput
from popup_probe import center, observe_popup, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


class PopupRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = EditNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.expected = {"open": 0, "modal": 0, "underlying": 0, "selected": 0, "checked": 0,
            "edit_focused": 0, "outside_focused": 0, "edge": 0, "right": 0, "visible": 1, "text_bytes": 5}

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def click(self, name):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][name])))

    def checkpoint(self, name, *, extent=None, **changes):
        self.expected.update(changes)
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during popup UI capture")
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 600.0 or snapshot["logical_extent"][1] < 390.0:
                raise SmokeSkip("the popup fixture needs a logical client of at least 600x390")
            self.backend.capture_client_window(self.handle, path)
            report = observe_popup(read_bmp_24_rows(path), snapshot, self.expected, extent=extent, skin=self.args.skin)
            if report["passed"]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        raise SmokeFailure(f"popup displayed-state gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "input_path": "Win32 posted keys/pointer/WM_CHAR messages with native Shift modifiers"
                if self.native.windows else "X11 XSendEvent ASCII keys and XSetInputFocus",
            "runtime_limit": "Synthetic text bypasses live IME. No native Wayland, physical pointer-grab, or native Linux qualification."}
        (self.args.output_directory / "popup.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial")
        self.click("outside")
        self.checkpoint("outside_focus", outside_focused=1)
        self.click("trigger")
        self.checkpoint("open_above_later_root", open=1, outside_focused=0)
        self.native.tap("Tab")
        self.native.tap("Tab")
        self.native.tap("space")
        self.checkpoint("popup_checkbox_by_keyboard", checked=1)
        self.native.tap("Tab")
        self.checkpoint("tab_reaches_popup_edit", edit_focused=1)
        self.native.text("z")
        self.checkpoint("popup_edit_native_commit", text_bytes=6)
        self.native.tap("Escape")
        self.checkpoint("escape_from_edit_closes", open=0, edit_focused=0)
        self.native.tap("Return")
        self.checkpoint("restored_trigger_reopens", open=1)
        self.native.tap("Tab", shift=True)
        self.checkpoint("shift_tab_wraps_to_last", edit_focused=1)
        self.native.tap("Tab")
        self.checkpoint("tab_wraps_to_first", edit_focused=0)
        self.native.tap("Return")
        self.checkpoint("keyboard_choice_closes", open=0, selected=1)
        self.native.tap("Return")
        self.checkpoint("choice_restores_trigger", open=1)
        outside = self.point(*center(self.snapshot["rectangles"]["counter"]))
        self.native.button(True, *outside)
        self.checkpoint("outside_press_dismisses", open=0)
        self.native.button(False, *outside)
        self.checkpoint("outside_release_is_consumed")
        self.click("counter")
        self.checkpoint("next_outside_sequence_activates", underlying=1)
        self.click("trigger")
        self.checkpoint("pointer_choice_open", open=1)
        self.click("second")
        self.checkpoint("pointer_choice_closes", open=0, selected=2)
        self.click("modal")
        self.checkpoint("modal_dims_later_root", open=1, modal=1)
        self.click("counter")
        self.checkpoint("modal_blocks_underlying")
        self.native.tap("Escape")
        self.checkpoint("modal_escape_restores", open=0)
        self.click("trigger")
        self.checkpoint("placement_popup_open", open=1, modal=0)
        self.native.tap("F5")
        self.checkpoint("below_flips_above_and_clamps", edge=1)
        self.native.tap("F6")
        self.checkpoint("right_flips_left", right=1)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("placement_after_resize", extent=(800, 600))
        self.native.tap("F5")
        self.checkpoint("ordinary_right_placement", extent=(800, 600), edge=0)
        first = self.point(*center(self.snapshot["rectangles"]["first"]))
        self.native.button(True, *first)
        self.native.tap("F7")
        self.checkpoint("hidden_popup_retires_capture", open=0, visible=0, extent=(800, 600))
        self.native.button(False, *first)
        self.checkpoint("hidden_popup_release_cannot_choose", extent=(800, 600))
        self.native.tap("F7")
        self.checkpoint("restored_popup_requires_open", visible=1, extent=(800, 600))
        self.click("trigger")
        self.checkpoint("focus_loss_popup_open", open=1, extent=(800, 600))
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_closes", open=0, extent=(800, 600))
        self.native.text("q")
        self.checkpoint("closed_popup_cannot_receive_text", extent=(800, 600))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    environment.update({"NWB_UI_LAYER_POPUP": "1", "NWB_UI_LAYER_POPUP_SKIN": "1" if args.skin == "alternate" else "0",
        "NWB_UI_LAYER_EDIT": "0", "NWB_UI_LAYER_INTERACTIVE": "0", "NWB_UI_LAYER_WINDOW": "0", "NWB_UI_LAYER_WINDOW_SKIN": "0",
        "NWB_UI_LAYER_LIST": "0", "NWB_UI_LAYER_LIST_SKIN": "0"})
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = popup_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the popup fixture appeared")
            raise SmokeFailure("custom popup fixture did not appear")
        backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiPopupSmoke: display", min(args.timeout, 15.0))
        popup_run = PopupRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        popup_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI popup fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI popup fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "popup.log").write_text(text, encoding="utf-8")
            finally:
                if logserver is not None:
                    terminate_process(logserver, "UI popup logserver")
                backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    popup_run.write_report(True)
    print(f"UI popup: {len(popup_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
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
