#!/usr/bin/env python3
"""Window capture render-ready, application, platform backend, caret, and resize lifecycles."""


import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

LIT_UI_LAYER_DIRECTORY = "ui_layer"

LIT_HELP_TIMEOUT_SECONDS = 15

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / LIT_UI_LAYER_DIRECTORY))

import window_capture_smoke  # noqa: E402


LIT_RAISE = "raise"
LIT_FLUSH = "flush"
LIT_SYNC = "sync"
LIT_ROOT = "root"
LIT_CLIENT = "client"
LIT_SETTLE = "settle"
LIT_CAPTURE = "capture"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_WRITE_STATUS = "write_status"
LIT_EARLY = "early"
LIT_CUT_VOID_0 = "cut_void=0"
LIT_UTF_8 = "utf-8"
LIT_LOG = "*.log"
LIT_MISSING_LOG_MESSAGE = "missing log message"
LIT_BLOCKING_LOG_MESSAGE = "blocking log message"
LIT_ORDINARY_STARTUP = "ordinary startup"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_MONOTONIC = "monotonic"
LIT_SLEEP = "sleep"
LIT_LOGSERVER_LAUNCH = "logserver launch"
LIT_APPLICATION_LAUNCH = "application launch"
LIT_APPLICATION_WAIT = "application wait"
LIT_APPLICATION_OUTPUT_COLLECT = "application output collect"
LIT_LOGSERVER_SHUTDOWN_AND_LOG_COLLECT = "logserver shutdown and log collect"
LIT_LOG_VALIDATION = "log validation"
LIT_BMP_PARSE = "BMP parse"
LIT_PIXEL_VALIDATION = "pixel validation"
LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT = "wait_for_application_capture_exit"
LIT_READ_BMP_24 = "read_bmp_24"
LIT_LOGS_COLLECTED = "logs collected"
LIT_APPLICATION_TERMINATED = "application terminated"
LIT_TIMED_OUT_WAITING_FOR_SELF_EXIT = "timed out waiting for self-exit"
LIT_CAPTURE_ROOT = "capture-root"
LIT_PREPARE = "prepare"
LIT_POST_PREPARE_CLIENT_RECT = "post-prepare-client-rect"
LIT_BITBLT = "BitBlt"
LIT_MAIN = "__main__"


