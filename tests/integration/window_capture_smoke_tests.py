#!/usr/bin/env python3


from contextlib import redirect_stderr, redirect_stdout
import io
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

LIT_UI_LAYER_DIRECTORY = "ui_layer"


SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / LIT_UI_LAYER_DIRECTORY))

from window_capture_arguments import WindowCaptureArguments
import window_capture_smoke  # noqa: E402
from window_capture_smoke import validate_expected_log_messages
from window_capture_temporal_capability_tests import TemporalLaggedCapabilityTests
from window_capture_bitmap_tests import BmpReadbackTests
from window_capture_failure_tests import CleanupErrorPreservationTests, DepthWarpFailureReportTests, FailureReportTests


LIT_TIMEOUT = "--timeout"
LIT_RENDER_READY_TIMEOUT = "--render-ready-timeout"
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
LIT_ORDINARY_STARTUP = "ordinary startup"
LIT_RESIZE_CLIENT = "--resize-client"
LIT_N_1001 = "1001"
LIT_N_701 = "701"
LIT_SETTLE_SECONDS = "--settle-seconds"
LIT_NAN = "nan"
LIT_RESIZE_SETTLE_SECONDS = "--resize-settle-seconds"
LIT_INF = "inf"
LIT_MAIN = "__main__"


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
                            WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(base_args + mode + [f"{option}={value}"])
                        self.assertEqual(rejection.exception.code, 2)
                        self.assertIn(option, errors.getvalue())
                        self.assertIn("finite", errors.getvalue())

    def test_zero_settle_is_allowed_but_zero_timeouts_are_rejected(self):
        base_args = [LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP]
        self.assertEqual(WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(base_args + [LIT_SETTLE_SECONDS, "0"]).settle_seconds, 0.0)
        for option in (LIT_TIMEOUT, LIT_RENDER_READY_TIMEOUT):
            with self.subTest(option=option), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as rejection:
                WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(base_args + [option, "0"])
            self.assertEqual(rejection.exception.code, 2)


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
            WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args([LIT_APPLICATION_CAPTURE, "--application-capture-frame-count", "0", LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP,])

    def test_application_capture_skip_reaches_process_exit_code_77_without_desktop_capture(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / LIT_CAPTURE_BMP
            with mock.patch.object(window_capture_smoke, LIT_LAUNCH_AND_CAPTURE_APPLICATION, side_effect=window_capture_smoke.SmokeSkip(window_capture_smoke.FRAMEBUFFER_CAPTURE_SKIP_MESSAGE),
            ), mock.patch.object(window_capture_smoke, LIT_CREATE_CAPTURE_BACKEND) as create_backend, \
                 mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS):
                exit_code = window_capture_smoke.main([LIT_APPLICATION_CAPTURE, LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, str(output)])

        self.assertEqual(exit_code, window_capture_smoke.SKIP_EXIT_CODE)
        create_backend.assert_not_called()


class RuntimeLogValidationTests(unittest.TestCase):
    def test_gpu_validation_cannot_be_enabled_on_an_existing_window(self):
        arguments = [LIT_WINDOW_HANDLE, "1", LIT_OUTPUT, LIT_CAPTURE_BMP]
        WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(arguments + ["--no-gpu-validation"])
        with mock.patch(LIT_SYS_STDERR), self.assertRaises(SystemExit) as error:
            WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(arguments + ["--gpu-validation"])
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


if __name__ == LIT_MAIN:
    unittest.main()
