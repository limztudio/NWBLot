#!/usr/bin/env python3

from __future__ import annotations

import sys
import time

from edit_native import EditNativeInput
from probe_common import center
from popup_probe import observe_popup, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import run_smoke_main
from checkpoint_report import CHECKPOINT_SETTLE_SECONDS
from fixture_launch import UiFixtureRunBase, run_ui_fixture

LIT_MODAL = "modal"
LIT_RECTANGLES = "rectangles"
LIT_TRIGGER = "trigger"
LIT_TAB = "Tab"
LIT_ESCAPE = "Escape"
LIT_RETURN = "Return"
LIT_COUNTER = "counter"
LIT_F5 = "F5"
LIT_F7 = "F7"


class PopupRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=EditNativeInput(backend, handle))
        self.expected = {"open": 0, LIT_MODAL: 0, "underlying": 0, "selected": 0, "checked": 0, "edit_focused": 0, "outside_focused": 0, "edge": 0, "right": 0, "visible": 1, "text_bytes": 5}

    def checkpoint(self, name, *, extent=None, **changes):
        self.expected.update(changes)
        time.sleep(CHECKPOINT_SETTLE_SECONDS)
        self.poll_checkpoint(name, minimum_extent=(600.0, 390.0), minimum_extent_message="the popup fixture needs a logical client of at least 600x390",
            process_stage="during popup UI capture", snapshot_reader=snapshot_from_logs,
            evaluate=lambda frame, snapshot: observe_popup(frame, snapshot, self.expected, extent=extent, skin=self.args.skin), failure_label="popup")

    def write_report(self, passed):
        self.write_smoke_report("popup.json", passed,
            input_path="Win32 posted keys/pointer/WM_CHAR messages with native Shift modifiers" if self.native.windows else "X11 XSendEvent ASCII keys and XSetInputFocus",
            runtime_limit="Synthetic text bypasses live IME. No native Wayland, physical pointer-grab, or native Linux qualification.")

    def execute(self):
        self.checkpoint("initial")
        self.click("outside")
        self.checkpoint("outside_focus", outside_focused=1)
        self.click(LIT_TRIGGER)
        self.checkpoint("open_above_later_root", open=1, outside_focused=0)
        self.native.tap(LIT_TAB)
        self.native.tap(LIT_TAB)
        self.native.tap("space")
        self.checkpoint("popup_checkbox_by_keyboard", checked=1)
        self.native.tap(LIT_TAB)
        self.checkpoint("tab_reaches_popup_edit", edit_focused=1)
        self.native.text("z")
        self.checkpoint("popup_edit_native_commit", text_bytes=6)
        self.native.tap(LIT_ESCAPE)
        self.checkpoint("escape_from_edit_closes", open=0, edit_focused=0)
        self.native.tap(LIT_RETURN)
        self.checkpoint("restored_trigger_reopens", open=1)
        self.native.tap(LIT_TAB, shift=True)
        self.checkpoint("shift_tab_wraps_to_last", edit_focused=1)
        self.native.tap(LIT_TAB)
        self.checkpoint("tab_wraps_to_first", edit_focused=0)
        self.native.tap(LIT_RETURN)
        self.checkpoint("keyboard_choice_closes", open=0, selected=1)
        self.native.tap(LIT_RETURN)
        self.checkpoint("choice_restores_trigger", open=1)
        outside = self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_COUNTER]))
        self.native.button(True, *outside)
        self.checkpoint("outside_press_dismisses", open=0)
        self.native.button(False, *outside)
        self.checkpoint("outside_release_is_consumed")
        self.click(LIT_COUNTER)
        self.checkpoint("next_outside_sequence_activates", underlying=1)
        self.click(LIT_TRIGGER)
        self.checkpoint("pointer_choice_open", open=1)
        self.click("second")
        self.checkpoint("pointer_choice_closes", open=0, selected=2)
        self.click(LIT_MODAL)
        self.checkpoint("modal_dims_later_root", open=1, modal=1)
        self.click(LIT_COUNTER)
        self.checkpoint("modal_blocks_underlying")
        self.native.tap(LIT_ESCAPE)
        self.checkpoint("modal_escape_restores", open=0)
        self.click(LIT_TRIGGER)
        self.checkpoint("placement_popup_open", open=1, modal=0)
        self.native.tap(LIT_F5)
        self.checkpoint("below_flips_above_and_clamps", edge=1)
        self.native.tap("F6")
        self.checkpoint("right_flips_left", right=1)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("placement_after_resize", extent=(800, 600))
        self.native.tap(LIT_F5)
        self.checkpoint("ordinary_right_placement", extent=(800, 600), edge=0)
        first = self.point(*center(self.snapshot[LIT_RECTANGLES]["first"]))
        self.native.button(True, *first)
        self.native.tap(LIT_F7)
        self.checkpoint("hidden_popup_retires_capture", open=0, visible=0, extent=(800, 600))
        self.native.button(False, *first)
        self.checkpoint("hidden_popup_release_cannot_choose", extent=(800, 600))
        self.native.tap(LIT_F7)
        self.checkpoint("restored_popup_requires_open", visible=1, extent=(800, 600))
        self.click(LIT_TRIGGER)
        self.checkpoint("focus_loss_popup_open", open=1, extent=(800, 600))
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_closes", open=0, extent=(800, 600))
        self.native.text("q")
        self.checkpoint("closed_popup_cannot_receive_text", extent=(800, 600))


def run(args):
    return run_ui_fixture(args, fixture="POPUP", display_marker="UiPopupSmoke: display", driver_type=PopupRun, log_name="popup.log", label="UI popup",
        window_label="popup", process_label="popup")


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
