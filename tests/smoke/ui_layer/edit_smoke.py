#!/usr/bin/env python3
"""Exercise single-line editing through native input and matching GPU captures."""
from __future__ import annotations

import platform
import sys
import time

from caret_capture import wait_for_caret_phase
from edit_native import EditNativeInput
from edit_probe import CARET_RGB, TOLERANCE, caret_probe_position, caret_required, model, observe_edit, snapshot_from_logs
from interaction_smoke import parse_args
from probe_common import center
from window_capture_smoke import run_smoke_main
from checkpoint_report import CHECKPOINT_SETTLE_SECONDS
from fixture_launch import UiFixtureRunBase, run_ui_fixture

LIT_SCALE = "scale"
LIT_FIELDS = "fields"
LIT_CONTENT = "content"
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
LIT_EDIT_GRACEFUL_EXIT_TIMEOUT_SECONDS = 60.0
LIT_PRIMARY_INITIAL_TEXT = "Hello 한글"
LIT_PRIMARY_BACKSPACED_TEXT = "Hello 한"


class EditRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=EditNativeInput(backend, handle))

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
        time.sleep(CHECKPOINT_SETTLE_SECONDS)
        wanted = primary

        def before_capture(snapshot, _report, stage_deadline):
            nonlocal wanted
            wanted = snapshot[LIT_PRIMARY] if allowed_primary is not None and snapshot[LIT_PRIMARY] in allowed_primary else primary
            if snapshot[LIT_PRIMARY] == wanted and snapshot[LIT_SECONDARY] == secondary and snapshot["readonly"] == readonly and snapshot["visible"] == visible:
                fields = [field for field, state in ((LIT_PRIMARY, wanted), (LIT_SECONDARY, secondary)) if caret_required(field, state, readonly, visible, selection)]
                if fields:
                    self.wait_for_caret_phase(snapshot, fields, stage_deadline)
            return True

        self.poll_checkpoint(name, minimum_extent=(390.0, 350.0), minimum_extent_message="the edit fixture needs a logical client of at least 390x350",
            process_stage="during edit-box capture", snapshot_reader=snapshot_from_logs, before_capture=before_capture,
            evaluate=lambda frame, snapshot: observe_edit(frame, snapshot, wanted, secondary, readonly, visible, extent=extent, selection=selection, scrolled=scrolled), failure_label="edit")

    def write_report(self, passed):
        self.write_json_report("edit.json", {LIT_PASSED: passed, "platform": platform.system(), "stages": self.stages,
            "input_path": "Win32 posted WM_CHAR/key/pointer messages, native Control/Shift modifiers and real cursor placement for double-click" if self.native.windows else "X11 XSendEvent ASCII keys and XSetInputFocus",
            "runtime_limit": "Synthetic commits bypass live IME composition. No native Wayland or physical pointer-grab qualification.",
            "queued_focus_limit": "A posted batch may straddle frames. Old-owner text may commit before focus moves; it must never enter the new owner."})

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
        primary, secondary = LIT_PRIMARY_INITIAL_TEXT, LIT_TARGET
        self.checkpoint("initial", model(primary), model(secondary))
        self.focus(LIT_PRIMARY)
        self.native.tap(LIT_END)
        self.checkpoint("primary_focus", model(primary, focused=True), model(secondary))
        self.native.tap(LIT_BACKSPACE)
        primary = LIT_PRIMARY_BACKSPACED_TEXT
        self.checkpoint("korean_grapheme_backspace", model(primary, focused=True), model(secondary))
        self.native.text(LIT_Z)
        primary += LIT_Z
        self.checkpoint("native_text_commit", model(primary, focused=True), model(secondary))
        self.native.tap(LIT_Z, control=True)
        self.checkpoint("undo", model(LIT_PRIMARY_BACKSPACED_TEXT, focused=True), model(secondary))
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
        primary, secondary = LIT_PRIMARY_INITIAL_TEXT, LIT_TARGET
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
        primary = LIT_PRIMARY_INITIAL_TEXT
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
            primary, secondary = LIT_PRIMARY_INITIAL_TEXT, LIT_TARGET
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
            primary, secondary = LIT_PRIMARY_INITIAL_TEXT, LIT_TARGET
            self.native.tap(LIT_END)
            self.checkpoint("double_click_baseline", model(primary, focused=True), model(secondary), extent=(800, 600))
            content_x, content_y, _, content_height = self.snapshot[LIT_FIELDS][LIT_PRIMARY][LIT_CONTENT]
            first_word = self.point(content_x + 12.0, content_y + content_height / 2.0)
            with self.native.positioned_cursor(*first_word):
                self.native.click(*first_word)
                self.native.click(*first_word)
                self.checkpoint("native_double_click_first_word", model(primary, 0, 5, True), model(secondary), selection=True, extent=(800, 600))


def run(args, *, run_type=EditRun, input_bindings=False):
    extra_env = {"NWB_UI_LAYER_INPUT_BINDINGS_SMOKE": "1" if input_bindings else "0"}
    if platform.system() == "Linux":
        extra_env.update({"XMODIFIERS": "", "GTK_IM_MODULE": LIT_SIMPLE, "QT_IM_MODULE": LIT_SIMPLE})
    return run_ui_fixture(args, fixture="EDIT", display_marker="UiEditSmoke: display", driver_type=run_type, log_name="edit.log", label="UI edit",
        window_label="edit", process_label="edit", graceful_exit_timeout=LIT_EDIT_GRACEFUL_EXIT_TIMEOUT_SECONDS, environment_kwargs={"force_x11": True}, extra_env=extra_env,
        summary_formatter=lambda driver: f"UI edit: {len(driver.stages)} matching native/GPU state gates; synthetic commits bypass live IME")


def main(argv):
    return run_smoke_main(argv, lambda current_argv: parse_args(current_argv, description=__doc__), run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
