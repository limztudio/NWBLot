#!/usr/bin/env python3
"""Window capture image analysis, text-area caret capture, caret pixel, and pixel-sample backend checks."""


import math
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
import window_capture_x11  # noqa: E402
import edit_probe  # noqa: E402
import numeric_edit_smoke  # noqa: E402
import text_area_probe  # noqa: E402
import text_area_smoke  # noqa: E402
from probe_reference import encoded_marker  # noqa: E402


LIT_PASSED = "passed"
LIT_READ = "read"
LIT_LOG_2 = "log"
LIT_UNUSED = "unused"
LIT_PHASE_CAPTURE = "phase_capture"
LIT_PROBES = "probes"
LIT_NAME = "name"
LIT_CARET = "caret"
LIT_MODEL_MATCHES = "model_matches"
LIT_GEOMETRY_MATCHES = "geometry_matches"
LIT_RECTANGLES = "rectangles"
LIT_MEASURE = "measure"
LIT_X_TRACK = "x_track"
LIT_Y_TRACK = "y_track"
LIT_X_THUMB = "x_thumb"
LIT_Y_THUMB = "y_thumb"
LIT_CORNER = "corner"
LIT_CAPTURE_BMP = "capture.bmp"
LIT_OUTPUT = "--output"
LIT_EARLY = "early"
LIT_MID = "mid"
LIT_CUT_VOID_0 = "cut_void=0"
LIT_REMAINING_CENTER_0 = "remaining_center=0"
LIT_WINDOW_HANDLE = "--window-handle"
LIT_SYS_STDERR = "sys.stderr"
LIT_LOG = "*.log"
LIT_COLLECT_LOG_DELTA = "collect_log_delta"
LIT_MONOTONIC = "monotonic"
LIT_SLEEP = "sleep"
LIT_MAIN = "__main__"


class TextureSmokeAnalysisTests(unittest.TestCase):
    def test_texture_smoke_analysis_isolates_red_bounce_on_the_white_receiver(self):
        rows = [[(210, 210, 210) for _ in range(200)] for _ in range(200)]
        for y in range(80, 85):
            for x in range(81, 84):
                rows[y][x] = (104, 70, 68)
        for y in range(134, 139):
            for x in range(76, 79):
                rows[y][x] = (104, 70, 68)

        analysis = window_capture_smoke.analyze_texture_smoke_rows(rows)

        self.assertEqual(analysis.red_pixels, 2 * 3 * 5)
        self.assertEqual(analysis.receiver_pixel_count, 12 * 7)
        self.assertEqual(analysis.receiver_red_pixels, 3 * 5)

    def test_texture_smoke_validation_requires_dense_receiver_only_red_coverage(self):
        result = SimpleNamespace(width=1280, height=900, texture_smoke=window_capture_smoke.TextureSmokeAnalysis(300, 300, 300, 256, 1024),)

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
        # Independent fixture projection: authored six-axis octahedron (.6), receiver scale .78, camera distance2.2, vertical FOV60.
        # Euler application is roll, pitch, then the combined local/parent yaw.
        # Clipping local y>0 removes only the upper axis vertex; the equator and lower vertex form the retained convex pyramid.
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
        return min(cls.cross(left, right, point) / math.hypot(right[0] - left[0], right[1] - left[1]) for left, right in zip(hull, hull[1:] + hull[:1]))

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
                    # A missing green receiver may expose dense red/blue neighboring meshes.
                    # Generic foreground coverage is deliberately preserved, so only receiver identity can reject this negative control.
                    rows[y][x] = (receiver_color or cls.RECEIVER) if receiver else ((180, 50, 30) if x % 2 == 0 else (40, 90, 180))
        return rows

    @staticmethod
    def capture_result(pose, analysis):
        return SimpleNamespace(transparent_csg={pose: analysis})

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
        for rejected in (window_capture_smoke.TransparentCsgAnalysis(199, 600, 200, 1000), window_capture_smoke.TransparentCsgAnalysis(200, 600, 199, 1000), window_capture_smoke.TransparentCsgAnalysis(159, 300, 160, 500),
            window_capture_smoke.TransparentCsgAnalysis(160, 300, 159, 500),):
            with self.subTest(analysis=rejected), self.assertRaises(window_capture_smoke.SmokeFailure):
                window_capture_smoke.validate_transparent_csg_result(self.capture_result(LIT_EARLY, rejected), LIT_EARLY)

    def test_validation_never_substitutes_a_different_pose(self):
        result = SimpleNamespace(transparent_csg={ LIT_EARLY: window_capture_smoke.TransparentCsgAnalysis(0, 600, 200, 1000), LIT_MID: window_capture_smoke.TransparentCsgAnalysis(200, 600, 200, 1000), })
        window_capture_smoke.validate_transparent_csg_result(result, LIT_MID)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "early pose"):
            window_capture_smoke.validate_transparent_csg_result(result, LIT_EARLY)

    def test_csg_cli_requires_one_explicit_pose(self):
        base = [LIT_WINDOW_HANDLE, "1", LIT_OUTPUT, LIT_CAPTURE_BMP, "--expect-transparent-csg"]
        with mock.patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
            window_capture_smoke.parse_args(base)
        window_capture_smoke.parse_args(base + ["--transparent-csg-pose", LIT_EARLY])


