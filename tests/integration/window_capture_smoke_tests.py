#!/usr/bin/env python3


from contextlib import redirect_stderr, redirect_stdout
import ctypes
import io
import json
import os
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

LIT_UI_LAYER_DIRECTORY = "ui_layer"

LIT_OUTPUT_CAPTURE_TIMEOUT_SECONDS = 5.0

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / LIT_UI_LAYER_DIRECTORY))

import window_capture_logs  # noqa: E402
import window_capture_image  # noqa: E402
import window_capture_outcome  # noqa: E402
import window_capture_smoke  # noqa: E402
import window_capture_win32  # noqa: E402
import interaction_smoke  # noqa: E402
import csg_gi_temporal_smoke  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    ensure_process_running, launch_captured_process, read_process_tail, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_messages,)


LIT_TIMEOUT = "--timeout"
LIT_RENDER_READY_TIMEOUT = "--render-ready-timeout"
LIT_CREATE_WINDOWS_USER32 = "create_windows_user32"
LIT_INVISIBLE_STATION = "invisible station"
LIT_PASSED = "passed"
LIT_SKIPPED = "skipped"
LIT_TEMPORAL_CAPTURE_FAILED_WITH_EXIT_1 = "temporal capture failed with exit 1"
LIT_UNIT = "unit"
LIT_CAPTURE = "capture"
LIT_I = "<I"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_APPLICATION_CAPTURE = "--application-capture"
LIT_EXECUTABLE = "--executable"
LIT_OUTPUT = "--output"
LIT_LAUNCH_AND_CAPTURE_APPLICATION = "launch_and_capture_application"
LIT_CREATE_CAPTURE_BACKEND = "create_capture_backend"
LIT_WRITE_STATUS = "write_status"
LIT_WINDOW_HANDLE = "--window-handle"
LIT_SYS_STDERR = "sys.stderr"
LIT_BENCHMARK_SKIPPED_BECAUSE_REQUIRED_HAR = "benchmark skipped because required hardware is unavailable"
LIT_TEST_LOG = "test.log"
LIT_UTF_8 = "utf-8"
LIT_LOG = "*.log"
LIT_REQUIRED_ROUTE_MARKER = "required route marker"
LIT_MISSING_LOG_MESSAGE = "missing log message"
LIT_ERROR = "[ERROR]"
LIT_BLOCKING_LOG_MESSAGE = "blocking log message"
LIT_LOGSERVER = "logserver"
LIT_ORDINARY_STARTUP = "ordinary startup"
LIT_SHUTDOWN = "shutdown"
LIT_DRAIN = "drain"
LIT_COLLECT = "collect"
LIT_WARNING_SHUTDOWN_ONLY_WARNING = "[WARNING] shutdown-only warning"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_WAIT_FOR_LOG_DRAIN = "wait_for_log_drain"
LIT_COLLECT_LOG_DELTA = "collect_log_delta"
LIT_LOGS = "logs"
LIT_WARNING = "[WARNING]"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_OFF = "off"
LIT_TESTBED_SHUTDOWN = "testbed shutdown"
LIT_LOGSERVER_SHUTDOWN = "logserver shutdown"
LIT_LOG_DRAIN = "log drain"
LIT_CAPTURE_RENDER_READY_WINDOW = "capture_render_ready_window"
LIT_PROJECTTESTBED_SHUTDOWN = "ProjectTestbed: shutdown"
LIT_TESTBED = "testbed"
LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT = "wait_for_application_capture_exit"
LIT_SYSTEM = "system"
LIT_LINUX = "Linux"
LIT_REQUEST_LINUX_GRACEFUL_EXIT = "request_linux_graceful_exit"
LIT_WINDOWS = "Windows"
LIT_RESIZE_CLIENT = "--resize-client"
LIT_N_1001 = "1001"
LIT_N_701 = "701"
LIT_SETTLE_SECONDS = "--settle-seconds"
LIT_NAN = "nan"
LIT_RESIZE_SETTLE_SECONDS = "--resize-settle-seconds"
LIT_INF = "inf"
LIT_MAIN = "__main__"


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


class FailureReportTests(unittest.TestCase):
    def test_unwritable_report_keeps_primary_exception(self):
        from smoke_failure_reporting import write_failure_report

        def broken_writer(_path, _value):
            raise PermissionError("report denied")

        captured = io.StringIO()
        with redirect_stderr(captured), self.assertRaisesRegex(ValueError, "primary failure"):
            try:
                raise ValueError("primary failure")
            except ValueError:
                write_failure_report(broken_writer, Path("failure.json"), {})
                raise
        self.assertIn("report denied", captured.getvalue())


class DepthWarpFailureReportTests(unittest.TestCase):
    def test_unwritable_skip_report_preserves_skip_exit(self):
        import avboit_depth_warp_probe_smoke as probe

        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(output_directory=Path(root))
            captured = io.StringIO()
            with mock.patch.object(probe, "parse_args", return_value=args), \
                    mock.patch.object(probe, "acquire", side_effect=probe.WindowsDesktopUnavailable("desktop unavailable")), \
                    mock.patch.object(probe, "write_json", side_effect=OSError("report denied")), \
                    redirect_stderr(captured), redirect_stdout(io.StringIO()):
                self.assertEqual(probe.main([]), probe.SKIP_EXIT_CODE)
            self.assertIn("desktop unavailable", captured.getvalue())
            self.assertIn("report denied", captured.getvalue())


