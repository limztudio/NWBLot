"""Native fixed-height list input, including positioned vertical wheel events."""
from __future__ import annotations

import ctypes
import math
import time

from edit_native import EditNativeInput
from window_capture_smoke import LinuxXEvent, SmokeFailure


LIST_KEYS = {"Up": 0x26, "Down": 0x28, "Page_Up": 0x21, "Page_Down": 0x22, "F8": 0x77, "F9": 0x78}


class ListNativeInput(EditNativeInput):
    def key(self, name, down, *, repeat=False, shift=False, control=False):
        if self.windows and name in LIST_KEYS:
            flags = 1 | ((1 << 24) if name in ("Up", "Down", "Page_Up", "Page_Down") else 0)
            flags |= (1 << 30) if repeat or not down else 0
            flags |= (1 << 31) if not down else 0
            self._post(0x0100 if down else 0x0101, LIST_KEYS[name], flags)
            return
        super().key(name, down, repeat=repeat, shift=shift, control=control)

    def wheel(self, steps, x, y):
        """Positive steps scroll upward; positions are physical client coordinates."""
        if not math.isfinite(steps) or steps != int(steps) or abs(steps) > 200:
            raise ValueError("native list wheel steps must be an integer between -200 and 200")
        self.pointer(x, y)
        if self.windows:
            point = self.backend.POINT(int(x), int(y))
            if not self.backend.user32.ClientToScreen(ctypes.c_void_p(self.handle), ctypes.byref(point)):
                raise SmokeFailure("ClientToScreen failed for the positioned list wheel event")
            delta = int(steps) * 120
            self._post(0x020A, (delta & 0xFFFF) << 16,
                ((int(point.y) & 0xFFFF) << 16) | (int(point.x) & 0xFFFF))
        else:
            for _ in range(abs(int(steps))):
                for down in (True, False):
                    event = LinuxXEvent()
                    event.xbutton.type = self.backend.BUTTON_PRESS if down else self.backend.BUTTON_RELEASE
                    self.backend._fill_input_event_prefix(event.xbutton, self.handle, int(x), int(y))
                    event.xbutton.button = 4 if steps > 0 else 5
                    mask = self.backend.BUTTON_PRESS_MASK if down else self.backend.BUTTON_RELEASE_MASK
                    if not self.backend.x11.XSendEvent(self.backend.display, self.handle, False, mask, ctypes.byref(event)):
                        raise SmokeFailure("failed to send the X11 list wheel event")
            self.backend.x11.XFlush(self.backend.display)
        time.sleep(0.15)

    def drag(self, start, end):
        self.button(True, *start)
        time.sleep(0.15)
        self.pointer(*end)
        time.sleep(0.15)
        self.button(False, *end)
        time.sleep(0.15)