class TextAreaCaretCaptureTests(unittest.TestCase):
    @staticmethod
    def fixture(sequence, caret_on):
        values = dict.fromkeys(text_area_probe.FIELDS, 0)
        values.update(text_area_probe.document_fields("", 0, 0), focus=1, enabled=1, coherent=1)
        rectangles = dict.fromkeys(text_area_probe.RECTANGLES, (0.0, 0.0, 0.0, 0.0))
        rectangles.update(bounds=(32.0, 140.0, 380.0, 160.0), content=(40.0, 148.0, 364.0, 144.0), clip=(40.0, 148.0, 364.0, 144.0), caret=(40.0, 148.0, 1.0, 21.796875))
        lines = ["UiTextAreaSmoke: display logical=640x440 scale=1x1", "UiTextAreaSmoke: skin=default", f"UiTextAreaSmoke: state sequence={sequence} values=" + ",".join(str(values[name]) for name in text_area_probe.FIELDS),
            f"UiTextAreaSmoke: metrics sequence={sequence} scroll=0,0 measure=0,21.796875 line_height=21.796875 selections=0 maximum=0,0"]
        lines.extend(f"UiTextAreaSmoke: geometry sequence={sequence} {name}=" + ",".join(map(str, rectangle)) for name, rectangle in rectangles.items())
        text = "\n".join(lines)
        snapshot = text_area_probe.snapshot_from_logs(text)
        rows = [[(24, 29, 37)] * 640 for _ in range(440)]
        for y in range(140, 300):
            rows[y][32:412] = [(18, 26, 37)] * 380
        marker = 0
        for value, parts in zip((*snapshot["values"], sequence), text_area_probe.MARKER_PARTS):
            for part in range(parts):
                color = encoded_marker(value >> (part * 12))
                x = 11 + marker * 8
                for y in range(426, 429):
                    rows[y][x - 1:x + 2] = [color] * 3
                marker += 1
        if caret_on:
            for y in range(148, 170):
                rows[y][40] = (249, 252, 255)
        return text, snapshot, (640, 440, rows), values

    def checkpoint_with_refresh(self, refreshed_sequence):
        before, snapshot, off_frame, expected = self.fixture(7, False)
        _on_text, _on_snapshot, on_frame, _on_expected = self.fixture(7, True)
        refreshed, _refreshed_snapshot, _frame, _expected = self.fixture(refreshed_sequence, True)
        clock = SimpleNamespace(now=0.0, visible_until=0.0, samples=0, reads=0, logs=0, frame=None)
        events, reports = [], []
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend.display, backend.root = object(), 1
        backend.x11 = mock.Mock()
        backend.get_attributes = mock.Mock(return_value=SimpleNamespace(width=640, height=440))
        backend._window_root_region = mock.Mock()

        def sleep(delay):
            clock.now += delay

        def read(*_args):
            clock.reads += 1
            on = clock.now < clock.visible_until
            events.append((LIT_READ, clock.reads, on))
            clock.frame = on_frame if on else off_frame
            clock.now += 0.05
            return SimpleNamespace(has_pixel_variation=True, appears_empty_or_white=False)

        def sample(*_args):
            clock.samples += 1
            if clock.samples == 2:
                clock.visible_until = clock.now + 0.125
            color = (249, 252, 255) if clock.now < clock.visible_until else (18, 26, 37)
            return [[color] for _ in range(3)]

        def collect(*_args):
            clock.logs += 1
            events.append((LIT_LOG_2, clock.logs))
            if clock.logs > 1:
                clock.now += 0.25
            return before if clock.logs == 1 else refreshed

        oracle = text_area_smoke.observe_text_area
        def observe(*args, **kwargs):
            report = oracle(*args, **kwargs)
            reports.append(report)
            return report

        backend._capture_drawable_region = mock.Mock(side_effect=read)
        backend.sample_client_pixels = mock.Mock(side_effect=sample)
        run = object.__new__(text_area_smoke.TextAreaRun)
        run.args = SimpleNamespace(output_directory=Path(LIT_UNUSED), skin="default")
        run.backend, run.handle, run.process = backend, 42, SimpleNamespace(poll=lambda: None)
        run.native = SimpleNamespace(windows=False, maintain_pointer=mock.Mock(), observe_window=lambda: {})
        run.log_directory, run.log_baseline, run.log_pattern = Path(LIT_UNUSED), {}, LIT_LOG
        run.deadline, run.snapshot, run.failure_report, run.stages, run.expected = 1.0, None, None, [], expected
        run.caret_phase_waits = 0
        run.write_report = mock.Mock()
        with mock.patch.object(text_area_smoke.time, LIT_MONOTONIC, side_effect=lambda: clock.now), \
             mock.patch.object(text_area_smoke.time, LIT_SLEEP, side_effect=sleep), \
             mock.patch.object(text_area_smoke, LIT_COLLECT_LOG_DELTA, side_effect=collect), \
             mock.patch.object(text_area_smoke, "read_bmp_24_rows", side_effect=lambda _path: clock.frame), \
             mock.patch.object(text_area_smoke, "observe_text_area", side_effect=observe):
            if refreshed_sequence == 7:
                run.checkpoint(LIT_PHASE_CAPTURE, settle=0.0)
            else:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "displayed-state gate"):
                    run.checkpoint(LIT_PHASE_CAPTURE, settle=0.0)
        return run, events, reports, backend, clock

    def test_phase_capture_freezes_visible_pixels_before_slow_latest_log_refresh(self):
        run, events, reports, backend, clock = self.checkpoint_with_refresh(7)
        self.assertEqual(len(run.stages), 1)
        self.assertEqual(clock.samples, 2)
        self.assertEqual(clock.reads, 2)
        self.assertLess(events.index((LIT_READ, 2, True)), events.index((LIT_LOG_2, 2)))
        self.assertTrue(reports[1][LIT_PASSED])
        self.assertEqual(reports[1]["marker_count"], 57)
        self.assertTrue(next(probe for probe in reports[1][LIT_PROBES] if probe[LIT_NAME] == LIT_CARET)[LIT_PASSED])
        backend.x11.XRaiseWindow.assert_called_once()
        backend._window_root_region.assert_not_called()

    def test_newer_post_capture_snapshot_rejects_old_markers_despite_visible_caret(self):
        run, events, reports, _backend, _clock = self.checkpoint_with_refresh(8)
        self.assertEqual(run.stages, [])
        self.assertLess(events.index((LIT_READ, 2, True)), events.index((LIT_LOG_2, 2)))
        self.assertEqual(reports[1]["snapshot"]["sequence"], 8)
        self.assertTrue(reports[1][LIT_MODEL_MATCHES])
        self.assertTrue(reports[1][LIT_GEOMETRY_MATCHES])
        self.assertTrue(next(probe for probe in reports[1][LIT_PROBES] if probe[LIT_NAME] == LIT_CARET)[LIT_PASSED])
        self.assertEqual([probe[LIT_NAME] for probe in reports[1][LIT_PROBES] if not probe[LIT_PASSED]], ["model_25_0"])
        self.assertFalse(reports[1][LIT_PASSED])


