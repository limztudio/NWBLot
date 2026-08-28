#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))

import window_capture_smoke  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    launch_captured_process,
    read_process_tail,
    require_normal_testbed_exit,
    terminate_process,
    validate_expected_log_messages,
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


class TextureSmokeAnalysisTests(unittest.TestCase):
    def test_texture_smoke_analysis_requires_each_primary_hue(self):
        rows = [
            [(240, 40, 40), (40, 230, 50), (45, 70, 238)],
            [(230, 52, 52), (52, 220, 60), (50, 85, 228)],
        ]
        analysis = window_capture_smoke.analyze_texture_smoke_rows(rows)

        self.assertEqual(analysis.red_pixels, 2)
        self.assertEqual(analysis.green_pixels, 2)
        self.assertEqual(analysis.blue_pixels, 2)

    def test_texture_smoke_analysis_isolates_red_bounce_on_the_white_receiver(self):
        rows = [[(210, 210, 210) for _ in range(100)] for _ in range(100)]
        for y in range(68, 76):
            for x in range(54, 62):
                rows[y][x] = (104, 70, 68)

        analysis = window_capture_smoke.analyze_texture_smoke_rows(rows)

        self.assertEqual(analysis.receiver_pixel_count, 15 * 14)
        self.assertEqual(analysis.receiver_red_pixels, 8 * 8)


