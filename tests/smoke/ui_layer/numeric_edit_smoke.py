#!/usr/bin/env python3
"""Qualify numeric draft/value separation through OS input and displayed GPU state."""
from __future__ import annotations

import json
import platform
import sys
import time

from numeric_edit_native import NumericEditNativeInput
from numeric_edit_probe import center, draft_fields, float_bits, integer_bits, observe_numeric_edit, snapshot_from_logs, text_hash
from caret_capture import wait_for_caret_phase
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)
from fixture_environment import build_fixture_environment
from probe_reference import linear_rgb_bytes


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
        self.expected = {"integer_bits": integer_bits(7), "float_bits": float_bits(1.25), "i_focus": 0,
            "f_focus": 0, "clipboard_focus": 0, "enabled": 1, "readonly": 0, "clamp": 1, "coherent": 1,
            **draft_fields("i", "7", anchor=1, caret=1, status=0, dirty=False),
            **draft_fields("f", "1.25", anchor=4, caret=4, status=0, dirty=False),
            **draft_fields("clipboard", "", anchor=0, caret=0)}

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def counters(self, **increments):
        return {name: self.snapshot[name] + count for name, count in increments.items()}

    def focus(self, field, name, **changes):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][f"{field}_bounds"])))
        self.checkpoint(name, i_focus=int(field == "integer"), f_focus=int(field == "float"),
            clipboard_focus=int(field == "clipboard"), **changes)

    def draft_checkpoint(self, name, prefix, text, *, status=None, dirty=None, anchor=None, caret=None, **changes):
        end = len(text.encode("utf-8"))
        self.checkpoint(name, **draft_fields(prefix, text, anchor=end if anchor is None else anchor,
            caret=end if caret is None else caret, status=status, dirty=dirty), **changes)

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), settle=0.25, **changes):
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        self.native.maintain_pointer()
        time.sleep(settle)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during numeric edit UI capture")
            self.native.maintain_pointer()
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 640.0 or snapshot["logical_extent"][1] < 400.0:
                raise SmokeSkip("the numeric editor fixture needs a logical client of at least 640x400")
            if not self.native.windows and self._caret_probe_sample(snapshot) is not None:
                # Full-frame processing can alias the 1s caret blink cycle; reuse the
                # exact oracle sample only when every other gate already matches.
                sample = self._caret_probe_sample(snapshot)
                wait_for_caret_phase(self.backend, self.handle, self.process, [sample], stage_deadline)
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_numeric_edit(read_bmp_24_rows(path), snapshot, self.expected,
                extent=extent, skin=self.args.skin, extra=extra)
            if report["passed"]:
                self.snapshot = snapshot
                for field in dynamic:
                    self.expected[field] = snapshot[field]
                report.update({"stage": name, "capture": str(path), "native_window": self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        self.failure_report = {"stage": name, "capture": str(path), "native_window": self.native.observe_window(),
            "observation": report}
        self.write_report(False)
        raise SmokeFailure(f"numeric edit displayed-state gate '{name}' failed: {self.failure_report}")

    def _caret_probe_sample(self, snapshot):
        width, height = self.backend.client_size(self.handle)
        scale_x, scale_y = snapshot["scale"]
        color = linear_rgb_bytes((0.95, 0.97, 1.0))
        for field, prefix in (("integer", "i"), ("float", "f"), ("clipboard", "clipboard")):
            if not snapshot.get(f"{prefix}_focus") or snapshot.get(f"{prefix}_anchor") != snapshot.get(f"{prefix}_caret"):
                continue
            if field != "clipboard" and (not snapshot.get("enabled") or snapshot.get("readonly")):
                continue
            kx, ky, kw, kh = snapshot["rectangles"][f"{field}_caret"]
            cx, cy, cw, ch = snapshot["rectangles"][f"{field}_content"]
            if not (cx - 0.75 <= kx and kx + kw <= cx + cw + 0.75 and cy - 0.75 <= ky and ky + kh <= cy + ch + 0.75):
                continue
            column, row = round(kx * scale_x), round((min(ky + kh, cy + ch) - 0.75) * scale_y)
            if 0 <= column < width and 0 <= row < height:
                return (column, max(0, row - 1), min(height, row + 2), color, 5)
        return None

    def require_clipboard(self, text):
        deadline, observed = min(self.deadline, time.monotonic() + 5.0), None
        while time.monotonic() < deadline:
            ensure_process_running(self.process, "during numeric OS clipboard verification")
            observed = self.native.clipboard_text(timeout=min(2.0, max(0.1, deadline - time.monotonic())))
            if observed == text:
                self.stages[-1]["os_clipboard"] = {"expected": text, "observed": observed, "passed": True}
                self.write_report(False)
                return
            time.sleep(0.05)
        self.failure_report = {"stage": self.stages[-1]["stage"], "os_clipboard": {"expected": text,
            "observed_bytes": len(observed.encode("utf-8")) if observed is not None else None,
            "observed_hash": text_hash(observed) if observed is not None else None, "passed": False}}
        self.write_report(False)
        raise SmokeFailure(f"numeric copy did not publish the expected OS clipboard text: {self.failure_report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "failure": self.failure_report, "marker_count_per_gate": 73,
            "input_path": "Win32 maintained cursor, posted WM_CHAR/keys/buttons, and real modifier chords; CF_UNICODETEXT read"
                if self.native.windows else "X11 XSendEvent ASCII keys/buttons, native focus, and UTF8_STRING selection read",
            "ordered_batch_scope": "Enter then text is posted without an intermediate displayed-state wait; delivery may cross frames.",
            "runtime_limit": "Synthetic input does not qualify live IME, native Wayland, or physical keyboard hardware."}
        (self.args.output_directory / "numeric_edit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.native.pointer(4, 4)
        self.checkpoint("initial_typed_values_and_drafts", dynamic=COUNTERS)
        self.focus("integer", "integer_focus_is_displayed_before_typing")
        self.native.replace_text("-")
        self.draft_checkpoint("integer_incomplete_sign_keeps_typed_value", "i", "-", status=1, dirty=True)
        self.native.tap("Return")
        self.checkpoint("integer_incomplete_submit_rejects_without_rewrite", **self.counters(rejects=1))
        self.native.tap("Escape")
        self.draft_checkpoint("integer_escape_restores_committed_text", "i", "7", status=0, dirty=False,
            i_focus=0, **self.counters(cancels=1, restored=1))
        self.focus("integer", "integer_refocus_after_escape_precedes_typing")
        exact_integer = "9007199254740993"
        self.native.replace_text(exact_integer)
        self.draft_checkpoint("integer_above_double_precision_remains_a_draft", "i", exact_integer, status=0, dirty=True)
        self.native.tap("Return")
        self.checkpoint("integer_submit_preserves_exact_64_bit_value", integer_bits=integer_bits(int(exact_integer)),
            i_dirty=0, **self.counters(commits=1))
        self.native.tap("a", control=True)
        self.checkpoint("integer_select_all_has_displayed_selection", i_anchor=0, i_caret=len(exact_integer))
        self.native.tap("c", control=True)
        self.checkpoint("integer_copy_keeps_typed_value_and_selection")
        self.require_clipboard(exact_integer)
        self.focus("clipboard", "clipboard_destination_focus_precedes_paste",
            **draft_fields("i", exact_integer, anchor=len(exact_integer), caret=len(exact_integer)), **self.counters(commits=1))
        self.native.tap("v", control=True)
        self.draft_checkpoint("exact_integer_pastes_through_the_os_service", "clipboard", exact_integer)
        self.native.replace_text("12x")
        self.draft_checkpoint("invalid_numeric_clipboard_source", "clipboard", "12x")
        self.native.tap("a", control=True)
        self.native.tap("c", control=True)
        self.checkpoint("invalid_source_copy_is_visible", clipboard_anchor=0, clipboard_caret=3)
        self.require_clipboard("12x")
        self.focus("integer", "integer_refocus_precedes_invalid_paste")
        self.native.tap("a", control=True)
        self.native.tap("v", control=True)
        self.draft_checkpoint("invalid_os_paste_changes_only_the_draft", "i", "12x", status=2, dirty=True)
        self.native.tap("Return")
        self.checkpoint("invalid_submit_retains_draft_and_rejects", **self.counters(rejects=1))
        self.focus("clipboard", "invalid_blur_restores_the_exact_committed_integer",
            **draft_fields("i", exact_integer, anchor=len(exact_integer), caret=len(exact_integer), status=0, dirty=False),
            clipboard_anchor=3, clipboard_caret=3, **self.counters(rejects=1, restored=1))
        self.focus("integer", "integer_refocus_precedes_cut")
        self.native.tap("a", control=True)
        self.native.tap("x", control=True)
        self.draft_checkpoint("cut_publishes_os_text_and_leaves_incomplete_draft", "i", "", status=1, dirty=True)
        self.require_clipboard(exact_integer)
        self.native.tap("v", control=True)
        self.draft_checkpoint("paste_after_cut_restores_the_accepted_draft", "i", exact_integer, status=0, dirty=False)
        self.native.replace_text("9223372036854775808")
        self.draft_checkpoint("integer_representation_overflow_keeps_typed_value", "i", "9223372036854775808",
            status=3, dirty=True)
        self.native.tap("Tab")
        self.checkpoint("tab_rejects_overflow_and_focuses_float", i_focus=0, f_focus=1,
            **draft_fields("i", exact_integer, anchor=len(exact_integer), caret=len(exact_integer), status=0, dirty=False),
            **self.counters(rejects=1, restored=1))
        self.native.replace_text("-")
        self.draft_checkpoint("float_incomplete_sign_keeps_typed_value", "f", "-", status=1, dirty=True)
        self.native.tap("Escape")
        self.draft_checkpoint("float_escape_restores_committed_decimal", "f", "1.25", status=0, dirty=False,
            f_focus=0, **self.counters(cancels=1, restored=1))
        self.focus("float", "float_refocus_after_escape_precedes_exponent")
        self.native.replace_text("2.5e-")
        self.draft_checkpoint("float_incomplete_exponent_is_preserved", "f", "2.5e-", status=1, dirty=True)
        self.native.text("1")
        self.draft_checkpoint("float_exponent_completion_does_not_commit", "f", "2.5e-1", status=0, dirty=True)
        self.native.tap("Return")
        self.checkpoint("float_submit_retains_lexical_exponent", float_bits=float_bits(0.25), f_dirty=0,
            **self.counters(commits=1))
        self.native.tap("z", control=True)
        self.draft_checkpoint("undo_after_submit_changes_draft_only", "f", "2.5e-", status=1, dirty=True)
        self.native.tap("y", control=True)
        self.draft_checkpoint("redo_after_submit_restores_accepted_lexical_text", "f", "2.5e-1", status=0, dirty=False)
        self.native.tap("Tab")
        self.checkpoint("float_blur_commits_and_canonicalizes", f_focus=0, clipboard_focus=1,
            **draft_fields("f", "0.25", anchor=4, caret=4, status=0, dirty=False), **self.counters(commits=1))
        self.focus("float", "float_refocus_precedes_signed_zero")
        self.native.replace_text("-0")
        self.draft_checkpoint("signed_zero_is_a_complete_draft", "f", "-0", status=0, dirty=True)
        self.native.tap("Return")
        self.checkpoint("signed_zero_commit_preserves_the_sign_bit", float_bits=float_bits(-0.0), f_dirty=0,
            **self.counters(commits=1))
        self.native.replace_text("+1.5E+1")
        self.draft_checkpoint("uppercase_exponent_and_plus_are_outside_user_bounds", "f", "+1.5E+1", status=3, dirty=True)
        self.native.tap("Return")
        self.draft_checkpoint("clamp_commits_a_canonical_bound", "f", "10", status=0, dirty=False,
            float_bits=float_bits(10.0), **self.counters(commits=1, clamps=1))
        self.native.tap("F8")
        self.checkpoint("reject_policy_applies_to_later_numeric_actions", clamp=0, dynamic=("restored",))
        self.native.replace_text("11")
        self.draft_checkpoint("reject_policy_keeps_out_of_range_draft", "f", "11", status=3, dirty=True)
        self.native.tap("Return")
        self.checkpoint("reject_policy_submit_preserves_committed_value", **self.counters(rejects=1))
        self.native.tap("Tab")
        self.checkpoint("reject_policy_blur_restores_the_committed_bound", f_focus=0, clipboard_focus=1,
            **draft_fields("f", "10", anchor=2, caret=2, status=0, dirty=False), **self.counters(rejects=1, restored=1))
        self.focus("float", "float_refocus_precedes_representation_overflow")
        self.native.replace_text("1e309")
        self.draft_checkpoint("float_representation_overflow_is_distinct_from_bounds", "f", "1e309", status=3, dirty=True)
        self.native.tap("Return")
        self.checkpoint("representation_overflow_rejects_without_clamping", **self.counters(rejects=1))
        self.native.tap("Escape")
        self.draft_checkpoint("escape_restores_after_float_representation_overflow", "f", "10", status=0, dirty=False,
            f_focus=0, **self.counters(cancels=1, restored=1))
        self.focus("float", "float_refocus_after_overflow_escape_precedes_readonly")
        self.native.replace_text("3")
        self.draft_checkpoint("dirty_draft_precedes_readonly_policy_change", "f", "3", status=0, dirty=True)
        self.native.tap("F5")
        self.draft_checkpoint("readonly_transition_restores_and_keeps_focus", "f", "10", status=0, dirty=False,
            readonly=1, dynamic=("restored",))
        self.native.tap("a", control=True)
        self.native.tap("c", control=True)
        self.native.text("9")
        self.native.tap("x", control=True)
        self.native.tap("v", control=True)
        self.native.tap("z", control=True)
        self.native.tap("Return")
        self.checkpoint("readonly_allows_copy_and_blocks_mutation_and_commit", f_anchor=0, f_caret=2)
        self.require_clipboard("10")
        self.native.tap("F5")
        self.checkpoint("editable_policy_restores_a_fresh_selection_epoch", readonly=0, f_anchor=2, f_caret=2,
            dynamic=("restored",))
        self.native.replace_text("-4")
        self.draft_checkpoint("dirty_draft_precedes_disable", "f", "-4", status=0, dirty=True)
        self.native.tap("F6")
        self.native.text("9")
        self.native.tap("Return")
        self.draft_checkpoint("disable_restores_and_blocks_native_text", "f", "10", status=0, dirty=False,
            enabled=0, f_focus=0, dynamic=("restored",))
        self.native.tap("F6")
        self.checkpoint("enable_requires_a_fresh_numeric_focus", enabled=1)
        self.focus("integer", "integer_focus_precedes_external_replacement")
        self.native.replace_text("8")
        self.draft_checkpoint("dirty_integer_precedes_external_replacement", "i", "8", status=0, dirty=True)
        self.native.text("9", settle=False)
        self.native.key("F7", True)
        self.native.key("F7", False)
        replacement = "9007199254740995"
        self.checkpoint("external_replacement_fences_queued_old_text", integer_bits=integer_bits(int(replacement)),
            float_bits=float_bits(2.5), **draft_fields("i", replacement, anchor=len(replacement), caret=len(replacement),
                status=0, dirty=False), **draft_fields("f", "2.5", anchor=3, caret=3, status=0, dirty=False),
            dynamic=("restored",))
        self.native.tap("z", control=True)
        self.checkpoint("external_replacement_clears_prior_draft_history")
        self.native.replace_text("42")
        self.draft_checkpoint("draft_precedes_ordered_submit_text_batch", "i", "42", status=0, dirty=True)
        self.native.key("Return", True)
        self.native.key("Return", False)
        self.native.text("7", settle=False)
        self.draft_checkpoint("native_submit_then_text_preserves_event_order", "i", "427", status=0, dirty=True,
            integer_bits=integer_bits(42), **self.counters(commits=1))
        self.native.tap("Escape")
        self.draft_checkpoint("escape_restores_value_from_the_ordered_submit", "i", "42", status=0, dirty=False,
            i_focus=0, **self.counters(cancels=1, restored=1))
        self.focus("integer", "canonical_refocus_precedes_native_focus_loss")
        self.native.focus_loss_and_gain()
        self.native.text("4")
        self.checkpoint("native_focus_loss_fences_unfocused_text", i_focus=0, dynamic=("restored",))
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_typed_and_canonical_drafts", extent=(800, 600))
        self.focus("integer", "resized_focus_precedes_long_numeric_draft")
        long_integer = "0" * 64 + "9223372036854775807"
        # Drain each bounded native batch before the next one; the host deliberately
        # retires input on queue overflow rather than applying a truncated batch.
        self.native.replace_text(long_integer[:32])
        self.draft_checkpoint("long_integer_first_native_batch_is_displayed", "i", long_integer[:32],
            status=0, dirty=True, extent=(800, 600))
        self.native.text(long_integer[32:64])
        self.draft_checkpoint("long_integer_second_native_batch_is_displayed", "i", long_integer[:64],
            status=0, dirty=True, extent=(800, 600))
        self.native.text(long_integer[64:])
        self.draft_checkpoint("valid_long_integer_scrolls_caret_inside_clip", "i", long_integer, status=0, dirty=True,
            extent=(800, 600), extra=lambda state: state["rectangles"]["integer_caret"][0]
                >= sum(state["rectangles"]["integer_content"][::2]) - 2.0)
        self.native.tap("Return")
        self.checkpoint("long_integer_submit_keeps_bytes_and_exact_maximum", integer_bits=integer_bits(9223372036854775807),
            i_dirty=0, extent=(800, 600), **self.counters(commits=1))
        self.native.tap("Home")
        self.checkpoint("home_restores_the_long_draft_left_edge", i_anchor=0, i_caret=0, extent=(800, 600),
            extra=lambda state: abs(state["rectangles"]["integer_caret"][0] - state["rectangles"]["integer_content"][0]) <= 0.75)
        self.native.tap("End")
        self.checkpoint("end_restores_the_long_draft_right_edge", i_anchor=len(long_integer), i_caret=len(long_integer),
            extent=(800, 600), extra=lambda state: state["rectangles"]["integer_caret"][0]
                >= sum(state["rectangles"]["integer_content"][::2]) - 2.0)
        self.native.tap("Escape")
        self.draft_checkpoint("escape_canonicalizes_the_committed_maximum", "i", "9223372036854775807", status=0,
            dirty=False, i_focus=0, extent=(800, 600), **self.counters(cancels=1, restored=1))
        self.focus("integer", "integer_refocus_after_escape_precedes_minimum")
        self.native.replace_text("-9223372036854775808")
        self.draft_checkpoint("minimum_integer_draft_is_representable", "i", "-9223372036854775808", status=0, dirty=True)
        self.native.tap("Return")
        self.checkpoint("minimum_integer_commits_all_64_bits", integer_bits=integer_bits(-9223372036854775808),
            i_dirty=0, **self.counters(commits=1))
        self.native.replace_text("12")
        self.draft_checkpoint("dirty_numeric_draft_precedes_native_focus_loss", "i", "12", status=0, dirty=True)
        self.native.focus_loss_and_gain()
        self.native.text("3")
        self.draft_checkpoint("native_focus_loss_abandons_without_commit_or_cancel", "i", "-9223372036854775808",
            status=0, dirty=False, i_focus=0, **self.counters(restored=1))


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
                exit_code, tail = terminate_process(application, "UI numeric editor fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI numeric editor fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "numeric_edit.log").write_text(text, encoding="utf-8")
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