class LogEvidenceFailureTests(unittest.TestCase):
    def test_unreadable_log_directory_is_not_an_empty_snapshot_or_delta(self):
        with mock.patch.object(window_capture_logs.os, "scandir", side_effect=PermissionError("log directory denied")):
            with self.assertRaisesRegex(PermissionError, "log directory denied"):
                window_capture_logs.snapshot_log_files(Path("logs"), "*.log")
            with self.assertRaisesRegex(PermissionError, "log directory denied"):
                window_capture_logs.collect_log_delta(Path("logs"), {}, "*.log")

    def test_truncated_or_unreadable_log_cannot_be_omitted(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            path = directory / "current.log"
            path.write_text("original data", encoding="utf-8")
            baseline = window_capture_logs.snapshot_log_files(directory, "*.log")
            path.write_text("new", encoding="utf-8")
            with self.assertRaisesRegex(window_capture_logs.SmokeFailure, "shrank"):
                window_capture_logs.collect_log_delta(directory, baseline, "*.log")
            with mock.patch.object(Path, "open", side_effect=PermissionError("log denied")):
                with self.assertRaisesRegex(PermissionError, "log denied"):
                    window_capture_logs.collect_log_delta(directory, {}, "*.log")

    def test_replaced_regrown_or_removed_baseline_cannot_hide_log_output(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            path = directory / "one.log"
            path.write_bytes(b"old record")
            baseline = window_capture_logs.snapshot_log_files(directory, "*.log")
            path.write_bytes(b"new error record")
            with self.assertRaisesRegex(window_capture_logs.SmokeFailure, "changed before its baseline"):
                window_capture_logs.collect_log_delta(directory, baseline, "*.log")
            path.rename(directory / "previous.txt")
            with self.assertRaisesRegex(window_capture_logs.SmokeFailure, "disappeared"):
                window_capture_logs.collect_log_delta(directory, baseline, "*.log")
            path.write_bytes(b"replacement record")
            with self.assertRaisesRegex(window_capture_logs.SmokeFailure, "replaced"):
                window_capture_logs.collect_log_delta(directory, baseline, "*.log")

    def test_failed_poll_during_failed_termination_retains_capture(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=mock.Mock(side_effect=[OSError("first poll"), OSError("final poll")]),
                _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
            output = io.StringIO()
            with redirect_stderr(output), self.assertRaisesRegex(OSError, "first poll"):
                terminate_process(process, LIT_UNIT)
            self.assertIn("final poll", output.getvalue())
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())
            self.assertIsNone(process._nwb_output_capture)

    def test_resize_does_not_capture_before_render_marker(self):
        args = SimpleNamespace(expect_before_resize_log_message="painted", render_ready_timeout=1.0)
        backend = mock.Mock()
        with mock.patch.object(window_capture_smoke, "wait_for_log_message", side_effect=window_capture_smoke.SmokeFailure("marker missing")), \
                mock.patch.object(window_capture_smoke, "capture_render_ready_window") as capture:
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "marker missing"):
                window_capture_smoke.capture_resized_window(args, backend, 1, mock.Mock(), Path("logs"), {}, "*.log")
        capture.assert_not_called()

    def test_close_failure_retains_capture_and_is_reported(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            path.write_bytes(b"evidence")
            stream = mock.Mock()
            stream.close.side_effect = OSError("close denied")
            capture = window_capture_smoke.ProcessOutputCapture(path, stream)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "close denied"):
                window_capture_smoke.dispose_process_output_capture(capture)
            self.assertEqual(path.read_bytes(), b"evidence")

    def test_failed_capture_cleanup_attempts_both_and_retains_primary(self):
        output = io.StringIO()
        with mock.patch.object(window_capture_smoke, "terminate_process", side_effect=[OSError("app cleanup"), OSError("logger cleanup")]) as terminate, redirect_stderr(output):
            window_capture_smoke.cleanup_capture_processes(mock.sentinel.app, 7, mock.sentinel.logger, ValueError("capture failed"))
        self.assertEqual(terminate.call_count, 2)
        self.assertIn("app cleanup", output.getvalue())
        self.assertIn("logger cleanup", output.getvalue())

    def test_early_process_diagnostic_across_chunk_boundary_is_retained(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            stream.write(b"x" * 65533 + b"[ERROR] early\n" + b"y" * 70000)
            stream.flush()
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "forbidden message"):
                terminate_process(process, LIT_UNIT, rejected_output=("[ERROR]",))
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())

    def test_cancelled_readiness_stops_owned_logger(self):
        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(no_logserver=False, logserver_executable=Path(root) / "logger", log_port=1, working_directory=Path(root), timeout=1)
            process = object()
            with mock.patch.object(window_capture_smoke, "snapshot_log_files", return_value={}), \
                    mock.patch.object(window_capture_smoke, "launch_captured_process", return_value=process), \
                    mock.patch.object(window_capture_smoke, "wait_for_tcp_port", side_effect=KeyboardInterrupt), \
                    mock.patch.object(window_capture_smoke, "terminate_process", return_value=(0, "")) as terminate:
                with self.assertRaises(KeyboardInterrupt):
                    window_capture_smoke.launch_logserver(args, Path(root) / "app", {})
            terminate.assert_called_once_with(process, window_capture_smoke.LIT_LOGSERVER, preserve_capture=True)

    def test_nonzero_output_diagnostic_keeps_exit_and_capture_path(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            stream.write(b"[ERROR] failed")
            stream.flush()
            process = SimpleNamespace(poll=lambda: 9, _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "exited with code 9.*forbidden") as failure:
                terminate_process(process, LIT_UNIT, rejected_output=("[ERROR]",))
            self.assertIn(str(path), str(failure.exception))
            self.assertTrue(path.exists())

    def test_skip_only_output_blocks_skip_but_not_success(self):
        for skip in (None, "unsupported"):
            with self.subTest(skip=skip), tempfile.TemporaryDirectory() as root:
                path = Path(root) / "process.log"
                stream = path.open("w+b")
                stream.write(b"custom blocker")
                stream.flush()
                process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
                observed = []
                terminate_process(process, LIT_UNIT, observed_output=observed, observed_needles=("custom blocker",))
                self.assertTrue(path.exists())
                if skip:
                    with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "blocking process output"):
                        window_capture_smoke.finish_observed_output(skip, observed)
                    self.assertTrue(path.exists())
                else:
                    window_capture_smoke.finish_observed_output(skip, observed)
                    self.assertFalse(path.exists())

    def test_late_logger_cleanup_failure_retains_both_captures(self):
        with tempfile.TemporaryDirectory() as root:
            first_path = Path(root) / "app.log"
            second_path = Path(root) / "logger.log"
            first = SimpleNamespace(_nwb_output_capture=window_capture_smoke.ProcessOutputCapture(first_path, first_path.open("w+b")))
            second = SimpleNamespace(_nwb_output_capture=window_capture_smoke.ProcessOutputCapture(second_path, second_path.open("w+b")))
            def terminate(process, _name, _window, **_kwargs):
                process._nwb_output_capture.stream.close()
                if process is second:
                    raise OSError("logger cleanup failed")
            with mock.patch.object(window_capture_smoke, "terminate_process", side_effect=terminate), redirect_stderr(io.StringIO()):
                with self.assertRaisesRegex(OSError, "logger cleanup failed"):
                    window_capture_smoke.cleanup_capture_processes(first, None, second, None)
            self.assertTrue(first_path.exists())
            self.assertTrue(second_path.exists())

    def test_failed_start_still_stops_logger_when_capture_is_unreadable(self):
        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(no_logserver=False, logserver_executable=Path(root) / "logger", log_port=1, working_directory=Path(root), timeout=1)
            process = object()
            with mock.patch.object(window_capture_smoke, "snapshot_log_files", return_value={}), \
                    mock.patch.object(window_capture_smoke, "launch_captured_process", return_value=process), \
                    mock.patch.object(window_capture_smoke, "wait_for_tcp_port", side_effect=window_capture_smoke.SmokeFailure("port failed")), \
                    mock.patch.object(window_capture_smoke, "terminate_process", side_effect=window_capture_smoke.SmokeFailure("capture denied")) as terminate:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "(?s)port failed.*capture denied"):
                    window_capture_smoke.launch_logserver(args, Path(root) / "app", {})
            terminate.assert_called_once_with(process, "logserver", preserve_capture=True)

    def test_cleanup_report_failure_keeps_primary_and_prints_cleanup(self):
        captured = io.StringIO()
        with redirect_stderr(captured):
            window_capture_smoke.finish_logserver_cleanup(None, Path("output"), ["cleanup failed"], True, lambda *_: None,
                mock.Mock(side_effect=OSError("report denied")))
        self.assertIn("cleanup failed", captured.getvalue())
        self.assertIn("report denied", captured.getvalue())


