"""Native multiline input and reads of the actual Win32/X11 clipboard.

Posted Win32 characters bypass live IME. The X11 path uses the existing ASCII
key transport and UTF8_STRING clipboard reader; neither path claims Wayland.
"""
from __future__ import annotations

import ctypes
import math
import time

from numeric_edit_native import NumericEditNativeInput
from window_capture_smoke import LinuxXEvent, SmokeFailure


class TextAreaNativeInput(NumericEditNativeInput):
    def focus_click(self, x, y, *, deadline):
        """Keep the physical cursor at the caret until posted release retires."""
        if not self.windows:
            # XSendEvent preserves release/motion order without moving the physical cursor.
            self.click(x, y)
            return
        click_deadline = min(deadline, time.monotonic() + 2.0)
        self.pointer(x, y)
        self._wait_for_capture(None, click_deadline, "before press")
        self.button(True, x, y)
        try:
            self._wait_for_capture(self.handle, click_deadline, "press")
        finally:
            self.button(False, x, y)
        self._wait_for_capture(None, click_deadline, "release")

    def _wait_for_capture(self, expected_owner, deadline, phase):
        while True:
            self.maintain_pointer()
            observed_owner = self.capture_owner()
            if observed_owner == expected_owner:
                return
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                raise SmokeFailure(f"TextArea focus click {phase} did not acknowledge capture: "
                    f"expected owner {expected_owner!r}, observed {observed_owner!r}, "
                    f"fixture {self.handle:#x}, native window {self.observe_window()}")
            time.sleep(min(0.01, remaining))

    def text_down_text(self, first, last):
        """Preserve native event order without an intermediate display wait."""
        self.text(first, settle=False)
        self.key("Down", True)
        self.key("Down", False)
        self.text(last, settle=False)

    def wheel_x(self, steps, x, y):
        """Positive steps scroll right; positions are physical client coordinates."""
        if not math.isfinite(steps) or steps != int(steps) or abs(steps) > 200:
            raise ValueError("native horizontal wheel steps must be an integer between -200 and 200")
        self.pointer(x, y)
        if self.windows:
            point = self.backend.POINT(int(x), int(y))
            if not self.backend.user32.ClientToScreen(ctypes.c_void_p(self.handle), ctypes.byref(point)):
                raise SmokeFailure("ClientToScreen failed for the positioned horizontal wheel event")
            delta = int(steps) * 120
            self._post(0x020E, (delta & 0xFFFF) << 16,
                ((int(point.y) & 0xFFFF) << 16) | (int(point.x) & 0xFFFF))
        else:
            for _ in range(abs(int(steps))):
                for down in (True, False):
                    event = LinuxXEvent()
                    event.xbutton.type = self.backend.BUTTON_PRESS if down else self.backend.BUTTON_RELEASE
                    self.backend._fill_input_event_prefix(event.xbutton, self.handle, int(x), int(y))
                    event.xbutton.button = 7 if steps > 0 else 6
                    mask = self.backend.BUTTON_PRESS_MASK if down else self.backend.BUTTON_RELEASE_MASK
                    if not self.backend.x11.XSendEvent(self.backend.display, self.handle, False, mask, ctypes.byref(event)):
                        raise SmokeFailure("failed to send the X11 horizontal wheel event")
            self.backend.x11.XFlush(self.backend.display)
        time.sleep(0.15)
