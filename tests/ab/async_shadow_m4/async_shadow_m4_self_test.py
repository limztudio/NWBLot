"""Offline self-test for the M4 async-shadow A/B harness: CLI validation, lane parsing, pixel diffing, report gates, and orchestration with faked processes."""


from __future__ import annotations


import struct
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from harness_test_support import make_abnormal_exit_terminator
from async_shadow_m4_admission_checks import M4AdmissionChecks, LIT_ASYNC, LIT_SYNC
from async_shadow_m4_windows_checks import M4WindowsCaptureChecks


LIT_SELF_TEST_TIMEOUT_SECONDS = 1.0
LIT_SELF_TEST_MISSING_BOUNDARY_TIMEOUT_SECONDS = 0.1
LIT_B_BM = b"BM"
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
LIT_BENCHMARK_LOGSERVER = "benchmark logserver"
LIT_PASS = "pass"
LIT_FAIL = "fail"
LIT_INCONCLUSIVE = "inconclusive"
LIT_VERDICT = "verdict"
LIT_SELF_TEST = "--self-test"
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
    M4AdmissionChecks.check_cli(harness)
    capture_args = harness.parse_args([LIT_SELF_TEST])

    M4AdmissionChecks.check_lane(harness)

    M4WindowsCaptureChecks.check_client_geometry(harness)
    M4WindowsCaptureChecks.check_compositor(harness)

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
            image = harness.BitmapImage(capture_path)
            assert (image.width, image.height, image.rgb) == (1, 1, bytes((1, 2, 3)))
            for invalid_offset in (0, 14, pixel_offset - 1):
                struct.pack_into(LIT_I, bmp, 10, invalid_offset)
                capture_path.write_bytes(bmp)
                try:
                    harness.BitmapImage(capture_path)
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

        terminate = make_abnormal_exit_terminator(app, events)

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
             mock.patch.object(module.CaptureProcessTermination, "terminate_process", side_effect=terminate) as terminate_mock, \
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

        # The prefix envelope is absent when its endpoints are in separate submissions.
        stable_scope = harness.ScopeSummary(11, 11, 1.0, 1.0, 1.0, 1.0)
        async_run = harness.RunResult(mode=LIT_ASYNC, executable="async.exe", timing_file="async.timing.txt", log_file=LIT_ASYNC_LOG, capture_file=None, lane=harness.LaneStatus(True, True, harness.PhysicalQueueId(0, 17), harness.PhysicalQueueId(1, 17), True, True, False), scopes={
                LIT_RENDER_FRAME: stable_scope, LIT_RENDER_ASYNC_SHADOW: stable_scope, LIT_RENDER_ASYNC_FINAL: stable_scope, }, forbidden_log_messages=[],)
        sync_run = harness.RunResult(mode=LIT_SYNC, executable="sync.exe", timing_file="sync.timing.txt", log_file=LIT_SYNC_LOG, capture_file=None, lane=harness.LaneStatus(False, False, harness.PhysicalQueueId(0, 17), harness.PhysicalQueueId(1, 17), True, True, False),
            scopes={LIT_RENDER_FRAME: stable_scope}, forbidden_log_messages=[],)
        capture_args.skip_pixel_parity = True
        assert harness.AsyncShadowM4Report().evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == LIT_PASS

        bitmap = bytearray(58)
        bitmap[:2] = LIT_B_BM
        struct.pack_into(LIT_I, bitmap, 10, 54)
        struct.pack_into("<IiiHHI", bitmap, 14, 40, 1, 1, 1, 24, 0)
        sync_capture = root / "sync-parity.bmp"
        async_capture = root / "async-parity.bmp"
        sync_capture.write_bytes(bitmap)
        bitmap[56] = 255
        async_capture.write_bytes(bitmap)
        sync_run.capture_file = str(sync_capture)
        async_run.capture_file = str(async_capture)
        capture_args.skip_pixel_parity = False
        parity_report = harness.AsyncShadowM4Report().evaluate_runs(capture_args, sync_run, async_run)
        assert parity_report[LIT_VERDICT] == LIT_FAIL
        assert parity_report["pixel_diff"]["max_abs"] == 255
        assert parity_report["pixel_diff"]["changed_fraction"] == 1.0
        capture_args.skip_pixel_parity = True

        # Ten positive shadow intervals cannot qualify timing, but a functional failure still fails.
        async_run.scopes = {**async_run.scopes, LIT_RENDER_ASYNC_SHADOW: harness.ScopeSummary(12, 10, 1.0, 1.0, 0.0, 1.0)}
        assert harness.AsyncShadowM4Report().evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == LIT_INCONCLUSIVE
        async_run.forbidden_log_messages = [LIT_CANNOT_SAFELY_CONTINUE_AFTER_AN_UNRESO]
        assert harness.AsyncShadowM4Report().evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == LIT_FAIL
        async_run.forbidden_log_messages = []

        # Exclude warmup intervals from the gate.
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
                    assert harness.AsyncShadowM4Report().evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] == expected_verdict
                    if cutoff_kind != LIT_MISSING_FILE:
                        async_run.scopes = harness.summarize_scopes(harness.parse_timing_file(timing_path, timing_symbols))
                        assert harness.AsyncShadowM4Report().evaluate_runs(capture_args, sync_run, async_run)[LIT_VERDICT] != expected_verdict

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
