#!/usr/bin/env python3
"""Exercise delayed noninteractive tooltips and anchor-bound context menu commands."""
from __future__ import annotations

import json
import platform
import sys
import time

from popup_tools_native import PopupToolsNativeInput
from popup_tools_probe import center, observe_popup_tools, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


class PopupToolsRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = PopupToolsNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None
        self.expected = {"tooltip": 0, "open": 0, "cursor": 0, "command": 0, "commits": 0, "anchor_clicks": 0,
            "underlying": 0, "sentinel_focused": 0, "enabled": 1, "reversed": 0, "source_revision": 1,
            "source_generation": 1, "text_bytes": 8}

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def click(self, name):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][name])))

    def hover(self, name):
        self.native.pointer(*self.point(*center(self.snapshot["rectangles"][name])))

    def right_click_anchor(self):
        self.native.right_click(*self.point(*center(self.snapshot["rectangles"]["anchor"])))

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), settle=0.25, **changes):
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        time.sleep(settle)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during popup tools UI capture")
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 600.0 or snapshot["logical_extent"][1] < 400.0:
                raise SmokeSkip("the popup tools fixture needs a logical client of at least 600x400")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_popup_tools(read_bmp_24_rows(path), snapshot, self.expected,
                extent=extent, skin=self.args.skin, extra=extra)
            if report["passed"]:
                self.snapshot = snapshot
                for field in dynamic:
                    self.expected[field] = snapshot[field]
                report.update({"stage": name, "capture": str(path), "native_window": self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        self.failure_report = {"stage": name, "capture": str(path), "native_window": self.native.observe_window(),
            "observation": report}
        self.write_report(False)
        raise SmokeFailure(f"popup tools displayed-state gate '{name}' failed: {self.failure_report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "failure": self.failure_report,
            "input_path": "Win32 physical hover positioning with posted motion/buttons/Menu keys and native Shift+F10 modifiers"
                if self.native.windows else "X11 XSendEvent keys/button1/button3 and XSetInputFocus",
            "runtime_limit": "No live IME, native Wayland, physical pointer-grab, or native Linux qualification."}
        (self.args.output_directory / "popup_tools.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.native.pointer(4, 4)
        self.checkpoint("initial_tools_state")
        self.click("sentinel")
        self.checkpoint("outside_edit_owns_focus", sentinel_focused=1)
        self.hover("anchor")
        self.checkpoint("hover_before_delay", settle=0.05)
        self.checkpoint("delayed_tooltip_keeps_edit_focus", tooltip=1)
        self.native.text("x")
        self.checkpoint("tooltip_does_not_steal_native_text", text_bytes=9)
        self.hover("counter")
        self.checkpoint("leaving_anchor_hides_tooltip", tooltip=0)
        self.hover("anchor")
        self.checkpoint("new_hover_restarts_delay", settle=0.05)
        self.checkpoint("tooltip_reappears_after_new_delay", tooltip=1)
        anchor = self.point(*center(self.snapshot["rectangles"]["anchor"]))
        self.native.button(True, *anchor)
        self.checkpoint("held_primary_suppresses_tooltip", tooltip=0, sentinel_focused=0)
        self.native.button(False, *anchor)
        self.checkpoint("primary_release_activates_anchor", anchor_clicks=1)
        self.hover("counter")
        self.native.tap("Menu")
        self.checkpoint("menu_key_opens_on_focused_anchor", open=1, cursor=1)
        self.native.tap("Down")
        self.checkpoint("menu_down_previews_second", cursor=2)
        self.native.tap("Down")
        self.checkpoint("menu_down_skips_disabled", cursor=4)
        self.native.tap("Up")
        self.checkpoint("menu_up_skips_disabled", cursor=2)
        self.native.tap("Return")
        self.checkpoint("menu_enter_activates_and_closes", open=0, command=2, commits=1, dynamic=("cursor",))
        self.native.tap("F10", shift=True)
        self.checkpoint("shift_f10_opens_on_returned_focus", open=1, cursor=1)
        self.native.tap("space")
        self.checkpoint("menu_space_activates_first_command", open=0, command=1, commits=2, dynamic=("cursor",))
        self.right_click_anchor()
        self.checkpoint("right_click_opens_without_primary_activation", open=1, cursor=1)
        self.click("disabled")
        self.checkpoint("disabled_menu_command_cannot_activate")
        self.click("last")
        self.checkpoint("pointer_command_activates_and_closes", open=0, command=5, commits=3, dynamic=("cursor",))
        self.right_click_anchor()
        self.checkpoint("outside_dismissal_setup", open=1, cursor=1)
        self.click("counter")
        self.checkpoint("outside_press_and_release_are_consumed", open=0, dynamic=("cursor",))
        self.click("counter")
        self.checkpoint("next_outside_sequence_activates", underlying=1)
        self.right_click_anchor()
        self.checkpoint("source_revision_held_pointer_setup", open=1, cursor=1)
        old_second = self.point(*center(self.snapshot["rectangles"]["second"]))
        self.native.button(True, *old_second)
        self.native.tap("F5")
        self.checkpoint("source_revision_resets_preview_and_fences_pointer", reversed=1, source_revision=2, cursor=5)
        self.native.button(False, *old_second)
        self.checkpoint("stale_pointer_release_cannot_activate_new_order")
        self.hover("counter")
        self.native.tap("Home")
        self.checkpoint("reversed_home_previews_first", cursor=5)
        self.native.tap("End")
        self.checkpoint("reversed_end_previews_last", cursor=1)
        self.native.tap("Return")
        self.checkpoint("reversed_command_commit", open=0, command=1, commits=4, dynamic=("cursor",))
        self.right_click_anchor()
        self.checkpoint("source_replacement_held_key_setup", open=1, cursor=5)
        self.native.key("Down", True)
        self.checkpoint("held_key_previews_once", cursor=4)
        self.native.tap("F6")
        self.checkpoint("source_replacement_dismisses_and_retires_owner", open=0, source_generation=2,
            source_revision=3, dynamic=("cursor",))
        self.native.key("Down", True, repeat=True)
        self.native.key("Down", False)
        self.checkpoint("stale_held_key_cannot_reopen_or_activate")
        self.native.tap("F9")
        self.right_click_anchor()
        self.native.tap("Menu")
        self.native.tap("F10", shift=True)
        self.checkpoint("disabled_anchor_blocks_pointer_and_keyboard_menu", enabled=0)
        self.hover("anchor")
        self.checkpoint("disabled_anchor_blocks_delayed_tooltip", settle=1.2)
        self.native.tap("F9")
        self.hover("counter")
        self.checkpoint("enable_restores_attachment", enabled=1)
        self.click("anchor")
        self.checkpoint("fresh_primary_focuses_enabled_anchor", anchor_clicks=2)
        self.hover("counter")
        self.native.tap("Menu")
        self.checkpoint("fresh_menu_after_source_replacement", open=1, cursor=5)
        self.native.tap("Escape")
        self.checkpoint("escape_dismisses_context_commands", open=0, dynamic=("cursor",))
        self.right_click_anchor()
        self.checkpoint("focus_loss_menu_setup", open=1, cursor=5)
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_dismisses_menu", open=0, sentinel_focused=0, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("unfocused_navigation_cannot_activate")
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_host_commands", extent=(800, 600))
        self.right_click_anchor()
        self.checkpoint("first_secondary_request_after_resize", open=1, cursor=5, extent=(800, 600))
        self.click("second")
        self.checkpoint("pointer_command_after_resize", open=0, command=2, commits=5, extent=(800, 600), dynamic=("cursor",))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    environment.update({"NWB_UI_LAYER_POPUP_TOOLS": "1", "NWB_UI_LAYER_POPUP_TOOLS_SKIN": "1" if args.skin == "alternate" else "0",
        "NWB_UI_LAYER_COMBO": "0", "NWB_UI_LAYER_COMBO_SKIN": "0",
        "NWB_UI_LAYER_SEARCH_COMBO": "0", "NWB_UI_LAYER_SEARCH_COMBO_SKIN": "0",
        "NWB_UI_LAYER_LIST": "0", "NWB_UI_LAYER_LIST_SKIN": "0",
        "NWB_UI_LAYER_POPUP": "0", "NWB_UI_LAYER_POPUP_SKIN": "0", "NWB_UI_LAYER_EDIT": "0",
        "NWB_UI_LAYER_INTERACTIVE": "0", "NWB_UI_LAYER_WINDOW": "0", "NWB_UI_LAYER_WINDOW_SKIN": "0"})
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = popup_tools_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the popup tools fixture appeared")
            raise SmokeFailure("custom popup tools fixture did not appear")
        if platform.system() == "Windows":
            backend.prepare_raw_client_window(handle)
        else:
            backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiPopupToolsSmoke: display", min(args.timeout, 15.0))
        popup_tools_run = PopupToolsRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        popup_tools_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI popup tools fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI popup tools fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "popup_tools.log").write_text(text, encoding="utf-8")
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI popup tools logserver")
                finally:
                    try:
                        if popup_tools_run is not None:
                            popup_tools_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    popup_tools_run.write_report(True)
    print(f"UI popup tools: {len(popup_tools_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
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
