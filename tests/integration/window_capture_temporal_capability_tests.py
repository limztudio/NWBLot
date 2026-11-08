from contextlib import redirect_stderr, redirect_stdout
import io
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import csg_gi_temporal_smoke
import window_capture_image
from window_capture_arguments import WindowCaptureArguments
import window_capture_smoke


LIT_TEMPORAL_CAPTURE_FAILED_WITH_EXIT_1 = "temporal capture failed with exit 1"
LIT_WRITE_STATUS = "write_status"
LIT_LOG = "*.log"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT = "wait_for_application_capture_exit"


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
                capture_args = WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(command[2:])

                def wait_for_capture(*_args):
                    rows = [[(255, 255, 255) if (x + y) % 2 else (0, 0, 0) for x in range(8)] for y in range(8)]
                    window_capture_image.write_bmp_24(capture_args.output, 8, 8, rows)
                    return application_exit, True

                with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                     mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, return_value=(object(), 49152, directory, {}, LIT_LOG)), \
                     mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                     mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, side_effect=wait_for_capture), \
                     mock.patch.object(window_capture_smoke.CaptureProcessTermination, LIT_TERMINATE_PROCESS, return_value=(application_exit, "")), \
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

