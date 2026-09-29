#!/usr/bin/env python3
"""Qualify skinned multiline plain-text editing through native input and displayed GPU state."""
from __future__ import annotations

import json
import math
import platform
import sys
import time

from text_area_native import TextAreaNativeInput
from text_area_probe import center, document_fields, observe_text_area, snapshot_from_logs, text_hash
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


INITIAL = "abcdef\nx\nabcdef\n한국어"
LONG_LINE = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl"
LONG_DOCUMENT = (LONG_LINE + "\n") * 16 + "tail"


class TextAreaRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args, self.backend, self.handle, self.process = args, backend, handle, process
        self.log_directory, self.log_baseline, self.log_pattern = log_directory, log_baseline, log_pattern
        self.native = TextAreaNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot, self.failure_report = None, None
        self.stages = []
        self.expected = {**document_fields(INITIAL, 5, 5), "focus": 0, "enabled": 1, "readonly": 0,
            "compact": 0, "long_document": 0, "preferred_valid": 0, "preferred_bits": 0, "submits": 0,
            "cancels": 0, "outside": 0, "clipboard_seeds": 0, "clipboard_pending": 0, "coherent": 1}

    def point(self, x, y):
        sx, sy = self.snapshot["scale"]
        return round(x * sx), round(y * sy)

    def controller(self, name):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][name])))

    def focus(self, stage):
        x, y, width, height = self.snapshot["rectangles"]["caret"]
        self.native.click(*self.point(x + width * 0.5, y + height * 0.5))
        self.checkpoint(stage, focus=1, anchor=self.snapshot["caret"], preferred_valid=0, preferred_bits=0)

    def checkpoint(self, name, *, text=None, anchor=None, caret=None, extent=None, extra=None,
        minimum_selection_lines=0, settle=0.25, **changes):
        if text is not None:
            if anchor is None or caret is None:
                raise ValueError("text checkpoints require exact UTF-8 anchor and caret positions")
            self.expected.update(document_fields(text, anchor, caret))
        else:
            if anchor is not None:
                self.expected["anchor"] = anchor
            if caret is not None:
                self.expected["caret"] = caret
        if changes.get("preferred_valid") == 1 and not self.expected.get("preferred_valid"):
            self.expected.pop("preferred_bits", None)
        self.expected.update(changes)
        time.sleep(settle)
        path = self.args.output_directory / f"{len(self.stages):02}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during TextArea capture")
            self.native.maintain_pointer()
            text = collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)
            snapshot = snapshot_from_logs(text)
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 640.0 or snapshot["logical_extent"][1] < 440.0:
                raise SmokeSkip("the TextArea fixture needs a logical client of at least 640x440")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_text_area(read_bmp_24_rows(path), snapshot, self.expected, skin=self.args.skin,
                extent=extent, extra=extra, minimum_selection_lines=minimum_selection_lines)
            if report["passed"]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path), "native_window": self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        self.failure_report = {"stage": name, "capture": str(path), "observation": report,
            "native_window": self.native.observe_window()}
        self.write_report(False)
        raise SmokeFailure(f"TextArea displayed-state gate '{name}' failed: {self.failure_report}")

    def require_clipboard(self, text):
        expected = text.replace("\n", "\r\n") if self.native.windows else text
        deadline, observed = min(self.deadline, time.monotonic() + 5.0), None
        while time.monotonic() < deadline:
            ensure_process_running(self.process, "during TextArea OS clipboard verification")
            observed = self.native.clipboard_text(timeout=min(2.0, max(0.1, deadline - time.monotonic())))
            if observed == expected:
                self.stages[-1]["os_clipboard"] = {"expected": expected, "observed": observed, "passed": True}
                self.write_report(False)
                return
            time.sleep(0.05)
        self.failure_report = {"stage": self.stages[-1]["stage"], "os_clipboard": {"expected": expected,
            "observed_bytes": len(observed.encode("utf-8")) if observed is not None else None,
            "observed_hash": text_hash(observed) if observed is not None else None, "passed": False}}
        self.write_report(False)
        raise SmokeFailure(f"TextArea OS clipboard gate failed: {self.failure_report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "failure": self.failure_report, "marker_count_per_gate": 53,
            "input_path": "Win32 maintained cursor, posted text/keys/buttons, modifier chords, CF_UNICODETEXT reads"
                if self.native.windows else "X11 synthetic ASCII keys/buttons, native focus, UTF8_STRING selection reads",
            "ordered_batch_scope": "Text then Down then text has no displayed-state wait; native delivery may cross frames.",
            "runtime_limit": "No fake preedit injection, live IME, native Wayland, or physical keyboard/grab qualification."}
        (self.args.output_directory / "text_area.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.native.pointer(4, 4)
        self.checkpoint("initial_multilingual_document")
        self.focus("focus_precedes_vertical_navigation")
        self.native.tap("Down")
        self.checkpoint("short_line_preserves_preferred_column", anchor=8, caret=8, preferred_valid=1)
        column = self.snapshot["preferred_bits"]
        self.expected["preferred_bits"] = column
        self.native.tap("Down")
        self.checkpoint("next_long_line_restores_column", anchor=14, caret=14)
        self.native.tap("Up")
        self.native.tap("Up")
        self.checkpoint("up_returns_to_original_column", anchor=5, caret=5)
        self.native.tap("Down", shift=True)
        self.native.tap("Down", shift=True)
        self.checkpoint("shift_down_paints_multiple_selection_lines", anchor=5, caret=14, minimum_selection_lines=2)
        self.native.tap("c", control=True)
        self.checkpoint("cross_line_copy_preserves_selection", minimum_selection_lines=2)
        self.require_clipboard("f\nx\nabcde")
        self.native.tap("x", control=True)
        self.checkpoint("cross_line_cut_is_one_completed_edit", text="abcdef\n한국어", anchor=5, caret=5,
            preferred_valid=0, preferred_bits=0)
        self.require_clipboard("f\nx\nabcde")
        self.native.tap("z", control=True)
        self.checkpoint("one_undo_restores_cross_line_cut_and_selection", text=INITIAL, anchor=5, caret=14,
            minimum_selection_lines=2, preferred_valid=0, preferred_bits=0)
        self.controller("clipboard")
        seeds = self.snapshot["clipboard_seeds"] + 1
        self.checkpoint("real_clipboard_seed_is_complete", focus=0, clipboard_seeds=seeds, preferred_valid=0, preferred_bits=0)
        self.require_clipboard("ab\r\ncdef\r\nxy" if not self.native.windows else "ab\ncdef\nxy")
        self.focus("focus_precedes_crlf_paste")
        self.native.tap("a", control=True)
        self.native.tap("v", control=True)
        canonical = "ab\ncdef\nxy"
        self.checkpoint("os_paste_admits_canonical_lf", text=canonical, anchor=10, caret=10, preferred_valid=0, preferred_bits=0)
        self.native.tap("z", control=True)
        self.checkpoint("one_undo_restores_multiline_paste", text=INITIAL, anchor=0, caret=len(INITIAL.encode("utf-8")),
            minimum_selection_lines=3)
        self.native.tap("y", control=True)
        self.checkpoint("redo_restores_canonical_lf", text=canonical, anchor=10, caret=10)
        self.native.tap("Return")
        self.checkpoint("enter_inserts_trailing_hard_line", text=canonical + "\n", anchor=11, caret=11)
        self.native.tap("Return", shift=True)
        self.checkpoint("shift_enter_inserts_another_lf", text=canonical + "\n\n", anchor=12, caret=12)
        submits = self.snapshot["submits"] + 1
        self.native.tap("Return", control=True)
        self.checkpoint("ctrl_enter_submits_without_rewrite", submits=submits)
        self.controller("outside")
        self.checkpoint("ordinary_blur_preserves_plain_text", focus=0, outside=self.snapshot["outside"] + 1,
            extra=lambda state: state["blurs"] > self.snapshot["blurs"])
        self.controller("reset")
        self.checkpoint("reset_replaces_document_before_loan", text=INITIAL, anchor=5, caret=5, preferred_valid=0,
            preferred_bits=0, long_document=0, compact=0)
        self.focus("fresh_focus_precedes_ordered_batch")
        self.native.text_down_text("q", "z")
        ordered = "abcdeqf\nxz\nabcdef\n한국어"
        self.checkpoint("text_down_text_keeps_event_order", text=ordered, anchor=10, caret=10,
            preferred_valid=0, preferred_bits=0)
        cancels = self.snapshot["cancels"] + 1
        self.native.tap("Escape")
        self.checkpoint("escape_preserves_plain_draft_and_retires_focus", focus=0, cancels=cancels)
        self.controller("reset")
        self.checkpoint("reset_precedes_readonly", text=INITIAL, anchor=5, caret=5)
        self.controller("readonly")
        self.checkpoint("readonly_policy_is_displayed", readonly=1, focus=0)
        self.focus("readonly_focus_precedes_selection")
        self.native.tap("a", control=True)
        self.native.tap("c", control=True)
        self.native.text("z")
        self.native.tap("Return")
        self.native.tap("x", control=True)
        self.native.tap("v", control=True)
        self.checkpoint("readonly_allows_copy_and_blocks_mutation", anchor=0, caret=len(INITIAL.encode("utf-8")),
            minimum_selection_lines=3)
        self.require_clipboard(INITIAL)
        self.native.tap("Home", control=True)
        self.native.tap("Down", shift=True)
        self.checkpoint("readonly_vertical_navigation_still_extends", anchor=0, caret=7, preferred_valid=1)
        self.controller("readonly")
        self.checkpoint("editable_policy_preserves_plain_selection", readonly=0, focus=0, preferred_valid=0, preferred_bits=0)
        self.controller("enabled")
        self.checkpoint("disabled_policy_is_displayed", enabled=0, focus=0)
        self.native.click(*self.point(*center(self.snapshot["rectangles"]["bounds"])))
        self.native.text("z")
        self.native.tap("Down")
        self.native.tap("Return")
        self.checkpoint("disable_blocks_text_navigation_and_submit", enabled=0, focus=0)
        self.controller("enabled")
        self.checkpoint("enable_requires_fresh_focus", enabled=1, focus=0)
        self.controller("long")
        self.checkpoint("long_document_reveals_caret_on_both_axes", text=LONG_DOCUMENT, anchor=1039, caret=1039,
            long_document=1, extra=lambda state: state["scroll"][0] > 0.0 and state["scroll"][1] > 0.0)
        self.controller("viewport")
        self.checkpoint("smaller_viewport_reveals_caret", compact=1,
            extra=lambda state: state["scroll"][0] > 0.0 and state["scroll"][1] > 0.0)
        self.focus("small_viewport_focus_precedes_home")
        self.native.tap("Home", control=True)
        self.checkpoint("document_home_restores_top_left", anchor=0, caret=0, preferred_valid=0, preferred_bits=0,
            extra=lambda state: state["scroll"] == (0.0, 0.0))
        page = min(16, math.floor(0.5 + self.snapshot["rectangles"]["content"][3] / self.snapshot["line_height"]))
        self.native.tap("Page_Down", shift=True)
        self.checkpoint("page_down_uses_accepted_content_height", anchor=0, caret=page * 65, preferred_valid=1,
            minimum_selection_lines=2)
        self.native.tap("Page_Up")
        self.checkpoint("page_up_returns_to_document_start", anchor=0, caret=0)
        self.native.tap("End", control=True)
        self.checkpoint("document_end_reveals_trailing_line", anchor=len(LONG_DOCUMENT), caret=len(LONG_DOCUMENT),
            preferred_valid=0, preferred_bits=0, extra=lambda state: state["scroll"][1] > 0.0)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("client_resize_publishes_matching_geometry", focus=0, preferred_valid=0, preferred_bits=0,
            extent=(800, 600))
        self.focus("fresh_focus_after_resize_precedes_native_focus_loss")
        self.native.focus_loss_and_gain()
        self.native.text("z")
        self.checkpoint("native_focus_loss_preserves_plain_draft", focus=0, extent=(800, 600))
        self.controller("reset")
        self.checkpoint("reset_after_resize_has_fresh_plain_model", text=INITIAL, anchor=5, caret=5, compact=0,
            long_document=0, extent=(800, 600))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    for suffix in ("WINDOW", "POPUP", "POPUP_TOOLS", "NESTED_POPUP", "NUMERIC_EDIT", "LIST", "COMBO", "SEARCH_COMBO"):
        environment["NWB_UI_LAYER_" + suffix] = "0"
        environment["NWB_UI_LAYER_" + suffix + "_SKIN"] = "0"
    environment.update({"NWB_UI_LAYER_TEXT_AREA": "1", "NWB_UI_LAYER_TEXT_AREA_SKIN": "1" if args.skin == "alternate" else "0",
        "NWB_UI_LAYER_EDIT": "0", "NWB_UI_LAYER_INTERACTIVE": "0"})
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = text_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the TextArea fixture appeared")
            raise SmokeFailure("custom TextArea fixture did not appear")
        if platform.system() == "Windows":
            backend.prepare_raw_client_window(handle)
        else:
            backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiTextAreaSmoke: display", min(args.timeout, 15.0))
        text_run = TextAreaRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        text_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI TextArea fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI TextArea fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "text_area.log").write_text(text, encoding="utf-8")
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI TextArea logserver")
                finally:
                    try:
                        if text_run is not None:
                            text_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    text_run.write_report(True)
    print(f"UI TextArea: {len(text_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
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
