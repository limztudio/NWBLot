#!/usr/bin/env python3

from __future__ import annotations

import sys
import time

from nested_popup_native import NestedPopupNativeInput
from nested_popup_probe import observe_nested_popup, snapshot_from_logs
from probe_common import center
from window_smoke import parse_args
from window_capture_smoke import run_smoke_main
from fixture_launch import UiFixtureRunBase, run_ui_fixture

LIT_FOCUS_CODE = "focus_code"
LIT_COMBO = "combo"
LIT_SEARCH = "search"
LIT_MENU_CURSOR = "menu_cursor"
LIT_RAW_CHILD = "raw_child"
LIT_RAW_COMBO = "raw_combo"
LIT_RAW_SEARCH = "raw_search"
LIT_RAW_MENU = "raw_menu"
LIT_RECTANGLES = "rectangles"
LIT_LABEL_READS = "label_reads"
LIT_POPUP_TARGETS = "popup_targets"
LIT_AFTER_BUTTON = "after_button"
LIT_CHILD_OPEN = "child_open"
LIT_TAB = "Tab"
LIT_RETURN = "Return"
LIT_ESCAPE = "Escape"
LIT_DOWN = "Down"
LIT_MENU_ANCHOR = "menu_anchor"

NESTED_POPUP_SETTLE_SECONDS = 0.2


class NestedPopupRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=NestedPopupNativeInput(backend, handle))
        self.expected = {"parent": 0, "child": 0, "focus_scope": 0, LIT_FOCUS_CODE: 0, "before_clicks": 0, "after_clicks": 0, "child_clicks": 0, "outside": 0, "before_bytes": 6, "after_bytes": 5, "child_bytes": 5,
            "before_selected": 0, "after_selected": 0, LIT_COMBO: 0, "combo_selected": 1, LIT_SEARCH: 0, "search_selected": 1, "query_bytes": 0, "menu": 0, LIT_MENU_CURSOR: 0, "command": 0, LIT_RAW_CHILD: 0, LIT_RAW_COMBO: 0,
            LIT_RAW_SEARCH: 0, LIT_RAW_MENU: 0, "popup_count": 0, "child_parent_scope": 0}

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), **changes):
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        time.sleep(NESTED_POPUP_SETTLE_SECONDS)
        self.poll_checkpoint(name, minimum_extent=(800.0, 540.0), minimum_extent_message="the nested popup fixture needs a logical client of at least 800x540",
            process_stage="during nested popup capture", snapshot_reader=snapshot_from_logs,
            evaluate=lambda frame, snapshot: observe_nested_popup(frame, snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra),
            raw_windows=True, failure_label="nested popup", accept_extras=lambda _: {"native_window": self.native.observe_window()}, dynamic=dynamic, failure_extras=True)

    def write_report(self, passed):
        self.write_smoke_report("nested_popup.json", passed, include_failure=True,
            input_path="Win32 physical hover positioning, prepared raw capture, posted buttons/keys/WM_CHAR" if self.native.windows else "X11 XSendEvent ASCII keys/button1/button3 and XSetInputFocus",
            runtime_limit="No live IME, native Wayland, physical pointer-grab, or native Linux qualification.")

    def reopen_parent(self, name):
        self.click("open")
        self.checkpoint(name, parent=1, child=0, combo=0, search=0, menu=0, raw_child=0, raw_combo=0, raw_search=0, raw_menu=0, menu_cursor=0, popup_count=1, child_parent_scope=0, focus_scope=1, focus_code=5)

    def close_ancestor(self, name):
        self.native.close_ancestor()
        self.checkpoint(name, parent=0, child=0, combo=0, search=0, menu=0, popup_count=0, child_parent_scope=0, focus_scope=0, focus_code=1, popup_targets=0, label_reads=0, dynamic=(LIT_RAW_CHILD, LIT_RAW_COMBO, LIT_RAW_SEARCH, LIT_RAW_MENU))
        self.expected.pop(LIT_LABEL_READS, None)
        self.expected.pop(LIT_POPUP_TARGETS, None)

    def execute(self):
        self.native.pointer(4, 4)
        self.checkpoint("initial_no_popup_targets", popup_targets=0, label_reads=0)
        self.expected.pop(LIT_POPUP_TARGETS, None)
        self.expected.pop(LIT_LABEL_READS, None)
        self.reopen_parent("parent_opens_and_autofocuses_before_button")
        self.click("before_button")
        self.checkpoint("before_button_uses_parent_scope", before_clicks=1)
        self.click("before_edit")
        self.checkpoint("before_editor_focus", focus_code=6)
        self.native.text("a")
        self.checkpoint("before_editor_borrows_native_text", before_bytes=7)
        self.click("before_row2")
        self.checkpoint("before_list_uses_parent_scope", before_selected=2, focus_code=7)
        self.click(LIT_AFTER_BUTTON)
        self.checkpoint("after_button_resumes_parent_scope", after_clicks=1, focus_code=10)
        self.click("after_edit")
        self.checkpoint("after_editor_focus", focus_code=11)
        self.native.text("b")
        self.checkpoint("after_editor_borrows_native_text", after_bytes=6)
        self.click("after_row2")
        self.checkpoint("after_list_resumes_parent_scope", after_selected=2, focus_code=12)
        self.click(LIT_CHILD_OPEN)
        self.checkpoint("child_escapes_parent_clip_and_autofocuses", child=1, raw_child=1, popup_count=2, child_parent_scope=1, focus_scope=2, focus_code=14)
        self.native.tap(LIT_TAB)
        self.checkpoint("top_child_traps_tab_in_editor", focus_code=15)
        self.native.text("c")
        self.checkpoint("child_editor_owns_native_text", child_bytes=6)
        self.native.tap(LIT_TAB)
        self.checkpoint("top_child_tabs_to_ancestor_close", focus_code=16)
        self.native.tap(LIT_TAB)
        self.checkpoint("top_child_tab_wraps_without_parent_focus", focus_code=14)
        self.native.tap(LIT_RETURN)
        self.checkpoint("child_keyboard_action", child_clicks=1)
        self.native.tap(LIT_ESCAPE)
        self.checkpoint("escape_closes_only_child_and_restores_parent", child=0, raw_child=0, popup_count=1, child_parent_scope=0, focus_scope=1, focus_code=9)
        self.native.tap(LIT_RETURN)
        self.checkpoint("restored_parent_trigger_reopens_child", child=1, raw_child=1, popup_count=2, child_parent_scope=1, focus_scope=2, focus_code=14)
        self.click(LIT_AFTER_BUTTON)
        self.checkpoint("outside_child_consumes_parent_press", child=0, raw_child=0, popup_count=1, child_parent_scope=0, focus_scope=1, focus_code=9)
        self.click(LIT_AFTER_BUTTON)
        self.checkpoint("next_parent_press_works_after_child", after_clicks=2, focus_code=10)
        self.click(LIT_CHILD_OPEN)
        self.checkpoint("ancestor_close_button_setup", child=1, raw_child=1, popup_count=2, child_parent_scope=1, focus_scope=2, focus_code=14)
        self.click("close_branch")
        self.checkpoint("ancestor_close_suppresses_child_paint_and_targets", parent=0, child=0, popup_count=0, child_parent_scope=0, focus_scope=0, focus_code=1, popup_targets=0, label_reads=0, dynamic=(LIT_RAW_CHILD,))
        self.expected.pop(LIT_POPUP_TARGETS, None)
        self.expected.pop(LIT_LABEL_READS, None)
        self.reopen_parent("ancestor_reopen_keeps_old_child_closed")
        self.click(LIT_COMBO)
        self.checkpoint("combo_opens_as_child_of_parent", combo=1, raw_combo=1, popup_count=2, focus_scope=3, dynamic=(LIT_FOCUS_CODE,))
        self.native.tap(LIT_DOWN)
        self.checkpoint("combo_preview_preserves_committed_value")
        self.native.tap(LIT_RETURN)
        self.checkpoint("combo_commit_restores_parent_field", combo=0, raw_combo=0, combo_selected=2, popup_count=1, focus_scope=1, focus_code=17)
        self.click(LIT_COMBO)
        self.checkpoint("combo_ancestor_close_setup", combo=1, raw_combo=1, popup_count=2, focus_scope=3, dynamic=(LIT_FOCUS_CODE,))
        self.close_ancestor("ancestor_close_suppresses_deferred_combo")
        self.reopen_parent("ancestor_reopen_keeps_combo_closed")
        self.click(LIT_SEARCH)
        self.checkpoint("search_query_autofocuses_inside_parent", search=1, raw_search=1, popup_count=2, focus_scope=3, focus_code=22)
        self.native.text("be")
        self.checkpoint("nested_search_filters_without_changing_selection", query_bytes=2)
        self.native.tap(LIT_DOWN)
        self.checkpoint("nested_search_previews_filtered_beta")
        self.native.tap(LIT_RETURN)
        self.checkpoint("nested_search_commit_restores_parent_field", search=0, raw_search=0, search_selected=2, popup_count=1, focus_scope=1, focus_code=20)
        self.click(LIT_SEARCH)
        self.checkpoint("search_ancestor_close_setup_retains_query", search=1, raw_search=1, popup_count=2, focus_scope=3, focus_code=22)
        self.close_ancestor("ancestor_close_suppresses_deferred_search")
        self.reopen_parent("ancestor_reopen_keeps_search_closed_and_query_owned")
        self.native.right_click(*self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_MENU_ANCHOR])))
        self.checkpoint("context_menu_opens_inside_parent", menu=1, raw_menu=1, popup_count=2, focus_scope=3, menu_cursor=1, dynamic=(LIT_FOCUS_CODE,))
        self.native.tap(LIT_DOWN)
        self.checkpoint("nested_menu_navigates_beta", menu_cursor=2)
        self.native.tap(LIT_RETURN)
        self.checkpoint("nested_menu_command_restores_previous_parent_focus", menu=0, raw_menu=0, command=2, popup_count=1, focus_scope=1, focus_code=5, dynamic=(LIT_MENU_CURSOR,))
        self.native.right_click(*self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_MENU_ANCHOR])))
        self.checkpoint("menu_ancestor_close_setup", menu=1, raw_menu=1, popup_count=2, focus_scope=3, menu_cursor=1, dynamic=(LIT_FOCUS_CODE,))
        self.close_ancestor("ancestor_close_suppresses_deferred_context_menu")
        self.reopen_parent("ancestor_reopen_keeps_context_menu_closed")
        self.click(LIT_CHILD_OPEN)
        self.checkpoint("nested_native_focus_loss_setup", child=1, raw_child=1, popup_count=2, child_parent_scope=1, focus_scope=2, focus_code=14)
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_dismisses_entire_branch", parent=0, child=0, popup_count=0, child_parent_scope=0, focus_scope=0, popup_targets=0, label_reads=0, dynamic=(LIT_FOCUS_CODE, LIT_RAW_CHILD))
        self.expected.pop(LIT_POPUP_TARGETS, None)
        self.expected.pop(LIT_LABEL_READS, None)
        self.reopen_parent("reopen_after_focus_loss_has_no_old_descendants")
        self.backend.resize_client(self.handle, 900, 620)
        self.checkpoint("resize_preserves_parent_and_controls", extent=(900, 620))
        self.click(LIT_CHILD_OPEN)
        self.checkpoint("nested_child_opens_after_resize", child=1, raw_child=1, popup_count=2, child_parent_scope=1, focus_scope=2, focus_code=14, extent=(900, 620))
        self.native.tap(LIT_ESCAPE)
        self.checkpoint("escape_after_resize_restores_parent_trigger", child=0, raw_child=0, popup_count=1, child_parent_scope=0, focus_scope=1, focus_code=9, extent=(900, 620))


def run(args):
    return run_ui_fixture(args, fixture="NESTED_POPUP", display_marker="UiNestedPopupSmoke: display", driver_type=NestedPopupRun, log_name="nested_popup.log", label="UI nested popup",
        window_label="nested popup", process_label="nested popup", raw_windows=True, restore_pointer=True)


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
