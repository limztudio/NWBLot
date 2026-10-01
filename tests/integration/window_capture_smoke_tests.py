#!/usr/bin/env python3
import ctypes
import math
import os
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))

import window_capture_smoke  # noqa: E402
import runtime_log_smoke  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    ensure_process_running,
    launch_captured_process,
    read_process_tail,
    require_normal_process_exit,
    shutdown_logserver_and_collect,
    terminate_process,
    validate_expected_log_messages,
)

# Shared literals (no inline hardcodes below this block).
LIT_UNIT = "unit"
LIT_CAPTURE_RESULT_FROM_RGB_ROWS = "capture_result_from_rgb_rows"
LIT_CAPTURE = "capture"
LIT_I = "<I"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_PRESERVED = "PRESERVED"
LIT_YES = "yes"
LIT_APPLICATION_CAPTURE = "--application-capture"
LIT_EXECUTABLE = "--executable"
LIT_OUTPUT = "--output"
LIT_LAUNCH_AND_CAPTURE_APPLICATION = "launch_and_capture_application"
LIT_CREATE_CAPTURE_BACKEND = "create_capture_backend"
LIT_WRITE_STATUS = "write_status"
LIT_EARLY = "early"
LIT_MID = "mid"
LIT_CUT_VOID_0 = "cut_void=0"
LIT_REMAINING_CENTER_0 = "remaining_center=0"
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
LIT_LOGSERVER = "logserver"
LIT_ORDINARY_STARTUP = "ordinary startup"
LIT_SHUTDOWN = "shutdown"
LIT_DRAIN = "drain"
LIT_COLLECT = "collect"
LIT_WARNING_SHUTDOWN_ONLY_WARNING = "[WARNING] shutdown-only warning"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_WAIT_FOR_LOG_DRAIN = "wait_for_log_drain"
LIT_COLLECT_LOG_DELTA = "collect_log_delta"
LIT_LOGS = "logs"
LIT_SHUTDOWN_MARKER = "shutdown marker"
LIT_WARNING = "[WARNING]"
LIT_RUNTIME_EXIT = "runtime exit"
LIT_LOGSERVER_SHUTDOWN_AND_COLLECT = "logserver shutdown and collect"
LIT_VALIDATE = "validate"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_OFF = "off"
LIT_TESTBED_SHUTDOWN = "testbed shutdown"
LIT_LOGSERVER_SHUTDOWN = "logserver shutdown"
LIT_LOG_DRAIN = "log drain"
LIT_CAPTURE_RENDER_READY_WINDOW = "capture_render_ready_window"
LIT_PROJECTTESTBED_SHUTDOWN = "ProjectTestbed: shutdown"
LIT_MONOTONIC = "monotonic"
LIT_SLEEP = "sleep"
LIT_LOGSERVER_LAUNCH = "logserver launch"
LIT_APPLICATION_LAUNCH = "application launch"
LIT_APPLICATION_WAIT = "application wait"
LIT_TESTBED = "testbed"
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
LIT_RAISE = "raise"
LIT_FOCUS = "focus"
LIT_FLUSH = "flush"
LIT_SYSTEM = "system"
LIT_LINUX = "Linux"
LIT_REQUEST_LINUX_GRACEFUL_EXIT = "request_linux_graceful_exit"
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


class _FakeProcess:
    def __init__(self, graceful_exit=False, graceful_exit_code=0):
        self.pid = 4321
        self._alive = True
        self._graceful_exit = graceful_exit
        self._graceful_exit_code = graceful_exit_code
        self.terminate_calls = 0
        self.kill_calls = 0
        self.wait_timeouts = []

    def poll(self):
        return None if self._alive else self._graceful_exit_code

    def wait(self, timeout):
        self.wait_timeouts.append(timeout)
        if self._alive and self._graceful_exit:
            self._alive = False
        if self._alive:
            raise subprocess.TimeoutExpired("fake", timeout)
        return self._graceful_exit_code

    def terminate(self):
        self.terminate_calls += 1
        self._alive = False

    def kill(self):
        self.kill_calls += 1
        self._alive = False


class ProcessOutputCaptureTests(unittest.TestCase):
    def test_large_output_does_not_block_and_tail_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            working_directory = Path(temp_dir)
            script = (
                "import sys\n"
                "sys.stdout.buffer.write(b'x' * (512 * 1024))\n"
                "sys.stdout.flush()\n"
                "sys.stderr.write('\\nwindow-capture-output-sentinel\\n')\n"
                "sys.stderr.flush()\n"
            )
            process = launch_captured_process(
                [sys.executable, "-c", script],
                working_directory,
                os.environ.copy(),
                LIT_UNIT,
            )
            capture = process._nwb_output_capture
            try:
                self.assertIsNone(process.stdout)
                self.assertEqual(process.wait(timeout=5.0), 0)
                self.assertTrue(capture.path.exists())
                self.assertIn("window-capture-output-sentinel", read_process_tail(process))
            finally:
                terminate_process(process, LIT_UNIT)

            self.assertFalse(capture.path.exists())


class BmpReadbackTests(unittest.TestCase):
    @staticmethod
    def write_top_down_bmp(path, rows):
        height = len(rows)
        width = len(rows[0])
        row_stride = ((width * 3 + 3) // 4) * 4
        image_size = row_stride * height
        pixel_offset = 54
        padding = b"\0" * (row_stride - width * 3)
        with path.open("wb") as output:
            output.write(struct.pack("<2sIHHI", b"BM", pixel_offset + image_size, 0, 0, pixel_offset))
            output.write(struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 24, 0, image_size, 0, 0, 0, 0))
            for row in rows:
                for red, green, blue in row:
                    output.write(bytes((blue, green, red)))
                output.write(padding)

    def test_bottom_up_bmp_honors_bgr_channels_padding_and_row_order(self):
        rows = [
            [(1, 2, 3), (4, 5, 6), (7, 8, 9)],
            [(10, 11, 12), (13, 14, 15), (16, 17, 18)],
        ]
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "bottom_up.bmp"
            window_capture_smoke.write_bmp_24(path, 3, 2, rows)
            with mock.patch.object(
                window_capture_smoke,
                LIT_CAPTURE_RESULT_FROM_RGB_ROWS,
                return_value=LIT_CAPTURE,
            ) as analyze:
                self.assertEqual(window_capture_smoke.read_bmp_24(path), LIT_CAPTURE)

        analyze.assert_called_once_with(0, 3, 2, rows)

    def test_top_down_bmp_preserves_stored_row_order(self):
        rows = [
            [(21, 22, 23), (24, 25, 26)],
            [(31, 32, 33), (34, 35, 36)],
        ]
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "top_down.bmp"
            self.write_top_down_bmp(path, rows)
            with mock.patch.object(
                window_capture_smoke,
                LIT_CAPTURE_RESULT_FROM_RGB_ROWS,
                return_value=LIT_CAPTURE,
            ) as analyze:
                self.assertEqual(window_capture_smoke.read_bmp_24(path), LIT_CAPTURE)

        analyze.assert_called_once_with(0, 2, 2, rows)

    def test_bmp_parser_rejects_declared_size_and_compression_mismatches(self):
        rows = [[(1, 2, 3), (4, 5, 6)]]
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "invalid.bmp"
            window_capture_smoke.write_bmp_24(path, 2, 1, rows)
            data = bytearray(path.read_bytes())

            struct.pack_into(LIT_I, data, 2, len(data) + 1)
            path.write_bytes(data)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "declares .* but contains"):
                window_capture_smoke.read_bmp_24(path)

            struct.pack_into(LIT_I, data, 2, len(data))
            struct.pack_into(LIT_I, data, 30, 1)
            path.write_bytes(data)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "must be uncompressed 24-bit"):
                window_capture_smoke.read_bmp_24(path)


