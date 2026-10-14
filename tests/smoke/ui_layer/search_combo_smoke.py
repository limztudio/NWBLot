#!/usr/bin/env python3
"""Exercise searchable combo query editing, stable selection, and cached result navigation."""
from __future__ import annotations

import json
import platform
import sys

from search_combo_native import SearchComboNativeInput
from edit_probe import text_hash
from search_combo_probe import SEARCH_COMBO_PROBE
from window_smoke import parse_args
from window_capture_smoke import run_smoke_main
from keyed_run import KeyedRun, run_keyed_fixture


class SearchComboRun(KeyedRun):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native_type=SearchComboNativeInput, snapshot_reader=SEARCH_COMBO_PROBE.snapshot, observer=SEARCH_COMBO_PROBE.observe,
            minimum_height=430, fixture="searchable combo", capture_stage="during searchable combo UI capture")
        self.expected = {"selected": 0, "cursor": 0, "full_count": 100000, "count": 100000, "focused": 0, "reversed": 0, "removed": 0, "commits": 0, "underlying": 0, "open": 0, "enabled": 1, "source_revision": 1, "query_focused": 0, "composing": 0}
        self.query_expectation("")

    def query_expectation(self, value, anchor=None, caret=None):
        length = len(value.encode("utf-8"))
        self.expected.update({"query_bytes": length, "query_hash": text_hash(value) & 0xFFFFFF, "query_anchor": length if anchor is None else anchor, "query_caret": length if caret is None else caret})


    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages, "input_path": "Win32 posted keys/pointer/positioned WM_MOUSEWHEEL messages"
                if self.native.windows else "X11 XSendEvent keys/button4/button5 and XSetInputFocus", "runtime_limit": "No live IME, native Wayland, physical grabs, or native Linux qualification."}
        (self.args.output_directory / "search_combo.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial_closed_search_combo")
        self.native.tap("F4")
        self.checkpoint("external_committed_selection", selected=100000, dynamic=("cursor",))
        self.expected.pop("focused")
        self.click("trigger")
        self.checkpoint("open_focuses_query", open=1, query_focused=1, cursor=100000)
        self.native.text("12")
        self.query_expectation("12")
        self.checkpoint("query_filters_without_clearing_full_selection", count=1111, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("typing_down_previews_first_result", cursor=12)
        self.native.tap("Down")
        self.checkpoint("typing_down_previews_next_result", cursor=120)
        self.native.key("Return", True)
        self.checkpoint("query_enter_press_keeps_preview_uncommitted")
        self.native.tap("Escape")
        self.checkpoint("cancel_preserves_committed_selection_and_query", open=0, query_focused=0, dynamic=("cursor",))
        self.native.key("Return", True, repeat=True)
        self.native.key("Return", False)
        self.checkpoint("cancelled_query_enter_release_cannot_commit")
        self.native.tap("space")
        self.checkpoint("reopen_retains_filtered_query", open=1, query_focused=1, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("reopened_result_navigation", cursor=12)
        self.native.key("Return", True)
        self.checkpoint("fresh_query_enter_press_waits_for_release")
        self.native.key("Return", True, repeat=True)
        self.native.key("Return", False)
        self.checkpoint("query_enter_commits_preview", selected=12, cursor=12, open=0, query_focused=0, commits=1)
        self.native.tap("space")
        self.checkpoint("open_for_disabled_result_query", open=1, query_focused=1)
        self.native.replace_query("5")
        self.query_expectation("5")
        self.checkpoint("query_exclusion_keeps_committed_key", count=11111, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("result_navigation_skips_disabled_key5", cursor=50)
        self.native.tap("Return")
        self.checkpoint("commit_enabled_filtered_key", selected=50, cursor=50, open=0, query_focused=0, commits=2)
        self.native.tap("space")
        self.checkpoint("open_for_no_results", open=1, query_focused=1)
        self.native.replace_query("999999")
        self.query_expectation("999999")
        self.checkpoint("no_results_keeps_full_selection", count=0, cursor=0)
        self.native.tap("Return")
        self.checkpoint("empty_query_results_cannot_commit")
        self.native.tap("Escape")
        self.checkpoint("cancel_no_results", open=0, query_focused=0, dynamic=("cursor",))
        self.native.tap("space")
        self.checkpoint("reopen_preserves_no_result_query", open=1, query_focused=1, cursor=0)
        self.native.replace_query("12")
        self.query_expectation("12")
        self.checkpoint("query_restores_cached_results", count=1111, dynamic=("cursor",))
        self.native.tap("Home")
        self.query_expectation("12", 0, 0)
        self.checkpoint("home_moves_query_caret_without_list_navigation")
        self.native.tap("End")
        self.query_expectation("12")
        self.checkpoint("end_moves_query_caret_without_list_navigation")
        self.native.tap("a", control=True)
        self.native.tap("c", control=True)
        self.query_expectation("12", 0, 2)
        self.checkpoint("query_selection_copies_through_os_clipboard")
        self.native.text("5")
        self.query_expectation("5")
        self.checkpoint("typing_replaces_query_selection", count=11111, dynamic=("cursor",))
        self.native.tap("a", control=True)
        self.native.tap("v", control=True)
        self.query_expectation("12")
        self.checkpoint("query_paste_uses_os_clipboard", count=1111, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("query_navigation_after_paste", cursor=12)
        self.native.tap("Page_Down")
        self.checkpoint("page_down_from_query_moves_results", dynamic=("cursor",), extra=lambda s: s["cursor"] > 12 and str(s["cursor"]).startswith("12"))
        self.native.tap("Page_Up")
        self.checkpoint("page_up_from_query_restores_result", cursor=12)
        self.native.tap("F5")
        self.checkpoint("source_reorder_preserves_committed_key", reversed=1, source_revision=2, selected=50, cursor=0, first=0)
        self.expected.pop("first")
        self.native.tap("Tab")
        self.checkpoint("tab_focuses_filtered_results", query_focused=0)
        self.native.tap("Home")
        self.checkpoint("home_on_results_reaches_reversed_first", cursor=12999)
        self.native.tap("Return")
        self.checkpoint("result_enter_commits_full_source_key", selected=12999, cursor=12999, open=0, commits=3)
        self.native.tap("F6")
        self.checkpoint("source_removal_clears_committed_key", selected=0, full_count=99999, count=1110, removed=12999, source_revision=3, dynamic=("cursor",))
        self.native.tap("F7")
        self.query_expectation("")
        self.checkpoint("external_query_clear_refreshes_view", count=99999, dynamic=("cursor",))
        self.native.tap("space")
        self.checkpoint("open_after_external_query_clear", open=1, query_focused=1, dynamic=("cursor",))
        self.native.tap("Escape")
        self.checkpoint("escape_closes_query_popup", open=0, query_focused=0, dynamic=("cursor",))
        self.native.tap("F9")
        self.click("trigger")
        self.checkpoint("disabled_search_combo_blocks_open", enabled=0)
        self.native.tap("F9")
        self.checkpoint("enable_restores_search_combo", enabled=1)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_query_and_selection", extent=(800, 600))
        self.click("trigger")
        self.checkpoint("open_after_resize_focuses_query", open=1, query_focused=1, extent=(800, 600), dynamic=("cursor",))
        self.native.text("100000")
        self.query_expectation("100000")
        self.checkpoint("exact_query_has_one_cached_result", count=1, extent=(800, 600), dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("exact_result_navigation", cursor=100000, extent=(800, 600))
        self.native.tap("Return")
        self.checkpoint("query_commit_after_resize", selected=100000, cursor=100000, open=0, query_focused=0, commits=4, extent=(800, 600))


def run(args):
    return run_keyed_fixture(args, fixture="SEARCH_COMBO", display_marker="UiSearchComboSmoke: display", driver_type=SearchComboRun, log_name="search_combo.log", label="UI searchable combo", window_label="combo", process_label="combo")


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
