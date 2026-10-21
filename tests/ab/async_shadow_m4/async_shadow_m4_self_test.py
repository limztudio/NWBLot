"""Offline self-test for the M4 async-shadow A/B harness: CLI validation, lane parsing, pixel diffing, report gates, and orchestration with faked processes."""


from __future__ import annotations


import math
import struct
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


LIT_SELF_TEST_TIMEOUT_SECONDS = 1.0
LIT_SELF_TEST_MISSING_BOUNDARY_TIMEOUT_SECONDS = 0.1
LIT_B_BM = b"BM"
LIT_SYNC_EXECUTABLE = "--sync-executable"
LIT_ASYNC_EXECUTABLE = "--async-executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_NON_FINITE_BENCHMARK_OPTION = "non-finite benchmark option"
LIT_SCREEN_CLIENT = "screen-client"
LIT_CURSOR = "cursor"
LIT_PARTIAL_HEADER = "partial_header"
LIT_PARTIAL_SCOPE = "partial_scope"
LIT_MISSING_FILE = "missing_file"
LIT_MONOTONIC = "monotonic"
LIT_SLEEP = "sleep"
LIT_RENDER_FRAME = "render.frame"
LIT_RENDER_ASYNC_SHADOW = "render.async_shadow"
LIT_RENDER_ASYNC_FINAL = "render.async_final"
LIT_CANNOT_SAFELY_CONTINUE_AFTER_AN_UNRESO = "cannot safely continue after an unresolved frame recovery submission"
LIT_I = "<I"
LIT_UTF_8 = "utf-8"
LIT_SYNC = "sync"
LIT_ASYNC = "async"
LIT_BENCHMARK_LOGSERVER = "benchmark logserver"
LIT_PASS = "pass"
LIT_FAIL = "fail"
LIT_INCONCLUSIVE = "inconclusive"
LIT_VERDICT = "verdict"
LIT_SELF_TEST = "--self-test"
LIT_WARMUP_SECONDS = "--warmup-seconds"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_PREPARE = "prepare"
LIT_DWMSETWINDOWATTRIBUTE = "DwmSetWindowAttribute"
LIT_DWMFLUSH = "DwmFlush"
LIT_APP_STOP = "app-stop"
LIT_CLEANUP_NONE = "cleanup-none"
LIT_LOGSERVER_LOG = "logserver_*.log"
LIT_LOGSERVER_HELPER = "logserver-helper"
LIT_EXIT_7 = "exit 7"
LIT_SYNC_FRAME_LOCKED_CAPTURE = "sync frame-locked capture"
LIT_SYNC_BENCHMARK = "sync benchmark"
LIT_ASYNC_LOG = "async.log"
LIT_SYNC_LOG = "sync.log"