class CleanupErrorPreservationTests(unittest.TestCase):
    def test_pdh_setup_error_survives_close_error(self):
        import gpu_load_telemetry as telemetry

        api = mock.Mock()
        def open_query(_source, _data, query):
            ctypes.cast(query, ctypes.POINTER(ctypes.c_void_p))[0] = ctypes.c_void_p(1)
            return 0
        api.PdhOpenQueryW.side_effect = open_query
        api.PdhAddEnglishCounterW.return_value = 1
        api.PdhCloseQuery.return_value = 2
        captured = io.StringIO()
        with mock.patch.object(telemetry, "os", SimpleNamespace(name="nt")), \
                mock.patch.object(telemetry.ctypes, "WinDLL", return_value=api, create=True), redirect_stderr(captured):
            with self.assertRaisesRegex(telemetry.SmokeFailure, "add engine counter"):
                telemetry.PdhEngineQuery()
        self.assertIn("PDH close failed", captured.getvalue())

    def test_partial_upload_error_survives_socket_close_error(self):
        module_path = str(Path(__file__).resolve().parent / "logger_server")
        with mock.patch.object(sys, "path", [module_path, *sys.path]):
            import aborted_upload_cli_tests as aborted

        connection = mock.Mock()
        connection.sendall.side_effect = OSError("send failed")
        connection.close.side_effect = OSError("close failed")
        captured = io.StringIO()
        with mock.patch.object(aborted.socket, "create_connection", return_value=connection), redirect_stderr(captured):
            with self.assertRaisesRegex(OSError, "send failed"):
                aborted.start_partial_upload(1, None, None, 1, b"marker")
        connection.close.assert_called_once_with()
        self.assertIn("close failed", captured.getvalue())