class CaretPixelColumnTests(unittest.TestCase):
    def test_half_pixel_boundaries_choose_the_covered_column(self):
        cases = ((333.0, 333), (333.5, 333), (334.5, 334), (335.5, 335),
            (math.nextafter(333.5, -math.inf), 333), (math.nextafter(333.5, math.inf), 334))
        for left, expected_column in cases:
            with self.subTest(left=left):
                self.assertEqual(edit_probe.caret_pixel_column(left, 1.0), expected_column)

    def test_scale_is_applied_before_the_physical_half_pixel_offset(self):
        for left, scale, expected_column in ((10.4, 2.0, 21), (10.25, 2.0, 20), (667.0, 0.5, 333)):
            with self.subTest(left=left, scale=scale):
                self.assertEqual(edit_probe.caret_pixel_column(left, scale), expected_column)

    def test_edit_and_numeric_phase_samples_use_the_same_one_pixel_column(self):
        caret, content = (333.5, 148.0, 1.0, 21.796875), (40.0, 148.0, 364.0, 144.0)
        self.assertEqual(edit_probe.caret_probe_position(caret, content, 1.0, 1.0, 640, 440)[0], 333)
        run = object.__new__(numeric_edit_smoke.NumericEditRun)
        run.backend, run.handle = SimpleNamespace(client_size=lambda _handle: (640, 440)), 42
        snapshot = {"scale": (1.0, 1.0), "enabled": 1, "readonly": 0, "i_focus": 1, "i_anchor": 32, "i_caret": 32,
            LIT_RECTANGLES: {"integer_caret": caret, "integer_content": content}}
        self.assertEqual(run._caret_probe_sample(snapshot)[0], 333)

    def test_text_area_oracle_requires_the_covered_column_and_rejects_an_adjacent_decoy(self):
        _text, snapshot, frame, expected = TextAreaCaretCaptureTests.fixture(7, False)
        snapshot[LIT_RECTANGLES][LIT_CARET] = (333.5, 148.0, 1.0, 21.796875)
        rows = frame[2]
        for y in range(148, 170):
            rows[y][333] = (249, 252, 255)
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_PASSED])
        self.assertEqual(next(probe for probe in report[LIT_PROBES] if probe[LIT_NAME] == LIT_CARET)["position"][0], 333)
        for y in range(148, 170):
            rows[y][333], rows[y][334] = (18, 26, 37), (249, 252, 255)
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_MODEL_MATCHES])
        self.assertTrue(report[LIT_GEOMETRY_MATCHES])
        self.assertEqual([probe[LIT_NAME] for probe in report[LIT_PROBES] if not probe[LIT_PASSED]], [LIT_CARET])
        self.assertFalse(report[LIT_PASSED])


