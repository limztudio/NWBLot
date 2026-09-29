"""Positioned native input with a saved and restored Win32 cursor constraint."""
from __future__ import annotations

import ctypes

from popup_tools_native import PopupToolsNativeInput
from window_capture_smoke import SmokeFailure


class GuardedNativeInput(PopupToolsNativeInput):
    def __init__(self, backend, handle):
        super().__init__(backend, handle)
        self.original_clip = None
        self.cursor_guard = None
        if self.windows:
            backend.user32.GetClipCursor.argtypes = [ctypes.POINTER(backend.RECT)]
            backend.user32.GetClipCursor.restype = ctypes.c_int
            backend.user32.ClipCursor.argtypes = [ctypes.POINTER(backend.RECT)]
            backend.user32.ClipCursor.restype = ctypes.c_int

    def pointer(self, x, y):
        self.constrain_pointer(x, y)
        super().pointer(x, y)

    def constrain_pointer(self, x, y):
        if not self.windows:
            return
        if self.original_cursor is None:
            original = self.backend.POINT()
            if not self.backend.user32.GetCursorPos(ctypes.byref(original)):
                raise SmokeFailure("failed to save the native cursor before applying its guard")
            self.original_cursor = (original.x, original.y)
        if self.original_clip is None:
            original = self.backend.RECT()
            if not self.backend.user32.GetClipCursor(ctypes.byref(original)):
                raise SmokeFailure("failed to save the native cursor clip")
            self.original_clip = (original.left, original.top, original.right, original.bottom)
        position = self.backend.POINT(int(x), int(y))
        if not self.backend.user32.ClientToScreen(ctypes.c_void_p(self.handle), ctypes.byref(position)):
            raise SmokeFailure("failed to map the native cursor guard to screen coordinates")
        bounds = self.backend.RECT(position.x, position.y, position.x + 1, position.y + 1)
        if not self.backend.user32.ClipCursor(ctypes.byref(bounds)):
            raise SmokeFailure("failed to isolate the native cursor at its intended position")
        self.cursor_guard = (bounds.left, bounds.top, bounds.right, bounds.bottom)

    def release_cursor_guard(self):
        if not self.windows or self.original_clip is None or self.cursor_guard is None:
            return
        bounds = self.backend.RECT(*self.original_clip)
        if not self.backend.user32.ClipCursor(ctypes.byref(bounds)):
            raise SmokeFailure("failed to restore the native cursor clip")
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
