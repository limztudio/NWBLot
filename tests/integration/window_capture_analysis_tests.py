#!/usr/bin/env python3
"""Window capture texture and transparent CSG image analysis checks."""


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

import window_capture_image  # noqa: E402
from window_capture_arguments import WindowCaptureArguments
import window_capture_smoke  # noqa: E402
import window_capture_validation  # noqa: E402


LIT_CAPTURE_BMP = "capture.bmp"
LIT_OUTPUT = "--output"
LIT_EARLY = "early"
LIT_MID = "mid"
LIT_CUT_VOID_0 = "cut_void=0"
LIT_REMAINING_CENTER_0 = "remaining_center=0"
LIT_WINDOW_HANDLE = "--window-handle"
LIT_SYS_STDERR = "sys.stderr"
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

        analysis = window_capture_image.analyze_texture_smoke_rows(rows)

        self.assertEqual(analysis.red_pixels, 2 * 3 * 5)
        self.assertEqual(analysis.receiver_pixel_count, 12 * 7)
        self.assertEqual(analysis.receiver_red_pixels, 3 * 5)

    def test_texture_smoke_validation_requires_dense_receiver_only_red_coverage(self):
        result = SimpleNamespace(width=1280, height=900, texture_smoke=window_capture_image.TextureSmokeAnalysis(300, 300, 300, 256, 1024),)

        window_capture_validation.validate_texture_smoke_result(result)

        result.texture_smoke = window_capture_image.TextureSmokeAnalysis(300, 300, 300, 255, 1024)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "receiver_red=255"):
            window_capture_validation.validate_texture_smoke_result(result)


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
        # Project the scaled octahedron and keep the local y<=0 pyramid after roll, pitch, and yaw.
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
                    # Keep foreground coverage high so only missing green receiver identity rejects this control.
                    rows[y][x] = (receiver_color or cls.RECEIVER) if receiver else ((180, 50, 30) if x % 2 == 0 else (40, 90, 180))
        return rows

    @staticmethod
    def capture_result(pose, analysis):
        return SimpleNamespace(transparent_csg={pose: analysis})

    def test_all_three_missing_cutter_poses_fail_even_with_background_outside_receiver(self):
        for pose in window_capture_smoke.TRANSPARENT_CSG_REGIONS:
            with self.subTest(pose=pose):
                rows = self.synthetic_scene(pose, clipped=False)
                analysis = window_capture_image.analyze_transparent_csg_rows(rows, pose)
                self.assertEqual(analysis.cut_void_pixels, 0)
                self.assertEqual(analysis.remaining_center_pixels, analysis.remaining_region_pixels)
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_CUT_VOID_0):
                    window_capture_validation.validate_transparent_csg_result(self.capture_result(pose, analysis), pose)

    def test_all_three_missing_receiver_poses_reject_dense_non_green_neighbors(self):
        for pose in window_capture_smoke.TRANSPARENT_CSG_REGIONS:
            with self.subTest(pose=pose):
                rows = self.synthetic_scene(pose, receiver=False)
                _, retained = window_capture_image.transparent_csg_regions(self.WIDTH, self.HEIGHT, pose)
                generic_foreground = window_capture_image.count_foreground_pixels_in_region(rows, self.BACKGROUND, retained)
                analysis = window_capture_image.analyze_transparent_csg_rows(rows, pose)
                self.assertEqual(generic_foreground, analysis.remaining_region_pixels)
                self.assertEqual(analysis.cut_void_pixels, analysis.cut_region_pixels)
                self.assertEqual(analysis.remaining_center_pixels, 0)
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_REMAINING_CENTER_0):
                    window_capture_validation.validate_transparent_csg_result(self.capture_result(pose, analysis), pose)

    def test_green_dominance_does_not_relax_foreground_contrast(self):
        rows = self.synthetic_scene(LIT_EARLY, receiver_color=(82, 102, 101))
        analysis = window_capture_image.analyze_transparent_csg_rows(rows, LIT_EARLY)
        self.assertEqual(analysis.remaining_center_pixels, 0)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, LIT_REMAINING_CENTER_0):
            window_capture_validation.validate_transparent_csg_result(self.capture_result(LIT_EARLY, analysis), LIT_EARLY)

    def test_original_density_thresholds_remain_required(self):
        accepted = window_capture_image.TransparentCsgAnalysis(200, 600, 200, 1000)
        window_capture_validation.validate_transparent_csg_result(self.capture_result(LIT_EARLY, accepted), LIT_EARLY)
        for rejected in (window_capture_image.TransparentCsgAnalysis(199, 600, 200, 1000), window_capture_image.TransparentCsgAnalysis(200, 600, 199, 1000), window_capture_image.TransparentCsgAnalysis(159, 300, 160, 500),
            window_capture_image.TransparentCsgAnalysis(160, 300, 159, 500),):
            with self.subTest(analysis=rejected), self.assertRaises(window_capture_smoke.SmokeFailure):
                window_capture_validation.validate_transparent_csg_result(self.capture_result(LIT_EARLY, rejected), LIT_EARLY)

    def test_validation_never_substitutes_a_different_pose(self):
        result = SimpleNamespace(transparent_csg={ LIT_EARLY: window_capture_image.TransparentCsgAnalysis(0, 600, 200, 1000), LIT_MID: window_capture_image.TransparentCsgAnalysis(200, 600, 200, 1000), })
        window_capture_validation.validate_transparent_csg_result(result, LIT_MID)
        with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "early pose"):
            window_capture_validation.validate_transparent_csg_result(result, LIT_EARLY)

    def test_csg_cli_requires_one_explicit_pose(self):
        base = [LIT_WINDOW_HANDLE, "1", LIT_OUTPUT, LIT_CAPTURE_BMP, "--expect-transparent-csg"]
        with mock.patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
            WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(base)
        WindowCaptureArguments(window_capture_smoke.STRICT_LOG_FAILURE_MESSAGES).parse_args(base + ["--transparent-csg-pose", LIT_EARLY])


if __name__ == LIT_MAIN:
    unittest.main()
