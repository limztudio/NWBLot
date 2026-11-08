"""Observe the logger window, visible rows, and active idle CPU usage."""

import ctypes
import time

from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SmokeFailure, ensure_process_running
from window_capture_win32 import WinRect


LIT_IDLE_SAMPLE_POLL_SECONDS = 0.1
LIT_WINDOW_MESSAGE_TIMEOUT_MS = 1000
LIT_LOGGER_READY_TIMEOUT_SECONDS = 10.0
LIT_LOG_ROW_POLL_SECONDS = 0.05
LIT_LOGSERVER = "logserver"
LIT_SINGLE_CORE_CPU_FRACTION = "single_core_cpu_fraction"
SAMPLE_SECONDS = 3.0
FILETIME_SECONDS = 1e-7
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
LB_GETCOUNT = 0x018B
LB_GETITEMRECT = 0x0198
SMTO_ABORTIFHUNG = 0x0002


class FileTime(ctypes.Structure):
    _fields_ = [("low", ctypes.c_uint32), ("high", ctypes.c_uint32)]


class GuiThreadInfo(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("active", ctypes.c_void_p), ("focus", ctypes.c_void_p), ("capture", ctypes.c_void_p), ("menu_owner", ctypes.c_void_p), ("move_size", ctypes.c_void_p), ("caret", ctypes.c_void_p),
        ("caret_rect", WinRect),]


