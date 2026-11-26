#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
import time

from guarded_native import GuardedNativeInput
from probe_common import center
from popup_tools_probe import observe_popup_tools, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import run_smoke_main, SmokeFailure, ensure_process_running
from checkpoint_report import CHECKPOINT_POLL_SECONDS, CHECKPOINT_STAGE_TIMEOUT_SECONDS, prepare_guarded_checkpoint
from fixture_launch import UiFixtureRunBase, run_ui_fixture

LIT_CURSOR = "cursor"
LIT_RECTANGLES = "rectangles"
LIT_ANCHOR = "anchor"
LIT_COUNTER = "counter"
LIT_MENU = "Menu"
LIT_DOWN = "Down"
LIT_RETURN = "Return"
LIT_F10 = "F10"
LIT_SECOND = "second"
LIT_F9 = "F9"
LIT_POPUP_TOOLS_GRACEFUL_EXIT_TIMEOUT_SECONDS = 60.0


class PopupToolsRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=GuardedNativeInput(backend, handle))
        self.expected = {"tooltip": 0, "open": 0, LIT_CURSOR: 0, "command": 0, "commits": 0, "anchor_clicks": 0, "underlying": 0, "sentinel_focused": 0, "enabled": 1, "reversed": 0, "source_revision": 1, "source_generation": 1, "text_bytes": 8}

    def click(self, name):
        self.native.confirmed_click(*self.point(*center(self.snapshot[LIT_RECTANGLES][name])), deadline=self.deadline)

    def hover(self, name):
        self.native.pointer(*self.point(*center(self.snapshot[LIT_RECTANGLES][name])))

    def hover_transition(self, name, expected):
        pattern = r"UiPopupToolsSmoke: anchor_hover=([01])"
        previous = len(re.findall(pattern, self.logs()))
        self.hover(name)
        deadline = min(self.deadline, time.monotonic() + 10.0)
        while time.monotonic() < deadline:
            ensure_process_running(self.process, "while waiting for popup tools hover")
            self.native.maintain_pointer()
            events = re.findall(pattern, self.logs())
            if len(events) > previous and int(events[-1]) == int(expected):
                return
            time.sleep(CHECKPOINT_POLL_SECONDS)
        raise SmokeFailure(f"popup tools hover transition to {name} was not observed")

    def right_click_anchor(self):
        self.native.confirmed_click(*self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_ANCHOR])), deadline=self.deadline, secondary=True)

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), settle=0.25, **changes):
        prepare_guarded_checkpoint(self, name, changes, dynamic, settle)
        stage_timeout = 20.0 if name in ("delayed_tooltip_keeps_edit_focus", "tooltip_reappears_after_new_delay") else CHECKPOINT_STAGE_TIMEOUT_SECONDS
        self.poll_checkpoint(name, stage_timeout=stage_timeout, minimum_extent=(600.0, 400.0), minimum_extent_message="the popup tools fixture needs a logical client of at least 600x400",
            process_stage="during popup tools UI capture", snapshot_reader=snapshot_from_logs,
            evaluate=lambda frame, snapshot: observe_popup_tools(frame, snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra),
            raw_windows=True, failure_label="popup tools", maintain_pointer=True, accept_extras=lambda _: {"native_window": self.native.observe_window()}, dynamic=dynamic, failure_extras=True)

    def write_report(self, passed):
        self.write_smoke_report("popup_tools.json", passed, include_failure=True,
            input_path="Win32 physical hover positioning with posted motion/buttons/Menu keys and native Shift+F10 modifiers" if self.native.windows else "X11 XSendEvent keys/button1/button3 and XSetInputFocus",
            runtime_limit="No live IME, native Wayland, physical pointer-grab, or native Linux qualification.")

    def execute(self):
        self.native.pointer(4, 4)
        self.checkpoint("initial_tools_state")
        self.click("sentinel")
        self.checkpoint("outside_edit_owns_focus", sentinel_focused=1)
        self.hover_transition(LIT_ANCHOR, True)
        self.checkpoint("hover_before_delay", settle=0.05)
        self.checkpoint("delayed_tooltip_keeps_edit_focus", tooltip=1)
        self.native.text("x")
        self.checkpoint("tooltip_does_not_steal_native_text", text_bytes=9)
        self.hover_transition(LIT_COUNTER, False)
        self.checkpoint("leaving_anchor_hides_tooltip", tooltip=0)
        self.hover_transition(LIT_ANCHOR, True)
        self.checkpoint("new_hover_restarts_delay", settle=0.05)
        self.checkpoint("tooltip_reappears_after_new_delay", tooltip=1)
        anchor = self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_ANCHOR]))
        self.native.confirmed_button(True, *anchor, deadline=self.deadline)
        self.checkpoint("held_primary_suppresses_tooltip", tooltip=0, sentinel_focused=0)
        self.native.confirmed_button(False, *anchor, deadline=self.deadline)
        self.hover(LIT_COUNTER)
        self.checkpoint("primary_release_activates_anchor", anchor_clicks=1)
        self.hover(LIT_COUNTER)
        self.native.key(LIT_MENU, True)
        self.checkpoint("menu_key_press_waits_for_release")
        self.native.key(LIT_MENU, False)
        self.checkpoint("menu_key_opens_on_focused_anchor", open=1, cursor=1)
        self.native.tap(LIT_DOWN)
        self.checkpoint("menu_down_previews_second", cursor=2)
        self.native.tap(LIT_DOWN)
        self.checkpoint("menu_down_skips_disabled", cursor=4)
        self.native.tap("Up")
        self.checkpoint("menu_up_skips_disabled", cursor=2)
        self.native.tap(LIT_RETURN)
        self.checkpoint("menu_enter_activates_and_closes", open=0, command=2, commits=1, dynamic=(LIT_CURSOR,))
        self.native.tap(LIT_F10, shift=True)
        self.checkpoint("shift_f10_opens_on_returned_focus", open=1, cursor=1)
        self.native.tap("space")
        self.checkpoint("menu_space_activates_first_command", open=0, command=1, commits=2, dynamic=(LIT_CURSOR,))
        anchor = self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_ANCHOR]))
        self.native.confirmed_button(True, *anchor, deadline=self.deadline, secondary=True)
        self.checkpoint("secondary_press_waits_for_release")
        self.native.confirmed_button(False, *anchor, deadline=self.deadline, secondary=True)
        self.checkpoint("right_click_opens_without_primary_activation", open=1, cursor=1)
        self.click("disabled")
        self.checkpoint("disabled_menu_command_cannot_activate")
        self.click("last")
        self.checkpoint("pointer_command_activates_and_closes", open=0, command=5, commits=3, dynamic=(LIT_CURSOR,))
        self.right_click_anchor()
        self.checkpoint("outside_dismissal_setup", open=1, cursor=1)
        self.click(LIT_COUNTER)
        self.checkpoint("outside_press_and_release_are_consumed", open=0, dynamic=(LIT_CURSOR,))
        self.click(LIT_COUNTER)
        self.checkpoint("next_outside_sequence_activates", underlying=1)
        self.right_click_anchor()
        self.checkpoint("source_revision_held_pointer_setup", open=1, cursor=1)
        old_second = self.point(*center(self.snapshot[LIT_RECTANGLES][LIT_SECOND]))
        self.native.confirmed_button(True, *old_second, deadline=self.deadline)
        self.native.tap("F5")
        self.checkpoint("source_revision_resets_preview_and_fences_pointer", reversed=1, source_revision=2, cursor=5)
        self.native.confirmed_button(False, *old_second, deadline=self.deadline)
        self.checkpoint("stale_pointer_release_cannot_activate_new_order")
        self.hover(LIT_COUNTER)
        self.native.tap("Home")
        self.checkpoint("reversed_home_previews_first", cursor=5)
        self.native.tap("End")
        self.checkpoint("reversed_end_previews_last", cursor=1)
        self.native.tap(LIT_RETURN)
        self.checkpoint("reversed_command_commit", open=0, command=1, commits=4, dynamic=(LIT_CURSOR,))
        self.right_click_anchor()
        self.checkpoint("source_replacement_held_key_setup", open=1, cursor=5)
        self.native.key(LIT_DOWN, True)
        self.checkpoint("held_key_previews_once", cursor=4)
        self.hover(LIT_COUNTER)
        self.native.tap("F6")
        self.checkpoint("source_replacement_dismisses_and_retires_owner", open=0, source_generation=2, source_revision=3, dynamic=(LIT_CURSOR,))
        self.native.key(LIT_DOWN, True, repeat=True)
        self.native.key(LIT_DOWN, False)
        self.checkpoint("stale_held_key_cannot_reopen_or_activate")
        self.native.tap(LIT_F9)
        self.right_click_anchor()
        self.native.tap(LIT_MENU)
        self.native.tap(LIT_F10, shift=True)
        self.checkpoint("disabled_anchor_blocks_pointer_and_keyboard_menu", enabled=0)
        self.hover(LIT_ANCHOR)
        self.checkpoint("disabled_anchor_blocks_delayed_tooltip", settle=8.2)
        self.native.tap(LIT_F9)
        self.hover(LIT_COUNTER)
        self.checkpoint("enable_restores_attachment", enabled=1)
        self.click(LIT_ANCHOR)
        self.hover(LIT_COUNTER)
        self.checkpoint("fresh_primary_focuses_enabled_anchor", anchor_clicks=2)
        self.hover(LIT_COUNTER)
        self.native.tap(LIT_MENU)
        self.checkpoint("fresh_menu_after_source_replacement", open=1, cursor=5)
        self.native.tap("Escape")
        self.checkpoint("escape_dismisses_context_commands", open=0, dynamic=(LIT_CURSOR,))
        self.right_click_anchor()
        self.checkpoint("focus_loss_menu_setup", open=1, cursor=5)
        self.hover(LIT_COUNTER)
        self.native.focus_loss_and_gain()
        self.checkpoint("native_focus_loss_dismisses_menu", open=0, sentinel_focused=0, dynamic=(LIT_CURSOR,))
        self.native.tap(LIT_DOWN)
        self.checkpoint("unfocused_navigation_cannot_activate")
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resize_preserves_host_commands", extent=(800, 600))
        self.right_click_anchor()
        self.checkpoint("first_secondary_request_after_resize", open=1, cursor=5, extent=(800, 600))
        self.click(LIT_SECOND)
        self.checkpoint("pointer_command_after_resize", open=0, command=2, commits=5, extent=(800, 600), dynamic=(LIT_CURSOR,))


def run(args):
    return run_ui_fixture(args, fixture="POPUP_TOOLS", display_marker="UiPopupToolsSmoke: display", driver_type=PopupToolsRun, log_name="popup_tools.log", label="UI popup tools",
        window_label="popup tools", process_label="popup tools", raw_windows=True, restore_pointer=True, graceful_exit_timeout=LIT_POPUP_TOOLS_GRACEFUL_EXIT_TIMEOUT_SECONDS)


def main(argv):
    return run_smoke_main(argv, parse_args, run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
