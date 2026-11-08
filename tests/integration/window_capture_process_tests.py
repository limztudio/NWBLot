#!/usr/bin/env python3

from contextlib import redirect_stderr
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / "ui_layer"))

import window_capture_logs
import window_capture_frame
from window_capture_frame import WindowFrameCapture
import window_capture_smoke
from window_capture_output_reader import CaptureOutputReader
from window_capture_process_output import CapturedProcessOutput, ProcessOutputCapture
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import ensure_process_running, require_normal_process_exit, shutdown_logserver_and_collect

LIT_OUTPUT_CAPTURE_TIMEOUT_SECONDS = 5.0
LIT_UNIT = "unit"
LIT_CAPTURE = "capture"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_TEST_LOG = "test.log"
LIT_UTF_8 = "utf-8"
LIT_LOG = "*.log"
LIT_LOGSERVER = "logserver"
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
LIT_OFF = "off"
LIT_TESTBED_SHUTDOWN = "testbed shutdown"
LIT_LOGSERVER_SHUTDOWN = "logserver shutdown"
LIT_LOG_DRAIN = "log drain"
LIT_CAPTURE_RENDER_READY_WINDOW = "capture_render_ready_window"
LIT_PROJECTTESTBED_SHUTDOWN = "ProjectTestbed: shutdown"


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

    def test_scoped_live_poll_keeps_reused_pid_and_collects_other_new_logs_at_shutdown(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            reused = directory / "logserver_20260101_010101_4321_0.log"
            other = directory / "logserver_20260101_010101_12_0.log"
            time_collision = directory / "logserver_20260101_004321_12_0.log"
            reused.write_text("old owned\n", encoding="utf-8")
            other.write_text("old other\n", encoding="utf-8")
            time_collision.write_text("old collision\n", encoding="utf-8")
            baseline = window_capture_logs.snapshot_log_files(directory, "logserver_*.log")
            scoped = window_capture_logs.ScopedLogBaseline(baseline, "logserver_*.log", 4321)
            with reused.open("a", encoding="utf-8") as stream:
                stream.write("reused pid append\n")
            with other.open("a", encoding="utf-8") as stream:
                stream.write("other append\n")
            with time_collision.open("a", encoding="utf-8") as stream:
                stream.write("timestamp collision append\n")
            (directory / "logserver_20260102_010101_4321_0.log").write_text("owned current\n", encoding="utf-8")
            (directory / "logserver_20260102_010101_99_0.log").write_text("other current\n", encoding="utf-8")
            live = window_capture_logs.collect_log_delta(directory, scoped, scoped.live_pattern)
            self.assertIn("reused pid append", live)
            self.assertIn("owned current", live)
            self.assertNotIn("other append", live)
            self.assertNotIn("other current", live)
            self.assertNotIn("timestamp collision append", live)
            with mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN):
                final = shutdown_logserver_and_collect(None, directory, scoped, scoped.live_pattern)
            for message in ("reused pid append", "owned current", "other append", "other current", "timestamp collision append"):
                self.assertIn(message, final)

    def test_scoped_shutdown_detects_rewritten_unrelated_baseline(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            other = directory / "logserver_20260101_010101_12_0.log"
            other.write_bytes(b"old record")
            baseline = window_capture_logs.snapshot_log_files(directory, "logserver_*.log")
            scoped = window_capture_logs.ScopedLogBaseline(baseline, "logserver_*.log", 4321)
            other.write_bytes(b"new record")
            self.assertEqual(window_capture_logs.collect_log_delta(directory, scoped, scoped.live_pattern), "")
            with mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN), self.assertRaisesRegex(window_capture_logs.SmokeFailure, "changed before its baseline"):
                shutdown_logserver_and_collect(None, directory, scoped, scoped.live_pattern)

    def test_failed_poll_during_failed_termination_retains_capture(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=mock.Mock(side_effect=[OSError("first poll"), OSError("final poll")]),
                _nwb_output_capture=ProcessOutputCapture(path, stream))
            output = io.StringIO()
            with redirect_stderr(output), self.assertRaisesRegex(OSError, "first poll"):
                CaptureProcessTermination.terminate_process(process, LIT_UNIT)
            self.assertIn("final poll", output.getvalue())
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())
            self.assertIsNone(process._nwb_output_capture)

    def test_resize_does_not_capture_before_render_marker(self):
        args = SimpleNamespace(expect_before_resize_log_message="painted", render_ready_timeout=1.0)
        backend = mock.Mock()
        with mock.patch.object(window_capture_frame, "wait_for_log_message", side_effect=window_capture_smoke.SmokeFailure("marker missing")), \
                mock.patch.object(WindowFrameCapture, "capture_render_ready_window") as capture:
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "marker missing"):
                WindowFrameCapture(window_capture_smoke.ensure_process_running).capture_resized_window(args, backend, 1, mock.Mock(), Path("logs"), {}, "*.log")
        capture.assert_not_called()

    def test_close_failure_retains_capture_and_is_reported(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            path.write_bytes(b"evidence")
            stream = mock.Mock()
            stream.close.side_effect = OSError("close denied")
            capture = ProcessOutputCapture(path, stream)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "close denied"):
                CapturedProcessOutput.dispose_process_output_capture(capture)
            self.assertEqual(path.read_bytes(), b"evidence")

    def test_failed_capture_cleanup_attempts_both_and_retains_primary(self):
        output = io.StringIO()
        with mock.patch.object(window_capture_smoke.CaptureProcessTermination, "terminate_process", side_effect=[OSError("app cleanup"), OSError("logger cleanup")]) as terminate, redirect_stderr(output):
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
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=ProcessOutputCapture(path, stream))
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "forbidden message"):
                CaptureProcessTermination.terminate_process(process, LIT_UNIT, rejected_output=("[ERROR]",))
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())

    def test_cancelled_readiness_stops_owned_logger(self):
        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(no_logserver=False, logserver_executable=Path(root) / "logger", log_port=1, working_directory=Path(root), timeout=1)
            process = object()
            with mock.patch.object(window_capture_smoke, "snapshot_log_files", return_value={}), \
                    mock.patch.object(CapturedProcessOutput, "launch_captured_process", return_value=process), \
                    mock.patch.object(window_capture_smoke, "wait_for_tcp_port", side_effect=KeyboardInterrupt), \
                    mock.patch.object(window_capture_smoke.CaptureProcessTermination, "terminate_process", return_value=(0, "")) as terminate:
                with self.assertRaises(KeyboardInterrupt):
                    window_capture_smoke.launch_logserver(args, Path(root) / "app", {})
            terminate.assert_called_once_with(process, window_capture_smoke.LIT_LOGSERVER, preserve_capture=True)

    def test_nonzero_output_diagnostic_keeps_exit_and_capture_path(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            stream.write(b"[ERROR] failed")
            stream.flush()
            process = SimpleNamespace(poll=lambda: 9, _nwb_output_capture=ProcessOutputCapture(path, stream))
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "exited with code 9.*forbidden") as failure:
                CaptureProcessTermination.terminate_process(process, LIT_UNIT, rejected_output=("[ERROR]",))
            self.assertIn(str(path), str(failure.exception))
            self.assertTrue(path.exists())

    def test_skip_only_output_blocks_skip_but_not_success(self):
        for skip in (None, "unsupported"):
            with self.subTest(skip=skip), tempfile.TemporaryDirectory() as root:
                path = Path(root) / "process.log"
                stream = path.open("w+b")
                stream.write(b"custom blocker")
                stream.flush()
                process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=ProcessOutputCapture(path, stream))
                observed = []
                CaptureProcessTermination.terminate_process(process, LIT_UNIT, observed_output=observed, observed_needles=("custom blocker",))
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
            first = SimpleNamespace(_nwb_output_capture=ProcessOutputCapture(first_path, first_path.open("w+b")))
            second = SimpleNamespace(_nwb_output_capture=ProcessOutputCapture(second_path, second_path.open("w+b")))
            def terminate(process, _name, _window, **_kwargs):
                process._nwb_output_capture.stream.close()
                if process is second:
                    raise OSError("logger cleanup failed")
            with mock.patch.object(window_capture_smoke.CaptureProcessTermination, "terminate_process", side_effect=terminate), redirect_stderr(io.StringIO()):
                with self.assertRaisesRegex(OSError, "logger cleanup failed"):
                    window_capture_smoke.cleanup_capture_processes(first, None, second, None)
            self.assertTrue(first_path.exists())
            self.assertTrue(second_path.exists())

    def test_failed_start_still_stops_logger_when_capture_is_unreadable(self):
        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(no_logserver=False, logserver_executable=Path(root) / "logger", log_port=1, working_directory=Path(root), timeout=1)
            process = object()
            with mock.patch.object(window_capture_smoke, "snapshot_log_files", return_value={}), \
                    mock.patch.object(CapturedProcessOutput, "launch_captured_process", return_value=process), \
                    mock.patch.object(window_capture_smoke, "wait_for_tcp_port", side_effect=window_capture_smoke.SmokeFailure("port failed")), \
                    mock.patch.object(window_capture_smoke.CaptureProcessTermination, "terminate_process", side_effect=window_capture_smoke.SmokeFailure("capture denied")) as terminate:
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