class NativeKeyCleanupTests(unittest.TestCase):
    def driver(self):
        import edit_native
        driver = edit_native.EditNativeInput.__new__(edit_native.EditNativeInput)
        driver.windows = True
        driver.handle = 1
        driver.backend = SimpleNamespace(focus_window=mock.Mock(), user32=SimpleNamespace(GetForegroundWindow=lambda: 1))
        return edit_native, driver

    def test_modifier_press_failure_keeps_primary_and_attempts_release(self):
        edit_native, driver = self.driver()
        def modifier(key, down):
            if key == 0x11 and down:
                raise OSError("control press failed")
            if key == 0x10 and not down:
                raise OSError("shift release failed")
        driver._native_key = mock.Mock(side_effect=modifier)
        captured = io.StringIO()
        with mock.patch.object(edit_native.time, "sleep"), redirect_stderr(captured):
            with self.assertRaisesRegex(OSError, "control press failed"):
                driver.tap("Tab", shift=True, control=True)
        driver._native_key.assert_any_call(0x10, False)
        self.assertIn("shift release failed", captured.getvalue())

    def test_key_failure_keeps_primary_and_attempts_all_releases(self):
        edit_native, driver = self.driver()
        def modifier(key, down):
            if not down:
                raise OSError(f"modifier {key} release failed")
        def chord(_name, down, **_kwargs):
            raise OSError("key press failed" if down else "key release failed")
        driver._native_key = mock.Mock(side_effect=modifier)
        driver.native_chord_key = mock.Mock(side_effect=chord)
        captured = io.StringIO()
        with mock.patch.object(edit_native.time, "sleep"), redirect_stderr(captured):
            with self.assertRaisesRegex(OSError, "key press failed"):
                driver.tap("Tab", shift=True, control=True)
        driver._native_key.assert_any_call(0x11, False)
        driver._native_key.assert_any_call(0x10, False)
        self.assertIn("key release failed", captured.getvalue())
        self.assertIn("modifier 17 release failed", captured.getvalue())
        self.assertIn("modifier 16 release failed", captured.getvalue())

    def test_interrupted_key_submission_still_releases_attempted_key(self):
        edit_native, driver = self.driver()
        driver.windows = False
        def key(_name, down, **_kwargs):
            if down:
                raise KeyboardInterrupt()
        driver.key = mock.Mock(side_effect=key)
        with mock.patch.object(edit_native.time, "sleep"), self.assertRaises(KeyboardInterrupt):
            driver.tap("Tab")
        driver.key.assert_any_call("Tab", False, shift=False, control=False)

    def test_interrupted_modifier_submission_still_attempts_release(self):
        edit_native, driver = self.driver()
        def modifier(_key, down):
            if down:
                raise KeyboardInterrupt()
        driver._native_key = mock.Mock(side_effect=modifier)
        with mock.patch.object(edit_native.time, "sleep"), self.assertRaises(KeyboardInterrupt):
            driver.tap("Tab", shift=True)
        driver._native_key.assert_any_call(0x10, False)

    def test_release_failure_after_success_is_not_ignored(self):
        edit_native, driver = self.driver()
        driver._native_key = mock.Mock()
        def chord(_name, down, **_kwargs):
            if not down:
                raise OSError("key release failed")
        driver.native_chord_key = mock.Mock(side_effect=chord)
        with mock.patch.object(edit_native.time, "sleep"), self.assertRaisesRegex(edit_native.SmokeFailure, "key release failed"):
            driver.tap("Tab", shift=True)
        driver._native_key.assert_any_call(0x10, False)


class ProcessOutputCaptureTests(unittest.TestCase):
    def test_large_output_does_not_block_and_tail_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            working_directory = Path(temp_dir)
            script = ("import sys\n" "sys.stdout.buffer.write(b'x' * (512 * 1024))\n" "sys.stdout.flush()\n" "sys.stderr.write('\\nwindow-capture-output-sentinel\\n')\n" "sys.stderr.flush()\n")
            process = launch_captured_process([sys.executable, "-c", script], working_directory, os.environ.copy(), LIT_UNIT,)
            capture = process._nwb_output_capture
            try:
                self.assertEqual(process.wait(timeout=LIT_OUTPUT_CAPTURE_TIMEOUT_SECONDS), 0)
                self.assertTrue(capture.path.exists())
                self.assertIn("window-capture-output-sentinel", read_process_tail(process))
            finally:
                terminate_process(process, LIT_UNIT)

            self.assertFalse(capture.path.exists())


    def test_unreadable_output_preserves_capture_and_fails(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
            with mock.patch.object(Path, "open", side_effect=PermissionError("capture denied")):
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "capture denied"):
                    terminate_process(process, LIT_UNIT)
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())
            self.assertIsNone(process._nwb_output_capture)


    def test_cleanup_and_output_read_failures_keep_both_diagnostics(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=mock.Mock(side_effect=[None, None, 0]), terminate=mock.Mock(side_effect=OSError("terminate denied")),
                _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
            captured = io.StringIO()
            with mock.patch.object(window_capture_smoke.platform, "system", return_value="Other"), \
                    mock.patch.object(Path, "open", side_effect=PermissionError("capture denied")), redirect_stderr(captured):
                with self.assertRaisesRegex(OSError, "terminate denied"):
                    terminate_process(process, LIT_UNIT)
            self.assertIn("capture denied", captured.getvalue())
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())

    def test_outer_exception_does_not_hide_capture_read_failure(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
            try:
                raise ValueError("outer failure")
            except ValueError:
                with mock.patch.object(Path, "open", side_effect=PermissionError("capture denied")):
                    with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "capture denied"):
                        terminate_process(process, LIT_UNIT)
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())

    def test_requested_capture_retention_preserves_full_output(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            stream.write(b"first line\n" + b"x" * 70000)
            stream.flush()
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=window_capture_smoke.ProcessOutputCapture(path, stream))
            terminate_process(process, LIT_UNIT, preserve_capture=True)
            self.assertTrue(stream.closed)
            self.assertTrue(path.read_bytes().startswith(b"first line\n"))


class LoggerProcessEvidenceTests(unittest.TestCase):
    def test_full_output_validation_rejects_errors_before_tail(self):
        import logger_busy_spin_smoke as logger

        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            source = directory / "capture.tmp"
            source.write_text("[ERROR] early failure\n" + "x" * 70000, encoding="utf-8")
            with self.assertRaises(window_capture_smoke.SmokeFailure):
                logger.retain_and_validate_process_output(SimpleNamespace(path=source), directory)
            self.assertEqual((directory / f"process_output_{source.name}").read_bytes(), source.read_bytes())


class BmpReadbackTests(unittest.TestCase):
    def test_bmp_parser_rejects_declared_size_and_compression_mismatches(self):
        rows = [[(1, 2, 3), (4, 5, 6)]]
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "invalid.bmp"
            window_capture_image.write_bmp_24(path, 2, 1, rows)
            data = bytearray(path.read_bytes())

            struct.pack_into(LIT_I, data, 2, len(data) + 1)
            path.write_bytes(data)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "declares .* but contains"):
                window_capture_smoke.read_bmp_24(path)

            struct.pack_into(LIT_I, data, 2, len(data))
            struct.pack_into(LIT_I, data, 30, 1)
            path.write_bytes(data)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "must be uncompressed 24-bit"):
                window_capture_smoke.read_bmp_24(path)


