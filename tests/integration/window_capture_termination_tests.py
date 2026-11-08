#!/usr/bin/env python3

import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))

import window_capture_smoke
import window_capture_win32
import window_capture_process_closer
import window_capture_termination
from window_capture_process_closer import WindowProcessCloser
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import require_normal_process_exit

LIT_CREATE_WINDOWS_USER32 = "create_windows_user32"
LIT_WRITE_STATUS = "write_status"
LIT_TESTBED = "testbed"
LIT_SYSTEM = "system"
LIT_LINUX = "Linux"
LIT_REQUEST_LINUX_GRACEFUL_EXIT = "request_linux_graceful_exit"


class _FakeProcess:
    def __init__(self, graceful_exit=False, graceful_exit_code=0):
        self.pid = 4321
        self._alive = True
        self._graceful_exit = graceful_exit
        self._graceful_exit_code = graceful_exit_code
        self.terminate_calls = 0
        self.kill_calls = 0
        self.wait_timeouts = []

    def poll(self):
        return None if self._alive else self._graceful_exit_code

    def wait(self, timeout):
        self.wait_timeouts.append(timeout)
        if self._alive and self._graceful_exit:
            self._alive = False
        if self._alive:
            raise subprocess.TimeoutExpired("fake", timeout)
        return self._graceful_exit_code

    def terminate(self):
        self.terminate_calls += 1
        self._alive = False

    def kill(self):
        self.kill_calls += 1
        self._alive = False


class GracefulTerminationTests(unittest.TestCase):
    def test_windows_graceful_exit_preserves_a_high_bit_hwnd(self):
        process_id = 4321
        high_bit_hwnd = 0xF234567887654321
        observed_owner_queries = []
        observed_posts = []

        class FakeFunction:
            def __init__(self, implementation):
                self.implementation = implementation
                self.argtypes = None
                self.restype = None

            def __call__(self, *args):
                return self.implementation(*args)

        class FakeUser32:
            def __init__(self):
                self.EnumWindows = FakeFunction(lambda callback, lparam: callback(high_bit_hwnd, lparam))
                self.GetWindowThreadProcessId = FakeFunction(self.get_window_thread_process_id)
                self.PostMessageW = FakeFunction(self.post_message)

            @staticmethod
            def get_window_thread_process_id(hwnd, owner_pid):
                observed_owner_queries.append(hwnd)
                owner_pid._obj.value = process_id
                return 1

            @staticmethod
            def post_message(hwnd, message, wparam, lparam):
                observed_posts.append((hwnd, message, wparam, lparam))
                return 1

        user32 = window_capture_win32.bind_windows_user32(FakeUser32())
        with mock.patch.object(window_capture_process_closer, LIT_CREATE_WINDOWS_USER32, return_value=user32):
            self.assertTrue(WindowProcessCloser.request_windows_graceful_exit(process_id))

        self.assertEqual(observed_owner_queries, [high_bit_hwnd])
        self.assertEqual(observed_posts, [(high_bit_hwnd, 0x0010, 0, 0)])

    def test_linux_x11_close_falls_back_to_sigterm_after_timeout(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(WindowProcessCloser, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=True), \
             mock.patch.object(window_capture_termination, LIT_WRITE_STATUS) as write_status:
            CaptureProcessTermination.terminate_process(process, LIT_TESTBED, 0x4a)

        self.assertEqual(process.wait_timeouts, [10.0, 5.0])
        self.assertEqual(process.terminate_calls, 1)
        self.assertEqual(process.kill_calls, 0)
        write_status.assert_called_once_with("testbed: did not exit after X11 WM_DELETE_WINDOW; terminating")

    def test_extended_grace_period_still_forces_a_stuck_process(self):
        for host, close_name in (("Windows", "request_windows_graceful_exit"), (LIT_LINUX, LIT_REQUEST_LINUX_GRACEFUL_EXIT)):
            with self.subTest(host=host):
                process = _FakeProcess()
                with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=host), \
                     mock.patch.object(WindowProcessCloser, close_name, return_value=True), \
                     mock.patch.object(window_capture_termination, LIT_WRITE_STATUS):
                    CaptureProcessTermination.terminate_process(process, LIT_TESTBED, 0x4a, graceful_exit_timeout=60.0)
                self.assertEqual(process.wait_timeouts, [60.0, 5.0])
                self.assertEqual(process.terminate_calls, 1)

    def test_linux_x11_helper_failure_falls_back_to_sigterm(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(WindowProcessCloser, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=False) as close:
            CaptureProcessTermination.terminate_process(process, LIT_TESTBED, 0x4a)

        close.assert_called_once()
        self.assertEqual(process.wait_timeouts, [5.0])
        self.assertEqual(process.terminate_calls, 1)

    def test_linux_without_a_captured_handle_does_not_discover_a_window_by_title(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(WindowProcessCloser, LIT_REQUEST_LINUX_GRACEFUL_EXIT) as close:
            CaptureProcessTermination.terminate_process(process, LIT_TESTBED)

        close.assert_not_called()
        self.assertEqual(process.terminate_calls, 1)

    def test_nonzero_graceful_exit_is_reported_to_the_smoke_runner(self):
        process = _FakeProcess(graceful_exit=True, graceful_exit_code=-6)
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(WindowProcessCloser, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=True):
            exit_code, tail = CaptureProcessTermination.terminate_process(process, LIT_TESTBED, 0x4a)

        self.assertEqual(exit_code, -6)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "exit -6"):
            require_normal_process_exit(exit_code, tail, LIT_TESTBED)

    def test_missing_shutdown_exit_code_is_reported_to_the_smoke_runner(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "did not exit"):
            require_normal_process_exit(None, "", LIT_TESTBED)


if __name__ == "__main__":
    unittest.main()