class ProcessOutputCaptureTests(unittest.TestCase):
    def test_large_output_does_not_block_and_tail_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            working_directory = Path(temp_dir)
            script = ("import sys\n" "sys.stdout.buffer.write(b'x' * (512 * 1024))\n" "sys.stdout.flush()\n" "sys.stderr.write('\\nwindow-capture-output-sentinel\\n')\n" "sys.stderr.flush()\n")
            process = CapturedProcessOutput.launch_captured_process([sys.executable, "-c", script], working_directory, os.environ.copy(), LIT_UNIT,)
            capture = process._nwb_output_capture
            try:
                self.assertEqual(process.wait(timeout=LIT_OUTPUT_CAPTURE_TIMEOUT_SECONDS), 0)
                self.assertTrue(capture.path.exists())
                self.assertIn("window-capture-output-sentinel", CaptureOutputReader.read_process_tail(process))
            finally:
                CaptureProcessTermination.terminate_process(process, LIT_UNIT)

            self.assertFalse(capture.path.exists())


    def test_unreadable_output_preserves_capture_and_fails(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=ProcessOutputCapture(path, stream))
            with mock.patch.object(Path, "open", side_effect=PermissionError("capture denied")):
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "capture denied"):
                    CaptureProcessTermination.terminate_process(process, LIT_UNIT)
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())
            self.assertIsNone(process._nwb_output_capture)


    def test_cleanup_and_output_read_failures_keep_both_diagnostics(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=mock.Mock(side_effect=[None, None, 0]), terminate=mock.Mock(side_effect=OSError("terminate denied")),
                _nwb_output_capture=ProcessOutputCapture(path, stream))
            captured = io.StringIO()
            with mock.patch.object(window_capture_smoke.platform, "system", return_value="Other"), \
                    mock.patch.object(Path, "open", side_effect=PermissionError("capture denied")), redirect_stderr(captured):
                with self.assertRaisesRegex(OSError, "terminate denied"):
                    CaptureProcessTermination.terminate_process(process, LIT_UNIT)
            self.assertIn("capture denied", captured.getvalue())
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())

    def test_outer_exception_does_not_hide_capture_read_failure(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=ProcessOutputCapture(path, stream))
            try:
                raise ValueError("outer failure")
            except ValueError:
                with mock.patch.object(Path, "open", side_effect=PermissionError("capture denied")):
                    with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "capture denied"):
                        CaptureProcessTermination.terminate_process(process, LIT_UNIT)
            self.assertTrue(stream.closed)
            self.assertTrue(path.exists())

    def test_requested_capture_retention_preserves_full_output(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "process.log"
            stream = path.open("w+b")
            stream.write(b"first line\n" + b"x" * 70000)
            stream.flush()
            process = SimpleNamespace(poll=lambda: 0, _nwb_output_capture=ProcessOutputCapture(path, stream))
            CaptureProcessTermination.terminate_process(process, LIT_UNIT, preserve_capture=True)
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

        with mock.patch.object(window_capture_smoke.CaptureProcessTermination, LIT_TERMINATE_PROCESS, side_effect=terminate), \
             mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN, side_effect=drain), \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, side_effect=collect):
            shutdown_logserver_and_collect(process, Path(LIT_LOGS), {}, LIT_LOG)

        self.assertEqual(events, [LIT_SHUTDOWN, LIT_DRAIN, LIT_COLLECT])

    def test_abnormal_logserver_exit_fails_before_collection(self):
        process = SimpleNamespace(poll=lambda: None)
        with mock.patch.object(window_capture_smoke.CaptureProcessTermination, LIT_TERMINATE_PROCESS, return_value=(7, "logserver tail")), \
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
                 mock.patch.object(WindowFrameCapture, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=LIT_CAPTURE), \
                 mock.patch.object(window_capture_smoke.CaptureProcessTermination, LIT_TERMINATE_PROCESS, side_effect=terminate), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN, side_effect=drain):
                window_capture_smoke.launch_and_capture(args, backend)

            self.assertEqual(events, [LIT_TESTBED_SHUTDOWN, LIT_LOGSERVER_SHUTDOWN, LIT_LOG_DRAIN])

    def test_shutdown_marker_is_validated_after_graceful_exit(self):
        self.run_capture_with_shutdown_log(LIT_PROJECTTESTBED_SHUTDOWN, required=(LIT_PROJECTTESTBED_SHUTDOWN,))

    def test_teardown_warning_fails_after_graceful_exit(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "rejected log message"):
            self.run_capture_with_shutdown_log("[WARNING] teardown failure", rejected=(LIT_WARNING,))


if __name__ == "__main__":
    unittest.main()