class RenderReadyCaptureTests(unittest.TestCase):
    @staticmethod
    def make_args(directory, timeout=1.0):
        return SimpleNamespace(expect_texture_smoke=False, expect_transparent_csg=False, expect_transparent_multi=False, output=directory / LIT_CAPTURE_BMP, render_ready_timeout=timeout,)

    @staticmethod
    def make_capture(appears_empty_or_white, has_pixel_variation):
        return SimpleNamespace(appears_empty_or_white=appears_empty_or_white, handle=0x4A, has_pixel_variation=has_pixel_variation,)

    def test_sub_eight_pixel_captures_bound_background_sampling_to_existing_rows(self):
        for width in range(1, 8):
            for height in range(1, 8):
                with self.subTest(width=width, height=height):
                    rows = [[(32, 64, 96)] * width for _ in range(height)]
                    self.assertEqual(window_capture_smoke.estimate_background_rgb(rows, width, height), (32, 64, 96))
                    result = window_capture_smoke.capture_result_from_rgb_rows(0, width, height, rows)
                    self.assertFalse(result.appears_empty_or_white)
                    self.assertFalse(result.has_pixel_variation)

    def test_uniform_gray_is_flat_but_not_a_readiness_placeholder(self):
        analysis = window_capture_smoke.analyze_rgb_rows([[(128, 128, 128), (128, 128, 128)], [(128, 128, 128), (128, 128, 128)],])

        self.assertFalse(analysis.appears_empty_or_white)
        self.assertFalse(analysis.has_pixel_variation)

    def test_near_white_window_is_a_readiness_placeholder(self):
        rows = [[(250, 250, 250) for _ in range(20)] for _ in range(20)]
        rows[0][0] = (240, 240, 240)

        analysis = window_capture_smoke.analyze_rgb_rows(rows)

        self.assertTrue(analysis.appears_empty_or_white)
        self.assertTrue(analysis.has_pixel_variation)

    def test_white_window_is_retried_until_rendered_content_is_ready(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            white = self.make_capture(True, False)
            rendered = self.make_capture(False, True)
            backend = mock.Mock()
            backend.capture_window.side_effect = [white, rendered]
            process = SimpleNamespace(poll=lambda: None)

            with mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, side_effect=(10.0, 10.25)), \
                 mock.patch.object(window_capture_smoke.time, LIT_SLEEP) as sleep:
                result = window_capture_smoke.capture_render_ready_window(args, backend, 0x4A, process)

            self.assertIs(result, rendered)
            self.assertEqual(backend.capture_window.call_count, 2)
            sleep.assert_called_once_with(window_capture_smoke.RENDER_READY_POLL_SECONDS)

    def test_persistent_white_window_fails_at_readiness_deadline(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir), timeout=0.25)
            white = self.make_capture(True, False)
            backend = mock.Mock()
            backend.capture_window.return_value = white
            process = SimpleNamespace(poll=lambda: None)

            with mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, side_effect=(10.0, 10.25)), \
                 mock.patch.object(window_capture_smoke.time, LIT_SLEEP) as sleep:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "remained blank or white"):
                    window_capture_smoke.capture_render_ready_window(args, backend, 0x4A, process)

            backend.capture_window.assert_called_once()
            sleep.assert_not_called()

    def test_process_exit_after_white_capture_fails_without_another_attempt(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            white = self.make_capture(True, False)
            backend = mock.Mock()
            backend.capture_window.return_value = white
            process = mock.Mock()
            process.poll.side_effect = (None, None, 7)
            process.returncode = 7
            process._nwb_output_capture = None

            with mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, side_effect=(10.0, 10.25)), \
                 mock.patch.object(window_capture_smoke.time, LIT_SLEEP) as sleep:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, r"testbed exited while waiting for rendered window content \(exit 7\)",):
                    window_capture_smoke.capture_render_ready_window(args, backend, 0x4A, process)

            backend.capture_window.assert_called_once()
            sleep.assert_called_once_with(window_capture_smoke.RENDER_READY_POLL_SECONDS)

    def test_nonblank_csg_failure_is_not_retried_to_find_a_passing_frame(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            args.expect_transparent_csg = True
            args.transparent_csg_pose = LIT_EARLY
            uncut = self.make_capture(False, True)
            uncut.transparent_csg = {LIT_EARLY: window_capture_smoke.TransparentCsgAnalysis(0, 600, 200, 1000)}
            backend = mock.Mock()
            backend.capture_window.return_value = uncut
            process = SimpleNamespace(poll=lambda: None)
            with mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, return_value=10.0), \
                 mock.patch.object(window_capture_smoke.time, LIT_SLEEP) as sleep:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_CUT_VOID_0):
                    window_capture_smoke.capture_render_ready_window(args, backend, 0x4A, process)
            backend.capture_window.assert_called_once()
            sleep.assert_not_called()

    def test_nonblank_invalid_frame_fails_without_retry(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            flat = self.make_capture(False, False)
            backend = mock.Mock()
            backend.capture_window.return_value = flat
            process = SimpleNamespace(poll=lambda: None)

            with mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, return_value=10.0), \
                 mock.patch.object(window_capture_smoke.time, LIT_SLEEP) as sleep:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "image appears flat"):
                    window_capture_smoke.capture_render_ready_window(args, backend, 0x4A, process)

            backend.capture_window.assert_called_once()
            sleep.assert_not_called()