class TextAreaVisualMeasurementTests(unittest.TestCase):
    def test_visual_extent_matches_scroll_maximum_and_stale_logical_extent_is_rejected(self):
        _text, snapshot, frame, expected = TextAreaCaretCaptureTests.fixture(7, True)
        snapshot[LIT_MEASURE], snapshot["maximum"] = (533.09375, 370.54688), (184.09375, 240.54688)
        rectangles = snapshot[LIT_RECTANGLES]
        rectangles["content"] = rectangles["clip"] = (40.0, 148.0, 350.0, 130.0)
        rectangles[LIT_X_TRACK], rectangles[LIT_Y_TRACK] = (40.0, 278.0, 350.0, 14.0), (390.0, 148.0, 14.0, 130.0)
        rectangles[LIT_X_THUMB] = (40.0, 278.0, 350.0 * 350.0 / 534.09375, 14.0)
        rectangles[LIT_Y_THUMB] = (390.0, 148.0, 14.0, 130.0 * 130.0 / 370.54688)
        rectangles[LIT_CORNER] = (390.0, 278.0, 14.0, 14.0)
        rows = frame[2]
        def fill(rectangle, color):
            x, y, width, height = rectangle
            for row in range(math.ceil(y - 0.5), math.ceil(y + height - 0.5)):
                for column in range(math.ceil(x - 0.5), math.ceil(x + width - 0.5)):
                    rows[row][column] = color
        for name in (LIT_X_TRACK, LIT_Y_TRACK, LIT_CORNER):
            fill(rectangles[name], (24, 29, 37))
        for name in (LIT_X_THUMB, LIT_Y_THUMB):
            fill(rectangles[name], (45, 60, 80))
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_MODEL_MATCHES])
        self.assertTrue(report[LIT_GEOMETRY_MATCHES])
        self.assertTrue(all(probe[LIT_PASSED] for probe in report[LIT_PROBES]))
        self.assertTrue(report[LIT_PASSED])
        snapshot[LIT_MEASURE] = (532.59375, 370.54688)
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_MODEL_MATCHES])
        self.assertTrue(all(probe[LIT_PASSED] for probe in report[LIT_PROBES]))
        self.assertFalse(report[LIT_GEOMETRY_MATCHES])
        self.assertFalse(report[LIT_PASSED])


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
        with mock.patch.object(window_capture_x11, "ximage_rgb_rows", side_effect=ValueError("invalid image")), \
             self.assertRaises(ValueError):
            backend.sample_client_pixels(42, 10, 20, 1, 3)
        backend.x11.XDestroyImage.assert_called_once_with(image)


if __name__ == LIT_MAIN:
    unittest.main()
