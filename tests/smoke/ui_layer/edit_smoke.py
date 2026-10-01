#!/usr/bin/env python3
"""Exercise single-line editing through native input and matching GPU captures."""
from __future__ import annotations

import json
import platform
import sys
import time

from edit_native import EditNativeInput
from edit_probe import center, model, observe_edit, snapshot_from_logs
from interaction_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


class EditRun:
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

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def focus(self, field):
        self.native.click(*self.point(*center(self.snapshot["fields"][field]["bounds"])))

    def checkpoint(self, name, primary, secondary, *, readonly=False, visible=True, extent=None,
        selection=False, scrolled=False, allowed_primary=None):
        # Settle unchanged-state gates as well, exposing replay or late clipboard delivery.
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during edit-box capture")
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 390.0 or snapshot["logical_extent"][1] < 350.0:
                raise SmokeSkip("the edit fixture needs a logical client of at least 390x350")
            wanted = primary
            if allowed_primary is not None and snapshot["primary"] in allowed_primary:
                wanted = snapshot["primary"]
            self.backend.capture_client_window(self.handle, path)
            report = observe_edit(read_bmp_24_rows(path), snapshot, wanted, secondary, readonly, visible,
                extent=extent, selection=selection, scrolled=scrolled)
            if report["passed"]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        raise SmokeFailure(f"edit displayed-state gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "stages": self.stages,
            "input_path": "Win32 posted WM_CHAR/key/pointer messages, native Control/Shift modifiers and real cursor placement for double-click"
                if self.native.windows else "X11 XSendEvent ASCII keys and XSetInputFocus",
            "runtime_limit": "Synthetic commits bypass live IME composition. No native Wayland or physical pointer-grab qualification.",
            "queued_focus_limit": "A posted batch may straddle frames. Old-owner text may commit before focus moves; it must never enter the new owner."}
        (self.args.output_directory / "edit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def alternate_clipboard(self):
        primary = "Alternate"
        length = len(primary.encode("utf-8"))
        self.native.tap("a", control=True)
        self.native.text(primary)
        self.native.tap("a", control=True)
        self.native.tap("Insert", control=True)
        self.native.tap("Tab")
        self.native.tap("a", control=True)
        self.native.tap("Insert", shift=True)
        self.checkpoint("alternate_clipboard_copy_paste", model(primary, 0, length), model(primary, focused=True), extent=(800, 600))
        self.native.tap("a", control=True)
        self.native.tap("Delete", shift=True)
        self.checkpoint("alternate_clipboard_cut", model(primary, 0, length), model("", focused=True), extent=(800, 600))
        self.native.tap("Insert", shift=True)
        self.checkpoint("alternate_clipboard_cut_paste", model(primary, 0, length), model(primary, focused=True), extent=(800, 600))
        self.native.tap("Tab", shift=True)
        self.native.tap("F5")
        self.native.tap("a", control=True)
        self.native.tap("Delete", shift=True)
        self.native.tap("Insert", shift=True)
        self.checkpoint("alternate_clipboard_read_only", model(primary, 0, length, True), model(primary),
            readonly=True, selection=True, extent=(800, 600))
        self.native.tap("F5")
        self.native.tap("End")
        return primary

    def execute(self):
        primary, secondary = "Hello 한글", "Target"
        self.checkpoint("initial", model(primary), model(secondary))
        self.focus("primary")
        self.native.tap("End")
        self.checkpoint("primary_focus", model(primary, focused=True), model(secondary))
        self.native.tap("BackSpace")
        primary = "Hello 한"
        self.checkpoint("korean_grapheme_backspace", model(primary, focused=True), model(secondary))
        self.native.text("z")
        primary += "z"
        self.checkpoint("native_text_commit", model(primary, focused=True), model(secondary))
        self.native.tap("z", control=True)
        self.checkpoint("undo", model("Hello 한", focused=True), model(secondary))
        self.native.tap("y", control=True)
        self.checkpoint("redo", model(primary, focused=True), model(secondary))
        self.native.tap("Left", shift=True)
        self.native.tap("Left", shift=True)
        self.checkpoint("grapheme_selection", model(primary, 10, 6, True), model(secondary), selection=True)
        self.native.tap("c", control=True)
        self.checkpoint("copy_preserves_selection", model(primary, 10, 6, True), model(secondary), selection=True)
        self.native.tap("Tab")
        self.native.tap("a", control=True)
        self.native.tap("v", control=True)
        secondary = "한z"
        self.checkpoint("clipboard_paste_second_owner", model(primary, 10, 6), model(secondary, focused=True))
        self.native.tap("z", control=True)
        self.checkpoint("paste_undo", model(primary, 10, 6), model("Target", 0, 6, True), selection=True)
        self.native.tap("y", control=True)
        self.checkpoint("paste_redo", model(primary, 10, 6), model(secondary, focused=True))
        self.native.tap("a", control=True)
        self.native.tap("x", control=True)
        self.checkpoint("clipboard_cut", model(primary, 10, 6), model("", focused=True))
        self.native.tap("v", control=True)
        self.checkpoint("cut_paste_roundtrip", model(primary, 10, 6), model(secondary, focused=True))
        self.native.tap("Tab", shift=True)
        self.native.tap("End")
        self.native.tap("F5")
        self.native.tap("a", control=True)
        self.native.text("z")
        self.native.tap("BackSpace")
        self.native.tap("x", control=True)
        self.native.tap("v", control=True)
        self.checkpoint("read_only_edit_barriers", model(primary, 0, 10, True), model(secondary), readonly=True, selection=True)
        self.native.tap("c", control=True)
        self.native.tap("Tab")
        self.native.tap("a", control=True)
        self.native.tap("v", control=True)
        secondary = primary
        self.checkpoint("read_only_copy", model(primary, 0, 10), model(secondary, focused=True), readonly=True)
        self.native.tap("F5")
        self.native.tap("Tab", shift=True)
        self.native.tap("End")
        self.native.tap("F7")
        primary, secondary = "Hello 한글", "Target"
        self.checkpoint("external_model_replacement", model(primary, focused=True), model(secondary))
        self.native.text("x", settle=False)
        self.native.key("Left", True)
        self.native.key("Left", False)
        self.native.text("y", settle=False)
        primary += "yx"
        self.checkpoint("commit_arrow_commit_order", model(primary, 13, 13, True), model(secondary))
        self.native.tap("End")
        self.native.text_then_tab("q")
        before = model(primary)
        after = model(primary + "q")
        self.checkpoint("queued_commit_focus_fence", before, model(secondary, focused=True), allowed_primary=[before, after])
        self.native.tap("Tab", shift=True)
        self.native.text("r", settle=False)
        self.native.key("F7", True)
        self.native.key("F7", False)
        primary = "Hello 한글"
        self.checkpoint("queued_commit_replacement_fence", model(primary, focused=True), model(secondary))
        self.native.tap("F6")
        self.checkpoint("hidden_focused_owner", model(primary), model(secondary), visible=False)
        self.native.text("z")
        self.checkpoint("hidden_owner_commit_barrier", model(primary), model(secondary), visible=False)
        self.native.tap("F6")
        self.checkpoint("restored_owner_requires_focus", model(primary), model(secondary))
        self.focus("primary")
        self.native.tap("End")
        self.native.focus_loss_and_gain()
        self.native.text("z")
        self.checkpoint("native_focus_loss_commit_barrier", model(primary), model(secondary))
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("client_resized", model(primary), model(secondary), extent=(800, 600))
        self.focus("primary")
        self.native.tap("a", control=True)
        primary = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
        self.native.text(primary)
        self.checkpoint("resized_horizontal_scroll", model(primary, focused=True), model(secondary), extent=(800, 600), scrolled=True)
        self.native.tap("Home")
        self.checkpoint("horizontal_scroll_home", model(primary, 0, 0, True), model(secondary), extent=(800, 600))
        if self.snapshot["fields"]["primary"]["scroll"] != 0.0:
            raise SmokeFailure("Home did not restore the edit box's left text edge")
        primary = secondary = self.alternate_clipboard()
        if self.native.windows:
            self.native.tap("F7")
            primary, secondary = "Hello 한글", "Target"
            self.native.tap("End")
            self.checkpoint("aggregate_repeat_baseline", model(primary, focused=True), model(secondary), extent=(800, 600))
            self.native.aggregate_tap("BackSpace", 3)
            primary = "Hello"
            self.checkpoint("aggregate_backspace_press_repeat", model(primary, focused=True), model(secondary), extent=(800, 600))
            self.native.aggregate_text("Q", 3)
            primary += "QQQ"
            self.checkpoint("aggregate_wm_char_commit", model(primary, focused=True), model(secondary), extent=(800, 600))
            self.native.aggregate_tap("Left", 2)
            self.checkpoint("aggregate_left_press_repeat", model(primary, 6, 6, True), model(secondary), extent=(800, 600))
            self.native.aggregate_tap("F5", 2)
            self.checkpoint("aggregate_press_once", model(primary, 6, 6, True), model(secondary),
                readonly=True, extent=(800, 600))
            self.native.aggregate_tap("F5", 2, repeat=True)
            self.checkpoint("aggregate_repeat_without_press", model(primary, 6, 6, True), model(secondary),
                readonly=True, extent=(800, 600))
            self.native.tap("F5")
            self.native.tap("F7")
            primary, secondary = "Hello 한글", "Target"
            self.native.tap("End")
            self.checkpoint("double_click_baseline", model(primary, focused=True), model(secondary), extent=(800, 600))
            content_x, content_y, _, content_height = self.snapshot["fields"]["primary"]["content"]
            first_word = self.point(content_x + 12.0, content_y + content_height / 2.0)
            with self.native.positioned_cursor(*first_word):
                # Both releases precede the next press; the two clicks stay inside the 500 ms window.
                self.native.click(*first_word)
                self.native.click(*first_word)
                self.checkpoint("native_double_click_first_word", model(primary, 0, 5, True), model(secondary),
                    selection=True, extent=(800, 600))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    environment.update({"NWB_UI_LAYER_EDIT": "1", "NWB_UI_LAYER_INTERACTIVE": "0",
        "NWB_UI_LAYER_WINDOW": "0", "NWB_UI_LAYER_WINDOW_SKIN": "0", "NWB_UI_LAYER_POPUP": "0", "NWB_UI_LAYER_POPUP_SKIN": "0",
        "NWB_UI_LAYER_LIST": "0", "NWB_UI_LAYER_LIST_SKIN": "0"})
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = edit_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the edit fixture appeared")
            raise SmokeFailure("custom edit fixture did not appear")
        backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiEditSmoke: display", min(args.timeout, 15.0))
        edit_run = EditRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        edit_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI edit fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI edit fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "edit.log").write_text(text, encoding="utf-8")
            finally:
                if logserver is not None:
                    terminate_process(logserver, "UI edit logserver")
                backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    edit_run.write_report(True)
    print(f"UI edit: {len(edit_run.stages)} matching native/GPU state gates; synthetic commits bypass live IME", flush=True)
    return 0


def main(argv):
    args = parse_args(argv, description=__doc__)
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
