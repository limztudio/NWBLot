#!/usr/bin/env python3
"""Verify configured keyboard actions and ordinary-text suppression through native UI/GPU output."""
from __future__ import annotations

import ctypes
import sys
import time

from edit_native import KEYS
from edit_probe import model
from edit_smoke import EditRun, run
from interaction_smoke import parse_args
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, SmokeSkip


class InputBindingsRun(EditRun):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if self.native.windows:
            user32 = self.backend.user32
            user32.MapVirtualKeyW.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
            user32.MapVirtualKeyW.restype = ctypes.c_uint32
            user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_size_t, ctypes.c_ssize_t]
            user32.SendMessageW.restype = ctypes.c_ssize_t

    def physical_text_key(self, name, character, *, repeat=False, settle=True):
        if self.native.windows:
            # Match each WM_CHAR to its producing scan code, including when another key is already queued.
            user32 = self.backend.user32
            virtual_key = KEYS[name]
            scan_code = int(user32.MapVirtualKeyW(virtual_key, 0))
            if not scan_code or not 0 <= ord(character) <= 0xFFFF:
                raise SmokeFailure("configured input smoke needs one ordinary native key and UTF16 unit")
            flags = 1 | (scan_code << 16)
            # Synchronous key delivery bypasses TranslateMessage, so only the explicit WM_CHAR commits text.
            user32.SendMessageW(self.handle, 0x0100, virtual_key, flags | ((1 << 30) if repeat else 0))
            user32.SendMessageW(self.handle, 0x0101, virtual_key, flags | (1 << 30) | (1 << 31))
            self.native._post(0x0102, ord(character), flags)
        else:
            self.native.key(name, True, repeat=repeat)
            self.native.key(name, False)
        if settle:
            time.sleep(0.15)

    def queued_text_keys(self):
        if not self.native.windows:
            self.physical_text_key("w", "w", settle=False)
            self.physical_text_key("x", "x")
            return
        user32 = self.backend.user32
        keys = [(KEYS[name], 1 | (int(user32.MapVirtualKeyW(KEYS[name], 0)) << 16), character)
            for name, character in (("w", "w"), ("x", "x"))]
        for virtual_key, flags, _ in keys:
            user32.SendMessageW(self.handle, 0x0100, virtual_key, flags)
        for virtual_key, flags, _ in keys:
            user32.SendMessageW(self.handle, 0x0101, virtual_key, flags | (1 << 30) | (1 << 31))
        # Both policies survive release and remain separate before either queued character is delivered.
        for _, flags, character in keys:
            self.native._post(0x0102, ord(character), flags)
        time.sleep(0.2)

    def execute(self):
        primary, secondary = "Hello 한글", "Target"
        self.checkpoint("initial", model(primary), model(secondary))
        self.focus("primary")
        self.native.tap("End")
        self.checkpoint("primary_focus", model(primary, focused=True), model(secondary))
        self.physical_text_key("q", "q")
        self.checkpoint("custom_Activate_suppresses_its_printable_key", model(primary, focused=True), model(secondary))
        if self.native.windows:
            self.physical_text_key("w", "w", repeat=True)
            self.checkpoint("orphan_bound_W_repeat_cannot_navigate_or_insert", model(primary, focused=True), model(secondary))
        self.physical_text_key("w", "w")
        self.checkpoint("printable_W_moves_without_inserting", model(primary, 9, 9, True), model(secondary))
        self.physical_text_key("x", "x")
        primary = "Hello 한x글"
        self.checkpoint("unbound_X_still_inserts", model(primary, 10, 10, True), model(secondary))
        self.physical_text_key("space", " ")
        primary = "Hello 한x 글"
        self.checkpoint("default_Space_remains_editor_text", model(primary, 11, 11, True), model(secondary))
        self.native.tap("Tab")
        self.checkpoint("disabled_default_Tab_keeps_focus", model(primary, 11, 11, True), model(secondary))
        self.physical_text_key("r", "r")
        self.checkpoint("printable_R_changes_focus_without_inserting", model(primary, 11, 11), model(secondary, focused=True))
        self.physical_text_key("t", "t")
        self.checkpoint("printable_T_returns_focus_without_inserting", model(primary, 11, 11, True), model(secondary))
        self.native.tap("End")
        self.queued_text_keys()
        primary = "Hello 한x x글"
        self.checkpoint("queued_scan_codes_keep_bound_and_unbound_text_separate", model(primary, 12, 12, True), model(secondary))
        self.native.tap("a", control=True)
        self.native.tap("c", control=True)
        self.physical_text_key("r", "r")
        self.native.tap("a", control=True)
        self.native.tap("v", control=True)
        self.checkpoint("configured_navigation_preserves_OS_clipboard", model(primary, 0, 15), model(primary, focused=True))


def main(argv):
    args = parse_args(argv, description=__doc__)
    try:
        return run(args, run_type=InputBindingsRun, input_bindings=True)
    except SmokeSkip as error:
        print(f"SKIP: {error}", flush=True)
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, ValueError, TimeoutError) as error:
        print(f"FAIL: {error}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
