from __future__ import annotations

from types import SimpleNamespace
from unittest import mock


LIT_SCREEN_CLIENT = "screen-client"
LIT_CURSOR = "cursor"
LIT_PREPARE = "prepare"
LIT_DWMSETWINDOWATTRIBUTE = "DwmSetWindowAttribute"
LIT_DWMFLUSH = "DwmFlush"


class M4WindowsCaptureChecks:
    @staticmethod
    def check_client_geometry(harness):
        class ClientRectUser32:
            def __init__(self, get_client_rect_result=True, client_to_screen_result=True):
                self.get_client_rect_result = get_client_rect_result
                self.client_to_screen_result = client_to_screen_result
                self.get_client_rect_calls = 0
                self.client_to_screen_calls = 0

            def GetClientRect(self, hwnd, rect_pointer):
                del hwnd
                self.get_client_rect_calls += 1
                if not self.get_client_rect_result:
                    return 0
                rect = rect_pointer._obj
                rect.left = 0
                rect.top = 0
                rect.right = 1280
                rect.bottom = 900
                return 1

            def ClientToScreen(self, hwnd, point_pointer):
                del hwnd
                self.client_to_screen_calls += 1
                if not self.client_to_screen_result:
                    return 0
                point = point_pointer._obj
                point.x += 104
                point.y += 73
                return 1

        client_rect_failure_user32 = ClientRectUser32(get_client_rect_result=False)
        client_rect_failure_capture = object.__new__(harness.WindowsCapture)
        client_rect_failure_capture.user32 = client_rect_failure_user32
        assert client_rect_failure_capture._client_rect(17) is None
        assert client_rect_failure_user32.get_client_rect_calls == 1
        assert client_rect_failure_user32.client_to_screen_calls == 0

        client_origin_failure_user32 = ClientRectUser32(client_to_screen_result=False)
        client_origin_failure_capture = object.__new__(harness.WindowsCapture)
        client_origin_failure_capture.user32 = client_origin_failure_user32
        assert client_origin_failure_capture._client_rect(17) is None
        assert client_origin_failure_user32.get_client_rect_calls == 1
        assert client_origin_failure_user32.client_to_screen_calls == 1

        cursor_capture = object.__new__(harness.WindowsCapture)
        cursor_events = []
        cursor_capture.prepare_raw_client_window = mock.Mock(side_effect=lambda hwnd: cursor_events.append((LIT_PREPARE, hwnd)))
        cursor_capture._client_rect = mock.Mock(side_effect=lambda hwnd: cursor_events.append((LIT_SCREEN_CLIENT, hwnd)) or harness.WindowsCapture.RECT(-1281, 73, -1, 974))
        cursor_capture.user32 = SimpleNamespace(SetCursorPos=mock.Mock(side_effect=lambda x, y: cursor_events.append((LIT_CURSOR, x, y)) or 1))
        harness.prepare_m4_client_area(cursor_capture, 17)
        assert cursor_events == [(LIT_PREPARE, 17), (LIT_SCREEN_CLIENT, 17), (LIT_CURSOR, -641, 523)]

        for client_rect in (None, harness.WindowsCapture.RECT(104, 73, 104, 973), harness.WindowsCapture.RECT(104, 73, 103, 973),
                            harness.WindowsCapture.RECT(104, 73, 1384, 73), harness.WindowsCapture.RECT(104, 73, 1384, 72)):
            cursor_capture._client_rect = mock.Mock(return_value=client_rect)
            cursor_capture.user32.SetCursorPos = mock.Mock(return_value=1)
            try:
                harness.prepare_m4_client_area(cursor_capture, 17)
            except harness.SmokeFailure as error:
                assert "positive screen client rectangle" in str(error)
            else:
                raise AssertionError("M4 cursor preparation accepted missing or nonpositive client geometry")
            cursor_capture.user32.SetCursorPos.assert_not_called()

        cursor_capture._client_rect = mock.Mock(return_value=harness.WindowsCapture.RECT(104, 73, 1384, 973))
        cursor_capture.user32.SetCursorPos = mock.Mock(return_value=0)
        try:
            harness.prepare_m4_client_area(cursor_capture, 17)
        except harness.SmokeFailure as error:
            assert "SetCursorPos failed" in str(error)
        else:
            raise AssertionError("M4 cursor preparation accepted a failed SetCursorPos call")
        cursor_capture.user32.SetCursorPos.assert_called_once_with(744, 523)

    @staticmethod
    def check_compositor(harness):
        class DwmApi:
            def __init__(self, calls, set_result=0, flush_result=0):
                self.calls = calls
                self.set_result = set_result
                self.flush_result = flush_result

            def DwmSetWindowAttribute(self, hwnd, attribute, preference_pointer, preference_size):
                self.calls.append((LIT_DWMSETWINDOWATTRIBUTE, hwnd.value, attribute, preference_pointer._obj.value, preference_size))
                return self.set_result

            def DwmFlush(self):
                self.calls.append((LIT_DWMFLUSH,))
                return self.flush_result

        unsupported_calls = []
        unsupported_capture = object.__new__(harness.WindowsCapture)
        unsupported_capture.dwmapi = DwmApi(unsupported_calls, -2147024809)
        unsupported_capture._prepare_capture_window = lambda hwnd: unsupported_calls.append((LIT_PREPARE, hwnd))
        try:
            unsupported_capture.prepare_raw_client_window(17)
        except harness.SmokeSkip as error:
            assert "Windows 11 build 22000 or later" in str(error)
            assert "0x80070057" in str(error)
        else:
            raise AssertionError("M4 DWM setup accepted an unsupported corner-preference attribute")
        assert unsupported_calls == [(LIT_PREPARE, 17), (LIT_DWMSETWINDOWATTRIBUTE, 17, 33, 1, 4),]

        negative_failure_calls = []
        negative_failure_capture = object.__new__(harness.WindowsCapture)
        negative_failure_capture.dwmapi = DwmApi(negative_failure_calls, -1)
        negative_failure_capture._prepare_capture_window = lambda hwnd: negative_failure_calls.append((LIT_PREPARE, hwnd))
        try:
            negative_failure_capture.prepare_raw_client_window(17)
        except harness.SmokeFailure as error:
            assert LIT_DWMSETWINDOWATTRIBUTE in str(error)
            assert "HRESULT 0xFFFFFFFF" in str(error)
        else:
            raise AssertionError("M4 DWM setup accepted a negative DwmSetWindowAttribute failure")
        assert len(negative_failure_calls) == 2

        for set_result, flush_result, expected_operation, expected_calls in ((1, 0, LIT_DWMSETWINDOWATTRIBUTE, 2), (0, 1, LIT_DWMFLUSH, 3),):
            failure_calls = []
            failure_capture = object.__new__(harness.WindowsCapture)
            failure_capture.dwmapi = DwmApi(failure_calls, set_result, flush_result)
            failure_capture._prepare_capture_window = lambda hwnd: failure_calls.append((LIT_PREPARE, hwnd))
            try:
                failure_capture.prepare_raw_client_window(17)
            except harness.SmokeFailure as error:
                assert expected_operation in str(error)
                assert "HRESULT 0x00000001" in str(error)
            else:
                raise AssertionError(f"M4 DWM setup accepted non-S_OK {expected_operation} result")
            assert len(failure_calls) == expected_calls
