"""Slider fixture commands use the shared positioned Win32/X11 input path."""
from __future__ import annotations

import ctypes
import time

from guarded_native import GuardedNativeInput
from window_capture_smoke import SmokeFailure


COMMAND_KEYS = {
    "fence_value": "F4",
    "toggle_range": "F5",
    "toggle_step": "F6",
    "toggle_enabled": "F7",
    "set_quarter": "F8",
    "close_parent": "F9",
}


class SliderNativeInput(GuardedNativeInput):
    def __init__(self, backend, handle):
        super().__init__(backend, handle)
        self.actions = []
        self.trace_started = time.monotonic()
        if self.windows:
            backend.user32.ScreenToClient.argtypes = [ctypes.c_void_p, ctypes.POINTER(backend.POINT)]
            backend.user32.ScreenToClient.restype = ctypes.c_int

    def record(self, action, **details):
        observation = {"time": round(time.monotonic() - self.trace_started, 6), "action": action,
            "native_window": self.observe_window(), **details}
        self.actions.append(observation)
        del self.actions[:-128]

    def pointer(self, x, y):
        self.record("pointer_before", client=[int(x), int(y)])
        super().pointer(x, y)
        self.record("pointer_after", client=[int(x), int(y)])

    def button(self, down, x, y):
        super().button(down, x, y)
        self.record("button", down=down, client=[int(x), int(y)])

    def key(self, name, down, *, repeat=False, shift=False, control=False):
        super().key(name, down, repeat=repeat, shift=shift, control=control)
        self.record("key", name=name, down=down, repeat=repeat, shift=shift, control=control)

    def maintain_pointer(self):
        before = self.observe_window()
        super().maintain_pointer()
        if self.windows and before.get("requested_client") is not None:
            if before.get("cursor_screen") == before.get("requested_screen"):
                return
            self.record("maintain_pointer_corrected", before=before)

    def observe_window(self):
        observation = super().observe_window()
        observation["requested_client"] = list(self.requested_pointer) if self.requested_pointer is not None else None
        observation["cursor_guard_screen"] = list(self.cursor_guard) if self.cursor_guard is not None else None
        if not self.windows:
            return observation
        if self.requested_pointer is not None:
            requested = self.backend.POINT(*self.requested_pointer)
            if not self.backend.user32.ClientToScreen(ctypes.c_void_p(self.handle), ctypes.byref(requested)):
                raise SmokeFailure("failed to observe the requested slider cursor position")
            observation["requested_screen"] = [requested.x, requested.y]
        cursor = observation.get("cursor_screen")
        if cursor is not None:
            observed = self.backend.POINT(*cursor)
            if not self.backend.user32.ScreenToClient(ctypes.c_void_p(self.handle), ctypes.byref(observed)):
                raise SmokeFailure("failed to observe the slider cursor client position")
            observation["cursor_client"] = [observed.x, observed.y]
        observation["capture_is_fixture"] = self.capture_owner() == self.handle
        return observation

    def command(self, name):
        """Send an application-owned policy or external-value command."""
        if name not in COMMAND_KEYS:
            raise ValueError(f"unknown slider fixture command '{name}'")
        self.record("command_before", name=name)
        self.tap(COMMAND_KEYS[name])
        self.record("command_after", name=name)
