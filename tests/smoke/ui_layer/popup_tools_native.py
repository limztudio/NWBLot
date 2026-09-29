"""Native tooltip hover and context menu keyboard/secondary-pointer requests."""
from __future__ import annotations

import ctypes
import time

from combo_native import ComboNativeInput
from window_capture_smoke import LinuxXEvent, SmokeFailure


class PopupToolsNativeInput(ComboNativeInput):
    def __init__(self, backend, handle):
        super().__init__(backend, handle)
        self.original_cursor = None
        if self.windows:
            backend.user32.GetCursorPos.argtypes = [ctypes.POINTER(backend.POINT)]
            backend.user32.GetCursorPos.restype = ctypes.c_int
            backend.user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
            backend.user32.SetCursorPos.restype = ctypes.c_int

    def pointer(self, x, y):
        if self.windows:
            if self.original_cursor is None:
                original = self.backend.POINT()
                if not self.backend.user32.GetCursorPos(ctypes.byref(original)):
                    raise SmokeFailure("failed to save the Win32 cursor position")
                self.original_cursor = (original.x, original.y)
            position = self.backend.POINT(int(x), int(y))
            if not self.backend.user32.ClientToScreen(ctypes.c_void_p(self.handle), ctypes.byref(position)):
                raise SmokeFailure("failed to map the popup tools hover to screen coordinates")
            if not self.backend.user32.SetCursorPos(position.x, position.y):
                raise SmokeFailure("failed to position the Win32 cursor for popup tools hover")
        super().pointer(x, y)

    def restore_pointer(self):
        if self.windows and self.original_cursor is not None:
            if not self.backend.user32.SetCursorPos(*self.original_cursor):
                raise SmokeFailure("failed to restore the Win32 cursor position")
            self.original_cursor = None

    def observe_window(self):
        if not self.windows:
            return {}
        position = self.backend.POINT()
        cursor_known = bool(self.backend.user32.GetCursorPos(ctypes.byref(position)))
        return {"cursor_screen": [position.x, position.y] if cursor_known else None,
            "foreground_is_fixture": self.backend.user32.GetForegroundWindow() == self.handle}

    def key(self, name, down, *, repeat=False, shift=False, control=False):
        if self.windows and name == "Menu":
            flags = 1 | ((1 << 30) if repeat or not down else 0) | ((1 << 31) if not down else 0)
            self._post(0x0100 if down else 0x0101, 0x5D, flags)
            return
        super().key(name, down, repeat=repeat, shift=shift, control=control)

    def right_button(self, down, x, y):
        self.pointer(x, y)
        if self.windows:
            self._post(0x0204 if down else 0x0205, 2 if down else 0,
                ((int(y) & 0xFFFF) << 16) | (int(x) & 0xFFFF))
        else:
            event = LinuxXEvent()
            event.xbutton.type = self.backend.BUTTON_PRESS if down else self.backend.BUTTON_RELEASE
            self.backend._fill_input_event_prefix(event.xbutton, self.handle, int(x), int(y), state=0 if down else 1024)
            event.xbutton.button = 3
            mask = self.backend.BUTTON_PRESS_MASK if down else self.backend.BUTTON_RELEASE_MASK
            if not self.backend.x11.XSendEvent(self.backend.display, self.handle, False, mask, ctypes.byref(event)):
                raise SmokeFailure("failed to send the X11 secondary pointer event")
            self.backend.x11.XFlush(self.backend.display)

    def right_click(self, x, y):
        self.right_button(True, x, y)
        time.sleep(0.08)
        self.right_button(False, x, y)
        time.sleep(0.1)
