#!/usr/bin/env python3
"""Exercise a 100000-row virtual list through native input and accepted GPU presentation."""
from __future__ import annotations

import json
import platform
import sys

from list_native import ListNativeInput
from probe_common import center
from list_probe import observe_list, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import run_smoke_main
from keyed_run import KeyedRun, run_keyed_fixture

LIT_LIST = "list"
LIT_SELECTED = "selected"
LIT_CURSOR = "cursor"
LIT_FIRST = "first"
LIT_DOWN = "Down"
LIT_RECTANGLES = "rectangles"
LIT_SELECTED_ROW = "selected_row"
LIT_HOME = "Home"
LIT_PAST = "past"
LIT_VIEWPORT = "viewport"
LIT_THUMB = "thumb"
LIT_TRACK = "track"
LIT_F7 = "F7"


class ListRun(KeyedRun):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native_type=ListNativeInput, snapshot_reader=snapshot_from_logs, observer=observe_list,
            minimum_height=400, fixture=LIT_LIST, capture_stage="during virtual-list UI capture")
        self.expected = {LIT_SELECTED: 0, LIT_CURSOR: 0, "count": 100000, "focused": 0, "reversed": 0, "removed": 0, "visible": 1, "commits": 0, "underlying": 0}


    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages, "input_path": "Win32 posted keys/pointer/positioned WM_MOUSEWHEEL messages"
                if self.native.windows else "X11 XSendEvent keys/button4/button5 and XSetInputFocus", "runtime_limit": "No live IME, native Wayland, physical pointer-grab, or native Linux qualification."}
        (self.args.output_directory / "list.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial_bounded_rows", first=0)
        self.expected.pop(LIT_FIRST)
        self.native.click(*self.row(1))
        self.checkpoint("pointer_selects_keyed_row", selected=2, cursor=2, focused=1, commits=1)
        self.native.tap(LIT_DOWN)
        self.checkpoint("down_selects_next_key", selected=3, cursor=3)
        self.native.tap(LIT_DOWN)
        self.checkpoint("down_reaches_before_disabled", selected=4, cursor=4)
        self.native.tap(LIT_DOWN)
        self.checkpoint("down_skips_disabled_key5", selected=6, cursor=6)
        self.native.tap("Up")
        self.checkpoint("up_skips_disabled_key5", selected=4, cursor=4)
        self.native.tap("Page_Down")
        self.checkpoint("page_down_ensures_selected_visible", dynamic=(LIT_SELECTED, LIT_CURSOR), extra=lambda s: s[LIT_SELECTED] == s[LIT_CURSOR] and s[LIT_SELECTED] > 4 and s[LIT_RECTANGLES][LIT_SELECTED_ROW][2] > 0.0)
        self.native.tap(LIT_HOME)
        self.checkpoint("home_selects_first", selected=1, cursor=1, first=0)
        self.expected.pop(LIT_FIRST)
        self.native.tap("End")
        self.checkpoint("end_reaches100000_without_bulk_labels", selected=100000, cursor=100000, extra=lambda s: s[LIT_PAST] == 100000 and s[LIT_FIRST] > 99980)
        self.native.tap("Return")
        self.checkpoint("enter_activates_current_key", commits=2)
        self.native.tap("Page_Up")
        self.checkpoint("page_up_ensures_selected_visible", dynamic=(LIT_SELECTED, LIT_CURSOR), extra=lambda s: s[LIT_SELECTED] == s[LIT_CURSOR] and 99980 < s[LIT_SELECTED] < 100000 and s[LIT_RECTANGLES][LIT_SELECTED_ROW][2] > 0.0)
        old_first = self.snapshot[LIT_FIRST]
        self.native.wheel(2, *self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_VIEWPORT])))
        self.checkpoint("wheel_scrolls_preserving_selected_key", extra=lambda s: s[LIT_FIRST] < old_first)
        self.native.tap(LIT_HOME)
        self.checkpoint("home_after_wheel_ensures_cursor", selected=1, cursor=1, first=0)
        self.expected.pop(LIT_FIRST)
        self.native.wheel(-2, *self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_VIEWPORT])))
        self.checkpoint("wheel_down_virtualizes_next_rows", extra=lambda s: s[LIT_FIRST] >= 5)
        thumb = self.snapshot[LIT_RECTANGLES][LIT_THUMB]
        track = self.snapshot[LIT_RECTANGLES][LIT_TRACK]
        self.native.drag(self.point(*center(thumb)), self.point(track[0] + track[2] / 2.0, track[1] + track[3] - thumb[3] / 2.0))
        self.checkpoint("thumb_drag_reaches_bottom_without_changing_selection", extra=lambda s: s[LIT_PAST] == 100000)
        thumb = self.snapshot[LIT_RECTANGLES][LIT_THUMB]
        track = self.snapshot[LIT_RECTANGLES][LIT_TRACK]
        self.native.drag(self.point(*center(thumb)), self.point(track[0] + track[2] / 2.0, track[1] + thumb[3] / 2.0))
        self.checkpoint("thumb_drag_reaches_top", first=0)
        self.expected.pop(LIT_FIRST)
        self.native.click(*self.row(3))
        self.checkpoint("pointer_reselects_known_key", selected=4, cursor=4, commits=3)
        self.native.tap("F5")
        self.checkpoint("reorder_keeps_selected_key_and_scrolls_to_new_index", reversed=1, extra=lambda s: s[LIT_FIRST] > 99980 and s[LIT_RECTANGLES][LIT_SELECTED_ROW][2] > 0.0)
        self.native.tap("F6")
        self.checkpoint("remove_selected_key_clears_selection", selected=0, cursor=0, count=99999, removed=4)
        self.native.tap(LIT_HOME)
        self.checkpoint("reversed_home_selects_largest_key", selected=100000, cursor=100000, first=0)
        self.expected.pop(LIT_FIRST)
        self.native.button(True, *self.row(2))
        self.native.tap(LIT_F7)
        self.checkpoint("omission_retires_held_pointer", visible=0, focused=0)
        self.native.button(False, *self.row(2))
        self.checkpoint("omitted_pointer_release_cannot_activate")
        self.native.tap(LIT_F7)
        self.checkpoint("restore_requires_new_focus", visible=1)
        self.native.click(*self.row(0))
        self.checkpoint("restored_list_accepts_new_pointer_sequence", focused=1, commits=4)
        self.native.key(LIT_DOWN, True)
        self.checkpoint("held_navigation_selects_once", selected=99999, cursor=99999)
        self.native.tap(LIT_F7)
        self.checkpoint("omission_retires_held_key", visible=0, focused=0)
        self.native.key(LIT_DOWN, True, repeat=True)
        self.native.key(LIT_DOWN, False)
        self.checkpoint("omitted_key_repeat_and_release_cannot_navigate")
        self.native.tap(LIT_F7)
        self.checkpoint("restore_after_held_key", visible=1)
        self.native.click(*self.row(1))
        self.checkpoint("restored_keyboard_owner", focused=1, commits=5)
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_retires_list_focus", focused=0)
        self.native.tap(LIT_DOWN)
        self.checkpoint("unfocused_navigation_cannot_change_model")
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_keyed_model", extent=(800, 600))
        self.native.click(*self.row(0))
        self.checkpoint("first_pointer_after_resize_uses_accepted_geometry", selected=100000, cursor=100000, focused=1, commits=6, extent=(800, 600))


def run(args):
    return run_keyed_fixture(args, fixture="LIST", display_marker="UiListSmoke: display", driver_type=ListRun, log_name="list.log", label="UI virtual list", window_label=LIT_LIST, process_label="virtual-list")


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