def run_self_test(harness) -> int:
    numeric_parser = mock.Mock()
    harness.require_positive(numeric_parser, LIT_STARTUP_TIMEOUT, 1.0)
    harness.require_non_negative(numeric_parser, LIT_WARMUP_SECONDS, 0.0)
    numeric_parser.error.assert_not_called()
    for validator in (harness.require_positive, harness.require_non_negative):
        for value in (math.nan, math.inf, -math.inf):
            numeric_parser.error.reset_mock()
            numeric_parser.error.side_effect = ValueError(LIT_NON_FINITE_BENCHMARK_OPTION)
            try:
                validator(numeric_parser, LIT_STARTUP_TIMEOUT, value)
            except ValueError as error:
                assert str(error) == LIT_NON_FINITE_BENCHMARK_OPTION
            else:
                raise AssertionError("a non-finite benchmark deadline or tolerance was accepted")
            numeric_parser.error.assert_called_once()

    yaw_arguments = [LIT_SYNC_EXECUTABLE, LIT_SYNC, LIT_ASYNC_EXECUTABLE, LIT_ASYNC, LIT_RUNTIME_DIR, "runtime", LIT_OUTPUT_DIR, "output"]
    for value in (
        "nan", "inf", "-inf", "not-a-number", "1e100", "1e300", "-1e100", "1_0", "+0.6", " 0.6", "0.6 ", "3.4028236e38", "1e-46", "1e-999",
        "340282356779733661637539395458142568448",
        "7.00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015625E-46",
    ):
        with mock.patch("sys.stderr"):
            try:
                harness.parse_args(yaw_arguments + [f"--frozen-yaw={value}"])
            except SystemExit as error:
                assert error.code == 2
            else:
                raise AssertionError(f"M4 CLI accepted malformed frozen yaw {value}")
    for value in (
        "0", "-1", "0.6000", "-0.0", ".6", "1.", "6e-1", "6E+0", "3.4028235e38", "1e-45", "0e-999", "-0e999",
        "340282356779733661637539395458142568447",
        "7.00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015626E-46",
        "0e9999999999999999999999999999999", "-0e-9999999999999999999999999999999",
    ):
        assert harness.parse_args(yaw_arguments + [f"--frozen-yaw={value}"]).frozen_yaw == value

    capture_args = harness.parse_args([LIT_SELF_TEST])

    lane_log = ("StressTestSmokeProject: M4 async compute requested=1 effective=1 graphicsQueue=0:17 computeQueue=1:17 " "computeDedicated=1 computeSupported=1 computeGraphics=0")
    lane = harness.parse_lane_status(lane_log)
    assert lane is not None
    harness.validate_lane_for_mode(LIT_ASYNC, lane)
    for invalid_log in (lane_log.replace("graphicsQueue=0:17", "graphicsQueue=65535:17"), lane_log.replace("computeQueue=1:17", "computeQueue=1:18"), lane_log.replace("computeQueue=1:17", "computeQueue=1:0"),
        lane_log.replace("computeQueue=1:17", "computeQueue=65535:0"), lane_log.replace("computeQueue=1:17", "computeQueue=0:17"), lane_log.replace("computeDedicated=1", "computeDedicated=0"), lane_log.replace("computeSupported=1", "computeSupported=0"),
        lane_log.replace("computeGraphics=0", "computeGraphics=1"), lane_log.replace("effective=1", "effective=0"), lane_log.replace("requested=1 effective=1", "requested=0 effective=0"),):
        invalid_lane = harness.parse_lane_status(invalid_log)
        assert invalid_lane is not None
        try:
            harness.validate_lane_for_mode(LIT_ASYNC, invalid_lane)
        except harness.DedicatedComputeUnavailable:
            raise AssertionError("M4 contradictory queue evidence was classified as a hardware skip")
        except harness.SmokeFailure:
            pass
        else:
            raise AssertionError("M4 accepted invalid or contradictory selected queue evidence")
    absent_compute_log = ("StressTestSmokeProject: M4 async compute requested=1 effective=0 graphicsQueue=0:17 computeQueue=65535:0 " "computeDedicated=0 computeSupported=0 computeGraphics=0")
    shared_compute_log = ("StressTestSmokeProject: M4 async compute requested=1 effective=0 graphicsQueue=0:17 computeQueue=0:17 " "computeDedicated=0 computeSupported=1 computeGraphics=1")
    for unavailable_log in (absent_compute_log, shared_compute_log):
        unavailable_lane = harness.parse_lane_status(unavailable_log)
        assert unavailable_lane is not None
        try:
            harness.validate_lane_for_mode(LIT_ASYNC, unavailable_lane)
        except harness.DedicatedComputeUnavailable:
            pass
        else:
            raise AssertionError("M4 unavailable dedicated compute transport was not a capability skip")
    disabled_lane = harness.parse_lane_status(lane_log.replace("requested=1 effective=1", "requested=0 effective=0"))
    assert disabled_lane is not None
    harness.validate_lane_for_mode(LIT_SYNC, disabled_lane)
    for malformed_log in (lane_log.replace("computeGraphics=0", "computeGraphics="), lane_log.replace("computeGraphics=0", "computeGraphics=2"), lane_log.replace("graphicsQueue=0:17", "graphicsQueue=0"),
        lane_log.replace("StressTestSmokeProject:", "OtherProject:"),):
        assert harness.parse_lane_status(malformed_log) is None

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

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        capture_path = root / "header-offset.bmp"
        for dib_size in (40, 56):
            pixel_offset = 14 + dib_size
            bmp = bytearray(pixel_offset + 4)
            bmp[:2] = LIT_B_BM
            struct.pack_into(LIT_I, bmp, 10, pixel_offset)
            struct.pack_into("<IiiHHI", bmp, 14, dib_size, 1, 1, 1, 24, 0)
            bmp[pixel_offset:] = bytes((3, 2, 1, 0))
            capture_path.write_bytes(bmp)
            assert harness.read_bmp_rgb(capture_path) == (1, 1, bytes((1, 2, 3)))
            for invalid_offset in (0, 14, pixel_offset - 1):
                struct.pack_into(LIT_I, bmp, 10, invalid_offset)
                capture_path.write_bytes(bmp)
                try:
                    harness.read_bmp_rgb(capture_path)
                except harness.SmokeFailure as error:
                    assert "overlaps its headers" in str(error)
                else:
                    raise AssertionError("BMP pixel admission accepted bytes inside a header")

        module = harness
        executable = root / "orchestration.exe"
        executable.write_bytes(b"exe")
        capture_args.output_dir = root
        capture_args.runtime_dir = root
        app = SimpleNamespace(pid=4321, poll=lambda: None)
        logserver = object()
        baseline = {root / "old.log": 7}
        backend = mock.Mock()
        backend.wait_for_window.return_value = 17
        backend.capture_client_window.return_value = SimpleNamespace(appears_empty_or_white=False, has_pixel_variation=True,)
        orchestration_lane = harness.LaneStatus(False, False, harness.PhysicalQueueId(0, 17), harness.PhysicalQueueId(1, 17), True, True, False)
        events = []

        terminate = harness.make_abnormal_exit_terminator(app, events)

        def shutdown(process, log_directory, received_baseline, pattern, shutdown_name="logserver"):
            assert process is logserver
            assert events[-1][0] == LIT_APP_STOP
            events.append((LIT_LOGSERVER_HELPER, shutdown_name))
            return "captured runtime evidence"

        with mock.patch.object(module, "build_launch_environment", return_value={}), \
             mock.patch.object(module, "launch_logserver", return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
             mock.patch.object(module, "launch_testbed", return_value=app), \
             mock.patch.object(module, "wait_for_lane_status", return_value=orchestration_lane), \
             mock.patch.object(module, "wait_for_log_message"), \
             mock.patch.object(module, "wait_while_running"), \
             mock.patch.object(module, "wait_for_measurement_timing_boundary", return_value=1), \
             mock.patch.object(module, "terminate_process", side_effect=terminate) as terminate_mock, \
             mock.patch.object(module, "shutdown_logserver_and_collect", side_effect=shutdown):
            try:
                harness.run_frame_locked_capture(capture_args, LIT_SYNC, executable, backend)
            except harness.SmokeFailure as error:
                assert LIT_EXIT_7 in str(error)
            else:
                raise AssertionError("frame-locked orchestration accepted an abnormal Testbed exit")

            try:
                harness.run_single_mode(capture_args, LIT_SYNC, executable, {}, backend, None)
            except harness.SmokeFailure as error:
                assert LIT_EXIT_7 in str(error)
            else:
                raise AssertionError("timed orchestration accepted an abnormal Testbed exit")

        assert events == [(LIT_APP_STOP, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17), (LIT_LOGSERVER_HELPER, LIT_BENCHMARK_LOGSERVER), (LIT_CLEANUP_NONE, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17), (LIT_CLEANUP_NONE, LIT_BENCHMARK_LOGSERVER, None),
            (LIT_APP_STOP, LIT_SYNC_BENCHMARK, 17), (LIT_LOGSERVER_HELPER, LIT_BENCHMARK_LOGSERVER), (LIT_CLEANUP_NONE, LIT_SYNC_BENCHMARK, 17), (LIT_CLEANUP_NONE, LIT_BENCHMARK_LOGSERVER, None),]
        assert terminate_mock.mock_calls == [mock.call(app, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17), mock.call(None, LIT_SYNC_FRAME_LOCKED_CAPTURE, 17), mock.call(None, LIT_BENCHMARK_LOGSERVER), mock.call(app, LIT_SYNC_BENCHMARK, 17),
            mock.call(None, LIT_SYNC_BENCHMARK, 17), mock.call(None, LIT_BENCHMARK_LOGSERVER),]

        # The prefix envelope is diagnostic-only: it is absent when the compiler splits its endpoints into separate submissions.
        # The rollout gate must still accept complete frame/shadow/final timing in that valid topology.
        stable_scope = harness.ScopeSummary(11, 11, 1.0, 1.0, 1.0, 1.0)
        async_run = harness.RunResult(mode=LIT_ASYNC, executable="async.exe", timing_file="async.timing.txt", log_file=LIT_ASYNC_LOG, capture_file=None, lane=harness.LaneStatus(True, True, harness.PhysicalQueueId(0, 17), harness.PhysicalQueueId(1, 17), True, True, False), scopes={
                LIT_RENDER_FRAME: stable_scope, LIT_RENDER_ASYNC_SHADOW: stable_scope, LIT_RENDER_ASYNC_FINAL: stable_scope, }, forbidden_log_messages=[],)
        sync_run = harness.RunResult(mode=LIT_SYNC, executable="sync.exe", timing_file="sync.timing.txt", log_file=LIT_SYNC_LOG, capture_file=None, lane=harness.LaneStatus(False, False, harness.PhysicalQueueId(0, 17), harness.PhysicalQueueId(1, 17), True, True, False),
            scopes={LIT_RENDER_FRAME: stable_scope}, forbidden_log_messages=[],)
        capture_args.skip_pixel_parity = True
        assert harness.evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == LIT_PASS

        # Ten positive shadow intervals cannot qualify timing, but a functional failure still fails.
        async_run.scopes = {**async_run.scopes, LIT_RENDER_ASYNC_SHADOW: harness.ScopeSummary(12, 10, 1.0, 1.0, 0.0, 1.0)}
        assert harness.evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == LIT_INCONCLUSIVE
        async_run.forbidden_log_messages = [LIT_CANNOT_SAFELY_CONTINUE_AFTER_AN_UNRESO]
        assert harness.evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == LIT_FAIL
        async_run.forbidden_log_messages = []

        # Warmup can falsely fail an accepted measurement or mask a real regression; neither prefix may reach the gate.
        sync_run.scopes = {LIT_RENDER_FRAME: harness.ScopeSummary(11, 11, 10.0, 10.0, 10.0, 10.0)}
        timing_path = root / "measurement-boundary.timing.txt"
        timing_symbols = harness.load_name_symbols(None)
        for newline in ("\n", "\r\n"):
            def timing_interval(frame_ms: float) -> bytes:
                lines = ["=== interval: 20 frames / 0.5s ==="]
                for scope, value in ((LIT_RENDER_FRAME, frame_ms), (LIT_RENDER_ASYNC_SHADOW, 1.0), (LIT_RENDER_ASYNC_FINAL, 0.1), ("warmup_한", 0.2)):
                    lines.append(f"  {scope}: window_avg_ms={value} window_min_ms={value} window_max_ms={value} " f"published_windows=20 total_ms={value * 20} gpu_samples=40 sample_avg_ms={value / 2}")
                return (newline.join(lines) + newline).encode(LIT_UTF_8)

            for warmup_ms, measured_ms, expected_verdict in ((500.0, 10.0, LIT_PASS), (1.0, 12.0, LIT_FAIL)):
                warmup_interval = timing_interval(warmup_ms)
                measured_interval = timing_interval(measured_ms)
                header = measured_interval.splitlines(keepends=True)[0]
                for cutoff_kind in ("completed_report", LIT_PARTIAL_HEADER, LIT_PARTIAL_SCOPE, LIT_MISSING_FILE):
                    full_prefix = warmup_interval * 16
                    if cutoff_kind == LIT_PARTIAL_HEADER:
                        initial_prefix = full_prefix + warmup_interval[:9]
                        remainder = warmup_interval[9:]
                        full_prefix += warmup_interval
                    elif cutoff_kind == LIT_PARTIAL_SCOPE:
                        cut = warmup_interval.index(b"window_avg_ms=") + 6
                        initial_prefix = full_prefix + warmup_interval[:cut]
                        remainder = warmup_interval[cut:]
                        full_prefix += warmup_interval
                    elif cutoff_kind == LIT_MISSING_FILE:
                        full_prefix = b""
                        initial_prefix = b""
                        remainder = b""
                    else:
                        initial_prefix = full_prefix
                        remainder = b""
                    if timing_path.exists():
                        timing_path.unlink()
                    if cutoff_kind != LIT_MISSING_FILE:
                        timing_path.write_bytes(initial_prefix)
                    chunks = [remainder, warmup_interval, header.rstrip(b"\r\n"), newline.encode(LIT_UTF_8) + measured_interval[len(header):] + measured_interval * 10]
                    clock = [0.0]
                    writes = []

                    def append_chunk(seconds: float) -> None:
                        clock[0] += seconds
                        chunk = chunks.pop(0)
                        with timing_path.open("ab") as timing_file:
                            timing_file.write(chunk)
                        writes.append(chunk)

                    with mock.patch.object(module.time, LIT_MONOTONIC, side_effect=lambda: clock[0]), \
                         mock.patch.object(module.time, LIT_SLEEP, side_effect=append_chunk):
                        offset = harness.wait_for_measurement_timing_boundary(app, timing_path, LIT_SELF_TEST_TIMEOUT_SECONDS)
                    assert len(writes) == 4, "an incomplete or first post-warmup header was admitted"
                    assert offset == len(full_prefix) + len(warmup_interval)
                    measured = harness.summarize_scopes(harness.parse_timing_file(timing_path, timing_symbols, offset))
                    assert measured[LIT_RENDER_FRAME].sample_count == 11
                    assert measured[LIT_RENDER_FRAME].median_ms == measured_ms
                    async_run.scopes = measured
                    assert harness.evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == expected_verdict
                    if cutoff_kind != LIT_MISSING_FILE:
                        async_run.scopes = harness.summarize_scopes(harness.parse_timing_file(timing_path, timing_symbols))
                        assert harness.evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] != expected_verdict

        # Missing publications and truncation fail admission instead of falling back to warmup or partial rows.
        timing_path.write_bytes(b"warmup prefix")
        clock = [0.0]
        with mock.patch.object(module.time, LIT_MONOTONIC, side_effect=lambda: clock[0]), \
             mock.patch.object(module.time, LIT_SLEEP, side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
            try:
                harness.wait_for_measurement_timing_boundary(app, timing_path, LIT_SELF_TEST_MISSING_BOUNDARY_TIMEOUT_SECONDS)
            except harness.SmokeFailure as error:
                assert "did not publish" in str(error)
            else:
                raise AssertionError("missing post-warmup boundaries admitted the prefix")
        with mock.patch.object(module.time, LIT_SLEEP, side_effect=lambda seconds: timing_path.write_bytes(b"")):
            try:
                harness.wait_for_measurement_timing_boundary(app, timing_path, LIT_SELF_TEST_TIMEOUT_SECONDS)
            except harness.SmokeFailure as error:
                assert "was truncated" in str(error)
            else:
                raise AssertionError("truncated timing input admitted a measurement")

    print("async-shadow M4 harness self-test passed")
    return 0
