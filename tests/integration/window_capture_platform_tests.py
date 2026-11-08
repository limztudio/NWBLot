#!/usr/bin/env python3

from contextlib import redirect_stderr, redirect_stdout
import ctypes
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / "ui_layer"))

import window_capture_outcome
import window_capture_smoke
import window_capture_win32
import interaction_smoke
import native_input

LIT_CREATE_WINDOWS_USER32 = "create_windows_user32"
LIT_INVISIBLE_STATION = "invisible station"
LIT_PASSED = "passed"
LIT_SKIPPED = "skipped"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_APPLICATION_CAPTURE = "--application-capture"
LIT_EXECUTABLE = "--executable"
LIT_OUTPUT = "--output"
LIT_WRITE_STATUS = "write_status"
LIT_UTF_8 = "utf-8"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_SYSTEM = "system"
LIT_WINDOWS = "Windows"


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
                with mock.patch.object(window_capture_win32.ctypes, "get_last_error", return_value=5, create=True), \
                     self.assertRaisesRegex(window_capture_smoke.SmokeFailure, api + ".*failed: 5"):
                    window_capture_win32.require_windows_interactive_desktop(user32)
                if missing_station:
                    user32.GetUserObjectInformationW.assert_not_called()

    def test_capture_constructor_checks_the_station_before_loading_gdi(self):
        with mock.patch.object(window_capture_win32, LIT_CREATE_WINDOWS_USER32, return_value=self.user32(0)), \
             mock.patch.object(window_capture_win32.ctypes, "WinDLL", create=True) as load_dll, \
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


class InteractionForegroundTests(unittest.TestCase):
    def native_input(self, foreground_values):
        native = native_input.NativeInput.__new__(native_input.NativeInput)
        native.windows = True
        native.handle = 42
        native.backend = SimpleNamespace(
            user32=SimpleNamespace(GetForegroundWindow=mock.Mock(side_effect=foreground_values)),
            focus_window=mock.Mock(),
        )
        return native

    def test_waits_until_the_fixture_owns_foreground(self):
        native = self.native_input([10, 42])
        with mock.patch.object(native_input, "ensure_process_running") as running, \
             mock.patch.object(native_input.time, "monotonic", side_effect=[0.0, 0.0, 0.2]), \
             mock.patch.object(native_input.time, "sleep"):
            native.wait_for_foreground(object(), 1.0)
        self.assertEqual(native.backend.focus_window.call_count, 2)
        self.assertEqual(running.call_count, 2)

    def test_unavailable_foreground_is_a_skip_and_process_failure_propagates(self):
        native = self.native_input([10])
        with mock.patch.object(native_input, "ensure_process_running"), \
             mock.patch.object(native_input.time, "monotonic", side_effect=[0.0, 0.0, 2.0]), \
             mock.patch.object(native_input.time, "sleep"):
            with self.assertRaisesRegex(interaction_smoke.SmokeSkip, "foreground"):
                native.wait_for_foreground(object(), 1.0)
        with mock.patch.object(native_input, "ensure_process_running", side_effect=interaction_smoke.SmokeFailure("stopped")):
            with self.assertRaisesRegex(interaction_smoke.SmokeFailure, "stopped"):
                native.wait_for_foreground(object(), 1.0)


if __name__ == "__main__":
    unittest.main()
