#!/usr/bin/env python3
"""Clipboard failure cleanup for native numeric and text-area fixtures."""

from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / "ui_layer"))

import numeric_clipboard_reader as clipboard
from window_capture_smoke import SmokeFailure


class NumericClipboardFailureTests(unittest.TestCase):
    def test_windows_lock_failure_closes_clipboard_without_unlocking(self):
        reader = object.__new__(clipboard.NumericClipboardReader)
        user32 = mock.Mock()
        user32.OpenClipboard.return_value = 1
        user32.IsClipboardFormatAvailable.return_value = 1
        user32.GetClipboardData.return_value = 42
        kernel32 = mock.Mock()
        kernel32.GlobalSize.return_value = 8
        kernel32.GlobalLock.return_value = 0
        reader.windows = True
        reader.backend = SimpleNamespace(user32=user32)
        reader.kernel32 = kernel32
        with self.assertRaisesRegex(SmokeFailure, "GlobalLock failed"):
            reader.read_text()
        user32.CloseClipboard.assert_called_once_with()
        kernel32.GlobalUnlock.assert_not_called()

    def test_x11_timeout_destroys_requestor_and_flushes(self):
        reader = object.__new__(clipboard.NumericClipboardReader)
        x11 = mock.Mock()
        x11.XInternAtom.side_effect = (1, 2, 3)
        x11.XGetSelectionOwner.return_value = 7
        x11.XCreateSimpleWindow.return_value = 9
        x11.XCheckTypedWindowEvent.return_value = 0
        display = object()
        reader.windows = False
        reader.backend = SimpleNamespace(x11=x11, display=display, root=5)
        with mock.patch.object(clipboard.time, "monotonic", side_effect=(0.0, 2.0)):
            with self.assertRaisesRegex(SmokeFailure, "conversion did not complete"):
                reader.read_text(timeout=1.0)
        x11.XDestroyWindow.assert_called_once_with(display, 9)
        self.assertEqual(x11.XFlush.call_count, 2)


if __name__ == "__main__":
    unittest.main()