class ApplicationCaptureConfigurationTests(unittest.TestCase):
    def test_environment_removes_stale_final_and_partial_artifacts(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / LIT_CAPTURE_BMP
            partial = window_capture_smoke.application_capture_partial_path(output)
            output.write_bytes(b"stale-final")
            partial.write_bytes(b"stale-partial")
            args = SimpleNamespace(output=output, application_capture_frame_count=27)
            base_environment = {LIT_PRESERVED: LIT_YES}

            env = window_capture_smoke.prepare_application_capture_environment(args, base_environment)

            self.assertFalse(output.exists())
            self.assertFalse(partial.exists())
            self.assertEqual(env[window_capture_smoke.FRAMEBUFFER_CAPTURE_PATH_ENV], str(output))
            self.assertEqual(env[window_capture_smoke.FRAMEBUFFER_CAPTURE_FRAME_COUNT_ENV], "27")
            self.assertEqual(env[LIT_PRESERVED], LIT_YES)
            self.assertEqual(base_environment, {LIT_PRESERVED: LIT_YES})

    def test_application_capture_frame_count_defaults_to_360_and_must_be_positive(self):
        args = window_capture_smoke.parse_args(
            [LIT_APPLICATION_CAPTURE, LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP]
        )
        self.assertEqual(args.application_capture_frame_count, 360)

        with self.assertRaises(SystemExit):
            window_capture_smoke.parse_args(
                [
                    LIT_APPLICATION_CAPTURE,
                    "--application-capture-frame-count",
                    "0",
                    LIT_EXECUTABLE,
                    sys.executable,
                    LIT_OUTPUT,
                    LIT_CAPTURE_BMP,
                ]
            )

    def test_application_capture_main_never_creates_a_desktop_capture_backend(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / LIT_CAPTURE_BMP
            capture = SimpleNamespace(width=3, height=2)
            with mock.patch.object(window_capture_smoke, LIT_LAUNCH_AND_CAPTURE_APPLICATION, return_value=capture), \
                 mock.patch.object(window_capture_smoke, LIT_CREATE_CAPTURE_BACKEND) as create_backend, \
                 mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS):
                exit_code = window_capture_smoke.main(
                    [LIT_APPLICATION_CAPTURE, LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, str(output)]
                )

        self.assertEqual(exit_code, 0)
        create_backend.assert_not_called()

    def test_application_capture_skip_reaches_process_exit_code_77_without_desktop_capture(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / LIT_CAPTURE_BMP
            with mock.patch.object(
                window_capture_smoke,
                LIT_LAUNCH_AND_CAPTURE_APPLICATION,
                side_effect=window_capture_smoke.SmokeSkip(window_capture_smoke.FRAMEBUFFER_CAPTURE_SKIP_MESSAGE),
            ), mock.patch.object(window_capture_smoke, LIT_CREATE_CAPTURE_BACKEND) as create_backend, \
                 mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS):
                exit_code = window_capture_smoke.main(
                    [LIT_APPLICATION_CAPTURE, LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, str(output)]
                )

        self.assertEqual(exit_code, window_capture_smoke.SKIP_EXIT_CODE)
        create_backend.assert_not_called()


class TextureSmokeAnalysisTests(unittest.TestCase):
    def test_texture_smoke_analysis_requires_each_primary_hue(self):
        rows = [
            [(240, 40, 40), (40, 230, 50), (45, 70, 238)],
            [(230, 52, 52), (52, 220, 60), (50, 85, 228)],
        ]
        analysis = window_capture_smoke.analyze_texture_smoke_rows(rows)

        self.assertEqual(analysis.red_pixels, 2)
        self.assertEqual(analysis.green_pixels, 2)
        self.assertEqual(analysis.blue_pixels, 2)

    def test_texture_smoke_analysis_isolates_red_bounce_on_the_white_receiver(self):
        rows = [[(210, 210, 210) for _ in range(200)] for _ in range(200)]
        for y in range(40, 45):
            for x in range(97, 100):
                rows[y][x] = (104, 70, 68)
        for y in range(126, 131):
            for x in range(97, 100):
                rows[y][x] = (104, 70, 68)

        analysis = window_capture_smoke.analyze_texture_smoke_rows(rows)

        self.assertEqual(analysis.red_pixels, 2 * 3 * 5)
        self.assertEqual(analysis.receiver_pixel_count, 5 * 7)
        self.assertEqual(analysis.receiver_red_pixels, 3 * 5)

    def test_texture_smoke_validation_requires_dense_receiver_only_red_coverage(self):
        result = SimpleNamespace(
            width=1280,
            height=900,
            texture_smoke=window_capture_smoke.TextureSmokeAnalysis(300, 300, 300, 256, 1024),
        )

        window_capture_smoke.validate_texture_smoke_result(result)

        result.texture_smoke = window_capture_smoke.TextureSmokeAnalysis(300, 300, 300, 255, 1024)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "receiver_red=255"):
            window_capture_smoke.validate_texture_smoke_result(result)