class TimingCaptureConfigurationTests(unittest.TestCase):
    def test_non_finite_timings_are_rejected_for_each_capture_mode(self):
        base_args = [LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP]
        for mode in ([], [LIT_APPLICATION_CAPTURE], [LIT_WINDOW_HANDLE, "1"], [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701]):
            options = [LIT_TIMEOUT, LIT_RENDER_READY_TIMEOUT, LIT_SETTLE_SECONDS]
            if LIT_RESIZE_CLIENT in mode:
                options.append(LIT_RESIZE_SETTLE_SECONDS)
            for option in options:
                for value in (LIT_NAN, LIT_INF, "-inf"):
                    with self.subTest(mode=mode, option=option, value=value), redirect_stderr(io.StringIO()) as errors:
                        with self.assertRaises(SystemExit) as rejection:
                            window_capture_smoke.parse_args(base_args + mode + [f"{option}={value}"])
                        self.assertEqual(rejection.exception.code, 2)
                        self.assertIn(option, errors.getvalue())
                        self.assertIn("finite", errors.getvalue())

    def test_zero_settle_is_allowed_but_zero_timeouts_are_rejected(self):
        base_args = [LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP]
        self.assertEqual(window_capture_smoke.parse_args(base_args + [LIT_SETTLE_SECONDS, "0"]).settle_seconds, 0.0)
        for option in (LIT_TIMEOUT, LIT_RENDER_READY_TIMEOUT):
            with self.subTest(option=option), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as rejection:
                window_capture_smoke.parse_args(base_args + [option, "0"])
            self.assertEqual(rejection.exception.code, 2)


class WindowsDesktopPreflightTests(unittest.TestCase):
    @staticmethod
    def user32(flags=1, station=0xF234567887654321):
        user32 = mock.Mock()
        user32.GetProcessWindowStation.return_value = station

        def get_flags(_station, _index, output, _size, needed):
            ctypes.cast(output, ctypes.POINTER(window_capture_win32.WindowsUserObjectFlags)).contents.dwFlags = flags
            ctypes.cast(needed, ctypes.POINTER(ctypes.c_uint32)).contents.value = ctypes.sizeof(window_capture_win32.WindowsUserObjectFlags)
            return 1

        user32.GetUserObjectInformationW.side_effect = get_flags
        return user32

    def test_only_window_station_visibility_bit_admits_the_desktop(self):
        for flags in (0, 2, 1, 5):
            with self.subTest(flags=flags):
                user32 = self.user32(flags)
                if flags & 1:
                    window_capture_win32.require_windows_interactive_desktop(user32)
                else:
                    with self.assertRaisesRegex(window_capture_outcome.WindowsDesktopUnavailable, "no visible display surfaces"):
                        window_capture_win32.require_windows_interactive_desktop(user32)
                self.assertEqual(user32.GetUserObjectInformationW.call_args.args[0].value, 0xF234567887654321)
                user32.CloseWindowStation.assert_not_called()

    def test_window_station_query_failure_remains_a_failure(self):
        for missing_station in (True, False):
            with self.subTest(missing_station=missing_station):
                user32 = self.user32(station=0 if missing_station else 42)
                user32.GetUserObjectInformationW.side_effect = None
                user32.GetUserObjectInformationW.return_value = 0
                api = "GetProcessWindowStation" if missing_station else "GetUserObjectInformationW"
                with mock.patch.object(window_capture_smoke.ctypes, "get_last_error", return_value=5, create=True), \
                     self.assertRaisesRegex(window_capture_smoke.SmokeFailure, api + ".*failed: 5"):
                    window_capture_win32.require_windows_interactive_desktop(user32)
                if missing_station:
                    user32.GetUserObjectInformationW.assert_not_called()

    def test_capture_constructor_checks_the_station_before_loading_gdi(self):
        with mock.patch.object(window_capture_win32, LIT_CREATE_WINDOWS_USER32, return_value=self.user32(0)), \
             mock.patch.object(window_capture_smoke.ctypes, "WinDLL", create=True) as load_dll, \
             self.assertRaises(window_capture_outcome.WindowsDesktopUnavailable):
            window_capture_smoke.WindowsCapture()
        load_dll.assert_not_called()

    def test_desktop_and_application_capture_skip_before_launching_processes(self):
        for mode in ([], [LIT_APPLICATION_CAPTURE]):
            with tempfile.TemporaryDirectory() as temp_dir, self.subTest(mode=mode):
                with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_WINDOWS), \
                     mock.patch.object(window_capture_win32, LIT_CREATE_WINDOWS_USER32, return_value=self.user32(0)), \
                     mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER) as launch_logger, \
                     mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED) as launch_application, \
                     mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS):
                    code = window_capture_smoke.main([LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, str(Path(temp_dir) / LIT_CAPTURE_BMP), *mode])
                self.assertEqual(code, window_capture_smoke.SKIP_EXIT_CODE)
                launch_logger.assert_not_called()
                launch_application.assert_not_called()

    def test_logger_preserves_desktop_skip_evidence_without_launching(self):
        import logger_busy_spin_smoke
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            with mock.patch.object(logger_busy_spin_smoke, "os", SimpleNamespace(name="nt")), \
                 mock.patch.object(logger_busy_spin_smoke, "WindowsCapture", side_effect=window_capture_outcome.WindowsDesktopUnavailable(LIT_INVISIBLE_STATION)), \
                 mock.patch.object(logger_busy_spin_smoke, LIT_LAUNCH_LOGSERVER) as launch_logger, \
                 mock.patch.object(logger_busy_spin_smoke, LIT_WRITE_STATUS):
                code = logger_busy_spin_smoke.main([LIT_EXECUTABLE, sys.executable, "--working-directory", temp_dir, "--output-directory", temp_dir])
            evidence = json.loads((directory / "evidence.json").read_text(encoding=LIT_UTF_8))
            self.assertEqual(code, window_capture_smoke.SKIP_EXIT_CODE)
            self.assertFalse(evidence[LIT_PASSED])
            self.assertTrue(evidence[LIT_SKIPPED])
            self.assertEqual(evidence["skip_reason"], LIT_INVISIBLE_STATION)
            self.assertEqual(evidence["failures"], [])
            launch_logger.assert_not_called()

    def test_native_probe_skips_only_the_unavailable_windows_desktop(self):
        import avboit_depth_warp_probe_smoke
        for error, expected in ((window_capture_outcome.WindowsDesktopUnavailable(LIT_INVISIBLE_STATION), 77), (window_capture_smoke.SmokeSkip("missing device support"), 1), (window_capture_smoke.SmokeFailure("query failed"), 1)):
            with tempfile.TemporaryDirectory() as temp_dir, self.subTest(error=error):
                args = SimpleNamespace(output_directory=Path(temp_dir))
                with mock.patch.object(avboit_depth_warp_probe_smoke, "parse_args", return_value=args), \
                     mock.patch.object(avboit_depth_warp_probe_smoke, "acquire", side_effect=error), \
                     redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    self.assertEqual(avboit_depth_warp_probe_smoke.main([]), expected)
                evidence = json.loads(next(args.output_directory.glob("*/result.json")).read_text(encoding=LIT_UTF_8))
                self.assertFalse(evidence[LIT_PASSED])
                self.assertEqual(evidence.get(LIT_SKIPPED, False), expected == 77)


