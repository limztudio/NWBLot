"""Cross-platform native input and foreground/capture tracking for UI smoke fixtures."""
from __future__ import annotations

import ctypes
import platform
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from window_capture_smoke import SmokeFailure, SmokeSkip, ensure_process_running
from window_capture_win32 import WinRect
from window_capture_x11_input import LinuxXEvent

LIT_FLAGS = "flags"
LIT_TIME = "time"
LIT_EXTRA = "extra"
LIT_DATA = "data"
VIRTUAL_KEYS = {"Tab": 0x09, "Return": 0x0D, "space": 0x20, "Escape": 0x1B}
KEY_REPEAT_INTERVAL_SECONDS = 0.04
PRESS_HOLD_SECONDS = 0.08
INPUT_SETTLE_SECONDS = 0.1
FOCUS_SETTLE_SECONDS = 0.15
WIN32_WORD_MASK = 0xFFFF


class WinKeyboardInput(ctypes.Structure):
    _fields_ = [("virtual_key", ctypes.c_uint16), ("scan", ctypes.c_uint16), (LIT_FLAGS, ctypes.c_uint32), (LIT_TIME, ctypes.c_uint32), (LIT_EXTRA, ctypes.c_void_p)]


class WinMouseInput(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int32), ("y", ctypes.c_int32), (LIT_DATA, ctypes.c_uint32), (LIT_FLAGS, ctypes.c_uint32), (LIT_TIME, ctypes.c_uint32), (LIT_EXTRA, ctypes.c_void_p)]


class WinInputUnion(ctypes.Union):
    _fields_ = [("keyboard", WinKeyboardInput), ("mouse", WinMouseInput)]


class WinInput(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint32), (LIT_DATA, WinInputUnion)]


class WinGuiThreadInfo(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_uint32), (LIT_FLAGS, ctypes.c_uint32), ("hwndActive", ctypes.c_void_p), ("hwndFocus", ctypes.c_void_p), ("hwndCapture", ctypes.c_void_p),
        ("hwndMenuOwner", ctypes.c_void_p), ("hwndMoveSize", ctypes.c_void_p), ("hwndCaret", ctypes.c_void_p), ("rcCaret", WinRect),]