class TransparentCsgAnalysisTests(unittest.TestCase):
    WIDTH = 1280
    HEIGHT = 900
    BACKGROUND = (75, 85, 101)
    RECEIVER = (30, 220, 50)

    @staticmethod
    def cross(origin, left, right):
        return (left[0] - origin[0]) * (right[1] - origin[1]) - (left[1] - origin[1]) * (right[0] - origin[0])

    @classmethod
    def receiver_hull(cls, pose, clipped):
        # Independent fixture projection: authored six-axis octahedron (.6), receiver scale .78, camera distance2.2,
        # vertical FOV60. Euler application is roll, pitch, then the combined local/parent yaw. Clipping local y>0
        # removes only the upper axis vertex; the equator and lower vertex form the retained convex pyramid.
        yaw = {LIT_EARLY: 0.0, LIT_MID: 1.0, "late": 2.0}[pose]
        pitch, roll = .32 * yaw, .16 * yaw
        points = []
        for axis in range(3):
            for sign in (-1, 1):
                if clipped and axis == 1 and sign == 1:
                    continue
                vertex = [0.0, 0.0, 0.0]
                vertex[axis] = sign * .600000024 * .78
                x, y, z = vertex
                x, y = math.cos(roll) * x - math.sin(roll) * y, math.sin(roll) * x + math.cos(roll) * y
                y, z = math.cos(pitch) * y - math.sin(pitch) * z, math.sin(pitch) * y + math.cos(pitch) * z
                x, z = math.cos(2 * yaw) * x + math.sin(2 * yaw) * z, -math.sin(2 * yaw) * x + math.cos(2 * yaw) * z
                points.append((x * math.sqrt(3) / (2 * (z + 2.2)), -y * math.sqrt(3) / (2 * (z + 2.2))))
        points = sorted(set(points))
        lower, upper = [], []
        for point in points:
            while len(lower) > 1 and cls.cross(lower[-2], lower[-1], point) <= 0:
                lower.pop()
            lower.append(point)
        for point in reversed(points):
            while len(upper) > 1 and cls.cross(upper[-2], upper[-1], point) <= 0:
                upper.pop()
            upper.append(point)
        return lower[:-1] + upper[:-1]

    @classmethod
    def signed_margin(cls, hull, point):
        return min(
            cls.cross(left, right, point) / math.hypot(right[0] - left[0], right[1] - left[1])
            for left, right in zip(hull, hull[1:] + hull[:1])
        )

    @classmethod
    def synthetic_scene(cls, pose, clipped=True, receiver=True, receiver_color=None):
        rows = [[cls.BACKGROUND] * cls.WIDTH for _ in range(cls.HEIGHT)]
        hull = cls.receiver_hull(pose, clipped)
        x0 = max(0, math.floor(cls.WIDTH / 2 + min(point[0] for point in hull) * cls.HEIGHT))
        x1 = min(cls.WIDTH, math.ceil(cls.WIDTH / 2 + max(point[0] for point in hull) * cls.HEIGHT))
        y0 = max(0, math.floor(cls.HEIGHT / 2 + min(point[1] for point in hull) * cls.HEIGHT))
        y1 = min(cls.HEIGHT, math.ceil(cls.HEIGHT / 2 + max(point[1] for point in hull) * cls.HEIGHT))
        for y in range(y0, y1):
            for x in range(x0, x1):
                point = ((x + .5 - cls.WIDTH / 2) / cls.HEIGHT, (y + .5 - cls.HEIGHT / 2) / cls.HEIGHT)
                if cls.signed_margin(hull, point) >= 0:
                    # A missing green receiver may expose dense red/blue neighboring meshes. Generic foreground
                    # coverage is deliberately preserved, so only receiver identity can reject this negative control.
                    rows[y][x] = (receiver_color or cls.RECEIVER) if receiver else (
                        (180, 50, 30) if x % 2 == 0 else (40, 90, 180)
                    )
        return rows

    @staticmethod
    def capture_result(pose, analysis):
        return SimpleNamespace(transparent_csg={pose: analysis})

    def test_pose_regions_are_inset_within_projected_receiver_geometry(self):
        for pose, (cut, retained) in window_capture_smoke.TRANSPARENT_CSG_REGIONS.items():
            with self.subTest(pose=pose):
                uncut_hull = self.receiver_hull(pose, False)
                clipped_hull = self.receiver_hull(pose, True)
                for x in (cut[0], cut[2]):
                    for y in (cut[1], cut[3]):
                        self.assertGreater(self.signed_margin(uncut_hull, (x, y)), .002)
                        self.assertLess(self.signed_margin(clipped_hull, (x, y)), -.002)
                for x in (retained[0], retained[2]):
                    for y in (retained[1], retained[3]):
                        self.assertGreater(self.signed_margin(clipped_hull, (x, y)), .002)

    def test_all_three_clipped_poses_pass_their_explicit_oracle(self):
        for pose in window_capture_smoke.TRANSPARENT_CSG_REGIONS:
            with self.subTest(pose=pose):
                rows = self.synthetic_scene(pose)
                analysis = window_capture_smoke.analyze_transparent_csg_rows(rows, pose)
                self.assertEqual(analysis.cut_void_pixels, analysis.cut_region_pixels)
                self.assertEqual(analysis.remaining_center_pixels, analysis.remaining_region_pixels)
                window_capture_smoke.validate_transparent_csg_result(self.capture_result(pose, analysis), pose)

    def test_all_three_missing_cutter_poses_fail_even_with_background_outside_receiver(self):
        for pose in window_capture_smoke.TRANSPARENT_CSG_REGIONS:
            with self.subTest(pose=pose):
                rows = self.synthetic_scene(pose, clipped=False)
                analysis = window_capture_smoke.analyze_transparent_csg_rows(rows, pose)
                self.assertEqual(analysis.cut_void_pixels, 0)
                self.assertEqual(analysis.remaining_center_pixels, analysis.remaining_region_pixels)
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_CUT_VOID_0):
                    window_capture_smoke.validate_transparent_csg_result(self.capture_result(pose, analysis), pose)

    def test_all_three_missing_receiver_poses_reject_dense_non_green_neighbors(self):
        for pose in window_capture_smoke.TRANSPARENT_CSG_REGIONS:
            with self.subTest(pose=pose):
                rows = self.synthetic_scene(pose, receiver=False)
                _, retained = window_capture_smoke.transparent_csg_regions(self.WIDTH, self.HEIGHT, pose)
                generic_foreground = window_capture_smoke.count_foreground_pixels_in_region(rows, self.BACKGROUND, retained)
                analysis = window_capture_smoke.analyze_transparent_csg_rows(rows, pose)
                self.assertEqual(generic_foreground, analysis.remaining_region_pixels)
                self.assertEqual(analysis.cut_void_pixels, analysis.cut_region_pixels)
                self.assertEqual(analysis.remaining_center_pixels, 0)
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_REMAINING_CENTER_0):
                    window_capture_smoke.validate_transparent_csg_result(self.capture_result(pose, analysis), pose)

    def test_green_dominance_does_not_relax_foreground_contrast(self):
        rows = self.synthetic_scene(LIT_EARLY, receiver_color=(82, 102, 101))
        analysis = window_capture_smoke.analyze_transparent_csg_rows(rows, LIT_EARLY)
        self.assertEqual(analysis.remaining_center_pixels, 0)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_REMAINING_CENTER_0):
            window_capture_smoke.validate_transparent_csg_result(self.capture_result(LIT_EARLY, analysis), LIT_EARLY)

    def test_original_density_thresholds_remain_required(self):
        accepted = window_capture_smoke.TransparentCsgAnalysis(200, 600, 200, 1000)
        window_capture_smoke.validate_transparent_csg_result(self.capture_result(LIT_EARLY, accepted), LIT_EARLY)
        for rejected in (
            window_capture_smoke.TransparentCsgAnalysis(199, 600, 200, 1000),
            window_capture_smoke.TransparentCsgAnalysis(200, 600, 199, 1000),
            window_capture_smoke.TransparentCsgAnalysis(159, 300, 160, 500),
            window_capture_smoke.TransparentCsgAnalysis(160, 300, 159, 500),
        ):
            with self.subTest(analysis=rejected), self.assertRaises(window_capture_smoke.SmokeFailure):
                window_capture_smoke.validate_transparent_csg_result(self.capture_result(LIT_EARLY, rejected), LIT_EARLY)

    def test_validation_never_substitutes_a_different_pose(self):
        result = SimpleNamespace(transparent_csg={
            LIT_EARLY: window_capture_smoke.TransparentCsgAnalysis(0, 600, 200, 1000),
            LIT_MID: window_capture_smoke.TransparentCsgAnalysis(200, 600, 200, 1000),
        })
        window_capture_smoke.validate_transparent_csg_result(result, LIT_MID)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "early pose"):
            window_capture_smoke.validate_transparent_csg_result(result, LIT_EARLY)

    def test_csg_cli_requires_one_explicit_pose(self):
        base = [LIT_WINDOW_HANDLE, "1", LIT_OUTPUT, LIT_CAPTURE_BMP, "--expect-transparent-csg"]
        with mock.patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
            window_capture_smoke.parse_args(base)
        for pose in window_capture_smoke.TRANSPARENT_CSG_REGIONS:
            with self.subTest(pose=pose):
                args = window_capture_smoke.parse_args(base + ["--transparent-csg-pose", pose])
                self.assertEqual(args.transparent_csg_pose, pose)