class ApplicationCaptureConfigurationTests(unittest.TestCase):
    def test_environment_removes_stale_final_and_partial_artifacts(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / LIT_CAPTURE_BMP
            partial = window_capture_smoke.application_capture_partial_path(output)
            output.write_bytes(b"stale-final")
            partial.write_bytes(b"stale-partial")
            args = SimpleNamespace(output=output, application_capture_frame_count=27)
            base_environment = {}

            window_capture_smoke.prepare_application_capture_environment(args, base_environment)

            self.assertFalse(output.exists())
            self.assertFalse(partial.exists())

    def test_application_capture_rejects_zero_frame_count(self):
        with self.assertRaises(SystemExit):
            window_capture_smoke.parse_args([LIT_APPLICATION_CAPTURE, "--application-capture-frame-count", "0", LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP,])

    def test_application_capture_skip_reaches_process_exit_code_77_without_desktop_capture(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / LIT_CAPTURE_BMP
            with mock.patch.object(window_capture_smoke, LIT_LAUNCH_AND_CAPTURE_APPLICATION, side_effect=window_capture_smoke.SmokeSkip(window_capture_smoke.FRAMEBUFFER_CAPTURE_SKIP_MESSAGE),
            ), mock.patch.object(window_capture_smoke, LIT_CREATE_CAPTURE_BACKEND) as create_backend, \
                 mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS):
                exit_code = window_capture_smoke.main([LIT_APPLICATION_CAPTURE, LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, str(output)])

        self.assertEqual(exit_code, window_capture_smoke.SKIP_EXIT_CODE)
        create_backend.assert_not_called()


class TemporalLaggedCapabilityTests(unittest.TestCase):
    CAPABILITY_MARKER = "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue, target generation 1)"
    BOOTSTRAP_MARKER = "RendererSystem: frame-lagged async lighting bootstrap accepted"
    HISTORY_MARKER = "RendererSystem: frame-lagged async lighting active history accepted"
    CAPTURE_LOG = "\n".join(("CsgVisibleSmokeProject: temporal scene animated=0 view=full minimum_frame=360 warmup_seconds=30 interval=8 samples=32", "CsgVisibleSmokeProject: temporal warmup started graphics frame 360",
        "CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds=30", "CsgVisibleSmokeProject: temporal series complete samples=32", "CsgVisibleSmokeProject: shutdown", "RendererSystem: dispatched surfel GI resolve",
        window_capture_smoke.FRAMEBUFFER_CAPTURE_READY_MESSAGE))

    def capture_with_log(self, log_text, *, lagged=True, application_exit=0):
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            args = SimpleNamespace(output_directory=directory, overwrite=False, gpu_timing_output=None, view="full", motion="static", frame_interval=8, lagged_lighting=lagged, overlapping=False,
                executable=Path(sys.executable), working_directory=directory, timeout=1.0, gpu_validation=True, route="natural", logserver_executable=None)

            def run_capture(command, **_kwargs):
                capture_args = window_capture_smoke.parse_args(command[2:])

                def wait_for_capture(*_args):
                    rows = [[(255, 255, 255) if (x + y) % 2 else (0, 0, 0) for x in range(8)] for y in range(8)]
                    window_capture_image.write_bmp_24(capture_args.output, 8, 8, rows)
                    return application_exit, True

                with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                     mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, return_value=(object(), 49152, directory, {}, LIT_LOG)), \
                     mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                     mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, side_effect=wait_for_capture), \
                     mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, return_value=(application_exit, "")), \
                     mock.patch.object(window_capture_smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=log_text), \
                     mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
                     redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    return SimpleNamespace(returncode=window_capture_smoke.main(command[2:]))

            with mock.patch.object(csg_gi_temporal_smoke.subprocess, "run", side_effect=run_capture):
                return csg_gi_temporal_smoke.capture_series(args)

    def test_graphics_alias_is_skipped_only_when_lagged_history_is_requested(self):
        log_text = self.CAPTURE_LOG + "\n" + self.CAPABILITY_MARKER
        self.assertEqual(self.capture_with_log(log_text), window_capture_smoke.SKIP_EXIT_CODE)
        self.assertEqual(self.capture_with_log(log_text, lagged=False), 0)

    def test_supported_route_still_requires_both_history_acceptance_markers(self):
        for missing in (self.BOOTSTRAP_MARKER, self.HISTORY_MARKER):
            with self.subTest(missing=missing):
                log_text = self.CAPTURE_LOG + "\n" + (self.HISTORY_MARKER if missing == self.BOOTSTRAP_MARKER else self.BOOTSTRAP_MARKER)
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_TEMPORAL_CAPTURE_FAILED_WITH_EXIT_1):
                    self.capture_with_log(log_text)
        self.assertEqual(self.capture_with_log(self.CAPTURE_LOG + "\n" + self.BOOTSTRAP_MARKER + "\n" + self.HISTORY_MARKER), 0)

    def test_capability_skip_cannot_hide_strict_runtime_diagnostics(self):
        for marker in window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES:
            with self.subTest(marker=marker):
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_TEMPORAL_CAPTURE_FAILED_WITH_EXIT_1):
                    self.capture_with_log(self.CAPTURE_LOG + "\n" + self.CAPABILITY_MARKER + "\n" + marker)

    def test_capability_skip_cannot_hide_nonzero_application_exit(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_TEMPORAL_CAPTURE_FAILED_WITH_EXIT_1):
            self.capture_with_log(self.CAPTURE_LOG + "\n" + self.CAPABILITY_MARKER, application_exit=9)