class ApplicationCaptureLifecycleTests(unittest.TestCase):
    @staticmethod
    def make_args(directory):
        return SimpleNamespace(application_capture_frame_count=12, executable=sys.executable, expect_log_message=[], expect_texture_smoke=False, expect_transparent_csg=False, expect_transparent_multi=False, output=directory / LIT_CAPTURE_BMP,
            reject_log_message=list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES), skip_blocking_log_message=list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES), skip_log_message=[], timeout=1.0,)

    def test_normal_capture_collects_shutdown_logs_before_marker_and_pixel_validation(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            args = self.make_args(directory)
            args.log_output = directory / "evidence" / "capture.log"
            captured_log = window_capture_smoke.FRAMEBUFFER_CAPTURE_READY_MESSAGE + "\ncompleted sample\r\nUTF-8: \u03bb\n"
            logserver_process = object()
            testbed_process = object()
            capture = SimpleNamespace(width=3, height=2)
            events = []

            def launch_logserver(*_args):
                events.append(LIT_LOGSERVER_LAUNCH)
                return logserver_process, 49152, directory, {}, LIT_LOG

            def launch_testbed(_args, _executable, env, _log_port):
                events.append(LIT_APPLICATION_LAUNCH)
                return testbed_process

            def wait_for_exit(*_args):
                events.append(LIT_APPLICATION_WAIT)
                args.output.write_bytes(b"engine-bmp")
                return 0, True

            def terminate(process, name):
                self.assertIs(process, testbed_process)
                events.append(LIT_APPLICATION_OUTPUT_COLLECT)
                return 0, ""

            def shutdown_and_collect(*_args):
                events.append(LIT_LOGSERVER_SHUTDOWN_AND_LOG_COLLECT)
                return captured_log

            def validate_logs(log_text, _args):
                events.append(LIT_LOG_VALIDATION)
                self.assertEqual(args.log_output.read_bytes(), captured_log.encode(LIT_UTF_8))

            def read_bmp(path):
                events.append(LIT_BMP_PARSE)
                return capture

            def validate_pixels(_args, result):
                events.append(LIT_PIXEL_VALIDATION)

            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, side_effect=launch_logserver), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, side_effect=launch_testbed), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, side_effect=wait_for_exit), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
                 mock.patch.object(window_capture_smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, side_effect=shutdown_and_collect,), \
                 mock.patch.object(window_capture_smoke, "validate_application_capture_log_text", side_effect=validate_logs,), \
                 mock.patch.object(window_capture_smoke, LIT_READ_BMP_24, side_effect=read_bmp), \
                 mock.patch.object(window_capture_smoke, "validate_capture_for_args", side_effect=validate_pixels):
                window_capture_smoke.launch_and_capture_application(args)

        self.assertEqual(events, [LIT_LOGSERVER_LAUNCH, LIT_APPLICATION_LAUNCH, LIT_APPLICATION_WAIT, LIT_APPLICATION_OUTPUT_COLLECT, LIT_LOGSERVER_SHUTDOWN_AND_LOG_COLLECT, LIT_LOG_VALIDATION, LIT_BMP_PARSE, LIT_PIXEL_VALIDATION,],)

    def test_nonzero_application_exit_fails_after_shutdown_log_collection(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            events = []
            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, return_value=(object(), 49152, Path(temp_dir), {}, LIT_LOG),), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, return_value=(9, False)), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, return_value=(9, "application tail")), \
                 mock.patch.object(window_capture_smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, side_effect=lambda *_args: events.append(LIT_LOGS_COLLECTED) or window_capture_smoke.FRAMEBUFFER_CAPTURE_READY_MESSAGE,), \
                 mock.patch.object(window_capture_smoke, LIT_READ_BMP_24) as read_bmp:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, r"self-exited.*exit 9"):
                    window_capture_smoke.launch_and_capture_application(args)

        self.assertEqual(events, [LIT_LOGS_COLLECTED])
        read_bmp.assert_not_called()

    def test_application_timeout_terminates_then_collects_logs_before_failing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            args.log_output = Path(temp_dir) / "timeout.log"
            events = []

            def terminate(*_args):
                events.append(LIT_APPLICATION_TERMINATED)
                return -15, ""

            def shutdown_and_collect(*_args):
                events.append(LIT_LOGS_COLLECTED)
                return LIT_ORDINARY_STARTUP

            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, return_value=(object(), 49152, Path(temp_dir), {}, LIT_LOG),), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, side_effect=window_capture_smoke.SmokeFailure(LIT_TIMED_OUT_WAITING_FOR_SELF_EXIT),), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
                 mock.patch.object(window_capture_smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, side_effect=shutdown_and_collect,):
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_TIMED_OUT_WAITING_FOR_SELF_EXIT):
                    window_capture_smoke.launch_and_capture_application(args)
            self.assertEqual(args.log_output.read_bytes(), LIT_ORDINARY_STARTUP.encode(LIT_UTF_8))

        self.assertEqual(events, [LIT_APPLICATION_TERMINATED, LIT_LOGS_COLLECTED])

    def test_capture_ready_marker_is_required_after_normal_exit(self):
        args = self.make_args(Path(LIT_CAPTURE_ROOT))
        self.assertIsNone(window_capture_smoke.validate_application_capture_log_text(window_capture_smoke.FRAMEBUFFER_CAPTURE_READY_MESSAGE, args,))
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_MISSING_LOG_MESSAGE):
            window_capture_smoke.validate_application_capture_log_text("ordinary shutdown", args)

    def test_capability_skip_requires_no_strict_blocking_diagnostics(self):
        args = self.make_args(Path(LIT_CAPTURE_ROOT))
        marker = window_capture_smoke.FRAMEBUFFER_CAPTURE_SKIP_MESSAGE
        self.assertEqual(window_capture_smoke.validate_application_capture_log_text(marker, args), marker)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_BLOCKING_LOG_MESSAGE):
            window_capture_smoke.validate_application_capture_log_text(f"{marker}\n[WARNING] teardown failure", args)

    def test_capability_skip_does_not_require_a_capture_artifact(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, return_value=(object(), 49152, Path(temp_dir), {}, LIT_LOG),), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, return_value=(0, False)), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, return_value=(0, "")), \
                 mock.patch.object(window_capture_smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=window_capture_smoke.FRAMEBUFFER_CAPTURE_SKIP_MESSAGE,):
                with self.assertRaisesRegex(window_capture_smoke.SmokeSkip, "transfer-source usage is unavailable"):
                    window_capture_smoke.launch_and_capture_application(args)