class RuntimeLogValidationTests(unittest.TestCase):
    def test_command_line_defaults_reject_every_strict_runtime_failure(self):
        args = window_capture_smoke.parse_args([LIT_WINDOW_HANDLE, "1", LIT_OUTPUT, LIT_CAPTURE_BMP])

        self.assertEqual(args.reject_log_message, list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES))
        self.assertEqual(args.skip_blocking_log_message, list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES))

    def test_capability_marker_is_classified_before_required_log_validation(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = LIT_BENCHMARK_SKIPPED_BECAUSE_REQUIRED_HAR
            (log_directory / LIT_TEST_LOG).write_text(marker, encoding=LIT_UTF_8)

            self.assertEqual(
                validate_expected_log_messages(
                    log_directory,
                    {},
                    LIT_LOG,
                    [LIT_REQUIRED_ROUTE_MARKER],
                    [],
                    [marker],
                ),
                marker,
            )

    def test_missing_required_marker_is_not_a_capability_skip(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            (log_directory / LIT_TEST_LOG).write_text(LIT_ORDINARY_STARTUP, encoding=LIT_UTF_8)

            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_MISSING_LOG_MESSAGE):
                validate_expected_log_messages(
                    log_directory,
                    {},
                    LIT_LOG,
                    [LIT_REQUIRED_ROUTE_MARKER],
                    [],
                    ["benchmark skipped"],
                )

    def test_capability_marker_preempts_supported_route_rejection(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = LIT_BENCHMARK_SKIPPED_BECAUSE_REQUIRED_HAR
            (log_directory / LIT_TEST_LOG).write_text(
                f"{marker}\nsoftware traversal route",
                encoding=LIT_UTF_8,
            )

            self.assertEqual(
                validate_expected_log_messages(
                    log_directory,
                    {},
                    LIT_LOG,
                    ["required hardware route"],
                    ["software traversal route"],
                    [marker],
                    [LIT_ERROR],
                ),
                marker,
            )

    def test_rejected_error_takes_precedence_over_capability_skip(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            marker = LIT_BENCHMARK_SKIPPED_BECAUSE_REQUIRED_HAR
            (log_directory / LIT_TEST_LOG).write_text(f"{marker}\n[ERROR] device failure", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_BLOCKING_LOG_MESSAGE):
                validate_expected_log_messages(
                    log_directory,
                    {},
                    LIT_LOG,
                    [LIT_REQUIRED_ROUTE_MARKER],
                    [LIT_ERROR],
                    [marker],
                    [LIT_ERROR],
                )

    def test_reject_only_validation_requires_runtime_log_evidence(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "captured no output"):
                validate_expected_log_messages(
                    Path(temp_dir),
                    {},
                    LIT_LOG,
                    [],
                    [LIT_ERROR],
                )


class ProcessLivenessTests(unittest.TestCase):
    def test_named_process_exit_before_validation_fails(self):
        process = SimpleNamespace(poll=lambda: 7, returncode=7)

        with self.assertRaisesRegex(
            window_capture_smoke.SmokeFailure,
            r"logserver exited before log validation \(exit 7\)",
        ):
            ensure_process_running(process, "before log validation", LIT_LOGSERVER)

    def test_named_process_abnormal_graceful_exit_fails(self):
        with self.assertRaisesRegex(
            window_capture_smoke.SmokeFailure,
            r"logserver exited during graceful shutdown \(exit 7\)",
        ):
            require_normal_process_exit(7, "", LIT_LOGSERVER)


class LogserverCollectionTests(unittest.TestCase):
    def test_shutdown_happens_before_drain_and_collection(self):
        process = SimpleNamespace(poll=lambda: None)
        events = []

        def terminate(_process, _name):
            events.append(LIT_SHUTDOWN)
            return 0, ""

        def drain(*_args):
            events.append(LIT_DRAIN)

        def collect(*_args):
            events.append(LIT_COLLECT)
            return LIT_WARNING_SHUTDOWN_ONLY_WARNING

        with mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
             mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN, side_effect=drain), \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, side_effect=collect):
            log_text = shutdown_logserver_and_collect(process, Path(LIT_LOGS), {}, LIT_LOG)

        self.assertEqual(events, [LIT_SHUTDOWN, LIT_DRAIN, LIT_COLLECT])
        self.assertEqual(log_text, LIT_WARNING_SHUTDOWN_ONLY_WARNING)

    def test_abnormal_logserver_exit_fails_before_collection(self):
        process = SimpleNamespace(poll=lambda: None)
        with mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, return_value=(7, "logserver tail")), \
             mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN) as drain, \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA) as collect:
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "logserver.*exit 7"):
                shutdown_logserver_and_collect(process, Path(LIT_LOGS), {}, LIT_LOG)

        drain.assert_not_called()
        collect.assert_not_called()


