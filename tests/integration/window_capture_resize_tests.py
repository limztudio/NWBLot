#!/usr/bin/env python3
"""Window capture resize configuration, lifecycle, and backend checks."""


import os
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

LIT_UI_LAYER_DIRECTORY = "ui_layer"

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / LIT_UI_LAYER_DIRECTORY))

import window_capture_smoke  # noqa: E402
import window_capture_environment  # noqa: E402


LIT_RENDER_READY_TIMEOUT = "--render-ready-timeout"
LIT_CAPTURE_BEFORE_RESIZE_BMP = "capture.before-resize.bmp"
LIT_CAPTURE = "capture"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_APPLICATION_CAPTURE = "--application-capture"
LIT_EXECUTABLE = "--executable"
LIT_OUTPUT = "--output"
LIT_WRITE_STATUS = "write_status"
LIT_WINDOW_HANDLE = "--window-handle"
LIT_SYS_STDERR = "sys.stderr"
LIT_LOG = "*.log"
LIT_COLLECT_LOG_DELTA = "collect_log_delta"
LIT_LOGS = "logs"
LIT_OFF = "off"
LIT_CAPTURE_RENDER_READY_WINDOW = "capture_render_ready_window"
LIT_MONOTONIC = "monotonic"
LIT_SLEEP = "sleep"
LIT_PREPARE = "prepare"
LIT_SYSTEM = "system"
LIT_WINDOWS = "Windows"
LIT_RESIZE_CLIENT = "--resize-client"
LIT_N_1001 = "1001"
LIT_N_701 = "701"
LIT_SETTLE_SECONDS = "--settle-seconds"
LIT_NAN = "nan"
LIT_RESIZE_SETTLE_SECONDS = "--resize-settle-seconds"
LIT_INF = "inf"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_RESIZE = "resize"
LIT_GRAPHICSRUNTIME_BACK_BUFFER_RESIZED_TO = "GraphicsRuntime: Back buffer resized to 1001x701"
LIT_MAIN = "__main__"


class ResizeCaptureConfigurationTests(unittest.TestCase):
    @staticmethod
    def base_args():
        return [LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP]

    def test_resize_rejects_incompatible_modes_and_invalid_limits(self):
        for extra in ([LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_APPLICATION_CAPTURE], [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_WINDOW_HANDLE, "1"], [LIT_RESIZE_CLIENT, "0", LIT_N_701], [LIT_RESIZE_CLIENT, "16385", LIT_N_701],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_SETTLE_SECONDS, "0"],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_SETTLE_SECONDS, LIT_NAN],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_RESIZE_SETTLE_SECONDS, "0.5"],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_RESIZE_SETTLE_SECONDS, LIT_INF], [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_RENDER_READY_TIMEOUT, LIT_INF], [LIT_RESIZE_SETTLE_SECONDS, "2"],):
            with self.subTest(extra=extra), mock.patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                window_capture_smoke.parse_args(self.base_args() + extra)

    def test_resize_rejects_active_freeze_and_application_self_capture(self):
        for name in (LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, "NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME"):
            for value in ("1", "120", LIT_NAN, "bad"):
                with self.subTest(name=name, value=value), self.assertRaises(window_capture_smoke.SmokeFailure):
                    window_capture_smoke.validate_resize_environment({name: value})
            window_capture_smoke.validate_resize_environment({name: "0"})
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "framebuffer capture request"):
            window_capture_smoke.validate_resize_environment({window_capture_smoke.FRAMEBUFFER_CAPTURE_PATH_ENV: "frame.bmp"})

    def test_launch_environment_checks_freeze_only_when_resize_is_requested(self):
        args = SimpleNamespace(resize_client=[1001, 701], software_vulkan=LIT_OFF)
        with mock.patch.dict(os.environ, {LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "6"}, clear=True), \
             mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_WINDOWS), \
             mock.patch.object(window_capture_smoke, "require_windows_interactive_desktop"), \
             mock.patch.object(window_capture_environment, "require_windows_interactive_desktop"):
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "active or invalid"):
                window_capture_smoke.build_launch_environment(args)
            args.resize_client = None
            window_capture_smoke.build_launch_environment(args)