class WindowsCaptureOrderingTests(unittest.TestCase):
    def test_capture_window_prepares_before_querying_the_client_screen_rect(self):
        hwnd = 0x4A
        output_path = Path(LIT_CAPTURE_BMP)
        calls = []
        expected_rect = object()
        capture = object.__new__(window_capture_smoke.WindowsCapture)

        def prepare(window):
            calls.append((LIT_PREPARE, window))

        def client_rect(window):
            self.assertEqual(calls, [(LIT_PREPARE, hwnd)])
            calls.append((LIT_POST_PREPARE_CLIENT_RECT, window))
            return expected_rect

        def screen_bitblt(window, rect, path):
            calls.append((LIT_BITBLT, window, rect, path))
            return LIT_CAPTURE

        capture._prepare_capture_window = prepare
        capture._client_rect = client_rect
        capture._capture_screen_rect = screen_bitblt

        capture.capture_window(hwnd, output_path)
        self.assertEqual(calls, [(LIT_PREPARE, hwnd), (LIT_POST_PREPARE_CLIENT_RECT, hwnd), (LIT_BITBLT, hwnd, expected_rect, output_path),],)

    def test_visible_normal_capture_does_not_restart_restore_or_show_on_repeated_preparation(self):
        hwnd = 0xF234567887654321
        capture = object.__new__(window_capture_smoke.WindowsCapture)
        capture.user32 = mock.Mock()
        capture.gdi32 = mock.Mock()
        capture._bind_functions()
        capture.user32.IsIconic.return_value = 0
        capture.user32.IsZoomed.return_value = 0
        capture.user32.IsWindowVisible.return_value = 1
        capture._client_rect = mock.Mock(return_value=object())
        capture._capture_screen_rect = mock.Mock(return_value=LIT_CAPTURE)
        with mock.patch.object(window_capture_smoke.time, LIT_SLEEP):
            capture.capture_window(hwnd, Path(LIT_CAPTURE_BMP))
            capture.capture_window(hwnd, Path(LIT_CAPTURE_BMP))
        capture.user32.ShowWindow.assert_not_called()
        self.assertEqual(capture.user32.SetWindowPos.call_count, 2)
        for call in capture.user32.SetWindowPos.call_args_list:
            self.assertEqual(call.args[0].value, hwnd)
            self.assertEqual(call.args[6], capture.SWP_NOMOVE | capture.SWP_NOSIZE)
        for query in (capture.user32.IsIconic, capture.user32.IsZoomed, capture.user32.IsWindowVisible):
            self.assertEqual([call.args[0].value for call in query.call_args_list], [hwnd, hwnd])

    def test_minimized_or_maximized_preparation_restores_once_then_preserves_normal_state(self):
        hwnd = 0xF234567887654321
        for iconic, zoomed in ((1, 0), (0, 1)):
            with self.subTest(iconic=iconic, zoomed=zoomed):
                capture = object.__new__(window_capture_smoke.WindowsCapture)
                capture.user32 = mock.Mock()
                capture.user32.IsIconic.return_value = iconic
                capture.user32.IsZoomed.return_value = zoomed
                capture.user32.IsWindowVisible.return_value = 1
                def restore(window, action):
                    self.assertEqual(window.value, hwnd)
                    self.assertEqual(action, capture.SW_RESTORE)
                    capture.user32.IsIconic.return_value = 0
                    capture.user32.IsZoomed.return_value = 0
                capture.user32.ShowWindow.side_effect = restore
                with mock.patch.object(window_capture_smoke.time, LIT_SLEEP):
                    capture.prepare_window(hwnd)
                    capture.prepare_window(hwnd)
                capture.user32.ShowWindow.assert_called_once()
                self.assertTrue(all(not call.args[6] & capture.SWP_SHOWWINDOW for call in capture.user32.SetWindowPos.call_args_list))

    def test_hidden_preparation_shows_current_geometry_once_without_restore(self):
        hwnd = 0xF234567887654321
        capture = object.__new__(window_capture_smoke.WindowsCapture)
        capture.user32 = mock.Mock()
        capture.user32.IsIconic.return_value = 0
        capture.user32.IsZoomed.return_value = 0
        capture.user32.IsWindowVisible.return_value = 0
        def show(window, action):
            self.assertEqual(window.value, hwnd)
            self.assertEqual(action, capture.SW_SHOW)
            capture.user32.IsWindowVisible.return_value = 1
        capture.user32.ShowWindow.side_effect = show
        with mock.patch.object(window_capture_smoke.time, LIT_SLEEP):
            capture.prepare_window(hwnd)
            capture.prepare_window(hwnd)
        capture.user32.ShowWindow.assert_called_once()
        self.assertTrue(all(not call.args[6] & capture.SWP_SHOWWINDOW for call in capture.user32.SetWindowPos.call_args_list))