class RuntimeLogSmokeTests(unittest.TestCase):
    def test_runtime_validation_follows_logserver_shutdown_collection(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            runtime_process = SimpleNamespace(poll=lambda: 0)
            logserver_process = SimpleNamespace(poll=lambda: None)
            args = SimpleNamespace(
                executable=sys.executable,
                expect_log_message=[LIT_SHUTDOWN_MARKER],
                reject_log_message=[LIT_WARNING],
                timeout=1.0,
            )
            events = []

            def wait_for_exit(_process, _timeout):
                events.append(LIT_RUNTIME_EXIT)
                return 0

            def shutdown_and_collect(*_args):
                events.append(LIT_LOGSERVER_SHUTDOWN_AND_COLLECT)
                return LIT_SHUTDOWN_MARKER

            def validate(*_args):
                events.append(LIT_VALIDATE)

            with mock.patch.object(runtime_log_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(
                     runtime_log_smoke,
                     LIT_LAUNCH_LOGSERVER,
                     return_value=(logserver_process, 49152, log_directory, {}, LIT_LOG),
                 ), \
                 mock.patch.object(runtime_log_smoke, LIT_LAUNCH_TESTBED, return_value=runtime_process), \
                 mock.patch.object(runtime_log_smoke, "wait_for_process_exit", side_effect=wait_for_exit), \
                 mock.patch.object(runtime_log_smoke, "read_process_tail", return_value=""), \
                 mock.patch.object(
                     runtime_log_smoke,
                     LIT_SHUTDOWN_LOGSERVER_AND_COLLECT,
                     side_effect=shutdown_and_collect,
                 ), \
                 mock.patch.object(runtime_log_smoke, "validate_expected_log_messages", side_effect=validate), \
                 mock.patch.object(runtime_log_smoke, LIT_TERMINATE_PROCESS, return_value=(0, "")), \
                 mock.patch.object(runtime_log_smoke, LIT_WRITE_STATUS):
                self.assertEqual(runtime_log_smoke.run(args), 0)

            self.assertEqual(events, [LIT_RUNTIME_EXIT, LIT_LOGSERVER_SHUTDOWN_AND_COLLECT, LIT_VALIDATE])


class ShutdownLogValidationTests(unittest.TestCase):
    def run_capture_with_shutdown_log(self, shutdown_log, required=(), rejected=()):
        with tempfile.TemporaryDirectory() as temp_dir:
            log_directory = Path(temp_dir)
            log_path = log_directory / LIT_TEST_LOG
            args = SimpleNamespace(
                application_arg=[],
                executable=sys.executable,
                expect_log_message=list(required),
                expect_texture_smoke=False,
                expect_transparent_csg=False,
                expect_transparent_multi=False,
                log_port=0,
                logserver_executable=None,
                no_logserver=False,
                output=log_directory / LIT_CAPTURE_BMP,
                reject_log_message=list(rejected),
                render_ready_timeout=1.0,
                settle_seconds=0.0,
                skip_blocking_log_message=[],
                skip_log_message=[],
                software_vulkan=LIT_OFF,
                timeout=1.0,
                window_title="NWB Test",
                working_directory=log_directory,
            )
            backend = mock.Mock()
            backend.wait_for_window.return_value = 0x4A
            testbed_process = SimpleNamespace(pid=4321, poll=lambda: None)
            logserver_process = SimpleNamespace(poll=lambda: None)
            events = []

            def terminate(process, name, _window_handle=None):
                if process is testbed_process:
                    events.append(LIT_TESTBED_SHUTDOWN)
                else:
                    events.append(LIT_LOGSERVER_SHUTDOWN)
                    log_path.write_text(shutdown_log, encoding=LIT_UTF_8)
                return 0, ""

            def drain(*_args):
                events.append(LIT_LOG_DRAIN)

            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_LAUNCH_LOGSERVER,
                     return_value=(logserver_process, 49152, log_directory, {}, LIT_LOG),
                 ), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=testbed_process), \
                 mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=LIT_CAPTURE), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_LOG_DRAIN, side_effect=drain):
                result = window_capture_smoke.launch_and_capture(args, backend)

            self.assertEqual(events, [LIT_TESTBED_SHUTDOWN, LIT_LOGSERVER_SHUTDOWN, LIT_LOG_DRAIN])
            return result

    def test_shutdown_marker_is_validated_after_graceful_exit(self):
        self.assertEqual(
            self.run_capture_with_shutdown_log(LIT_PROJECTTESTBED_SHUTDOWN, required=(LIT_PROJECTTESTBED_SHUTDOWN,)),
            LIT_CAPTURE,
        )

    def test_teardown_warning_fails_after_graceful_exit(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "rejected log message"):
            self.run_capture_with_shutdown_log("[WARNING] teardown failure", rejected=(LIT_WARNING,))


class RenderReadyCaptureTests(unittest.TestCase):
    @staticmethod
    def make_args(directory, timeout=1.0):
        return SimpleNamespace(
            expect_texture_smoke=False,
            expect_transparent_csg=False,
            expect_transparent_multi=False,
            output=directory / LIT_CAPTURE_BMP,
            render_ready_timeout=timeout,
        )

    @staticmethod
    def make_capture(appears_empty_or_white, has_pixel_variation):
        return SimpleNamespace(
            appears_empty_or_white=appears_empty_or_white,
            handle=0x4A,
            has_pixel_variation=has_pixel_variation,
        )

    def test_uniform_gray_is_flat_but_not_a_readiness_placeholder(self):
        analysis = window_capture_smoke.analyze_rgb_rows([
            [(128, 128, 128), (128, 128, 128)],
            [(128, 128, 128), (128, 128, 128)],
        ])

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

            backend.capture_window.assert_called_once_with(0x4A, args.output)
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
                with self.assertRaisesRegex(
                    window_capture_smoke.SmokeFailure,
                    r"testbed exited while waiting for rendered window content \(exit 7\)",
                ):
                    window_capture_smoke.capture_render_ready_window(args, backend, 0x4A, process)

            backend.capture_window.assert_called_once_with(0x4A, args.output)
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

            backend.capture_window.assert_called_once_with(0x4A, args.output)
            sleep.assert_not_called()