class NativeInput:
    def __init__(self, backend, handle):
        self.backend = backend
        self.handle = handle
        self.windows = platform.system() == "Windows"
        if self.windows:
            backend.user32.GetForegroundWindow.argtypes = []
            backend.user32.GetForegroundWindow.restype = ctypes.c_void_p
            backend.user32.SendInput.argtypes = [ctypes.c_uint32, ctypes.POINTER(WinInput), ctypes.c_int]
            backend.user32.SendInput.restype = ctypes.c_uint32
            backend.user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
            backend.user32.GetWindowThreadProcessId.restype = ctypes.c_uint32
            backend.user32.GetGUIThreadInfo.argtypes = [ctypes.c_uint32, ctypes.POINTER(WinGuiThreadInfo)]
            backend.user32.GetGUIThreadInfo.restype = ctypes.c_int

    def wait_for_foreground(self, process, timeout):
        if not self.windows:
            return
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ensure_process_running(process, "while waiting for foreground focus")
            self.backend.focus_window(self.handle)
            if self.backend.user32.GetForegroundWindow() == self.handle:
                return
            time.sleep(INPUT_SETTLE_SECONDS)
        raise SmokeSkip("the fixture could not acquire foreground input focus")

    def capture_owner(self):
        thread_id = self.backend.user32.GetWindowThreadProcessId(ctypes.c_void_p(self.handle), None)
        info = WinGuiThreadInfo(cbSize=ctypes.sizeof(WinGuiThreadInfo))
        if not thread_id or not self.backend.user32.GetGUIThreadInfo(thread_id, ctypes.byref(info)):
            raise SmokeFailure(f"failed to observe capture for window {self.handle:#x}, " f"thread {thread_id}: Win32 error {ctypes.get_last_error()}")
        return info.hwndCapture

    def _post(self, message, parameter=0, data=0):
        if not self.backend.user32.PostMessageW(ctypes.c_void_p(self.handle), message, parameter, data):
            raise SmokeFailure(f"PostMessageW failed for message {message:#x}")

    def pointer(self, x, y):
        if self.windows:
            self._post(0x0200, 0, ((int(y) & WIN32_WORD_MASK) << 16) | (int(x) & WIN32_WORD_MASK))
        else:
            self.backend.send_motion_event(self.handle, int(x), int(y))

    def button(self, down, x, y):
        self.pointer(x, y)
        if self.windows:
            self._post(0x0201 if down else 0x0202, 1 if down else 0, ((int(y) & WIN32_WORD_MASK) << 16) | (int(x) & WIN32_WORD_MASK))
        else:
            event_type = self.backend.BUTTON_PRESS if down else self.backend.BUTTON_RELEASE
            self.backend.send_button_event(self.handle, event_type, int(x), int(y))

    def click(self, x, y):
        self.button(True, x, y)
        time.sleep(PRESS_HOLD_SECONDS)
        self.button(False, x, y)

    def key(self, name, down, *, repeat=False, shift=False):
        if self.windows:
            self._post(0x0100 if down else 0x0101, VIRTUAL_KEYS[name], 1 | ((1 << 30) if repeat or not down else 0) | ((1 << 31) if not down else 0))
            return
        keysym = self.backend.x11.XStringToKeysym(name.encode("ascii"))
        keycode = self.backend.x11.XKeysymToKeycode(self.backend.display, keysym)
        if not keycode:
            raise SmokeFailure(f"could not resolve X11 key '{name}'")
        event = LinuxXEvent()
        event.xkey.type = self.backend.KEY_PRESS if down else self.backend.KEY_RELEASE
        self.backend._fill_input_event_prefix(event.xkey, self.handle, 0, 0, state=1 if shift else 0)
        event.xkey.keycode = keycode
        mask = self.backend.KEY_PRESS_MASK if down else self.backend.KEY_RELEASE_MASK
        if not self.backend.x11.XSendEvent(self.backend.display, self.handle, False, mask, ctypes.byref(event)):
            raise SmokeFailure(f"failed to send X11 key '{name}'")
        self.backend.x11.XFlush(self.backend.display)

    def _shift(self, down):
        if not self.windows:
            return
        event = WinInput(type=1)
        event.data.keyboard = WinKeyboardInput(0x10, 0, 0 if down else 0x0002, 0, None)
        if self.backend.user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(WinInput)) != 1:
            raise SmokeFailure("SendInput failed for Shift modifier")

    def tap(self, name, *, repeat_count=0, shift=False):
        if shift and self.windows:
            self.backend.focus_window(self.handle)
            time.sleep(INPUT_SETTLE_SECONDS)
            if self.backend.user32.GetForegroundWindow() != self.handle:
                raise SmokeSkip("the fixture cannot take foreground focus for native Shift+Tab")
            self._shift(True)
            time.sleep(INPUT_SETTLE_SECONDS)
        try:
            self.key(name, True, shift=shift)
            for _ in range(repeat_count):
                time.sleep(KEY_REPEAT_INTERVAL_SECONDS)
                self.key(name, True, repeat=True, shift=shift)
            time.sleep(PRESS_HOLD_SECONDS)
            self.key(name, False, shift=shift)
        finally:
            if shift and self.windows:
                self._shift(False)
        time.sleep(INPUT_SETTLE_SECONDS)

    def focus_loss_and_gain(self):
        if self.windows:
            self._post(0x0006, 0)  # WM_ACTIVATE / WA_INACTIVE reaches the real native lifecycle callback.
            time.sleep(FOCUS_SETTLE_SECONDS)
            self._post(0x0006, 1)
        else:
            self.backend.x11.XSetInputFocus(self.backend.display, self.backend.root, self.backend.REVERT_TO_PARENT, self.backend.CURRENT_TIME)
            self.backend.x11.XFlush(self.backend.display)
            time.sleep(FOCUS_SETTLE_SECONDS)
            self.backend.focus_window(self.handle)
        time.sleep(FOCUS_SETTLE_SECONDS)