class LoggerWindowEvidence:
    @staticmethod
    def bind_window_queries(user32):
        user32.GetForegroundWindow.argtypes = []
        user32.GetForegroundWindow.restype = ctypes.c_void_p
        user32.GetGUIThreadInfo.argtypes = [ctypes.c_uint32, ctypes.POINTER(GuiThreadInfo)]
        user32.GetGUIThreadInfo.restype = ctypes.c_int
        user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        user32.GetWindowThreadProcessId.restype = ctypes.c_uint32
        user32.GetDlgItem.argtypes = [ctypes.c_void_p, ctypes.c_int]
        user32.GetDlgItem.restype = ctypes.c_void_p
        user32.SendMessageTimeoutW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_size_t),]
        user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t

    @staticmethod
    def require_active_window(user32, handle):
        info = GuiThreadInfo()
        info.size = ctypes.sizeof(info)
        fixture_pid = ctypes.c_uint32()
        thread_id = user32.GetWindowThreadProcessId(handle, ctypes.byref(fixture_pid))
        if not thread_id or not user32.GetGUIThreadInfo(thread_id, ctypes.byref(info)):
            raise SmokeFailure(f"GetGUIThreadInfo failed for logger thread {thread_id}: {ctypes.get_last_error()}")
        foreground = user32.GetForegroundWindow()
        if foreground != handle or info.active != handle:
            foreground_pid = ctypes.c_uint32()
            if foreground:
                user32.GetWindowThreadProcessId(foreground, ctypes.byref(foreground_pid))
            raise SmokeFailure(f"logger window is not foreground and active: fixture={handle:#x} pid={fixture_pid.value}, "
                               f"foreground={foreground!r} pid={foreground_pid.value}, active={info.active!r}, focus={info.focus!r}")

    @staticmethod
    def sample_idle_cpu(process, backend, handle):
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        kernel32.OpenProcess.restype = ctypes.c_void_p
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        kernel32.CloseHandle.restype = ctypes.c_int
        kernel32.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(FileTime)] * 4
        kernel32.GetProcessTimes.restype = ctypes.c_int
        process_handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, process.pid)
        if not process_handle:
            raise SmokeFailure(f"OpenProcess failed: {ctypes.get_last_error()}")

        def read_times():
            creation, exit_time, kernel, user = (FileTime() for _ in range(4))
            if not kernel32.GetProcessTimes(process_handle, ctypes.byref(creation), ctypes.byref(exit_time), ctypes.byref(kernel), ctypes.byref(user),):
                raise SmokeFailure(f"GetProcessTimes failed: {ctypes.get_last_error()}")
            return ((kernel.high << 32) | kernel.low), ((user.high << 32) | user.low)

        try:
            LoggerWindowEvidence.require_active_window(backend.user32, handle)
            kernel_start, user_start = read_times()
            wall_start = time.perf_counter()
            deadline = time.monotonic() + SAMPLE_SECONDS
            foreground_checks = 0
            while time.monotonic() < deadline:
                time.sleep(min(LIT_IDLE_SAMPLE_POLL_SECONDS, max(0.0, deadline - time.monotonic())))
                ensure_process_running(process, "during idle CPU sample", LIT_LOGSERVER)
                LoggerWindowEvidence.require_active_window(backend.user32, handle)
                foreground_checks += 1
            kernel_end, user_end = read_times()
            wall_seconds = time.perf_counter() - wall_start
            kernel_seconds = (kernel_end - kernel_start) * FILETIME_SECONDS
            user_seconds = (user_end - user_start) * FILETIME_SECONDS
            return { "wall_seconds": wall_seconds, "kernel_cpu_seconds": kernel_seconds, "user_cpu_seconds": user_seconds, LIT_SINGLE_CORE_CPU_FRACTION: (kernel_seconds + user_seconds) / wall_seconds, "foreground_active_checks": foreground_checks, }
        finally:
            if not kernel32.CloseHandle(process_handle):
                raise SmokeFailure(f"CloseHandle failed: {ctypes.get_last_error()}")

    @staticmethod
    def query_log_list(backend, handle, message, index=0, address=0):
        list_handle = backend.user32.GetDlgItem(handle, 1)
        if not list_handle or not backend.user32.IsWindowVisible(list_handle):
            raise SmokeFailure("logger message list is not visible")
        result = ctypes.c_size_t()
        if not backend.user32.SendMessageTimeoutW(list_handle, message, index, address, SMTO_ABORTIFHUNG, LIT_WINDOW_MESSAGE_TIMEOUT_MS, ctypes.byref(result),):
            raise SmokeFailure(f"logger message list did not answer query 0x{message:x}")
        value = ctypes.c_ssize_t(result.value).value
        if value < 0:
            raise SmokeFailure(f"logger message list query 0x{message:x} failed")
        return value

    @staticmethod
    def wait_for_visible_log_rows(process, backend, handle, minimum_rows, timeout_seconds=LIT_LOGGER_READY_TIMEOUT_SECONDS):
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            ensure_process_running(process, "while waiting for its HTTP log row", LIT_LOGSERVER)
            if LoggerWindowEvidence.query_log_list(backend, handle, LB_GETCOUNT) >= minimum_rows:
                return
            time.sleep(LIT_LOG_ROW_POLL_SECONDS)
        raise SmokeFailure(f"HTTP log reached the file but fewer than {minimum_rows} rows are displayed")

    @staticmethod
    def capture_visible_messages(backend, handle, output, minimum_rows=1):
        capture = backend.capture_client_window(handle, output)
        width, height, rows = read_bmp_24_rows(output)
        if (width, height) != (capture.width, capture.height):
            raise SmokeFailure("saved logger capture dimensions changed")
        count = LoggerWindowEvidence.query_log_list(backend, handle, LB_GETCOUNT)
        if count < minimum_rows:
            raise SmokeFailure(f"logger displays {count} rows; expected at least {minimum_rows}")
        visible_rows = []
        for index in range(count):
            rect = WinRect()
            LoggerWindowEvidence.query_log_list(backend, handle, LB_GETITEMRECT, index, ctypes.addressof(rect))
            if rect.left < 0 or rect.top < 0 or rect.right > capture.width or rect.bottom > capture.height:
                raise SmokeFailure("log rows are outside the captured client area")
            # Check painted glyphs because owner-drawn rows store no LB_GETTEXT text.
            dark_pixels = sum(max(pixel) < 180 for row in rows[rect.top:rect.bottom] for pixel in row[max(1, rect.left):max(1, rect.right - 1)])
            if dark_pixels < 20:
                raise SmokeFailure(f"logger row {index} has no visible log text ({dark_pixels} dark pixels)")
            visible_rows.append({"index": index, "text_pixels": dark_pixels, "rect": [rect.left, rect.top, rect.right, rect.bottom]})
        return {"path": str(output), "width": capture.width, "height": capture.height, "visible_log_rows": visible_rows}