class ApplicationCaptureLifecycleTests(unittest.TestCase):
    @staticmethod
    def make_args(directory):
        return SimpleNamespace(
            application_capture_frame_count=12,
            executable=sys.executable,
            expect_log_message=[],
            expect_texture_smoke=False,
            expect_transparent_csg=False,
            expect_transparent_multi=False,
            output=directory / LIT_CAPTURE_BMP,
            reject_log_message=list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES),
            skip_blocking_log_message=list(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES),
            skip_log_message=[],
            timeout=1.0,
        )

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
                self.assertEqual(env[window_capture_smoke.FRAMEBUFFER_CAPTURE_PATH_ENV], str(args.output))
                self.assertEqual(env[window_capture_smoke.FRAMEBUFFER_CAPTURE_FRAME_COUNT_ENV], "12")
                return testbed_process

            def wait_for_exit(*_args):
                events.append(LIT_APPLICATION_WAIT)
                args.output.write_bytes(b"engine-bmp")
                return 0, True

            def terminate(process, name):
                self.assertIs(process, testbed_process)
                self.assertEqual(name, LIT_TESTBED)
                events.append(LIT_APPLICATION_OUTPUT_COLLECT)
                return 0, ""

            def shutdown_and_collect(*_args):
                events.append(LIT_LOGSERVER_SHUTDOWN_AND_LOG_COLLECT)
                return captured_log

            def validate_logs(log_text, _args):
                events.append(LIT_LOG_VALIDATION)
                self.assertEqual(log_text, captured_log)
                self.assertEqual(args.log_output.read_bytes(), captured_log.encode(LIT_UTF_8))

            def read_bmp(path):
                events.append(LIT_BMP_PARSE)
                self.assertEqual(path, args.output)
                return capture

            def validate_pixels(_args, result):
                events.append(LIT_PIXEL_VALIDATION)
                self.assertIs(result, capture)

            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_LOGSERVER, side_effect=launch_logserver), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, side_effect=launch_testbed), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, side_effect=wait_for_exit), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_SHUTDOWN_LOGSERVER_AND_COLLECT,
                     side_effect=shutdown_and_collect,
                 ), \
                 mock.patch.object(
                     window_capture_smoke,
                     "validate_application_capture_log_text",
                     side_effect=validate_logs,
                 ), \
                 mock.patch.object(window_capture_smoke, LIT_READ_BMP_24, side_effect=read_bmp), \
                 mock.patch.object(window_capture_smoke, "validate_capture_for_args", side_effect=validate_pixels):
                result = window_capture_smoke.launch_and_capture_application(args)

        self.assertIs(result, capture)
        self.assertEqual(
            events,
            [
                LIT_LOGSERVER_LAUNCH,
                LIT_APPLICATION_LAUNCH,
                LIT_APPLICATION_WAIT,
                LIT_APPLICATION_OUTPUT_COLLECT,
                LIT_LOGSERVER_SHUTDOWN_AND_LOG_COLLECT,
                LIT_LOG_VALIDATION,
                LIT_BMP_PARSE,
                LIT_PIXEL_VALIDATION,
            ],
        )

    def test_nonzero_application_exit_fails_after_shutdown_log_collection(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args = self.make_args(Path(temp_dir))
            events = []
            with mock.patch.object(window_capture_smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_LAUNCH_LOGSERVER,
                     return_value=(object(), 49152, Path(temp_dir), {}, LIT_LOG),
                 ), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, return_value=(9, False)), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, return_value=(9, "application tail")), \
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_SHUTDOWN_LOGSERVER_AND_COLLECT,
                     side_effect=lambda *_args: events.append(LIT_LOGS_COLLECTED) or window_capture_smoke.FRAMEBUFFER_CAPTURE_READY_MESSAGE,
                 ), \
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
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_LAUNCH_LOGSERVER,
                     return_value=(object(), 49152, Path(temp_dir), {}, LIT_LOG),
                 ), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT,
                     side_effect=window_capture_smoke.SmokeFailure(LIT_TIMED_OUT_WAITING_FOR_SELF_EXIT),
                 ), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, side_effect=terminate), \
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_SHUTDOWN_LOGSERVER_AND_COLLECT,
                     side_effect=shutdown_and_collect,
                 ):
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_TIMED_OUT_WAITING_FOR_SELF_EXIT):
                    window_capture_smoke.launch_and_capture_application(args)
            self.assertEqual(args.log_output.read_bytes(), LIT_ORDINARY_STARTUP.encode(LIT_UTF_8))

        self.assertEqual(events, [LIT_APPLICATION_TERMINATED, LIT_LOGS_COLLECTED])

    def test_capture_ready_marker_is_required_after_normal_exit(self):
        args = self.make_args(Path(LIT_CAPTURE_ROOT))
        self.assertIsNone(
            window_capture_smoke.validate_application_capture_log_text(
                window_capture_smoke.FRAMEBUFFER_CAPTURE_READY_MESSAGE,
                args,
            )
        )
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
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_LAUNCH_LOGSERVER,
                     return_value=(object(), 49152, Path(temp_dir), {}, LIT_LOG),
                 ), \
                 mock.patch.object(window_capture_smoke, LIT_LAUNCH_TESTBED, return_value=object()), \
                 mock.patch.object(window_capture_smoke, LIT_WAIT_FOR_APPLICATION_CAPTURE_EXIT, return_value=(0, False)), \
                 mock.patch.object(window_capture_smoke, LIT_TERMINATE_PROCESS, return_value=(0, "")), \
                 mock.patch.object(
                     window_capture_smoke,
                     LIT_SHUTDOWN_LOGSERVER_AND_COLLECT,
                     return_value=window_capture_smoke.FRAMEBUFFER_CAPTURE_SKIP_MESSAGE,
                 ):
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

        self.assertEqual(capture.capture_window(hwnd, output_path), LIT_CAPTURE)
        self.assertEqual(
            calls,
            [
                (LIT_PREPARE, hwnd),
                (LIT_POST_PREPARE_CLIENT_RECT, hwnd),
                (LIT_BITBLT, hwnd, expected_rect, output_path),
            ],
        )


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
        capture._capture_drawable_region.assert_called_once_with(window, window, 0, 0, 1280, 900, output_path)
        capture._window_root_region.assert_not_called()


class CaptureFocusTests(unittest.TestCase):
    def test_linux_focus_window_raises_sets_input_focus_and_flushes(self):
        window = 0x4A
        display = object()
        calls = []
        capture = object.__new__(window_capture_smoke.LinuxX11Capture)

        class FakeX11:
            def XRaiseWindow(self, received_display, received_window):
                calls.append((LIT_RAISE, received_display, received_window))

            def XSetInputFocus(self, received_display, received_window, revert_to, timestamp):
                calls.append((LIT_FOCUS, received_display, received_window, revert_to, timestamp))

            def XFlush(self, received_display):
                calls.append((LIT_FLUSH, received_display))

        capture.x11 = FakeX11()
        capture.display = display

        capture.focus_window(window)

        self.assertEqual(
            calls,
            [
                (LIT_RAISE, display, window),
                (LIT_FOCUS, display, window, capture.REVERT_TO_PARENT, capture.CURRENT_TIME),
                (LIT_FLUSH, display),
            ],
        )

    def test_windows_focus_window_foregrounds_the_captured_hwnd(self):
        window = 0x4A
        calls = []
        capture = object.__new__(window_capture_smoke.WindowsCapture)

        class FakeUser32:
            def SetForegroundWindow(self, hwnd):
                calls.append(hwnd.value)
                return 1

        capture.user32 = FakeUser32()

        capture.focus_window(window)

        self.assertEqual(calls, [window])

    def test_windows_prepare_window_uses_the_capture_preparation_path(self):
        window = 0x4A
        calls = []
        capture = object.__new__(window_capture_smoke.WindowsCapture)

        def prepare(received_window):
            calls.append(received_window)

        capture._prepare_capture_window = prepare

        capture.prepare_window(window)

        self.assertEqual(calls, [window])