class LinuxCaptureFallbackTests(unittest.TestCase):
    def test_white_direct_capture_defers_to_render_readiness_without_root_fallback(self):
        window = 0x4A
        output_path = Path(LIT_CAPTURE_BMP)
        capture = object.__new__(window_capture_smoke.LinuxX11Capture)
        capture.display = object()
        capture.root = 0x1
        capture.x11 = mock.Mock()
        capture.get_attributes = mock.Mock(return_value=SimpleNamespace(width=1280, height=900))
        white_capture = SimpleNamespace(has_pixel_variation=False, appears_empty_or_white=True)
        capture._capture_drawable_region = mock.Mock(return_value=white_capture)
        capture._window_root_region = mock.Mock()

        with mock.patch.object(window_capture_smoke.time, LIT_SLEEP):
            result = capture.capture_window(window, output_path)

        self.assertIs(result, white_capture)
        capture._window_root_region.assert_not_called()


    def test_flat_direct_capture_keeps_raise_settle_and_root_fallback(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend.display, backend.root = object(), 1
        backend.x11 = mock.Mock()
        backend.get_attributes = mock.Mock(return_value=SimpleNamespace(width=100, height=80))
        backend._window_root_region = mock.Mock(return_value=(10, 20, 100, 80))
        flat = SimpleNamespace(has_pixel_variation=False, appears_empty_or_white=False)
        rendered = SimpleNamespace(has_pixel_variation=True, appears_empty_or_white=False)
        events = []
        backend.x11.XRaiseWindow.side_effect = lambda *_args: events.append(LIT_RAISE)
        backend.x11.XFlush.side_effect = lambda *_args: events.append(LIT_FLUSH)
        backend.x11.XSync.side_effect = lambda *_args: events.append(LIT_SYNC)

        def read(drawable, *_args):
            events.append(LIT_ROOT if drawable == backend.root else LIT_CLIENT)
            return rendered if drawable == backend.root else flat

        backend._capture_drawable_region = mock.Mock(side_effect=read)
        with mock.patch.object(window_capture_smoke.time, LIT_SLEEP, side_effect=lambda delay: events.append((LIT_SETTLE, delay))), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS):
            result = backend.capture_client_window(42, Path(LIT_CAPTURE_BMP))
        self.assertIs(result, rendered)
        self.assertEqual(events, [LIT_RAISE, LIT_FLUSH, (LIT_SETTLE, 0.2), LIT_SYNC, LIT_CLIENT, LIT_ROOT])
        self.assertEqual(backend._capture_drawable_region.call_args_list, [mock.call(42, 42, 0, 0, 100, 80, Path(LIT_CAPTURE_BMP)), mock.call(1, 42, 10, 20, 100, 80, Path(LIT_CAPTURE_BMP))])


