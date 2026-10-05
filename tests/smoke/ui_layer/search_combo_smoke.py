#!/usr/bin/env python3
"""Exercise searchable combo query editing, stable selection, and cached result navigation."""
from __future__ import annotations

import json
import platform
import sys
import time

from search_combo_native import SearchComboNativeInput
from combo_probe_common import center
from edit_probe import text_hash
from search_combo_probe import SEARCH_COMBO_PROBE
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


class SearchComboRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = SearchComboNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.expected = {"selected": 0, "cursor": 0, "full_count": 100000, "count": 100000, "focused": 0,
            "reversed": 0, "removed": 0, "commits": 0, "underlying": 0, "open": 0, "enabled": 1,
            "source_revision": 1, "query_focused": 0, "composing": 0}
        self.query_expectation("")

    def query_expectation(self, value, anchor=None, caret=None):
        length = len(value.encode("utf-8"))
        self.expected.update({"query_bytes": length, "query_hash": text_hash(value) & 0xFFFFFF,
            "query_anchor": length if anchor is None else anchor, "query_caret": length if caret is None else caret})

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
            ensure_process_running(self.process, "during searchable combo UI capture")
            snapshot = SEARCH_COMBO_PROBE.snapshot(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 600.0 or snapshot["logical_extent"][1] < 430.0:
                raise SmokeSkip("the combo fixture needs a logical client of at least 600x430")
            self.backend.capture_client_window(self.handle, path)
            report = SEARCH_COMBO_PROBE.observe(read_bmp_24_rows(path), snapshot, self.expected,
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
        raise SmokeFailure(f"searchable combo displayed-state gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "input_path": "Win32 posted keys/pointer/positioned WM_MOUSEWHEEL messages"
                if self.native.windows else "X11 XSendEvent keys/button4/button5 and XSetInputFocus",
            "runtime_limit": "No live IME, native Wayland, physical grabs, or native Linux qualification."}
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
        self.native.tap("Escape")
        self.checkpoint("cancel_preserves_committed_selection_and_query", open=0, query_focused=0, dynamic=("cursor",))
        self.native.tap("space")
        self.checkpoint("reopen_retains_filtered_query", open=1, query_focused=1, dynamic=("cursor",))
        self.native.tap("Down")
        self.checkpoint("reopened_result_navigation", cursor=12)
        self.native.tap("Return")
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
        self.checkpoint("page_down_from_query_moves_results", dynamic=("cursor",),
            extra=lambda s: s["cursor"] > 12 and str(s["cursor"]).startswith("12"))
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
        self.checkpoint("source_removal_clears_committed_key", selected=0, full_count=99999, count=1110,
            removed=12999, source_revision=3, dynamic=("cursor",))
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
        self.checkpoint("query_commit_after_resize", selected=100000, cursor=100000, open=0, query_focused=0,
            commits=4, extent=(800, 600))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    environment.update({"NWB_UI_LAYER_SEARCH_COMBO": "1", "NWB_UI_LAYER_SEARCH_COMBO_SKIN": "1" if args.skin == "alternate" else "0",
        "NWB_UI_LAYER_COMBO": "0", "NWB_UI_LAYER_COMBO_SKIN": "0",
        "NWB_UI_LAYER_LIST": "0", "NWB_UI_LAYER_LIST_SKIN": "0",
        "NWB_UI_LAYER_POPUP": "0", "NWB_UI_LAYER_POPUP_SKIN": "0", "NWB_UI_LAYER_EDIT": "0",
        "NWB_UI_LAYER_INTERACTIVE": "0", "NWB_UI_LAYER_WINDOW": "0", "NWB_UI_LAYER_WINDOW_SKIN": "0"})
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = search_combo_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the combo fixture appeared")
            raise SmokeFailure("custom combo fixture did not appear")
        backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiSearchComboSmoke: display", min(args.timeout, 15.0))
        search_combo_run = SearchComboRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        search_combo_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI combo fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI combo fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "search_combo.log").write_text(text, encoding="utf-8")
            finally:
                if logserver is not None:
                    terminate_process(logserver, "UI combo logserver")
                backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    search_combo_run.write_report(True)
    print(f"UI searchable combo: {len(search_combo_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
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