class GracefulTerminationTests(unittest.TestCase):
    def test_windows_graceful_exit_preserves_a_high_bit_hwnd(self):
        process_id = 4321
        high_bit_hwnd = 0xF234567887654321
        observed_owner_queries = []
        observed_posts = []

        class FakeFunction:
            def __init__(self, implementation):
                self.implementation = implementation
                self.argtypes = None
                self.restype = None

            def __call__(self, *args):
                return self.implementation(*args)

        class FakeUser32:
            def __init__(self):
                self.EnumWindows = FakeFunction(lambda callback, lparam: callback(high_bit_hwnd, lparam))
                self.GetWindowThreadProcessId = FakeFunction(self.get_window_thread_process_id)
                self.PostMessageW = FakeFunction(self.post_message)

            @staticmethod
            def get_window_thread_process_id(hwnd, owner_pid):
                observed_owner_queries.append(hwnd)
                owner_pid._obj.value = process_id
                return 1

            @staticmethod
            def post_message(hwnd, message, wparam, lparam):
                observed_posts.append((hwnd, message, wparam, lparam))
                return 1

        user32 = window_capture_smoke.bind_windows_user32(FakeUser32())
        with mock.patch.object(window_capture_smoke, "create_windows_user32", return_value=user32):
            self.assertTrue(window_capture_smoke.request_windows_graceful_exit(process_id))

        self.assertEqual(observed_owner_queries, [high_bit_hwnd])
        self.assertEqual(observed_posts, [(high_bit_hwnd, 0x0010, 0, 0)])
        self.assertEqual(
            user32.GetWindowThreadProcessId.argtypes,
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)],
        )
        self.assertEqual(
            user32.PostMessageW.argtypes,
            [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t],
        )

    def test_linux_x11_helper_receives_captured_window_handle(self):
        result = mock.Mock(returncode=0)
        with mock.patch.object(window_capture_smoke.subprocess, "run", return_value=result) as run:
            self.assertTrue(window_capture_smoke.request_linux_graceful_exit(0x4a))

        run.assert_called_once_with(
            [
                sys.executable,
                str(SMOKE_DIRECTORY / "x11_graceful_close.py"),
                "0x4a",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=6.0,
        )

    def test_linux_x11_close_waits_for_normal_exit_before_fallback(self):
        process = _FakeProcess(graceful_exit=True)
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=True) as close:
            terminate_process(process, LIT_TESTBED, 0x4a)

        close.assert_called_once_with(0x4a)
        self.assertEqual(process.wait_timeouts, [10.0])
        self.assertEqual(process.terminate_calls, 0)

    def test_linux_x11_close_falls_back_to_sigterm_after_timeout(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=True), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS) as write_status:
            terminate_process(process, LIT_TESTBED, 0x4a)

        self.assertEqual(process.wait_timeouts, [10.0, 5.0])
        self.assertEqual(process.terminate_calls, 1)
        self.assertEqual(process.kill_calls, 0)
        write_status.assert_called_once_with("testbed: did not exit after X11 WM_DELETE_WINDOW; terminating")

    def test_linux_x11_helper_failure_falls_back_to_sigterm(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=False) as close:
            terminate_process(process, LIT_TESTBED, 0x4a)

        close.assert_called_once_with(0x4a)
        self.assertEqual(process.wait_timeouts, [5.0])
        self.assertEqual(process.terminate_calls, 1)

    def test_linux_without_a_captured_handle_does_not_discover_a_window_by_title(self):
        process = _FakeProcess()
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT) as close:
            terminate_process(process, LIT_TESTBED)

        close.assert_not_called()
        self.assertEqual(process.terminate_calls, 1)

    def test_windows_keeps_existing_wm_close_path(self):
        process = _FakeProcess(graceful_exit=True)
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_WINDOWS), \
             mock.patch.object(window_capture_smoke, "request_windows_graceful_exit", return_value=True) as windows_close, \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT) as linux_close:
            terminate_process(process, LIT_TESTBED, 0x4a)

        windows_close.assert_called_once_with(process.pid)
        linux_close.assert_not_called()
        self.assertEqual(process.terminate_calls, 0)

    def test_nonzero_graceful_exit_is_reported_to_the_smoke_runner(self):
        process = _FakeProcess(graceful_exit=True, graceful_exit_code=-6)
        with mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_LINUX), \
             mock.patch.object(window_capture_smoke, LIT_REQUEST_LINUX_GRACEFUL_EXIT, return_value=True):
            exit_code, tail = terminate_process(process, LIT_TESTBED, 0x4a)

        self.assertEqual(exit_code, -6)
        self.assertEqual(tail, "")
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "exit -6"):
            require_normal_process_exit(exit_code, tail, LIT_TESTBED)

    def test_missing_shutdown_exit_code_is_reported_to_the_smoke_runner(self):
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "did not exit"):
            require_normal_process_exit(None, "", LIT_TESTBED)



class ResizeCaptureConfigurationTests(unittest.TestCase):
    @staticmethod
    def base_args():
        return [LIT_EXECUTABLE, sys.executable, LIT_OUTPUT, LIT_CAPTURE_BMP]

    def test_resize_is_opt_in_and_has_two_meaningful_settle_intervals(self):
        ordinary = window_capture_smoke.parse_args(self.base_args())
        self.assertIsNone(ordinary.resize_client)
        self.assertIsNone(ordinary.resize_settle_seconds)
        resized = window_capture_smoke.parse_args(self.base_args() + [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701])
        self.assertEqual(resized.resize_client, [1001, 701])
        self.assertEqual(resized.settle_seconds, 2.0)
        self.assertEqual(resized.resize_settle_seconds, 2.0)

    def test_resize_rejects_incompatible_modes_and_invalid_limits(self):
        for extra in (
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_APPLICATION_CAPTURE],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_WINDOW_HANDLE, "1"],
            [LIT_RESIZE_CLIENT, "0", LIT_N_701],
            [LIT_RESIZE_CLIENT, "16385", LIT_N_701],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_SETTLE_SECONDS, "0"],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_SETTLE_SECONDS, LIT_NAN],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_RESIZE_SETTLE_SECONDS, "0.5"],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, LIT_RESIZE_SETTLE_SECONDS, LIT_INF],
            [LIT_RESIZE_CLIENT, LIT_N_1001, LIT_N_701, "--render-ready-timeout", LIT_INF],
            [LIT_RESIZE_SETTLE_SECONDS, "2"],
        ):
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
        window_capture_smoke.validate_resize_environment({})

    def test_launch_environment_checks_freeze_only_when_resize_is_requested(self):
        args = SimpleNamespace(resize_client=[1001, 701], software_vulkan=LIT_OFF)
        with mock.patch.dict(os.environ, {LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "6"}, clear=True), \
             mock.patch.object(window_capture_smoke.platform, LIT_SYSTEM, return_value=LIT_WINDOWS):
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "active or invalid"):
                window_capture_smoke.build_launch_environment(args)
            args.resize_client = None
            self.assertEqual(window_capture_smoke.build_launch_environment(args)[LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F], "6")