class LinuxPreparedCaptureTests(unittest.TestCase):
    def test_prepared_read_preserves_phase_that_ordinary_settle_misses(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend.display, backend.root = object(), 1
        backend.x11 = mock.Mock()
        backend.get_attributes = mock.Mock(return_value=SimpleNamespace(width=100, height=80))
        backend._window_root_region = mock.Mock()
        clock = SimpleNamespace(now=0.375)
        caret, background = (249, 252, 255), (18, 26, 37)

        def read(*_args):
            pixel = caret if clock.now % 1.0 < 0.5 else background
            clock.now += 0.6  # Conversion follows the already frozen native image.
            return SimpleNamespace(pixel=pixel, has_pixel_variation=True, appears_empty_or_white=False)

        def sleep(delay):
            clock.now += delay

        backend._capture_drawable_region = mock.Mock(side_effect=read)
        with mock.patch.object(window_capture_smoke.time, LIT_SLEEP, side_effect=sleep) as settle:
            prepared = backend.capture_prepared_raw_client_window(42, Path(LIT_CAPTURE_BMP))
            self.assertEqual(prepared.pixel, caret)
            self.assertGreater(clock.now, 0.5)
            backend._capture_drawable_region.assert_called_once_with(42, 42, 0, 0, 100, 80, Path(LIT_CAPTURE_BMP))
            backend.x11.XRaiseWindow.assert_not_called()
            backend.x11.XFlush.assert_not_called()
            backend.x11.XSync.assert_not_called()
            backend._window_root_region.assert_not_called()
            settle.assert_not_called()
            clock.now = 0.375
            ordinary = backend.capture_client_window(42, Path(LIT_CAPTURE_BMP))
            self.assertEqual(ordinary.pixel, background)
            settle.assert_called_once_with(0.2)

    def test_prepared_read_rejects_unavailable_or_invalid_client_before_capture(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend.get_attributes = mock.Mock()
        backend._capture_drawable_region = mock.Mock()
        for attributes in (None, SimpleNamespace(width=0, height=80), SimpleNamespace(width=100, height=-1)):
            with self.subTest(attributes=attributes), self.assertRaises(window_capture_smoke.SmokeFailure):
                backend.get_attributes.return_value = attributes
                backend.capture_prepared_raw_client_window(42, Path(LIT_CAPTURE_BMP))
        backend._capture_drawable_region.assert_not_called()


class UiSmokeEntrypointBootstrapTests(unittest.TestCase):
    def test_progress_help_bootstraps_from_an_unrelated_working_directory(self):
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(SMOKE_DIRECTORY / LIT_UI_LAYER_DIRECTORY / "progress_smoke.py"), "--help"],
                cwd=directory, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=LIT_HELP_TIMEOUT_SECONDS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("usage:", result.stdout)
        self.assertEqual(result.stderr, "")


if __name__ == LIT_MAIN:
    unittest.main()