class RuntimeLogValidationTests(unittest.TestCase):
    def test_gpu_validation_cannot_be_enabled_on_an_existing_window(self):
        arguments = [LIT_WINDOW_HANDLE, "1", LIT_OUTPUT, LIT_CAPTURE_BMP]
        window_capture_smoke.parse_args(arguments + ["--no-gpu-validation"])
        with mock.patch(LIT_SYS_STDERR), self.assertRaises(SystemExit) as error:
            window_capture_smoke.parse_args(arguments + ["--gpu-validation"])
        self.assertEqual(error.exception.code, 2)

    def test_capability_marker_is_classified_before_required_log_validation(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = LIT_BENCHMARK_SKIPPED_BECAUSE_REQUIRED_HAR
            (log_directory / LIT_TEST_LOG).write_text(marker, encoding=LIT_UTF_8)

            self.assertEqual(validate_expected_log_messages(log_directory, {}, LIT_LOG, [LIT_REQUIRED_ROUTE_MARKER], [], [marker],), marker,)

    def test_missing_required_marker_is_not_a_capability_skip(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            (log_directory / LIT_TEST_LOG).write_text(LIT_ORDINARY_STARTUP, encoding=LIT_UTF_8)

            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_MISSING_LOG_MESSAGE):
                validate_expected_log_messages(log_directory, {}, LIT_LOG, [LIT_REQUIRED_ROUTE_MARKER], [], ["benchmark skipped"],)

    def test_capability_marker_preempts_supported_route_rejection(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = LIT_BENCHMARK_SKIPPED_BECAUSE_REQUIRED_HAR
            (log_directory / LIT_TEST_LOG).write_text(f"{marker}\nsoftware traversal route", encoding=LIT_UTF_8,)

            self.assertEqual(validate_expected_log_messages(log_directory, {}, LIT_LOG, ["required hardware route"], ["software traversal route"], [marker], [LIT_ERROR],), marker,)

    def test_rejected_error_takes_precedence_over_capability_skip(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = LIT_BENCHMARK_SKIPPED_BECAUSE_REQUIRED_HAR
            (log_directory / LIT_TEST_LOG).write_text(f"{marker}\n[ERROR] device failure", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_BLOCKING_LOG_MESSAGE):
                validate_expected_log_messages(log_directory, {}, LIT_LOG, [LIT_REQUIRED_ROUTE_MARKER], [LIT_ERROR], [marker], [LIT_ERROR],)

    def test_reject_only_validation_requires_runtime_log_evidence(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "captured no output"):
                validate_expected_log_messages(Path(temp_dir), {}, LIT_LOG, [], [LIT_ERROR],)


class ProcessLivenessTests(unittest.TestCase):
    def test_named_process_exit_before_validation_fails(self):
        process = SimpleNamespace(poll=lambda: 7, returncode=7)

        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, r"logserver exited before log validation \(exit 7\)",):
            ensure_process_running(process, "before log validation", LIT_LOGSERVER)

    def test_named_process_abnormal_graceful_exit_fails(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, r"logserver exited during graceful shutdown \(exit 7\)",):
            require_normal_process_exit(7, "", LIT_LOGSERVER)


class LogserverCollectionTests(unittest.TestCase):
    def test_shutdown_happens_before_drain_and_collection(self):
        process = SimpleNamespace(poll=lambda: None)
        events = []

        def terminate(_process, _name):
            events.append(LIT_SHUTDOWN)
            return 0, ""

        def drain(*_args):
            events.append(LIT_DRAIN)

        def collect(*_args):
            events.append(LIT_COLLECT)
            return LIT_WARNING_SHUTDOWN_ONLY_WARNING

        with mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
             mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN, side_effect=drain), \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, side_effect=collect):
            shutdown_logserver_and_collect(process, Path(LIT_LOGS), {}, LIT_LOG)

        self.assertEqual(events, [LIT_SHUTDOWN, LIT_DRAIN, LIT_COLLECT])

    def test_abnormal_logserver_exit_fails_before_collection(self):
        process = SimpleNamespace(poll=lambda: None)
        with mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, return_value=(7, "logserver tail")), \
             mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN) as drain, \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA) as collect:
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "logserver.*exit 7"):
                shutdown_logserver_and_collect(process, Path(LIT_LOGS), {}, LIT_LOG)

        drain.assert_not_called()
        collect.assert_not_called()