class ResizeCaptureLifecycleTests(unittest.TestCase):
    @staticmethod
    def make_args():
        return SimpleNamespace(output=Path(LIT_CAPTURE_BMP), resize_client=[1001, 701], settle_seconds=2.0, resize_settle_seconds=3.0, render_ready_timeout=1.0)

    def test_resize_recaptures_settled_original_before_request_then_waits_for_runtime_ack(self):
        args = self.make_args()
        events = []
        backend = mock.Mock()
        backend.prepare_window.side_effect = lambda handle: events.append(LIT_PREPARE)
        backend.client_size.side_effect = [(1280, 900), (1280, 900), (1280, 900), (1001, 701), (1001, 701), (1001, 701)]
        backend.resize_client.side_effect = lambda *values: events.append((LIT_RESIZE, values))
        process = SimpleNamespace(poll=lambda: None)
        before = SimpleNamespace(width=1280, height=900)
        after = SimpleNamespace(width=1001, height=701)
        def capture(capture_args, *_):
            events.append((LIT_CAPTURE, capture_args.output))
            return before if len([event for event in events if isinstance(event, tuple) and event[0] == LIT_CAPTURE]) < 3 else after
        with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, side_effect=capture), \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, side_effect=["", LIT_GRAPHICSRUNTIME_BACK_BUFFER_RESIZED_TO]), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
             mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, side_effect=[10.0, 10.1]), \
             mock.patch.object(window_capture_smoke.time, LIT_SLEEP, side_effect=lambda seconds: events.append((LIT_SLEEP, seconds))):
            window_capture_smoke.capture_resized_window(args, backend, 42, process, Path(LIT_LOGS), {}, LIT_LOG)
        self.assertEqual(events, [LIT_PREPARE, (LIT_CAPTURE, Path(LIT_CAPTURE_BEFORE_RESIZE_BMP)), (LIT_SLEEP, 2.0), (LIT_CAPTURE, Path(LIT_CAPTURE_BEFORE_RESIZE_BMP)), (LIT_RESIZE, (42, 1001, 701)), (LIT_SLEEP, 0.1),
            (LIT_SLEEP, 3.0), (LIT_CAPTURE, Path(LIT_CAPTURE_BMP))])

    def test_resize_requires_both_client_extent_and_renderer_acknowledgement(self):
        for client, log in (((1280, 900), LIT_GRAPHICSRUNTIME_BACK_BUFFER_RESIZED_TO), ((1001, 701), "")):
            with self.subTest(client=client, log=log):
                args = self.make_args()
                backend = mock.Mock()
                backend.client_size.side_effect = [(1280, 900), (1280, 900), (1280, 900), client]
                with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=SimpleNamespace(width=1280, height=900)), \
                     mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, return_value=log), \
                     mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
                     mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, side_effect=[10.0, 11.0]), \
                     mock.patch.object(window_capture_smoke.time, LIT_SLEEP), \
                     self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "resize did not produce"):
                    window_capture_smoke.capture_resized_window(args, backend, 42, SimpleNamespace(poll=lambda: None), Path(LIT_LOGS), {}, LIT_LOG)
                backend.resize_client.assert_called_once()

    def test_final_capture_cannot_keep_the_old_extent(self):
        args = self.make_args()
        backend = mock.Mock()
        backend.client_size.side_effect = [(1280, 900), (1280, 900), (1280, 900), (1001, 701)]
        with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=SimpleNamespace(width=1280, height=900)), \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, return_value=LIT_GRAPHICSRUNTIME_BACK_BUFFER_RESIZED_TO), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
             mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, return_value=10.0), \
             mock.patch.object(window_capture_smoke.time, LIT_SLEEP), \
             self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "final client/capture extent"):
            window_capture_smoke.capture_resized_window(args, backend, 42, SimpleNamespace(poll=lambda: None), Path(LIT_LOGS), {}, LIT_LOG)

    def test_settled_original_capture_or_client_extent_change_prevents_resize(self):
        original = SimpleNamespace(width=1280, height=900)
        changed = SimpleNamespace(width=1279, height=900)
        for capture_changed in (True, False):
            with self.subTest(capture_changed=capture_changed):
                args = self.make_args()
                backend = mock.Mock()
                settled_client = (1280, 900) if capture_changed else (1279, 900)
                backend.client_size.side_effect = [(1280, 900), (1280, 900), settled_client]
                settled = changed if capture_changed else original
                with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, side_effect=[original, settled]), \
                     mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
                     mock.patch.object(window_capture_smoke.time, LIT_SLEEP), \
                     self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "settled original client/capture extent"):
                    window_capture_smoke.capture_resized_window(args, backend, 42, SimpleNamespace(poll=lambda: None), Path(LIT_LOGS), {}, LIT_LOG)
                backend.resize_client.assert_not_called()

    def test_noop_resize_is_rejected_before_sleep_or_window_mutation(self):
        args = self.make_args()
        backend = mock.Mock()
        with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=SimpleNamespace(width=1001, height=701)), \
             mock.patch.object(window_capture_smoke.time, LIT_SLEEP) as sleep, \
             self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "must change"):
            window_capture_smoke.capture_resized_window(args, backend, 42, SimpleNamespace(poll=lambda: None), Path(LIT_LOGS), {}, LIT_LOG)
        sleep.assert_not_called()
        backend.resize_client.assert_not_called()

    def test_process_exit_during_initial_settle_never_requests_resize(self):
        args = self.make_args()
        backend = mock.Mock()
        backend.client_size.return_value = (1280, 900)
        process = SimpleNamespace(poll=lambda: 7, returncode=7, _nwb_output_capture=None)
        with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=SimpleNamespace(width=1280, height=900)), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
             mock.patch.object(window_capture_smoke.time, LIT_SLEEP), \
             self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "before client resize"):
            window_capture_smoke.capture_resized_window(args, backend, 42, process, Path(LIT_LOGS), {}, LIT_LOG)
        backend.resize_client.assert_not_called()


class ResizeCaptureBackendTests(unittest.TestCase):
    def test_windows_resize_preserves_observed_nonclient_frame_and_issues_one_request(self):
        backend = object.__new__(window_capture_smoke.WindowsCapture)
        backend.user32 = mock.Mock()
        backend.user32.SetWindowPos.return_value = 1
        backend.client_size = mock.Mock(return_value=(1280, 900))
        backend._window_size = mock.Mock(return_value=(1296, 939))
        hwnd = 0xF234567887654321
        backend.resize_client(hwnd, 1001, 701)
        received = backend.user32.SetWindowPos.call_args.args
        self.assertEqual(received[0].value, hwnd)
        self.assertEqual(received[4:6], (1017, 740))
        self.assertFalse(received[6] & backend.SWP_NOSIZE)
        self.assertTrue(received[6] & backend.SWP_NOMOVE)
        backend.user32.SetWindowPos.return_value = 0
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "SetWindowPos failed"):
            backend.resize_client(hwnd, 1001, 701)

    def test_linux_resize_rejects_failed_native_request(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend.display = object()
        backend.x11 = mock.Mock()
        backend.x11.XResizeWindow.return_value = 0
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "XResizeWindow failed"):
            backend.resize_client(42, 1001, 701)


if __name__ == LIT_MAIN:
    unittest.main()
