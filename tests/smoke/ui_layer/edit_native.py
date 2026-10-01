"""Native edit-box input helpers, separate from the shared smoke backend.

Win32 commits use WM_CHAR and therefore bypass a live IME. X11 commits use real
ASCII key events; Korean coverage comes from the fixture's initial document.
"""
from __future__ import annotations

import ctypes
import time
from contextlib import contextmanager

from interaction_smoke import NativeInput, WinInput, WinKeyboardInput
from window_capture_smoke import LinuxXEvent, SmokeFailure, SmokeSkip, WinPoint

KEYS = {
    "Tab": 0x09, "Return": 0x0D, "space": 0x20, "Escape": 0x1B,
    "BackSpace": 0x08, "Insert": 0x2D, "Delete": 0x2E, "Left": 0x25, "Right": 0x27,
    "Home": 0x24, "End": 0x23, "F5": 0x74, "F6": 0x75, "F7": 0x76,
    **{letter: ord(letter.upper()) for letter in "abcdefghijklmnopqrstuvwxyz"},
}


class EditNativeInput(NativeInput):
    @contextmanager
    def positioned_cursor(self, x, y):
        if not self.windows:
            raise SmokeFailure("native cursor placement is Win32-only")
        user32 = self.backend.user32
        user32.GetCursorPos.argtypes = [ctypes.POINTER(WinPoint)]
        user32.GetCursorPos.restype = ctypes.c_int
        user32.ClientToScreen.argtypes = [ctypes.c_void_p, ctypes.POINTER(WinPoint)]
        user32.ClientToScreen.restype = ctypes.c_int
        user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
        user32.SetCursorPos.restype = ctypes.c_int
        original = WinPoint()
        position = WinPoint(int(x), int(y))
        if not user32.GetCursorPos(ctypes.byref(original)):
            raise SmokeFailure("GetCursorPos failed before native double-click")
        if not user32.ClientToScreen(self.handle, ctypes.byref(position)):
            raise SmokeFailure("ClientToScreen failed before native double-click")
        if not user32.SetCursorPos(position.x, position.y):
            raise SmokeFailure("SetCursorPos failed before native double-click")
        try:
            # TrackMouseEvent observes the real pointer even when button messages are posted.
            time.sleep(0.15)
            yield
        finally:
            if not user32.SetCursorPos(original.x, original.y):
                raise SmokeFailure("SetCursorPos failed to restore the original cursor")

    def _modifier(self, virtual_key, down):
        event = WinInput(type=1)
        event.data.keyboard = WinKeyboardInput(virtual_key, 0, 0 if down else 0x0002, 0, None)
        if self.backend.user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(WinInput)) != 1:
            raise SmokeFailure(f"SendInput failed for modifier {virtual_key:#x}")

    def key(self, name, down, *, repeat=False, shift=False, control=False, count=1):
        if not 1 <= count <= 0xFFFF or (not down and count != 1):
            raise SmokeFailure("native key message has an invalid repeat count")
        if self.windows:
            extended = name in ("Insert", "Delete", "Left", "Right", "Home", "End")
            flags = count | ((1 << 24) if extended else 0)
            flags |= (1 << 30) if repeat or not down else 0
            flags |= (1 << 31) if not down else 0
            self._post(0x0100 if down else 0x0101, KEYS[name], flags)
            return
        if count != 1:
            raise SmokeFailure("aggregate key messages are Win32-only")
        keysym = self.backend.x11.XStringToKeysym(name.encode("ascii"))
        keycode = self.backend.x11.XKeysymToKeycode(self.backend.display, keysym)
        if not keycode:
            raise SmokeFailure(f"could not resolve X11 edit key '{name}'")
        event = LinuxXEvent()
        event.xkey.type = self.backend.KEY_PRESS if down else self.backend.KEY_RELEASE
        state = (1 if shift else 0) | (4 if control else 0)
        self.backend._fill_input_event_prefix(event.xkey, self.handle, 0, 0, state=state)
        event.xkey.keycode = keycode
        mask = self.backend.KEY_PRESS_MASK if down else self.backend.KEY_RELEASE_MASK
        if not self.backend.x11.XSendEvent(self.backend.display, self.handle, False, mask, ctypes.byref(event)):
            raise SmokeFailure(f"failed to send X11 edit key '{name}'")
        self.backend.x11.XFlush(self.backend.display)

    def tap(self, name, *, repeat_count=0, shift=False, control=False):
        modifiers = []
        if self.windows and (shift or control):
            self.backend.focus_window(self.handle)
            time.sleep(0.1)
            if self.backend.user32.GetForegroundWindow() != self.handle:
                raise SmokeSkip("the edit fixture cannot take foreground focus for native modifier chords")
            try:
                for enabled, key in ((shift, 0x10), (control, 0x11)):
                    if enabled:
                        self._modifier(key, True)
                        modifiers.append(key)
                time.sleep(0.08)
            except BaseException:
                for key in reversed(modifiers):
                    self._modifier(key, False)
                raise
        try:
            self.key(name, True, shift=shift, control=control)
            for _ in range(repeat_count):
                time.sleep(0.04)
                self.key(name, True, repeat=True, shift=shift, control=control)
            time.sleep(0.08)
            self.key(name, False, shift=shift, control=control)
        finally:
            for key in reversed(modifiers):
                self._modifier(key, False)
        time.sleep(0.1)

    def aggregate_tap(self, name, count, *, repeat=False):
        if not self.windows:
            raise SmokeFailure("aggregate key messages are Win32-only")
        self.key(name, True, repeat=repeat, count=count)
        time.sleep(0.08)
        self.key(name, False)
        time.sleep(0.1)

    def aggregate_text(self, character, count):
        if not self.windows or len(character) != 1 or not 1 <= count <= 0xFFFF:
            raise SmokeFailure("aggregate text needs one Unicode scalar and a Win32 repeat count")
        encoded = character.encode("utf-16-le")
        for offset in range(0, len(encoded), 2):
            self._post(0x0102, int.from_bytes(encoded[offset:offset + 2], "little"), count)
        time.sleep(0.15)

    def text(self, value, *, settle=True):
        if self.windows:
            encoded = value.encode("utf-16-le")
            for offset in range(0, len(encoded), 2):
                self._post(0x0102, int.from_bytes(encoded[offset:offset + 2], "little"), 1)
        else:
            for character in value:
                name = "space" if character == " " else character
                if name not in KEYS or not character.isascii():
                    raise SmokeFailure(f"X11 synthetic text does not support '{character}'")
                self.key(name, True)
                self.key(name, False)
        if settle:
            time.sleep(0.15)

    def text_then_tab(self, value):
        self.text(value, settle=False)
        self.key("Tab", True)
        self.key("Tab", False)