class ShutdownLogValidationTests(unittest.TestCase):
    def run_capture_with_shutdown_log(self, shutdown_log, required=(), rejected=()):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            log_path = log_directory / LIT_TEST_LOG
            args = SimpleNamespace(application_arg=[], executable=sys.executable, expect_log_message=list(required), expect_texture_smoke=False, expect_transparent_csg=False, expect_transparent_multi=False, log_port=0, logserver_executable=None,
                no_logserver=False, output=log_directory / LIT_CAPTURE_BMP, reject_log_message=list(rejected), render_ready_timeout=1.0, settle_seconds=0.0, skip_blocking_log_message=[], skip_log_message=[], software_vulkan=LIT_OFF, timeout=1.0,
                window_title="NWB Test", working_directory=log_directory,)
            backend = mock.Mock()
            backend.wait_for_window.return_value = 0x4A
            testbed_process = SimpleNamespace(pid=4321, poll=lambda: None)
            logserver_process = SimpleNamespace(poll=lambda: None)
            events = []

            def terminate(process, name, _window_handle=None, **_kwargs):
                if process is testbed_process:
                    events.append(LIT_TESTBED_SHUTDOWN)
                else:
                    events.append(LIT_LOGSERVER_SHUTDOWN)
                    log_path.write_text(shutdown_log, encoding=LIT_UTF_8)
                return 0, ""

            def drain(*_args):
                events.append(LIT_LOG_DRAIN)

            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, return_value=(logserver_process, 49152, log_directory, {}, LIT_LOG),), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=testbed_process), \
                 mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=LIT_CAPTURE), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN, side_effect=drain):
                window_capture_smoke.launch_and_capture(args, backend)

            self.assertEqual(events, [LIT_TESTBED_SHUTDOWN, LIT_LOGSERVER_SHUTDOWN, LIT_LOG_DRAIN])

    def test_shutdown_marker_is_validated_after_graceful_exit(self):
        self.run_capture_with_shutdown_log(LIT_PROJECTTESTBED_SHUTDOWN, required=(LIT_PROJECTTESTBED_SHUTDOWN,))

    def test_teardown_warning_fails_after_graceful_exit(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "rejected log message"):
            self.run_capture_with_shutdown_log("[WARNING] teardown failure", rejected=(LIT_WARNING,))


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
        with mock.patch.object(window_capture_smoke, LIT_CREATE_WINDOWS_USER32, return_value=user32):
            self.assertTrue(window_capture_smoke.request_windows_graceful_exit(process_id))

        self.assertEqual(observed_owner_queries, [high_bit_hwnd])
        self.assertEqual(observed_posts, [(high_bit_hwnd, 0x0010, 0, 0)])

    def test_linux_x11_close_falls_back_to_sigterm_after_timeout(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=True), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS) as write_status:
            terminate_process(process, LIT_TESTBED, 0x4a)

        self.assertEqual(process.wait_timeouts, [10.0, 5.0])
        self.assertEqual(process.terminate_calls, 1)
        self.assertEqual(process.kill_calls, 0)
        write_status.assert_called_once_with("testbed: did not exit after X11 WM_DELETE_WINDOW; terminating")

    def test_linux_x11_helper_failure_falls_back_to_sigterm(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=False) as close:
            terminate_process(process, LIT_TESTBED, 0x4a)

        close.assert_called_once()
        self.assertEqual(process.wait_timeouts, [5.0])
        self.assertEqual(process.terminate_calls, 1)

    def test_linux_without_a_captured_handle_does_not_discover_a_window_by_title(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT) as close:
            terminate_process(process, LIT_TESTBED)

        close.assert_not_called()
        self.assertEqual(process.terminate_calls, 1)

    def test_nonzero_graceful_exit_is_reported_to_the_smoke_runner(self):
        process = _FakeProcess(graceful_exit=True, graceful_exit_code=-6)
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=True):
            exit_code, tail = terminate_process(process, LIT_TESTBED, 0x4a)

        self.assertEqual(exit_code, -6)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "exit -6"):
            require_normal_process_exit(exit_code, tail, LIT_TESTBED)

    def test_missing_shutdown_exit_code_is_reported_to_the_smoke_runner(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "did not exit"):
            require_normal_process_exit(None, "", LIT_TESTBED)


class InteractionForegroundTests(unittest.TestCase):
    def native_input(self, foreground_values):
        native = interaction_smoke.NativeInput.__new__(interaction_smoke.NativeInput)
        native.windows = True
        native.handle = 42
        native.backend = SimpleNamespace(
            user32=SimpleNamespace(GetForegroundWindow=mock.Mock(side_effect=foreground_values)),
            focus_window=mock.Mock(),
        )
        return native

    def test_waits_until_the_fixture_owns_foreground(self):
        native = self.native_input([10, 42])
        with mock.patch.object(interaction_smoke, "ensure_process_running") as running, \
             mock.patch.object(interaction_smoke.time, "monotonic", side_effect=[0.0, 0.0, 0.2]), \
             mock.patch.object(interaction_smoke.time, "sleep"):
            native.wait_for_foreground(object(), 1.0)
        self.assertEqual(native.backend.focus_window.call_count, 2)
        self.assertEqual(running.call_count, 2)

    def test_unavailable_foreground_is_a_skip_and_process_failure_propagates(self):
        native = self.native_input([10])
        with mock.patch.object(interaction_smoke, "ensure_process_running"), \
             mock.patch.object(interaction_smoke.time, "monotonic", side_effect=[0.0, 0.0, 2.0]), \
             mock.patch.object(interaction_smoke.time, "sleep"):
            with self.assertRaisesRegex(interaction_smoke.SmokeSkip, "foreground"):
                native.wait_for_foreground(object(), 1.0)
        with mock.patch.object(interaction_smoke, "ensure_process_running", side_effect=interaction_smoke.SmokeFailure("stopped")):
            with self.assertRaisesRegex(interaction_smoke.SmokeFailure, "stopped"):
                native.wait_for_foreground(object(), 1.0)


if __name__ == LIT_MAIN:
    unittest.main()
