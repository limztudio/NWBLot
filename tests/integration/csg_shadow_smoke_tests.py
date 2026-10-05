#!/usr/bin/env python3
"""Exercise the image oracle with missing cuts, thickness, overlap and stale-view failures."""

from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from csg_shadow_reference import (  # noqa: E402
    ANALYTIC_ARMS, ANALYTIC_REGIONS, ANALYTIC_REGION_SPACING, ARMS, REGIONS, analytic_optical_length,
    clipped_union_length, compare_analytic_frames, compare_frames, expected_rgb, expected_sample_rgb, ray_box,
    receiver_pixel_world, reference_optical_length, region_pixels,
)
from csg_shadow_smoke import capture_environment, parse_args  # noqa: E402
from window_capture_smoke import SmokeFailure  # noqa: E402

# Shared literals (no inline hardcodes below this block).
LIT_DIRECTIONAL = "directional"
LIT_CAMERA_SHIFT = "camera_shift"
LIT_POINT = "point"
LIT_CUT = "cut"
LIT_GLASS_DEPTH = "glass_depth"
LIT_UNCUT = "uncut"
LIT_MOVED = "moved"
LIT_OS_ENVIRON = "os.environ"
LIT_NWB_SOFTWARE_SHADOW_BACKEND = "NWB_SOFTWARE_SHADOW_BACKEND"
LIT_NWB_SHADOW_TRANSPARENT_SAMPLING = "NWB_SHADOW_TRANSPARENT_SAMPLING"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_NWB_CSG_SHADOW_LIGHT_SOURCE = "NWB_CSG_SHADOW_LIGHT_SOURCE"
LIT_FINITE = "finite"
LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH = "NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH"
LIT_CENTER1 = "center1"
LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE = "NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE"
LIT_EXECUTABLE = "--executable"
LIT_TEST_EXE = "test.exe"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_HARDWARE = "hardware"
LIT_SOFTWARE = "software"
LIT_ROUTE = "--route"
LIT_LIGHT = "--light"
LIT_UNCUT_CONTROL = "uncut_control"
LIT_CAPSULE_ENDCAP = "capsule_endcap"
LIT_BOXES = "boxes"
LIT_ATLAS = "--atlas"
LIT_ANALYTIC = "analytic"
LIT_NWB_CSG_SHADOW_ATLAS = "NWB_CSG_SHADOW_ATLAS"
LIT_NWB_CSG_SHADOW_LIGHT = "NWB_CSG_SHADOW_LIGHT"
LIT_MAIN = "__main__"


