#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
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
        rows = [[(210, 210, 210) for _ in range(200)] for _ in range(200)]
        for y in range(40, 45):
            for x in range(97, 100):
                rows[y][x] = (104, 70, 68)
        for y in range(126, 131):
            for x in range(97, 100):
                rows[y][x] = (104, 70, 68)

        analysis = window_capture_smoke.analyze_texture_smoke_rows(rows)

        self.assertEqual(analysis.red_pixels, 2 * 3 * 5)
        self.assertEqual(analysis.receiver_pixel_count, 5 * 7)
        self.assertEqual(analysis.receiver_red_pixels, 3 * 5)

    def test_texture_smoke_validation_requires_dense_receiver_only_red_coverage(self):
        result = SimpleNamespace(
            width=1280,
            height=900,
            texture_smoke=window_capture_smoke.TextureSmokeAnalysis(300, 300, 300, 256, 1024),
        )

        window_capture_smoke.validate_texture_smoke_result(result)

        result.texture_smoke = window_capture_smoke.TextureSmokeAnalysis(300, 300, 300, 255, 1024)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "receiver_red=255"):
            window_capture_smoke.validate_texture_smoke_result(result)


class TransparentCsgAnalysisTests(unittest.TestCase):
    def test_transparent_csg_analysis_finds_sky_void_and_retained_lower_half_in_client_pixels(self):
        background = (75, 85, 101)
        rows = [[background for _ in range(100)] for _ in range(100)]
        for y in range(52, 68):
            for x in range(40, 56):
                rows[y][x] = (83, 115, 123)
        for y in range(75, 100):
            for x in range(100):
                rows[y][x] = (140, 144, 150)

        analysis = window_capture_smoke.analyze_transparent_csg_rows(rows)

        self.assertEqual(analysis.cut_void_pixels, analysis.cut_region_pixels)
        self.assertEqual(analysis.remaining_center_pixels, analysis.remaining_region_pixels)


class RuntimeLogValidationTests(unittest.TestCase):
    def test_command_line_defaults_reject_every_strict_runtime_failure(self):
        args = window_capture_smoke.parse_args(["--window-handle", "1", "--output", "capture.bmp"])

        self.assertEqual(args.reject_log_message, list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES))
        self.assertEqual(args.skip_blocking_log_message, list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES))

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


class ShutdownLogValidationTests(unittest.TestCase):
    def run_capture_with_shutdown_log(self, shutdown_log, required=(), rejected=()):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            log_path = log_directory / "test.log"
            args = SimpleNamespace(
                application_arg=[],
                executable=sys.executable,
                expect_log_message=list(required),
                expect_texture_smoke=False,
                expect_transparent_csg=False,
                expect_transparent_multi=False,
                log_port=0,
                logserver_executable=None,
                no_logserver=False,
                output=log_directory / "capture.bmp",
                reject_log_message=list(rejected),
                settle_seconds=0.0,
                skip_blocking_log_message=[],
                skip_log_message=[],
                software_vulkan="off",
                timeout=1.0,
                window_title="NWB Test",
                working_directory=log_directory,
            )
            backend = mock.Mock()
            backend.wait_for_window.return_value = 0x4A
            testbed_process = SimpleNamespace(pid=4321)
            logserver_process = object()
            events = []

            def terminate(process, name, _window_handle=None):
                if process is testbed_process:
                    events.append("testbed shutdown")
                    log_path.write_text(shutdown_log, encoding="utf-8")
                else:
                    events.append("logserver shutdown")
                return 0, ""

            def drain(*_args):
                events.append("log drain")

            with mock.patch.object(window_capture_smoke, "build_launch_environment", return_value={}), \
                 mock.patch.object(
                     window_capture_smoke,
                     "launch_logserver",
                     return_value=(logserver_process, 49152, log_directory, {}, "*.log"),
                 ), \
                 mock.patch.object(window_capture_smoke, "launch_testbed", return_value=testbed_process), \
                 mock.patch.object(window_capture_smoke, "ensure_process_running"), \
                 mock.patch.object(window_capture_smoke, "capture_checked_window", return_value="capture"), \
                 mock.patch.object(window_capture_smoke, "terminate_process", side_effect=terminate), \
                 mock.patch.object(window_capture_smoke, "wait_for_log_drain", side_effect=drain):
                result = window_capture_smoke.launch_and_capture(args, backend)

            self.assertEqual(events, ["testbed shutdown", "log drain", "logserver shutdown"])
            return result

    def test_shutdown_marker_is_validated_after_graceful_exit(self):
        self.assertEqual(
            self.run_capture_with_shutdown_log("ProjectTestbed: shutdown", required=("ProjectTestbed: shutdown",)),
            "capture",
        )

    def test_teardown_warning_fails_after_graceful_exit(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "rejected log message"):
            self.run_capture_with_shutdown_log("[WARNING] teardown failure", rejected=("[WARNING]",))


class WindowsCaptureOrderingTests(unittest.TestCase):
    def test_capture_window_prepares_before_querying_the_client_screen_rect(self):
        hwnd = 0x4A
        output_path = Path("capture.bmp")
        calls = []
        expected_rect = object()
        capture = object.__new__(window_capture_smoke.WindowsCapture)

        def prepare(window):
            calls.append(("prepare", window))

        def client_rect(window):
            self.assertEqual(calls, [("prepare", hwnd)])
            calls.append(("post-prepare-client-rect", window))
            return expected_rect

        def screen_bitblt(window, rect, path):
            calls.append(("BitBlt", window, rect, path))
            return "capture"

        capture._prepare_capture_window = prepare
        capture._client_rect = client_rect
        capture._capture_screen_rect = screen_bitblt

        self.assertEqual(capture.capture_window(hwnd, output_path), "capture")
        self.assertEqual(
            calls,
            [
                ("prepare", hwnd),
                ("post-prepare-client-rect", hwnd),
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

