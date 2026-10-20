#!/usr/bin/env python3
"""Exercise single-line editing through native input and matching GPU captures."""
from __future__ import annotations

import json
import platform
import sys
import time

from caret_capture import wait_for_caret_phase
from edit_native import EditNativeInput
from edit_probe import CARET_RGB, TOLERANCE, caret_probe_position, caret_required, center, model, observe_edit, snapshot_from_logs
from interaction_smoke import parse_args
from window_capture_smoke import (SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, wait_for_log_message,)
from fixture_environment import build_fixture_environment

LIT_SCALE = "scale"
LIT_FIELDS = "fields"
LIT_CONTENT = "content"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_PRIMARY = "primary"
LIT_SECONDARY = "secondary"
LIT_PASSED = "passed"
LIT_UTF_8 = "utf-8"
LIT_A = "a"
LIT_INSERT = "Insert"
LIT_TAB = "Tab"
LIT_DELETE = "Delete"
LIT_F5 = "F5"
LIT_END = "End"
LIT_TARGET = "Target"
LIT_BACKSPACE = "BackSpace"
LIT_Z = "z"
LIT_Y = "y"
LIT_LEFT = "Left"
LIT_C = "c"
LIT_V = "v"
LIT_X = "x"
LIT_F7 = "F7"
LIT_Q = "q"
LIT_F6 = "F6"
LIT_SIMPLE = "simple"
LIT_UI_EDIT_FIXTURE = "UI edit fixture"


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
        scale_x, scale_y = self.snapshot[LIT_SCALE]
        return round(x * scale_x), round(y * scale_y)

    def focus(self, field):
        self.native.click(*self.point(*center(self.snapshot[LIT_FIELDS][field]["bounds"])))

    def wait_for_caret_phase(self, snapshot, fields, deadline):
        width, height = self.backend.client_size(self.handle)
        scale_x, scale_y = snapshot[LIT_SCALE]
        samples = []
        for field in fields:
            geometry = snapshot[LIT_FIELDS][field]
            column, _, first_row, end_row = caret_probe_position(geometry["caret"], geometry[LIT_CONTENT], scale_x, scale_y, width, height)
            samples.append((column, first_row, end_row, CARET_RGB, TOLERANCE))
        wait_for_caret_phase(self.backend, self.handle, self.process, samples, deadline)

    def checkpoint(self, name, primary, secondary, *, readonly=False, visible=True, extent=None, selection=False, scrolled=False, allowed_primary=None):
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
            if snapshot[LIT_LOGICAL_EXTENT][0] < 390.0 or snapshot[LIT_LOGICAL_EXTENT][1] < 350.0:
                raise SmokeSkip("the edit fixture needs a logical client of at least 390x350")
            wanted = primary
            if allowed_primary is not None and snapshot[LIT_PRIMARY] in allowed_primary:
                wanted = snapshot[LIT_PRIMARY]
            if (snapshot[LIT_PRIMARY] == wanted and snapshot[LIT_SECONDARY] == secondary and snapshot["readonly"] == readonly and snapshot["visible"] == visible):
                fields = [field for field, state in ((LIT_PRIMARY, wanted), (LIT_SECONDARY, secondary)) if caret_required(field, state, readonly, visible, selection)]
                if fields:
                    # Observe off-to-on before the capture's 0.2s settle; full-frame processing can alias the 1s blink cycle.
                    self.wait_for_caret_phase(snapshot, fields, stage_deadline)
            self.backend.capture_client_window(self.handle, path)
            report = observe_edit(read_bmp_24_rows(path), snapshot, wanted, secondary, readonly, visible, extent=extent, selection=selection, scrolled=scrolled)
            if report[LIT_PASSED]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        raise SmokeFailure(f"edit displayed-state gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {LIT_PASSED: passed, "platform": platform.system(), "stages": self.stages, "input_path": "Win32 posted WM_CHAR/key/pointer messages, native Control/Shift modifiers and real cursor placement for double-click"
                if self.native.windows else "X11 XSendEvent ASCII keys and XSetInputFocus", "runtime_limit": "Synthetic commits bypass live IME composition. No native Wayland or physical pointer-grab qualification.",
            "queued_focus_limit": "A posted batch may straddle frames. Old-owner text may commit before focus moves; it must never enter the new owner."}
        (self.args.output_directory / "edit.json").write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)

    def alternate_clipboard(self):
        primary = "Alternate"
        length = len(primary.encode(LIT_UTF_8))
        self.native.tap(LIT_A, control=True)
        self.native.text(primary)
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_INSERT, control=True)
        self.native.tap(LIT_TAB)
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_INSERT, shift=True)
        self.checkpoint("alternate_clipboard_copy_paste", model(primary, 0, length), model(primary, focused=True), extent=(800, 600))
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_DELETE, shift=True)
        self.checkpoint("alternate_clipboard_cut", model(primary, 0, length), model("", focused=True), extent=(800, 600))
        self.native.tap(LIT_INSERT, shift=True)
        self.checkpoint("alternate_clipboard_cut_paste", model(primary, 0, length), model(primary, focused=True), extent=(800, 600))
        self.native.tap(LIT_TAB, shift=True)
        self.native.tap(LIT_F5)
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_DELETE, shift=True)
        self.native.tap(LIT_INSERT, shift=True)
        self.checkpoint("alternate_clipboard_read_only", model(primary, 0, length, True), model(primary), readonly=True, selection=True, extent=(800, 600))
        self.native.tap(LIT_F5)
        self.native.tap(LIT_END)
        return primary

    def execute(self):
        primary, secondary = "Hello 한글", LIT_TARGET
        self.checkpoint("initial", model(primary), model(secondary))
        self.focus(LIT_PRIMARY)
        self.native.tap(LIT_END)
        self.checkpoint("primary_focus", model(primary, focused=True), model(secondary))
        self.native.tap(LIT_BACKSPACE)
        primary = "Hello 한"
        self.checkpoint("korean_grapheme_backspace", model(primary, focused=True), model(secondary))
        self.native.text(LIT_Z)
        primary += LIT_Z
        self.checkpoint("native_text_commit", model(primary, focused=True), model(secondary))
        self.native.tap(LIT_Z, control=True)
        self.checkpoint("undo", model("Hello 한", focused=True), model(secondary))
        self.native.tap(LIT_Y, control=True)
        self.checkpoint("redo", model(primary, focused=True), model(secondary))
        self.native.tap(LIT_LEFT, shift=True)
        self.native.tap(LIT_LEFT, shift=True)
        self.checkpoint("grapheme_selection", model(primary, 10, 6, True), model(secondary), selection=True)
        self.native.tap(LIT_C, control=True)
        self.checkpoint("copy_preserves_selection", model(primary, 10, 6, True), model(secondary), selection=True)
        self.native.tap(LIT_TAB)
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_V, control=True)
        secondary = "한z"
        self.checkpoint("clipboard_paste_second_owner", model(primary, 10, 6), model(secondary, focused=True))
        self.native.tap(LIT_Z, control=True)
        self.checkpoint("paste_undo", model(primary, 10, 6), model(LIT_TARGET, 0, 6, True), selection=True)
        self.native.tap(LIT_Y, control=True)
        self.checkpoint("paste_redo", model(primary, 10, 6), model(secondary, focused=True))
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_X, control=True)
        self.checkpoint("clipboard_cut", model(primary, 10, 6), model("", focused=True))
        self.native.tap(LIT_V, control=True)
        self.checkpoint("cut_paste_roundtrip", model(primary, 10, 6), model(secondary, focused=True))
        self.native.tap(LIT_TAB, shift=True)
        self.native.tap(LIT_END)
        self.native.tap(LIT_F5)
        self.native.tap(LIT_A, control=True)
        self.native.text(LIT_Z)
        self.native.tap(LIT_BACKSPACE)
        self.native.tap(LIT_X, control=True)
        self.native.tap(LIT_V, control=True)
        self.checkpoint("read_only_edit_barriers", model(primary, 0, 10, True), model(secondary), readonly=True, selection=True)
        self.native.tap(LIT_C, control=True)
        self.native.tap(LIT_TAB)
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_V, control=True)
        secondary = primary
        self.checkpoint("read_only_copy", model(primary, 0, 10), model(secondary, focused=True), readonly=True)
        self.native.tap(LIT_F5)
        self.native.tap(LIT_TAB, shift=True)
        self.native.tap(LIT_END)
        self.native.tap(LIT_F7)
        primary, secondary = "Hello 한글", LIT_TARGET
        self.checkpoint("external_model_replacement", model(primary, focused=True), model(secondary))
        self.native.text(LIT_X, settle=False)
        self.native.key(LIT_LEFT, True)
        self.native.key(LIT_LEFT, False)
        self.native.text(LIT_Y, settle=False)
        primary += "yx"
        self.checkpoint("commit_arrow_commit_order", model(primary, 13, 13, True), model(secondary))
        self.native.tap(LIT_END)
        self.native.text_then_tab(LIT_Q)
        before = model(primary)
        after = model(primary + LIT_Q)
        self.checkpoint("queued_commit_focus_fence", before, model(secondary, focused=True), allowed_primary=[before, after])
        self.native.tap(LIT_TAB, shift=True)
        self.native.text("r", settle=False)
        self.native.key(LIT_F7, True)
        self.native.key(LIT_F7, False)
        primary = "Hello 한글"
        self.checkpoint("queued_commit_replacement_fence", model(primary, focused=True), model(secondary))
        self.native.tap(LIT_F6)
        self.checkpoint("hidden_focused_owner", model(primary), model(secondary), visible=False)
        self.native.text(LIT_Z)
        self.checkpoint("hidden_owner_commit_barrier", model(primary), model(secondary), visible=False)
        self.native.tap(LIT_F6)
        self.checkpoint("restored_owner_requires_focus", model(primary), model(secondary))
        self.focus(LIT_PRIMARY)
        self.native.tap(LIT_END)
        self.native.focus_loss_and_gain()
        self.native.text(LIT_Z)
        self.checkpoint("native_focus_loss_commit_barrier", model(primary), model(secondary))
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("client_resized", model(primary), model(secondary), extent=(800, 600))
        self.focus(LIT_PRIMARY)
        self.native.tap(LIT_A, control=True)
        primary = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
        self.native.text(primary)
        self.checkpoint("resized_horizontal_scroll", model(primary, focused=True), model(secondary), extent=(800, 600), scrolled=True)
        self.native.tap("Home")
        self.checkpoint("horizontal_scroll_home", model(primary, 0, 0, True), model(secondary), extent=(800, 600))
        if self.snapshot[LIT_FIELDS][LIT_PRIMARY]["scroll"] != 0.0:
            raise SmokeFailure("Home did not restore the edit box's left text edge")
        primary = secondary = self.alternate_clipboard()
        if self.native.windows:
            self.native.tap(LIT_F7)
            primary, secondary = "Hello 한글", LIT_TARGET
            self.native.tap(LIT_END)
            self.checkpoint("aggregate_repeat_baseline", model(primary, focused=True), model(secondary), extent=(800, 600))
            self.native.aggregate_tap(LIT_BACKSPACE, 3)
            primary = "Hello"
            self.checkpoint("aggregate_backspace_press_repeat", model(primary, focused=True), model(secondary), extent=(800, 600))
            self.native.aggregate_text("Q", 3)
            primary += "QQQ"
            self.checkpoint("aggregate_wm_char_commit", model(primary, focused=True), model(secondary), extent=(800, 600))
            self.native.aggregate_tap(LIT_LEFT, 2)
            self.checkpoint("aggregate_left_press_repeat", model(primary, 6, 6, True), model(secondary), extent=(800, 600))
            self.native.aggregate_tap(LIT_F5, 2)
            self.checkpoint("aggregate_press_once", model(primary, 6, 6, True), model(secondary), readonly=True, extent=(800, 600))
            self.native.aggregate_tap(LIT_F5, 2, repeat=True)
            self.checkpoint("aggregate_repeat_without_press", model(primary, 6, 6, True), model(secondary), readonly=True, extent=(800, 600))
            self.native.tap(LIT_F5)
            self.native.tap(LIT_F7)
            primary, secondary = "Hello 한글", LIT_TARGET
            self.native.tap(LIT_END)
            self.checkpoint("double_click_baseline", model(primary, focused=True), model(secondary), extent=(800, 600))
            content_x, content_y, _, content_height = self.snapshot[LIT_FIELDS][LIT_PRIMARY][LIT_CONTENT]
            first_word = self.point(content_x + 12.0, content_y + content_height / 2.0)
            with self.native.positioned_cursor(*first_word):
                # Both releases precede the next press; the two clicks stay inside the 500 ms window.
                self.native.click(*first_word)
                self.native.click(*first_word)
                self.checkpoint("native_double_click_first_word", model(primary, 0, 5, True), model(secondary), selection=True, extent=(800, 600))


def run(args, *, run_type=EditRun, input_bindings=False):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), "EDIT", force_x11=True)
    environment["NWB_UI_LAYER_INPUT_BINDINGS_SMOKE"] = "1" if input_bindings else "0"
    if platform.system() == "Linux":
        # Synthetic XSendEvent keycodes resolve through XIM; disable host text commits for binding ownership checks.
        environment["XMODIFIERS"] = ""
        environment["GTK_IM_MODULE"] = LIT_SIMPLE
        environment["QT_IM_MODULE"] = LIT_SIMPLE
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
        edit_run = run_type(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        edit_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, LIT_UI_EDIT_FIXTURE, handle)
                require_normal_process_exit(exit_code, tail, LIT_UI_EDIT_FIXTURE)
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "edit.log").write_text(text, encoding=LIT_UTF_8)
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
