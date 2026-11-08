"""Native numeric drafts and reads of the actual Win32/X11 clipboard.

Win32 text uses posted WM_CHAR units and therefore does not qualify live IME.
X11 text resolves digits, punctuation, and shifted ASCII letters to native keys.
"""
from __future__ import annotations

import ctypes
import time

from edit_native import KEYS, TEXT_SETTLE_SECONDS
from numeric_clipboard_reader import NumericClipboardReader
from native_input import WinInput, WinKeyboardInput
from popup_tools_native import PopupToolsNativeInput
from window_capture_smoke import SmokeFailure

LIT_MINUS = "minus"
LIT_PERIOD = "period"
LIT_PLUS = "plus"

SUPPORTED_TEXT = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-.+ "


class NumericEditNativeInput(PopupToolsNativeInput):
    def __init__(self, backend, handle):
        super().__init__(backend, handle)
        self.clipboard = NumericClipboardReader(backend, self.windows)
    VK_OEM_PLUS = 0xBB
    VK_OEM_MINUS = 0xBD
    VK_OEM_PERIOD = 0xBE
    WM_KEYDOWN_MSG = 0x0100
    WM_KEYUP_MSG = 0x0101
    KEY_PREVIOUS_STATE_BIT = 30
    KEY_TRANSITION_STATE_BIT = 31

    def key(self, name, down, *, repeat=False, shift=False, control=False):
        numeric_keys = {LIT_MINUS: self.VK_OEM_MINUS, LIT_PERIOD: self.VK_OEM_PERIOD, LIT_PLUS: self.VK_OEM_PLUS, **{digit: ord(digit) for digit in "0123456789"}}
        if self.windows and name in numeric_keys:
            flags = 1 | ((1 << self.KEY_PREVIOUS_STATE_BIT) if repeat or not down else 0) | ((1 << self.KEY_TRANSITION_STATE_BIT) if not down else 0)
            self._post(self.WM_KEYDOWN_MSG if down else self.WM_KEYUP_MSG, numeric_keys[name], flags)
            return
        super().key(name, down, repeat=repeat, shift=shift, control=control)

    def text(self, value, *, settle=True):
        if any(character not in SUPPORTED_TEXT for character in value):
            raise SmokeFailure("numeric native text supports ASCII digits, letters, signs, period, and space")
        if self.windows:
            super().text(value, settle=settle)
            return
        punctuation = {"-": (LIT_MINUS, False), ".": (LIT_PERIOD, False), "+": (LIT_PLUS, True), " ": ("space", False)}
        for character in value:
            name, shift = punctuation.get(character, (character.lower(), character.isupper()))
            self.key(name, True, shift=shift)
            self.key(name, False, shift=shift)
        if settle:
            time.sleep(TEXT_SETTLE_SECONDS)

    def replace_text(self, value):
        if any(character not in SUPPORTED_TEXT for character in value):
            raise SmokeFailure("numeric native text supports ASCII digits, letters, signs, period, and space")
        self.tap("a", control=True)
        if not self.windows:
            if value:
                self.text(value)
            else:
                self.tap("BackSpace")
            return
        # Keep the replacement in the same native input queue as Ctrl+A.
        if value:
            encoded = value.encode("utf-16-le")
            for offset in range(0, len(encoded), 2):
                unit = int.from_bytes(encoded[offset:offset + 2], "little")
                event = WinInput(type=1)
                event.data.keyboard = WinKeyboardInput(0, unit, 0x0004, 0, None)
                if self.backend.user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(WinInput)) != 1:
                    raise SmokeFailure("SendInput failed for numeric text")
                event.data.keyboard.flags |= 0x0002
                if self.backend.user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(WinInput)) != 1:
                    retried = self.backend.user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(WinInput))
                    raise SmokeFailure(f"SendInput failed for numeric text release; retry returned {retried}")
        else:
            try:
                self._native_key(KEYS["BackSpace"], True)
            finally:
                self._native_key(KEYS["BackSpace"], False)
            time.sleep(TEXT_SETTLE_SECONDS)
            return
        time.sleep(TEXT_SETTLE_SECONDS)

