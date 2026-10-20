#!/usr/bin/env python3
"""Qualify numeric draft/value separation through OS input and displayed GPU state."""
from __future__ import annotations

import json
import platform
import sys
import time

from numeric_edit_native import NumericEditNativeInput
from numeric_edit_probe import center, draft_fields, float_bits, integer_bits, observe_numeric_edit, snapshot_from_logs, text_hash
from edit_probe import caret_pixel_column
from caret_capture import wait_for_caret_phase
from window_smoke import parse_args
from window_capture_smoke import (run_smoke_main, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, wait_for_log_message,)
from fixture_environment import build_fixture_environment
from checkpoint_report import accept_checkpoint, fail_checkpoint, prepare_guarded_checkpoint
from probe_reference import linear_rgb_bytes

LIT_ENABLED = "enabled"
LIT_READONLY = "readonly"
LIT_I = "i"
LIT_F = "f"
LIT_CLIPBOARD = "clipboard"
LIT_SCALE = "scale"
LIT_RECTANGLES = "rectangles"
LIT_INTEGER = "integer"
LIT_FLOAT = "float"
LIT_UTF_8 = "utf-8"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_PASSED = "passed"
LIT_OS_CLIPBOARD = "os_clipboard"
LIT_EXPECTED = "expected"
LIT_STAGE = "stage"
LIT_RETURN = "Return"
LIT_ESCAPE = "Escape"
LIT_A = "a"
LIT_C = "c"
LIT_V = "v"
LIT_N_12X = "12x"
LIT_X = "x"
LIT_TAB = "Tab"
LIT_N_2_5E = "2.5e-"
LIT_N_2_5E_1 = "2.5e-1"
LIT_Z = "z"
LIT_RESTORED = "restored"
LIT_N_1E309 = "1e309"
LIT_F5 = "F5"
LIT_F6 = "F6"
LIT_F7 = "F7"
LIT_INTEGER_CARET = "integer_caret"
LIT_INTEGER_CONTENT = "integer_content"
LIT_UI_NUMERIC_EDITOR_FIXTURE = "UI numeric editor fixture"


COUNTERS = ("commits", "cancels", "rejects", "clamps", "restored")


class NumericEditRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = NumericEditNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None
        self.expected = {"integer_bits": integer_bits(7), "float_bits": float_bits(1.25), "i_focus": 0, "f_focus": 0, "clipboard_focus": 0, LIT_ENABLED: 1, LIT_READONLY: 0, "clamp": 1, "coherent": 1,
            **draft_fields(LIT_I, "7", anchor=1, caret=1, status=0, dirty=False), **draft_fields(LIT_F, "1.25", anchor=4, caret=4, status=0, dirty=False), **draft_fields(LIT_CLIPBOARD, "", anchor=0, caret=0)}

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot[LIT_SCALE]
        return round(x * scale_x), round(y * scale_y)

    def counters(self, **increments):
        return {name: self.snapshot[name] + count for name, count in increments.items()}

    def focus(self, field, name, **changes):
        self.native.click(*self.point(*center(self.snapshot[LIT_RECTANGLES][f"{field}_bounds"])))
        self.checkpoint(name, i_focus=int(field == LIT_INTEGER), f_focus=int(field == LIT_FLOAT), clipboard_focus=int(field == LIT_CLIPBOARD), **changes)

    def draft_checkpoint(self, name, prefix, text, *, status=None, dirty=None, anchor=None, caret=None, **changes):
        end = len(text.encode(LIT_UTF_8))
        self.checkpoint(name, **draft_fields(prefix, text, anchor=end if anchor is None else anchor, caret=end if caret is None else caret, status=status, dirty=dirty), **changes)

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), settle=0.25, **changes):
        path, stage_deadline = prepare_guarded_checkpoint(self, name, changes, dynamic, settle)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during numeric edit UI capture")
            self.native.maintain_pointer()
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot[LIT_LOGICAL_EXTENT][0] < 640.0 or snapshot[LIT_LOGICAL_EXTENT][1] < 400.0:
                raise SmokeSkip("the numeric editor fixture needs a logical client of at least 640x400")
            if report is not None and report["model_matches"] and report["geometry_matches"] and report["extra_matches"]:
                if (extent is None or tuple(report["extent"]) == extent) and all(probe[LIT_PASSED] or probe["name"].endswith("_caret") for probe in report["probes"]):
                    # Synchronize a caret-only retry; an already visible initial capture needs no extra blink cycle.
                    sample = self._caret_probe_sample(snapshot)
                    if sample is not None:
                        column, first_row, end_row, color, tolerance = sample
                        pixels = self.backend.sample_client_pixels(self.handle, column, first_row, 1, end_row - first_row)
                        if not any(max(abs(actual - expected) for actual, expected in zip(row[0], color)) <= tolerance for row in pixels):
                            wait_for_caret_phase(self.backend, self.handle, self.process, [sample], stage_deadline)
            self.backend.capture_prepared_raw_client_window(self.handle, path)
            report = observe_numeric_edit(read_bmp_24_rows(path), snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra)
            if report[LIT_PASSED]:
                accept_checkpoint(self, name, path, snapshot, report, dynamic)
                return
            time.sleep(0.1)
        fail_checkpoint(self, name, path, report, "numeric edit")

    def _caret_probe_sample(self, snapshot):
        width, height = self.backend.client_size(self.handle)
        scale_x, scale_y = snapshot[LIT_SCALE]
        color = linear_rgb_bytes((0.95, 0.97, 1.0))
        for field, prefix in ((LIT_INTEGER, LIT_I), (LIT_FLOAT, LIT_F), (LIT_CLIPBOARD, LIT_CLIPBOARD)):
            if not snapshot.get(f"{prefix}_focus") or snapshot.get(f"{prefix}_anchor") != snapshot.get(f"{prefix}_caret"):
                continue
            if field != LIT_CLIPBOARD and (not snapshot.get(LIT_ENABLED) or snapshot.get(LIT_READONLY)):
                continue
            kx, ky, kw, kh = snapshot[LIT_RECTANGLES][f"{field}_caret"]
            cx, cy, cw, ch = snapshot[LIT_RECTANGLES][f"{field}_content"]
            if not (cx - 0.75 <= kx and kx + kw <= cx + cw + 0.75 and cy - 0.75 <= ky and ky + kh <= cy + ch + 0.75):
                continue
            column, row = caret_pixel_column(kx, scale_x), round((min(ky + kh, cy + ch) - 0.75) * scale_y)
            if 0 <= column < width and 0 <= row < height:
                return (column, max(0, row - 1), min(height, row + 2), color, 5)
        return None

    def require_clipboard(self, text):
        deadline, observed = min(self.deadline, time.monotonic() + 5.0), None
        while time.monotonic() < deadline:
            ensure_process_running(self.process, "during numeric OS clipboard verification")
            observed = self.native.clipboard_text(timeout=min(2.0, max(0.1, deadline - time.monotonic())))
            if observed == text:
                self.stages[-1][LIT_OS_CLIPBOARD] = {LIT_EXPECTED: text, "observed": observed, LIT_PASSED: True}
                self.write_report(False)
                return
            time.sleep(0.05)
        self.failure_report = {LIT_STAGE: self.stages[-1][LIT_STAGE], LIT_OS_CLIPBOARD: {LIT_EXPECTED: text, "observed_bytes": len(observed.encode(LIT_UTF_8)) if observed is not None else None,
            "observed_hash": text_hash(observed) if observed is not None else None, LIT_PASSED: False}}
        self.write_report(False)
        raise SmokeFailure(f"numeric copy did not publish the expected OS clipboard text: {self.failure_report}")

    def write_report(self, passed):
        report = {LIT_PASSED: passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages, "failure": self.failure_report, "marker_count_per_gate": 73,
            "input_path": "Win32 maintained cursor, posted WM_CHAR/keys/buttons, and real modifier chords; CF_UNICODETEXT read" if self.native.windows else "X11 XSendEvent ASCII keys/buttons, native focus, and UTF8_STRING selection read",
            "ordered_batch_scope": "Enter then text is posted without an intermediate displayed-state wait; delivery may cross frames.", "runtime_limit": "Synthetic input does not qualify live IME, native Wayland, or physical keyboard hardware."}
        (self.args.output_directory / "numeric_edit.json").write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)

    def execute(self):
        self.native.pointer(4, 4)
        self.checkpoint("initial_typed_values_and_drafts", dynamic=COUNTERS)
        self.focus(LIT_INTEGER, "integer_focus_is_displayed_before_typing")
        self.native.replace_text("-")
        self.draft_checkpoint("integer_incomplete_sign_keeps_typed_value", LIT_I, "-", status=1, dirty=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("integer_incomplete_submit_rejects_without_rewrite", **self.counters(rejects=1))
        self.native.tap(LIT_ESCAPE)
        self.draft_checkpoint("integer_escape_restores_committed_text", LIT_I, "7", status=0, dirty=False, i_focus=0, **self.counters(cancels=1, restored=1))
        self.focus(LIT_INTEGER, "integer_refocus_after_escape_precedes_typing")
        exact_integer = "9007199254740993"
        self.native.replace_text(exact_integer)
        self.draft_checkpoint("integer_above_double_precision_remains_a_draft", LIT_I, exact_integer, status=0, dirty=True)
        self.native.key(LIT_RETURN, True)
        self.checkpoint("numeric_submit_press_keeps_typed_value_uncommitted")
        self.native.tap(LIT_ESCAPE)
        self.draft_checkpoint("escape_cancels_held_numeric_submit", LIT_I, "7", status=0, dirty=False, i_focus=0, **self.counters(cancels=1, restored=1))
        self.native.key(LIT_RETURN, True, repeat=True)
        self.native.key(LIT_RETURN, False)
        self.checkpoint("cancelled_numeric_submit_release_cannot_commit")
        self.focus(LIT_INTEGER, "numeric_refocus_after_cancel_requires_fresh_submit")
        self.native.replace_text(exact_integer)
        self.draft_checkpoint("fresh_numeric_draft_after_cancel", LIT_I, exact_integer, status=0, dirty=True)
        self.native.key(LIT_RETURN, True)
        self.checkpoint("fresh_numeric_submit_press_waits_for_release")
        self.native.key(LIT_RETURN, False)
        self.checkpoint("integer_submit_preserves_exact_64_bit_value", integer_bits=integer_bits(int(exact_integer)), i_dirty=0, **self.counters(commits=1))
        self.native.tap(LIT_A, control=True)
        self.checkpoint("integer_select_all_has_displayed_selection", i_anchor=0, i_caret=len(exact_integer))
        self.native.tap(LIT_C, control=True)
        self.checkpoint("integer_copy_keeps_typed_value_and_selection")
        self.require_clipboard(exact_integer)
        self.focus(LIT_CLIPBOARD, "clipboard_destination_focus_precedes_paste", **draft_fields(LIT_I, exact_integer, anchor=len(exact_integer), caret=len(exact_integer)), **self.counters(commits=1))
        self.native.tap(LIT_V, control=True)
        self.draft_checkpoint("exact_integer_pastes_through_the_os_service", LIT_CLIPBOARD, exact_integer)
        self.native.replace_text(LIT_N_12X)
        self.draft_checkpoint("invalid_numeric_clipboard_source", LIT_CLIPBOARD, LIT_N_12X)
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_C, control=True)
        self.checkpoint("invalid_source_copy_is_visible", clipboard_anchor=0, clipboard_caret=3)
        self.require_clipboard(LIT_N_12X)
        self.focus(LIT_INTEGER, "integer_refocus_precedes_invalid_paste")
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_V, control=True)
        self.draft_checkpoint("invalid_os_paste_changes_only_the_draft", LIT_I, LIT_N_12X, status=2, dirty=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("invalid_submit_retains_draft_and_rejects", **self.counters(rejects=1))
        self.focus(LIT_CLIPBOARD, "invalid_blur_restores_the_exact_committed_integer", **draft_fields(LIT_I, exact_integer, anchor=len(exact_integer), caret=len(exact_integer), status=0, dirty=False),
            clipboard_anchor=3, clipboard_caret=3, **self.counters(rejects=1, restored=1))
        self.focus(LIT_INTEGER, "integer_refocus_precedes_cut")
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_X, control=True)
        self.draft_checkpoint("cut_publishes_os_text_and_leaves_incomplete_draft", LIT_I, "", status=1, dirty=True)
        self.require_clipboard(exact_integer)
        self.native.tap(LIT_V, control=True)
        self.draft_checkpoint("paste_after_cut_restores_the_accepted_draft", LIT_I, exact_integer, status=0, dirty=False)
        self.native.replace_text("9223372036854775808")
        self.draft_checkpoint("integer_representation_overflow_keeps_typed_value", LIT_I, "9223372036854775808", status=3, dirty=True)
        self.native.tap(LIT_TAB)
        self.checkpoint("tab_rejects_overflow_and_focuses_float", i_focus=0, f_focus=1, **draft_fields(LIT_I, exact_integer, anchor=len(exact_integer), caret=len(exact_integer), status=0, dirty=False), **self.counters(rejects=1, restored=1))
        self.native.replace_text("-")
        self.draft_checkpoint("float_incomplete_sign_keeps_typed_value", LIT_F, "-", status=1, dirty=True)
        self.native.tap(LIT_ESCAPE)
        self.draft_checkpoint("float_escape_restores_committed_decimal", LIT_F, "1.25", status=0, dirty=False, f_focus=0, **self.counters(cancels=1, restored=1))
        self.focus(LIT_FLOAT, "float_refocus_after_escape_precedes_exponent")
        self.native.replace_text(LIT_N_2_5E)
        self.draft_checkpoint("float_incomplete_exponent_is_preserved", LIT_F, LIT_N_2_5E, status=1, dirty=True)
        self.native.text("1")
        self.draft_checkpoint("float_exponent_completion_does_not_commit", LIT_F, LIT_N_2_5E_1, status=0, dirty=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("float_submit_retains_lexical_exponent", float_bits=float_bits(0.25), f_dirty=0, **self.counters(commits=1))
        self.native.tap(LIT_Z, control=True)
        self.draft_checkpoint("undo_after_submit_changes_draft_only", LIT_F, LIT_N_2_5E, status=1, dirty=True)
        self.native.tap("y", control=True)
        self.draft_checkpoint("redo_after_submit_restores_accepted_lexical_text", LIT_F, LIT_N_2_5E_1, status=0, dirty=False)
        self.native.tap(LIT_TAB)
        self.checkpoint("float_blur_commits_and_canonicalizes", f_focus=0, clipboard_focus=1, **draft_fields(LIT_F, "0.25", anchor=4, caret=4, status=0, dirty=False), **self.counters(commits=1))
        self.focus(LIT_FLOAT, "float_refocus_precedes_signed_zero")
        self.native.replace_text("-0")
        self.draft_checkpoint("signed_zero_is_a_complete_draft", LIT_F, "-0", status=0, dirty=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("signed_zero_commit_preserves_the_sign_bit", float_bits=float_bits(-0.0), f_dirty=0, **self.counters(commits=1))
        self.native.replace_text("+1.5E+1")
        self.draft_checkpoint("uppercase_exponent_and_plus_are_outside_user_bounds", LIT_F, "+1.5E+1", status=3, dirty=True)
        self.native.tap(LIT_RETURN)
        self.draft_checkpoint("clamp_commits_a_canonical_bound", LIT_F, "10", status=0, dirty=False, float_bits=float_bits(10.0), **self.counters(commits=1, clamps=1))
        self.native.tap("F8")
        self.checkpoint("reject_policy_applies_to_later_numeric_actions", clamp=0, dynamic=(LIT_RESTORED,))
        self.native.replace_text("11")
        self.draft_checkpoint("reject_policy_keeps_out_of_range_draft", LIT_F, "11", status=3, dirty=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("reject_policy_submit_preserves_committed_value", **self.counters(rejects=1))
        self.native.tap(LIT_TAB)
        self.checkpoint("reject_policy_blur_restores_the_committed_bound", f_focus=0, clipboard_focus=1, **draft_fields(LIT_F, "10", anchor=2, caret=2, status=0, dirty=False), **self.counters(rejects=1, restored=1))
        self.focus(LIT_FLOAT, "float_refocus_precedes_representation_overflow")
        self.native.replace_text(LIT_N_1E309)
        self.draft_checkpoint("float_representation_overflow_is_distinct_from_bounds", LIT_F, LIT_N_1E309, status=3, dirty=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("representation_overflow_rejects_without_clamping", **self.counters(rejects=1))
        self.native.tap(LIT_ESCAPE)
        self.draft_checkpoint("escape_restores_after_float_representation_overflow", LIT_F, "10", status=0, dirty=False, f_focus=0, **self.counters(cancels=1, restored=1))
        self.focus(LIT_FLOAT, "float_refocus_after_overflow_escape_precedes_readonly")
        self.native.replace_text("3")
        self.draft_checkpoint("dirty_draft_precedes_readonly_policy_change", LIT_F, "3", status=0, dirty=True)
        self.native.tap(LIT_F5)
        self.draft_checkpoint("readonly_transition_restores_and_keeps_focus", LIT_F, "10", status=0, dirty=False, readonly=1, dynamic=(LIT_RESTORED,))
        self.native.tap(LIT_A, control=True)
        self.native.tap(LIT_C, control=True)
        self.native.text("9")
        self.native.tap(LIT_X, control=True)
        self.native.tap(LIT_V, control=True)
        self.native.tap(LIT_Z, control=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("readonly_allows_copy_and_blocks_mutation_and_commit", f_anchor=0, f_caret=2)
        self.require_clipboard("10")
        self.native.tap(LIT_F5)
        self.checkpoint("editable_policy_restores_a_fresh_selection_epoch", readonly=0, f_anchor=2, f_caret=2, dynamic=(LIT_RESTORED,))
        self.native.replace_text("-4")
        self.draft_checkpoint("dirty_draft_precedes_disable", LIT_F, "-4", status=0, dirty=True)
        self.native.tap(LIT_F6)
        self.native.text("9")
        self.native.tap(LIT_RETURN)
        self.draft_checkpoint("disable_restores_and_blocks_native_text", LIT_F, "10", status=0, dirty=False, enabled=0, f_focus=0, dynamic=(LIT_RESTORED,))
        self.native.tap(LIT_F6)
        self.checkpoint("enable_requires_a_fresh_numeric_focus", enabled=1)
        self.focus(LIT_INTEGER, "integer_focus_precedes_external_replacement")
        self.native.replace_text("8")
        self.draft_checkpoint("dirty_integer_precedes_external_replacement", LIT_I, "8", status=0, dirty=True)
        self.native.text("9", settle=False)
        self.native.key(LIT_F7, True)
        self.native.key(LIT_F7, False)
        replacement = "9007199254740995"
        self.checkpoint("external_replacement_fences_queued_old_text", integer_bits=integer_bits(int(replacement)), float_bits=float_bits(2.5), **draft_fields(LIT_I, replacement, anchor=len(replacement), caret=len(replacement),
                status=0, dirty=False), **draft_fields(LIT_F, "2.5", anchor=3, caret=3, status=0, dirty=False), dynamic=(LIT_RESTORED,))
        self.native.tap(LIT_Z, control=True)
        self.checkpoint("external_replacement_clears_prior_draft_history")
        self.native.replace_text("42")
        self.draft_checkpoint("draft_precedes_ordered_submit_text_batch", LIT_I, "42", status=0, dirty=True)
        self.native.key(LIT_RETURN, True)
        self.native.key(LIT_RETURN, False)
        self.native.text("7", settle=False)
        self.draft_checkpoint("native_submit_then_text_preserves_event_order", LIT_I, "427", status=0, dirty=True, integer_bits=integer_bits(42), **self.counters(commits=1))
        self.native.tap(LIT_ESCAPE)
        self.draft_checkpoint("escape_restores_value_from_the_ordered_submit", LIT_I, "42", status=0, dirty=False, i_focus=0, **self.counters(cancels=1, restored=1))
        self.focus(LIT_INTEGER, "canonical_refocus_precedes_native_focus_loss")
        self.native.focus_loss_and_gain()
        self.native.text("4")
        self.checkpoint("native_focus_loss_fences_unfocused_text", i_focus=0, dynamic=(LIT_RESTORED,))
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_typed_and_canonical_drafts", extent=(800, 600))
        self.focus(LIT_INTEGER, "resized_focus_precedes_long_numeric_draft")
        long_integer = "0" * 64 + "9223372036854775807"
        # Drain each bounded native batch before the next one; the host deliberately retires input on queue overflow rather than applying a truncated batch.
        self.native.replace_text(long_integer[:32])
        self.draft_checkpoint("long_integer_first_native_batch_is_displayed", LIT_I, long_integer[:32], status=0, dirty=True, extent=(800, 600))
        self.native.text(long_integer[32:64])
        self.draft_checkpoint("long_integer_second_native_batch_is_displayed", LIT_I, long_integer[:64], status=0, dirty=True, extent=(800, 600))
        self.native.text(long_integer[64:])
        self.draft_checkpoint("valid_long_integer_scrolls_caret_inside_clip", LIT_I, long_integer, status=0, dirty=True, extent=(800, 600), extra=lambda state: state[LIT_RECTANGLES][LIT_INTEGER_CARET][0] >= sum(state[LIT_RECTANGLES][LIT_INTEGER_CONTENT][::2]) - 2.0)
        self.native.tap(LIT_RETURN)
        self.checkpoint("long_integer_submit_keeps_bytes_and_exact_maximum", integer_bits=integer_bits(9223372036854775807), i_dirty=0, extent=(800, 600), **self.counters(commits=1))
        self.native.tap("Home")
        self.checkpoint("home_restores_the_long_draft_left_edge", i_anchor=0, i_caret=0, extent=(800, 600), extra=lambda state: abs(state[LIT_RECTANGLES][LIT_INTEGER_CARET][0] - state[LIT_RECTANGLES][LIT_INTEGER_CONTENT][0]) <= 0.75)
        self.native.tap("End")
        self.checkpoint("end_restores_the_long_draft_right_edge", i_anchor=len(long_integer), i_caret=len(long_integer), extent=(800, 600), extra=lambda state: state[LIT_RECTANGLES][LIT_INTEGER_CARET][0] >= sum(state[LIT_RECTANGLES][LIT_INTEGER_CONTENT][::2]) - 2.0)
        self.native.tap(LIT_ESCAPE)
        self.draft_checkpoint("escape_canonicalizes_the_committed_maximum", LIT_I, "9223372036854775807", status=0, dirty=False, i_focus=0, extent=(800, 600), **self.counters(cancels=1, restored=1))
        self.focus(LIT_INTEGER, "integer_refocus_after_escape_precedes_minimum")
        self.native.replace_text("-9223372036854775808")
        self.draft_checkpoint("minimum_integer_draft_is_representable", LIT_I, "-9223372036854775808", status=0, dirty=True)
        self.native.tap(LIT_RETURN)
        self.checkpoint("minimum_integer_commits_all_64_bits", integer_bits=integer_bits(-9223372036854775808), i_dirty=0, **self.counters(commits=1))
        self.native.replace_text("12")
        self.draft_checkpoint("dirty_numeric_draft_precedes_native_focus_loss", LIT_I, "12", status=0, dirty=True)
        self.native.focus_loss_and_gain()
        self.native.text("3")
        self.draft_checkpoint("native_focus_loss_abandons_without_commit_or_cancel", LIT_I, "-9223372036854775808", status=0, dirty=False, i_focus=0, **self.counters(restored=1))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), "NUMERIC_EDIT", skin=args.skin, force_x11=True)
    backend = create_capture_backend()
    logserver = application = numeric_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the numeric editor fixture appeared")
            raise SmokeFailure("custom numeric editor fixture did not appear")
        if platform.system() == "Windows":
            backend.prepare_raw_client_window(handle)
        else:
            backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiNumericEditSmoke: display", min(args.timeout, 15.0))
        numeric_run = NumericEditRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        numeric_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, LIT_UI_NUMERIC_EDITOR_FIXTURE, handle)
                require_normal_process_exit(exit_code, tail, LIT_UI_NUMERIC_EDITOR_FIXTURE)
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "numeric_edit.log").write_text(text, encoding=LIT_UTF_8)
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI numeric editor logserver")
                finally:
                    try:
                        if numeric_run is not None:
                            numeric_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    numeric_run.write_report(True)
    print(f"UI numeric editor: {len(numeric_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
