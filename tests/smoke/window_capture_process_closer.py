import ctypes
from pathlib import Path
import subprocess
import sys

from window_capture_outcome import write_status
from window_capture_win32 import WINDOWS_ENUM_CALLBACK, create_windows_user32

LIT_CLOSE_HELPER_TIMEOUT_SECONDS = 6.0
WM_CLOSE_MESSAGE = 0x0010


class WindowProcessCloser:
    @staticmethod
    def request_windows_graceful_exit(pid):
        user32 = create_windows_user32()
        wm_close = WM_CLOSE_MESSAGE
        targets = []

        @WINDOWS_ENUM_CALLBACK
        def _enum(hwnd, _lparam):
            owner_pid = ctypes.c_uint32(0)
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner_pid))
            if owner_pid.value == pid:
                targets.append(hwnd)
            return True

        user32.EnumWindows(_enum, 0)
        for hwnd in targets:
            user32.PostMessageW(hwnd, wm_close, 0, 0)
        return bool(targets)

    @staticmethod
    def request_linux_graceful_exit(window_handle):
        if window_handle is None:
            return False

        window_id = f"0x{int(window_handle):x}"
        close_helper = Path(__file__).with_name("x11_graceful_close.py")
        try:
            result = subprocess.run([sys.executable, str(close_helper), window_id], check=False, capture_output=True, text=True, timeout=LIT_CLOSE_HELPER_TIMEOUT_SECONDS,)
        except (OSError, subprocess.TimeoutExpired) as error:
            write_status(f"Linux graceful X11 close failed for {window_id} ({error}); terminating")
            return False

        if result.returncode == 0:
            return True

        detail = (result.stderr or result.stdout).strip()
        suffix = f": {detail}" if detail else ""
        write_status(f"Linux graceful X11 close failed for {window_id} (exit {result.returncode}){suffix}; terminating")
        return False
