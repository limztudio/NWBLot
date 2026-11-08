#!/usr/bin/env python3
"""Window capture platform backend lifecycle tests."""


import subprocess
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))

import window_capture_smoke  # noqa: E402
import window_capture_windows_pixels  # noqa: E402
import window_capture_x11_pixels  # noqa: E402
import window_capture_x11_input  # noqa: E402


LIT_RAISE = "raise"
LIT_FLUSH = "flush"
LIT_SYNC = "sync"
LIT_ROOT = "root"
LIT_CLIENT = "client"
LIT_SETTLE = "settle"
LIT_CAPTURE = "capture"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_WRITE_STATUS = "write_status"
LIT_SLEEP = "sleep"
LIT_PREPARE = "prepare"
LIT_POST_PREPARE_CLIENT_RECT = "post-prepare-client-rect"
LIT_BITBLT = "BitBlt"


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


class WindowsPixelResourceTests(unittest.TestCase):
    def test_failed_bitmap_read_releases_every_acquired_resource(self):
        user32 = mock.Mock()
        gdi32 = mock.Mock()
        user32.GetDC.return_value = 10
        gdi32.CreateCompatibleDC.return_value = 20
        gdi32.CreateCompatibleBitmap.return_value = 30
        gdi32.SelectObject.return_value = 40
        gdi32.BitBlt.return_value = 1
        gdi32.GetDIBits.return_value = 0
        rect = SimpleNamespace(left=2, top=3, right=4, bottom=5)

        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "GetDIBits failed"):
            window_capture_windows_pixels.WindowsScreenPixels(user32, gdi32).capture(42, rect, Path(LIT_CAPTURE_BMP))
        self.assertEqual(gdi32.SelectObject.call_args_list, [mock.call(20, 30), mock.call(20, 40)])
        gdi32.DeleteObject.assert_called_once_with(30)
        gdi32.DeleteDC.assert_called_once_with(20)
        user32.ReleaseDC.assert_called_once_with(None, 10)

    def test_failed_pixel_sample_releases_the_screen_device_context(self):
        user32 = mock.Mock()
        gdi32 = mock.Mock()
        user32.GetDC.return_value = 10
        gdi32.GetPixel.return_value = 0xFFFFFFFF
        rect = SimpleNamespace(left=2, top=3)

        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "GetPixel failed"):
            window_capture_windows_pixels.WindowsScreenPixels(user32, gdi32).sample(42, rect, 0, 0, 1, 1)
        user32.ReleaseDC.assert_called_once_with(None, 10)


class LinuxInputFailureTests(unittest.TestCase):
    def test_failed_key_send_does_not_flush_and_reports_the_failure(self):
        x11 = mock.Mock()
        x11.XTranslateCoordinates.return_value = 0
        x11.XSendEvent.return_value = 0
        sender = window_capture_x11_input.X11InputSender(x11, 1, 2)

        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "failed to send key event"):
            sender.send_key_event(42, sender.KEY_PRESS, 10)
        x11.XFlush.assert_not_called()


class LinuxImageStructureTests(unittest.TestCase):
    def test_backend_import_defines_image_layout_before_smoke_import(self):
        code = (
            "import ctypes; from window_capture_x11 import LinuxXImage; "
            "assert ctypes.sizeof(LinuxXImage) > 0 and LinuxXImage.width.offset == 0; "
            "image = LinuxXImage(); image.width = 23; image.height = 7; "
            "assert image.width == 23 and image.height == 7; "
            "import window_capture_smoke; "
            "assert image.width == 23 and image.height == 7"
        )
        result = subprocess.run([sys.executable, "-c", code], cwd=SMOKE_DIRECTORY, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)


class LinuxPixelResourceTests(unittest.TestCase):
    def test_failed_bitmap_write_destroys_the_ximage(self):
        x11 = mock.Mock()
        image = SimpleNamespace(contents=object())
        x11.XGetImage.return_value = image
        pixels = window_capture_x11_pixels.X11DrawablePixels(x11, object())
        pixels.write_ximage_bmp = mock.Mock(side_effect=ValueError("invalid image"))

        with self.assertRaisesRegex(ValueError, "invalid image"):
            pixels.capture(10, 42, 0, 0, 2, 2, Path(LIT_CAPTURE_BMP))
        x11.XDestroyImage.assert_called_once_with(image)


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


if __name__ == "__main__":
    unittest.main()
