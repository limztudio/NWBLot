"""Bounded Win32 and X11 clipboard reads for native edit smoke tests."""
from __future__ import annotations

import ctypes
import time

from window_capture_smoke import SmokeFailure

LIT_TYPE = "type"
LIT_SELECTION = "selection"
CLIPBOARD_READ_TIMEOUT_SECONDS = 2.0
CLIPBOARD_POLL_SECONDS = 0.02
CLIPBOARD_MAX_BYTES = 65536


class NumericSelectionEvent(ctypes.Structure):
    _fields_ = [(LIT_TYPE, ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int), ("display", ctypes.c_void_p), ("requestor", ctypes.c_ulong), (LIT_SELECTION, ctypes.c_ulong),
        ("target", ctypes.c_ulong), ("property", ctypes.c_ulong), ("time", ctypes.c_ulong)]


class NumericClipboardEvent(ctypes.Union):
    _fields_ = [(LIT_TYPE, ctypes.c_int), (LIT_SELECTION, NumericSelectionEvent), ("pad", ctypes.c_long * 24)]


class NumericClipboardReader:
    def __init__(self, backend, windows):
        self.backend = backend
        self.windows = windows
        if self.windows:
            backend.user32.OpenClipboard.argtypes = [ctypes.c_void_p]
            backend.user32.OpenClipboard.restype = ctypes.c_int
            backend.user32.CloseClipboard.argtypes = []
            backend.user32.CloseClipboard.restype = ctypes.c_int
            backend.user32.IsClipboardFormatAvailable.argtypes = [ctypes.c_uint]
            backend.user32.IsClipboardFormatAvailable.restype = ctypes.c_int
            backend.user32.GetClipboardData.argtypes = [ctypes.c_uint]
            backend.user32.GetClipboardData.restype = ctypes.c_void_p
            self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            self.kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
            self.kernel32.GlobalLock.restype = ctypes.c_void_p
            self.kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
            self.kernel32.GlobalUnlock.restype = ctypes.c_int
            self.kernel32.GlobalSize.argtypes = [ctypes.c_void_p]
            self.kernel32.GlobalSize.restype = ctypes.c_size_t
        else:
            x11 = backend.x11
            x11.XCreateSimpleWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_ulong]
            x11.XCreateSimpleWindow.restype = ctypes.c_ulong
            x11.XDestroyWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
            x11.XDestroyWindow.restype = ctypes.c_int
            x11.XGetSelectionOwner.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
            x11.XGetSelectionOwner.restype = ctypes.c_ulong
            x11.XConvertSelection.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
            x11.XConvertSelection.restype = ctypes.c_int
            x11.XCheckTypedWindowEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.POINTER(NumericClipboardEvent)]
            x11.XCheckTypedWindowEvent.restype = ctypes.c_int


    def read_text(self, *, timeout=CLIPBOARD_READ_TIMEOUT_SECONDS, max_bytes=CLIPBOARD_MAX_BYTES):
        """Read CLIPBOARD/CF_UNICODETEXT without replacing its owner or data."""
        if max_bytes <= 0 or max_bytes > CLIPBOARD_MAX_BYTES:
            raise ValueError(f"numeric clipboard read bound must be between 1 and {CLIPBOARD_MAX_BYTES} bytes")
        if self.windows:
            return self._windows_clipboard_text(timeout, max_bytes)
        return self._x11_clipboard_text(timeout, max_bytes)

    def _windows_clipboard_text(self, timeout, max_bytes):
        deadline = time.monotonic() + timeout
        while not self.backend.user32.OpenClipboard(None):
            if time.monotonic() >= deadline:
                raise SmokeFailure("OpenClipboard remained unavailable during numeric copy verification")
            time.sleep(CLIPBOARD_POLL_SECONDS)
        try:
            if not self.backend.user32.IsClipboardFormatAvailable(13):
                return None
            handle = self.backend.user32.GetClipboardData(13)
            if not handle:
                raise SmokeFailure("GetClipboardData(CF_UNICODETEXT) failed")
            size = self.kernel32.GlobalSize(handle)
            if size < 2 or size > max_bytes or size % 2:
                raise SmokeFailure(f"numeric clipboard UTF-16 payload has an invalid byte size: {size}")
            data = self.kernel32.GlobalLock(handle)
            if not data:
                raise SmokeFailure("GlobalLock failed for numeric clipboard text")
            try:
                payload = ctypes.string_at(data, size)
            finally:
                self.kernel32.GlobalUnlock(handle)
            terminator = next((offset for offset in range(0, size, 2) if payload[offset:offset + 2] == b"\0\0"), None)
            if terminator is None:
                raise SmokeFailure("numeric clipboard UTF-16 text is not terminated")
            return payload[:terminator].decode("utf-16-le")
        finally:
            self.backend.user32.CloseClipboard()

    def _x11_clipboard_text(self, timeout, max_bytes):
        x11, display = self.backend.x11, self.backend.display
        selection = x11.XInternAtom(display, b"CLIPBOARD", False)
        target = x11.XInternAtom(display, b"UTF8_STRING", False)
        property_atom = x11.XInternAtom(display, b"NWB_NUMERIC_SMOKE_CLIPBOARD", False)
        if not x11.XGetSelectionOwner(display, selection):
            return None
        requestor = x11.XCreateSimpleWindow(display, self.backend.root, 0, 0, 1, 1, 0, 0, 0)
        if not requestor:
            raise SmokeFailure("could not create the X11 numeric clipboard requestor")
        try:
            x11.XConvertSelection(display, selection, target, property_atom, requestor, 0)
            x11.XFlush(display)
            deadline, event = time.monotonic() + timeout, NumericClipboardEvent()
            while not x11.XCheckTypedWindowEvent(display, requestor, 31, ctypes.byref(event)):
                if time.monotonic() >= deadline:
                    raise SmokeFailure("X11 numeric clipboard conversion did not complete")
                time.sleep(CLIPBOARD_POLL_SECONDS)
            if not event.selection.property:
                return None
            actual_type, actual_format = ctypes.c_ulong(), ctypes.c_int()
            count, remaining, data = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.POINTER(ctypes.c_ubyte)()
            status = x11.XGetWindowProperty(display, requestor, property_atom, 0, (max_bytes + 3) // 4, True, target, ctypes.byref(actual_type), ctypes.byref(actual_format), ctypes.byref(count), ctypes.byref(remaining), ctypes.byref(data))
            try:
                if status != 0 or actual_type.value != target or actual_format.value != 8:
                    raise SmokeFailure("X11 numeric clipboard did not return UTF8_STRING bytes")
                if remaining.value or count.value > max_bytes:
                    raise SmokeFailure("X11 numeric clipboard text exceeds the bounded read")
                return ctypes.string_at(data, count.value).decode("utf-8") if count.value else ""
            finally:
                if data:
                    x11.XFree(data)
        finally:
            x11.XDestroyWindow(display, requestor)
            x11.XFlush(display)