class RuntimeLogValidationTests(unittest.TestCase):
    def test_capability_marker_is_classified_before_required_log_validation(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = "benchmark skipped because required hardware is unavailable"
            (log_directory / "test.log").write_text(marker, encoding="utf-8")

            self.assertEqual(
                validate_expected_log_messages(
                    log_directory,
                    {},
                    "*.log",
                    ["required route marker"],
                    [],
                    [marker],
                ),
                marker,
            )

    def test_missing_required_marker_is_not_a_capability_skip(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            (log_directory / "test.log").write_text("ordinary startup", encoding="utf-8")

            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "missing log message"):
                validate_expected_log_messages(
                    log_directory,
                    {},
                    "*.log",
                    ["required route marker"],
                    [],
                    ["benchmark skipped"],
                )

    def test_capability_marker_preempts_supported_route_rejection(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = "benchmark skipped because required hardware is unavailable"
            (log_directory / "test.log").write_text(
                f"{marker}\nsoftware traversal route",
                encoding="utf-8",
            )

            self.assertEqual(
                validate_expected_log_messages(
                    log_directory,
                    {},
                    "*.log",
                    ["required hardware route"],
                    ["software traversal route"],
                    [marker],
                    ["[ERROR]"],
                ),
                marker,
            )

    def test_rejected_error_takes_precedence_over_capability_skip(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = "benchmark skipped because required hardware is unavailable"
            (log_directory / "test.log").write_text(f"{marker}\n[ERROR] device failure", encoding="utf-8")

            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "blocking log message"):
                validate_expected_log_messages(
                    log_directory,
                    {},
                    "*.log",
                    ["required route marker"],
                    ["[ERROR]"],
                    [marker],
                    ["[ERROR]"],
                )


class WindowsCaptureOrderingTests(unittest.TestCase):
    def test_capture_window_prepares_before_querying_the_screen_rect(self):
        hwnd = 0x4A
        output_path = Path("capture.bmp")
        calls = []
        expected_rect = object()
        capture = object.__new__(window_capture_smoke.WindowsCapture)

        def prepare(window):
            calls.append(("prepare", window))

        def window_rect(window):
            self.assertEqual(calls, [("prepare", hwnd)])
            calls.append(("post-prepare-rect", window))
            return expected_rect

        def screen_bitblt(window, rect, path):
            calls.append(("BitBlt", window, rect, path))
            return "capture"

        capture._prepare_capture_window = prepare
        capture._window_rect = window_rect
        capture._capture_screen_rect = screen_bitblt

        self.assertEqual(capture.capture_window(hwnd, output_path), "capture")
        self.assertEqual(
            calls,
            [
                ("prepare", hwnd),
                ("post-prepare-rect", hwnd),
                ("BitBlt", hwnd, expected_rect, output_path),
            ],
        )


class CaptureFocusTests(unittest.TestCase):
    def test_linux_focus_window_raises_sets_input_focus_and_flushes(self):
        window = 0x4A
        display = object()
        calls = []
        capture = object.__new__(window_capture_smoke.LinuxX11Capture)

        class FakeX11:
            def XRaiseWindow(self, received_display, received_window):
                calls.append(("raise", received_display, received_window))

            def XSetInputFocus(self, received_display, received_window, revert_to, timestamp):
                calls.append(("focus", received_display, received_window, revert_to, timestamp))

            def XFlush(self, received_display):
                calls.append(("flush", received_display))

        capture.x11 = FakeX11()
        capture.display = display

        capture.focus_window(window)

        self.assertEqual(
            calls,
            [
                ("raise", display, window),
                ("focus", display, window, capture.REVERT_TO_PARENT, capture.CURRENT_TIME),
                ("flush", display),
            ],
        )

    def test_windows_focus_window_foregrounds_the_captured_hwnd(self):
        window = 0x4A
        calls = []
        capture = object.__new__(window_capture_smoke.WindowsCapture)

        class FakeUser32:
            def SetForegroundWindow(self, hwnd):
                calls.append(hwnd.value)
                return 1

        capture.user32 = FakeUser32()

        capture.focus_window(window)

        self.assertEqual(calls, [window])

    def test_windows_prepare_window_uses_the_capture_preparation_path(self):
        window = 0x4A
        calls = []
        capture = object.__new__(window_capture_smoke.WindowsCapture)

        def prepare(received_window):
            calls.append(received_window)

        capture._prepare_capture_window = prepare

        capture.prepare_window(window)

        self.assertEqual(calls, [window])


class GracefulTerminationTests(unittest.TestCase):
    def test_linux_x11_helper_receives_captured_window_handle(self):
        result = mock.Mock(returncode=0)
        with mock.patch.object(window_capture_smoke.subprocess, "run", return_value=result) as run:
            self.assertTrue(window_capture_smoke.request_linux_graceful_exit(0x4a))

        run.assert_called_once_with(
            [
                sys.executable,
                str(SMOKE_DIRECTORY / "x11_graceful_close.py"),
                "0x4a",
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
            terminate_process(process, "testbed", 0x4a)

        close.assert_called_once_with(0x4a)
        self.assertEqual(process.wait_timeouts, [10.0])
        self.assertEqual(process.terminate_calls, 0)

    def test_linux_x11_close_falls_back_to_sigterm_after_timeout(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit", return_value=True), \
             mock.patch.object(window_capture_smoke, "write_status") as write_status:
            terminate_process(process, "testbed", 0x4a)

        self.assertEqual(process.wait_timeouts, [10.0, 5.0])
        self.assertEqual(process.terminate_calls, 1)
        self.assertEqual(process.kill_calls, 0)
        write_status.assert_called_once_with("testbed: did not exit after X11 WM_DELETE_WINDOW; terminating")

    def test_linux_x11_helper_failure_falls_back_to_sigterm(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit", return_value=False) as close:
            terminate_process(process, "testbed", 0x4a)

        close.assert_called_once_with(0x4a)
        self.assertEqual(process.wait_timeouts, [5.0])
        self.assertEqual(process.terminate_calls, 1)

    def test_linux_without_a_captured_handle_does_not_discover_a_window_by_title(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit") as close:
            terminate_process(process, "testbed")

        close.assert_not_called()
        self.assertEqual(process.terminate_calls, 1)

    def test_windows_keeps_existing_wm_close_path(self):
        process = _FakeProcess(graceful_exit=True)
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Windows"), \
             mock.patch.object(window_capture_smoke, "request_windows_graceful_exit", return_value=True) as windows_close, \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit") as linux_close:
            terminate_process(process, "testbed", 0x4a)

        windows_close.assert_called_once_with(process.pid)
        linux_close.assert_not_called()
        self.assertEqual(process.terminate_calls, 0)

    def test_nonzero_graceful_exit_is_reported_to_the_smoke_runner(self):
        process = _FakeProcess(graceful_exit=True, graceful_exit_code=-6)
        with mock.patch.object(window_capture_smoke.platform, "system", return_value="Linux"), \
             mock.patch.object(window_capture_smoke, "request_linux_graceful_exit", return_value=True):
            exit_code, tail = terminate_process(process, "testbed", 0x4a)

        self.assertEqual(exit_code, -6)
        self.assertEqual(tail, "")
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "exit -6"):
            require_normal_testbed_exit(exit_code, tail)

    def test_missing_shutdown_exit_code_is_reported_to_the_smoke_runner(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "did not exit"):
            require_normal_testbed_exit(None, "")


if __name__ == "__main__":
    unittest.main()