class CsgShadowImageTests(unittest.TestCase):
    width = 480
    height = 360

    def frames(self, light=LIT_DIRECTIONAL):
        frames = {}
        for arm in ARMS:
            rows = [[(255, 255, 255) for _ in range(self.width)] for _ in range(self.height)]
            for region in REGIONS:
                _, world_x, world_y, _, _ = region
                value = tuple(round(channel) for channel in expected_sample_rgb(world_x, world_y, arm, light))
                for x, y in region_pixels(region, self.width, self.height, 0.3 if arm == LIT_CAMERA_SHIFT else 0.0):
                    rows[y][x] = value
            frames[arm] = self.width, self.height, rows
        return frames

    def replace_region(self, frames, arm, name, value):
        region = next(region for region in REGIONS if region[0] == name)
        for x, y in region_pixels(region, self.width, self.height, 0.3 if arm == LIT_CAMERA_SHIFT else 0.0):
            frames[arm][2][y][x] = value


    def test_unchanged_caster_shadow_cannot_pass_as_cut(self):
        frames = self.frames()
        self.assertTrue(compare_frames(frames))
        frames[LIT_CUT] = frames[LIT_UNCUT]
        with self.assertRaisesRegex(SmokeFailure, "cut/opaque_hole"):
            compare_frames(frames)

    def test_all_lit_cannot_pass_opaque_or_glass(self):
        frames = self.frames()
        self.replace_region(frames, LIT_CUT, "opaque_cap", (255, 255, 255))
        with self.assertRaisesRegex(SmokeFailure, "cut/opaque_cap"):
            compare_frames(frames)

    def test_uncut_thickness_cannot_pass_transparent_depth_cut(self):
        frames = self.frames()
        value = tuple(round(channel) for channel in expected_rgb(2.0, 2.0, 1.0, LIT_DIRECTIONAL))
        self.replace_region(frames, LIT_CUT, LIT_GLASS_DEPTH, value)
        with self.assertRaisesRegex(SmokeFailure, "cut/glass_depth"):
            compare_frames(frames)

    def test_missing_second_volume_cannot_pass_overlap(self):
        frames = self.frames()
        value = tuple(round(channel) for channel in expected_rgb(1.0, 0.0, -1.0, LIT_DIRECTIONAL))
        self.replace_region(frames, LIT_CUT, "overlap", value)
        with self.assertRaisesRegex(SmokeFailure, "cut/overlap"):
            compare_frames(frames)

    def test_old_cutter_pose_fails_even_when_new_hole_is_present(self):
        frames = self.frames()
        self.replace_region(frames, LIT_MOVED, "opaque_old_hole", (255, 255, 255))
        with self.assertRaisesRegex(SmokeFailure, "moved/opaque_old_hole"):
            compare_frames(frames)

    def test_screen_locked_shadow_fails_camera_reprojection(self):
        frames = self.frames()
        frames[LIT_CAMERA_SHIFT] = frames[LIT_CUT]
        with self.assertRaises(SmokeFailure):
            compare_frames(frames)

    def test_missing_arm_or_extent_mismatch_is_rejected(self):
        frames = self.frames()
        del frames["reference"]
        with self.assertRaisesRegex(SmokeFailure, "all five arms"):
            compare_frames(frames)
        frames = self.frames()
        frames[LIT_MOVED] = self.width + 1, self.height, frames[LIT_MOVED][2]
        with self.assertRaisesRegex(SmokeFailure, "different extents"):
            compare_frames(frames)

    def test_capture_policy_overrides_inherited_quality_and_freeze(self):
        with patch.dict(LIT_OS_ENVIRON, {LIT_NWB_SOFTWARE_SHADOW_BACKEND: "trace", LIT_NWB_SHADOW_TRANSPARENT_SAMPLING: "temporal_one",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "8", LIT_NWB_CSG_SHADOW_LIGHT_SOURCE: LIT_FINITE,
            LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH: LIT_CENTER1}, clear=True):
            environment = capture_environment(LIT_MOVED, LIT_POINT)
        self.assertEqual(environment[LIT_NWB_SOFTWARE_SHADOW_BACKEND], "automatic")
        self.assertEqual(environment[LIT_NWB_SHADOW_TRANSPARENT_SAMPLING], "reference_three")
        self.assertEqual(environment[LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE], "every_frame")
        self.assertEqual(environment[LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH], "reference_grid9")
        self.assertEqual(environment[LIT_NWB_CSG_SHADOW_LIGHT_SOURCE], "hard")
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, environment)


    def test_capture_requires_post_motion_settle_interval(self):
        arguments = [LIT_EXECUTABLE, LIT_TEST_EXE, LIT_WORKING_DIRECTORY, ".", LIT_OUTPUT_DIRECTORY, ".", LIT_ROUTE, LIT_SOFTWARE]
        with self.assertRaises(SystemExit):
            parse_args(arguments + ["--frames", "40"])
        with self.assertRaises(SystemExit):
            parse_args(arguments + ["--timeout", "nan"])

    def test_geometric_oracle_counts_overlap_per_instance(self):
        self.assertEqual(reference_optical_length(0.0, -1.0, LIT_CUT, LIT_DIRECTIONAL), 2.0)
        self.assertEqual(reference_optical_length(0.0, -1.0, LIT_UNCUT, LIT_DIRECTIONAL), 4.0)

    def test_fully_subtracted_opaque_volume_has_zero_remaining_length(self):
        self.assertIsNone(reference_optical_length(-1.7, 1.0, LIT_UNCUT, LIT_DIRECTIONAL))
        self.assertEqual(reference_optical_length(-1.7, 1.0, LIT_CUT, LIT_DIRECTIONAL), 0.0)

    def test_parallel_and_oblique_slab_intersections(self):
        self.assertIsNone(ray_box((2.0, 0.0, 0.0), (0.0, 0.0, -1.0), (-1.0, -1.0, -3.0), (1.0, 1.0, -2.0)))
        self.assertEqual(ray_box((0.0, 0.0, 0.0), (0.0, 0.0, -1.0), (-1.0, -1.0, -3.0), (1.0, 1.0, -2.0)), (2.0, 3.0))


