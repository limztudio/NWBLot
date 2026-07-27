#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SMOKE_DIRECTORY = Path(__file__).resolve().parent
sys.path.insert(0, str(SMOKE_DIRECTORY))

import testbed_window_capture_smoke as window_capture_smoke  # noqa: E402
from testbed_window_capture_smoke import (  # noqa: E402
    launch_captured_process,
    read_process_tail,
    require_normal_testbed_exit,
    terminate_process,
)


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


class ProcessOutputCaptureTests(unittest.TestCase):
    def test_large_output_does_not_block_and_tail_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            working_directory = Path(temp_dir)
            script = (
                "import sys\n"
                "sys.stdout.buffer.write(b'x' * (512 * 1024))\n"
                "sys.stdout.flush()\n"
                "sys.stderr.write('\\nwindow-capture-output-sentinel\\n')\n"
                "sys.stderr.flush()\n"
            )
            process = launch_captured_process(
                [sys.executable, "-c", script],
                working_directory,
                os.environ.copy(),
                "unit",
            )
            capture = process._nwb_output_capture
            try:
                self.assertIsNone(process.stdout)
                self.assertEqual(process.wait(timeout=5.0), 0)
                self.assertTrue(capture.path.exists())
                self.assertIn("window-capture-output-sentinel", read_process_tail(process))
            finally:
                terminate_process(process, "unit")

            self.assertFalse(capture.path.exists())


class GracefulTerminationTests(unittest.TestCase):
    def test_linux_x11_helper_receives_testbed_window_title(self):
        result = mock.Mock(returncode=0)
        with mock.patch.object(window_capture_smoke.subprocess, "run", return_value=result) as run:
            self.assertTrue(window_capture_smoke.request_linux_graceful_exit("NWB Testbed"))

        run.assert_called_once_with(
            [
                sys.executable,
                str(SMOKE_DIRECTORY / "x11_graceful_close.py"),
                "NWB Testbed",
                "5.0",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=6.0,
        )

    def test_linux_x11_close_waits_for_normal_exit_before_fallback(self):
        process = _FakeProcess(graceful_exit=True)
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit", return_value=True) as close:
            terminate_process(process, "testbed", "NWB Testbed")

        close.assert_called_once_with("NWB Testbed")
        self.assertEqual(process.wait_timeouts, [10.0])
        self.assertEqual(process.terminate_calls, 0)

    def test_linux_x11_close_falls_back_to_sigterm_after_timeout(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit", return_value=True), \
             mock.patch.object(window_capture_smoke, "write_status") as write_status:
            terminate_process(process, "testbed", "NWB Testbed")

        self.assertEqual(process.wait_timeouts, [10.0, 5.0])
        self.assertEqual(process.terminate_calls, 1)
        self.assertEqual(process.kill_calls, 0)
        write_status.assert_called_once_with("testbed: did not exit after X11 WM_DELETE_WINDOW; terminating")

    def test_linux_x11_helper_failure_falls_back_to_sigterm(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit", return_value=False) as close:
            terminate_process(process, "testbed", "NWB Testbed")

        close.assert_called_once_with("NWB Testbed")
        self.assertEqual(process.wait_timeouts, [5.0])
        self.assertEqual(process.terminate_calls, 1)

    def test_windows_keeps_existing_wm_close_path(self):
        process = _FakeProcess(graceful_exit=True)
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Windows"), \
             mock.patch.object(window_capture_smoke, "request_windows_graceful_exit", return_value=True) as windows_close, \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit") as linux_close:
            terminate_process(process, "testbed", "NWB Testbed")

        windows_close.assert_called_once_with(process.pid)
        linux_close.assert_not_called()
        self.assertEqual(process.terminate_calls, 0)

    def test_nonzero_graceful_exit_is_reported_to_the_smoke_runner(self):
        process = _FakeProcess(graceful_exit=True, graceful_exit_code=-6)
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit", return_value=True):
            exit_code, tail = terminate_process(process, "testbed", "NWB Testbed")

        self.assertEqual(exit_code, -6)
        self.assertEqual(tail, "")
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "exit -6"):
            require_normal_testbed_exit(exit_code, tail)

    def test_missing_shutdown_exit_code_is_reported_to_the_smoke_runner(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "did not exit"):
            require_normal_testbed_exit(None, "")


if __name__ == "__main__":
    unittest.main()