class ResizeCaptureLifecycleTests(unittest.TestCase):
    @staticmethod
    def make_args():
        return SimpleNamespace(output=Path(LIT_CAPTURE_BMP), resize_client=[1001, 701], settle_seconds=2.0,
            resize_settle_seconds=3.0, render_ready_timeout=1.0)

    def test_resize_waits_for_original_render_then_runtime_ack_and_final_render(self):
        args = self.make_args()
        events = []
        backend = mock.Mock()
        backend.prepare_window.side_effect = lambda handle: events.append(LIT_PREPARE)
        backend.client_size.side_effect = [(1280, 900), (1280, 900), (1001, 701), (1001, 701), (1001, 701)]
        backend.resize_client.side_effect = lambda *values: events.append((LIT_RESIZE, values))
        process = SimpleNamespace(poll=lambda: None)
        before = SimpleNamespace(width=1280, height=900)
        after = SimpleNamespace(width=1001, height=701)
        def capture(capture_args, *_):
            events.append((LIT_CAPTURE, capture_args.output))
            return before if len([event for event in events if isinstance(event, tuple) and event[0] == LIT_CAPTURE]) == 1 else after
        with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, side_effect=capture), \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, side_effect=["", LIT_GRAPHICSRUNTIME_BACK_BUFFER_RESIZED_TO]), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
             mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, side_effect=[10.0, 10.1]), \
             mock.patch.object(window_capture_smoke.time, LIT_SLEEP, side_effect=lambda seconds: events.append((LIT_SLEEP, seconds))):
            result = window_capture_smoke.capture_resized_window(args, backend, 42, process, Path(LIT_LOGS), {}, LIT_LOG)
        self.assertIs(result, after)
        backend.resize_client.assert_called_once_with(42, 1001, 701)
        self.assertEqual(events, [LIT_PREPARE, (LIT_CAPTURE, Path("capture.before-resize.bmp")), (LIT_SLEEP, 2.0),
            (LIT_RESIZE, (42, 1001, 701)), (LIT_SLEEP, 0.1), (LIT_SLEEP, 3.0), (LIT_CAPTURE, Path(LIT_CAPTURE_BMP))])

    def test_resize_requires_both_client_extent_and_renderer_acknowledgement(self):
        for client, log in (((1280, 900), LIT_GRAPHICSRUNTIME_BACK_BUFFER_RESIZED_TO), ((1001, 701), "")):
            with self.subTest(client=client, log=log):
                args = self.make_args()
                backend = mock.Mock()
                backend.client_size.side_effect = [(1280, 900), (1280, 900), client]
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
        backend.client_size.side_effect = [(1280, 900), (1280, 900), (1001, 701)]
        with mock.patch.object(window_capture_smoke, LIT_CAPTURE_RENDER_READY_WINDOW, return_value=SimpleNamespace(width=1280, height=900)), \
             mock.patch.object(window_capture_smoke, LIT_COLLECT_LOG_DELTA, return_value=LIT_GRAPHICSRUNTIME_BACK_BUFFER_RESIZED_TO), \
             mock.patch.object(window_capture_smoke, LIT_WRITE_STATUS), \
             mock.patch.object(window_capture_smoke.time, LIT_MONOTONIC, return_value=10.0), \
             mock.patch.object(window_capture_smoke.time, LIT_SLEEP), \
             self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "final client/capture extent"):
            window_capture_smoke.capture_resized_window(args, backend, 42, SimpleNamespace(poll=lambda: None), Path(LIT_LOGS), {}, LIT_LOG)

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
        backend.user32.SetWindowPos.assert_called_once()
        received = backend.user32.SetWindowPos.call_args.args
        self.assertEqual(received[0].value, hwnd)
        self.assertEqual(received[1:6], (None, 0, 0, 1017, 740))
        self.assertFalse(received[6] & backend.SWP_NOSIZE)
        self.assertTrue(received[6] & backend.SWP_NOMOVE)
        backend.user32.SetWindowPos.return_value = 0
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "SetWindowPos failed"):
            backend.resize_client(hwnd, 1001, 701)

    def test_linux_resize_uses_client_dimensions_and_flushes_the_request(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend.display = object()
        backend.x11 = mock.Mock()
        backend.x11.XResizeWindow.return_value = 1
        backend.resize_client(42, 1001, 701)
        backend.x11.XResizeWindow.assert_called_once_with(backend.display, 42, 1001, 701)
        backend.x11.XFlush.assert_called_once_with(backend.display)
        backend.x11.XResizeWindow.return_value = 0
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "XResizeWindow failed"):
            backend.resize_client(42, 1001, 701)


class PixelSampleBackendTests(unittest.TestCase):
    def test_linux_sample_rejects_outside_or_empty_regions_before_request(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend._validated_window_size = mock.Mock(return_value=(100, 80))
        backend.x11 = mock.Mock()
        for rectangle in ((-1, 0, 1, 1), (0, -1, 1, 1), (0, 0, 0, 1), (0, 0, 1, 0), (99, 0, 2, 1), (0, 79, 1, 2)):
            with self.subTest(rectangle=rectangle), self.assertRaises(window_capture_smoke.SmokeFailure):
                backend.sample_client_pixels(42, *rectangle)
        backend.x11.XGetImage.assert_not_called()

    def test_linux_sample_releases_image_when_pixel_conversion_fails(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend._validated_window_size = mock.Mock(return_value=(100, 80))
        backend.display = object()
        backend.x11 = mock.Mock()
        image = SimpleNamespace(contents=SimpleNamespace(bits_per_pixel=32, data=1, byte_order=backend.LSB_FIRST))
        backend.x11.XGetImage.return_value = image
        with mock.patch.object(window_capture_smoke, "ximage_rgb_rows", side_effect=ValueError("invalid image")), \
             self.assertRaises(ValueError):
            backend.sample_client_pixels(42, 10, 20, 1, 3)
        backend.x11.XDestroyImage.assert_called_once_with(image)


if __name__ == LIT_MAIN:
    unittest.main()