class CsgAnalyticShadowImageTests(unittest.TestCase):
    width = 480
    height = 360

    def frames(self):
        frames = {}
        for arm in ANALYTIC_ARMS:
            rows = [[(255, 255, 255) for _ in range(self.width)] for _ in range(self.height)]
            for region in ANALYTIC_REGIONS:
                for x, y in region_pixels(region, self.width, self.height, spacing=ANALYTIC_REGION_SPACING):
                    world_x, world_y = receiver_pixel_world(x, y, self.width, self.height)
                    length = analytic_optical_length(world_x, world_y, arm)
                    rows[y][x] = tuple(round(value) for value in expected_rgb(length, world_x, world_y, LIT_DIRECTIONAL))
            frames[arm] = self.width, self.height, rows
        return frames


    def test_empty_receiver_and_zero_cutter_chord_preserve_optical_length(self):
        self.assertEqual(analytic_optical_length(0.0, 0.0, LIT_CUT), 0.0)
        self.assertEqual(analytic_optical_length(-1.5, -1.0, LIT_CUT), 2.0)

    def test_cutter_union_clips_and_counts_overlap_once(self):
        self.assertAlmostEqual(clipped_union_length([(-6.8, -5.7), (-6.3, -5.2)]), 1.6)
        self.assertEqual(clipped_union_length([(-9.0, -8.0), (-7.5, -6.5), (-6.75, -6.5), (-5.5, -4.0)]), 1.0)


    def test_missing_curvature_axis_length_or_union_cannot_pass(self):
        self.assertTrue(compare_analytic_frames(self.frames()))
        for name, incorrect_length in (("ellipsoid_center", 1.0), (LIT_CAPSULE_ENDCAP, 1.3),
            ("axial_capsule_center", 1.4), ("sphere_union_center", 0.0), (LIT_UNCUT_CONTROL, 0.0)):
            with self.subTest(name=name):
                frames = self.frames()
                region = next(region for region in ANALYTIC_REGIONS if region[0] == name)
                value = tuple(round(channel) for channel in expected_rgb(incorrect_length, region[1], region[2], LIT_DIRECTIONAL))
                for x, y in region_pixels(region, self.width, self.height, spacing=ANALYTIC_REGION_SPACING):
                    frames[LIT_CUT][2][y][x] = value
                with self.assertRaisesRegex(SmokeFailure, "cut/" + name):
                    compare_analytic_frames(frames)

    def test_uncut_atlas_cannot_pass_as_cut_or_missing_pair(self):
        frames = self.frames()
        frames[LIT_CUT] = frames[LIT_UNCUT]
        with self.assertRaisesRegex(SmokeFailure, "cut/plane_left"):
            compare_analytic_frames(frames)
        del frames[LIT_UNCUT]
        with self.assertRaisesRegex(SmokeFailure, "cut/uncut pair"):
            compare_analytic_frames(frames)

    def test_analytic_mode_is_explicit_directional_and_isolated_from_environment(self):
        arguments = [LIT_EXECUTABLE, LIT_TEST_EXE, LIT_WORKING_DIRECTORY, ".", LIT_OUTPUT_DIRECTORY, ".", LIT_ROUTE, LIT_HARDWARE]
        with self.assertRaises(SystemExit):
            parse_args(arguments + [LIT_ATLAS, LIT_ANALYTIC, LIT_LIGHT, LIT_POINT])
        with patch.dict(LIT_OS_ENVIRON, {LIT_NWB_CSG_SHADOW_ATLAS: LIT_BOXES, LIT_NWB_CSG_SHADOW_LIGHT: LIT_POINT}, clear=True):
            environment = capture_environment(LIT_CUT, LIT_DIRECTIONAL, LIT_ANALYTIC)
        self.assertEqual(environment[LIT_NWB_CSG_SHADOW_ATLAS], LIT_ANALYTIC)
        self.assertEqual(environment[LIT_NWB_CSG_SHADOW_LIGHT], LIT_DIRECTIONAL)


if __name__ == LIT_MAIN:
    unittest.main()
