"""Slider fixture commands use the shared positioned Win32/X11 input path."""
from __future__ import annotations

import ctypes
import time

from popup_tools_native import PopupToolsNativeInput
from window_capture_smoke import SmokeFailure, WinRect


COMMAND_KEYS = {
    "fence_value": "F4",
    "toggle_range": "F5",
    "toggle_step": "F6",
    "toggle_enabled": "F7",
    "set_quarter": "F8",
    "close_parent": "F9",
}


class SliderGuiThreadInfo(ctypes.Structure):
    _fields_ = [
        ("cbSize", ctypes.c_uint32), ("flags", ctypes.c_uint32),
        ("hwndActive", ctypes.c_void_p), ("hwndFocus", ctypes.c_void_p), ("hwndCapture", ctypes.c_void_p),
        ("hwndMenuOwner", ctypes.c_void_p), ("hwndMoveSize", ctypes.c_void_p), ("hwndCaret", ctypes.c_void_p),
        ("rcCaret", WinRect),
    ]


class SliderNativeInput(PopupToolsNativeInput):
    def __init__(self, backend, handle):
        super().__init__(backend, handle)
        self.actions = []
        self.trace_started = time.monotonic()
        self.original_clip = None
        self.cursor_guard = None
        if self.windows:
            backend.user32.ScreenToClient.argtypes = [ctypes.c_void_p, ctypes.POINTER(backend.POINT)]
            backend.user32.ScreenToClient.restype = ctypes.c_int
            backend.user32.GetGUIThreadInfo.argtypes = [ctypes.c_uint32, ctypes.POINTER(SliderGuiThreadInfo)]
            backend.user32.GetGUIThreadInfo.restype = ctypes.c_int
            backend.user32.GetClipCursor.argtypes = [ctypes.POINTER(backend.RECT)]
            backend.user32.GetClipCursor.restype = ctypes.c_int
            backend.user32.ClipCursor.argtypes = [ctypes.POINTER(backend.RECT)]
            backend.user32.ClipCursor.restype = ctypes.c_int

    def record(self, action, **details):
        observation = {"time": round(time.monotonic() - self.trace_started, 6), "action": action,
            "native_window": self.observe_window(), **details}
        self.actions.append(observation)
        del self.actions[:-128]

    def pointer(self, x, y):
        self.record("pointer_before", client=[int(x), int(y)])
        self.constrain_pointer(x, y)
        super().pointer(x, y)
        self.record("pointer_after", client=[int(x), int(y)])

    def constrain_pointer(self, x, y):
        if not self.windows:
            return
        if self.original_cursor is None:
            original = self.backend.POINT()
            if not self.backend.user32.GetCursorPos(ctypes.byref(original)):
                raise SmokeFailure("failed to save the slider cursor before applying its guard")
            self.original_cursor = (original.x, original.y)
        if self.original_clip is None:
            original = self.backend.RECT()
            if not self.backend.user32.GetClipCursor(ctypes.byref(original)):
                raise SmokeFailure("failed to save the slider cursor clip")
            self.original_clip = (original.left, original.top, original.right, original.bottom)
        position = self.backend.POINT(int(x), int(y))
        if not self.backend.user32.ClientToScreen(ctypes.c_void_p(self.handle), ctypes.byref(position)):
            raise SmokeFailure("failed to map the slider cursor guard to screen coordinates")
        bounds = self.backend.RECT(position.x, position.y, position.x + 1, position.y + 1)
        if not self.backend.user32.ClipCursor(ctypes.byref(bounds)):
            raise SmokeFailure("failed to isolate the slider cursor at its intended position")
        self.cursor_guard = (bounds.left, bounds.top, bounds.right, bounds.bottom)

    def release_cursor_guard(self):
        if not self.windows or self.original_clip is None or self.cursor_guard is None:
            return
        bounds = self.backend.RECT(*self.original_clip)
        if not self.backend.user32.ClipCursor(ctypes.byref(bounds)):
            raise SmokeFailure("failed to restore the slider cursor clip")
        self.cursor_guard = None

    def restore_pointer(self):
        try:
            self.release_cursor_guard()
        finally:
            super().restore_pointer()
        self.original_clip = None

    def focus_loss_and_gain(self):
        self.release_cursor_guard()
        super().focus_loss_and_gain()

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
        thread_id = self.backend.user32.GetWindowThreadProcessId(ctypes.c_void_p(self.handle), None)
        info = SliderGuiThreadInfo(cbSize=ctypes.sizeof(SliderGuiThreadInfo))
        if not thread_id or not self.backend.user32.GetGUIThreadInfo(thread_id, ctypes.byref(info)):
            raise SmokeFailure("failed to observe the slider window thread capture")
        observation["capture_is_fixture"] = info.hwndCapture == self.handle
        return observation

    def command(self, name):
        """Send an application-owned policy or external-value command."""
        if name not in COMMAND_KEYS:
            raise ValueError(f"unknown slider fixture command '{name}'")
        self.record("command_before", name=name)
        self.tap(COMMAND_KEYS[name])
        self.record("command_after", name=name)
