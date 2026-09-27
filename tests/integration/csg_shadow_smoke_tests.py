#!/usr/bin/env python3
"""Exercise the image oracle with missing cuts, thickness, overlap and stale-view failures."""

import math
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from csg_shadow_reference import (  # noqa: E402
    ANALYTIC_ARMS, ANALYTIC_REGIONS, ANALYTIC_REGION_SPACING, ARMS, REGIONS, analytic_optical_length,
    clipped_union_length, compare_analytic_frames, compare_frames, expected_rgb, expected_sample_rgb, ray_box,
    receiver_pixel_world, reference_optical_length, region_pixels,
)
import csg_shadow_smoke as smoke  # noqa: E402
from csg_shadow_smoke import capture_environment, parse_args  # noqa: E402
from window_capture_smoke import SmokeFailure  # noqa: E402


class CsgShadowImageTests(unittest.TestCase):
    width = 480
    height = 360

    def frames(self, light="directional"):
        frames = {}
        for arm in ARMS:
            rows = [[(255, 255, 255) for _ in range(self.width)] for _ in range(self.height)]
            for region in REGIONS:
                _, world_x, world_y, _, _ = region
                value = tuple(round(channel) for channel in expected_sample_rgb(world_x, world_y, arm, light))
                for x, y in region_pixels(region, self.width, self.height, 0.3 if arm == "camera_shift" else 0.0):
                    rows[y][x] = value
            frames[arm] = self.width, self.height, rows
        return frames

    def replace_region(self, frames, arm, name, value):
        region = next(region for region in REGIONS if region[0] == name)
        for x, y in region_pixels(region, self.width, self.height, 0.3 if arm == "camera_shift" else 0.0):
            frames[arm][2][y][x] = value

    def test_directional_and_point_oracles_accept_complete_signal(self):
        for light in ("directional", "point"):
            with self.subTest(light=light):
                result = compare_frames(self.frames(light), light)
                self.assertEqual(len(result["regions"]), 5)
                self.assertGreater(result["regions"]["cut"]["glass_depth"]["rgb"][0],
                    result["regions"]["cut"]["glass_control"]["rgb"][0])

    def test_unchanged_caster_shadow_cannot_pass_as_cut(self):
        frames = self.frames()
        frames["cut"] = frames["uncut"]
        with self.assertRaisesRegex(SmokeFailure, "cut/opaque_hole"):
            compare_frames(frames)

    def test_all_lit_cannot_pass_opaque_or_glass(self):
        frames = self.frames()
        self.replace_region(frames, "cut", "opaque_cap", (255, 255, 255))
        with self.assertRaisesRegex(SmokeFailure, "cut/opaque_cap"):
            compare_frames(frames)

    def test_uncut_thickness_cannot_pass_transparent_depth_cut(self):
        frames = self.frames()
        value = tuple(round(channel) for channel in expected_rgb(2.0, 2.0, 1.0, "directional"))
        self.replace_region(frames, "cut", "glass_depth", value)
        with self.assertRaisesRegex(SmokeFailure, "cut/glass_depth"):
            compare_frames(frames)

    def test_missing_second_volume_cannot_pass_overlap(self):
        frames = self.frames()
        value = tuple(round(channel) for channel in expected_rgb(1.0, 0.0, -1.0, "directional"))
        self.replace_region(frames, "cut", "overlap", value)
        with self.assertRaisesRegex(SmokeFailure, "cut/overlap"):
            compare_frames(frames)

    def test_old_cutter_pose_fails_even_when_new_hole_is_present(self):
        frames = self.frames()
        self.replace_region(frames, "moved", "opaque_old_hole", (255, 255, 255))
        with self.assertRaisesRegex(SmokeFailure, "moved/opaque_old_hole"):
            compare_frames(frames)

    def test_screen_locked_shadow_fails_camera_reprojection(self):
        frames = self.frames()
        frames["camera_shift"] = frames["cut"]
        with self.assertRaises(SmokeFailure):
            compare_frames(frames)

    def test_missing_arm_or_extent_mismatch_is_rejected(self):
        frames = self.frames()
        del frames["reference"]
        with self.assertRaisesRegex(SmokeFailure, "all five arms"):
            compare_frames(frames)
        frames = self.frames()
        frames["moved"] = self.width + 1, self.height, frames["moved"][2]
        with self.assertRaisesRegex(SmokeFailure, "different extents"):
            compare_frames(frames)

    def test_capture_policy_overrides_inherited_quality_and_freeze(self):
        with patch.dict("os.environ", {"NWB_SOFTWARE_SHADOW_BACKEND": "trace", "NWB_SHADOW_TRANSPARENT_SAMPLING": "temporal_one",
            "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "8", "NWB_CSG_SHADOW_LIGHT_SOURCE": "finite",
            "NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH": "center1"}, clear=True):
            environment = capture_environment("moved", "point")
        self.assertEqual(environment["NWB_SOFTWARE_SHADOW_BACKEND"], "automatic")
        self.assertEqual(environment["NWB_SHADOW_TRANSPARENT_SAMPLING"], "reference_three")
        self.assertEqual(environment["NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE"], "every_frame")
        self.assertEqual(environment["NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH"], "reference_grid9")
        self.assertEqual(environment["NWB_CSG_SHADOW_LIGHT_SOURCE"], "hard")
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", environment)

    def test_approximate_capture_preserves_explicit_quality_settings(self):
        environment = capture_environment("cut", "point", cadence="reuse_one_frame", map_resolution_divisor=2)
        self.assertEqual(environment["NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE"], "reuse_one_frame")
        self.assertEqual(environment["NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION"], "256")
        self.assertEqual(environment["NWB_SOFTWARE_SHADOW_POINT_RESOLUTION"], "128")

    def test_two_frame_capture_requires_the_matching_accepted_runtime_cadence(self):
        arguments = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", "."]
        for route in ("hardware", "software"):
            args = parse_args(arguments + ["--route", route, "--capture-cadence", "reuse_two_frames"])
            with patch.object(smoke.subprocess, "run", return_value=Mock(returncode=0)) as run, \
                patch.object(smoke, "read_bmp_24_rows", return_value=(960, 720, [])):
                smoke.capture_arm(args, "cut")
            command = run.call_args.args[0]
            expected = [command[index + 1] for index, value in enumerate(command[:-1]) if value == "--expect-log-message"]
            rejected = [command[index + 1] for index, value in enumerate(command[:-1]) if value == "--reject-log-message"]
            self.assertIn("RendererSystem: accepted light-space capture reuse (cadence=3)", expected)
            self.assertIn("RendererSystem: accepted light-space capture reuse (cadence=2)", rejected)
            self.assertEqual(run.call_args.kwargs["env"]["NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE"], "reuse_two_frames")

    def test_center_blocker_capture_requires_finite_source_and_requested_policy_evidence(self):
        arguments = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", "."]
        for route in ("hardware", "software"):
            for light in ("directional", "point"):
                with self.subTest(route=route, light=light):
                    args = parse_args(arguments + ["--route", route, "--light", light,
                        "--blocker-search", "center1", "--light-source", "finite"])
                    with patch.object(smoke.subprocess, "run", return_value=Mock(returncode=0)) as run, \
                        patch.object(smoke, "read_bmp_24_rows", return_value=(960, 720, [])):
                        smoke.capture_arm(args, "cut")
                    command = run.call_args.args[0]
                    expected = [command[index + 1] for index, value in enumerate(command[:-1]) if value == "--expect-log-message"]
                    self.assertIn("SoftwareShadowSmoke: requested backend=0 directional_resolution=512 point_resolution=256 "
                        "budget_bytes=268435456 coverage=0 blocker_search=2 capture_cadence=0", expected)
                    self.assertIn("CsgShadowSmokeProject: light_source=finite angular_radius="
                        + ("0.005 source_radius=0.000" if light == "directional" else "0.000 source_radius=0.020"), expected)
                    self.assertEqual(run.call_args.kwargs["env"]["NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH"], "center1")
                    self.assertEqual(run.call_args.kwargs["env"]["NWB_CSG_SHADOW_LIGHT_SOURCE"], "finite")

    def test_capture_requires_post_motion_settle_interval(self):
        arguments = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", ".", "--route", "software"]
        with self.assertRaises(SystemExit):
            parse_args(arguments + ["--frames", "40"])
        with self.assertRaises(SystemExit):
            parse_args(arguments + ["--timeout", "nan"])

    def test_geometric_oracle_counts_overlap_per_instance(self):
        self.assertEqual(reference_optical_length(0.0, -1.0, "cut", "directional"), 2.0)
        self.assertEqual(reference_optical_length(0.0, -1.0, "uncut", "directional"), 4.0)
        self.assertEqual(reference_optical_length(2.0, 1.0, "cut", "directional"), 1.0)
        self.assertEqual(reference_optical_length(-1.7, 1.0, "cut", "directional"), 0.0)
        self.assertIsNone(reference_optical_length(-1.7, 1.0, "uncut", "directional"))

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
                    rows[y][x] = tuple(round(value) for value in expected_rgb(length, world_x, world_y, "directional"))
            frames[arm] = self.width, self.height, rows
        return frames

    def test_closed_form_depths_cover_plane_scale_capsule_axes_and_union(self):
        cases = (
            (-2.3, 1.0, 0.85), (-1.7, 1.0, 1.15),
            (0.0, 1.0, 0.5), (0.25, 1.0, 2.0 - 1.5 * math.sqrt(0.75)), (0.61, 1.0, 2.0),
            (2.0, 1.0, 1.3), (2.0, 1.52, 2.0 - 2.0 * math.sqrt(0.35 ** 2 - 0.27 ** 2)),
            (-2.0, -1.0, 0.6), (-1.82, -1.0, 0.72),
            (0.0, -1.0, 0.4), (0.3, -1.0, 1.5 - 2.0 * math.sqrt(0.55 ** 2 - 0.3 ** 2)),
            (2.0, -1.0, 2.0),
        )
        for x, y, length in cases:
            with self.subTest(x=x, y=y):
                self.assertAlmostEqual(analytic_optical_length(x, y, "cut"), length)
                self.assertEqual(analytic_optical_length(x, y, "uncut"), 2.0)
        self.assertEqual(analytic_optical_length(0.0, 0.0, "cut"), 0.0)
        self.assertEqual(analytic_optical_length(-1.5, -1.0, "cut"), 2.0)

    def test_cutter_union_clips_and_counts_overlap_once(self):
        self.assertAlmostEqual(clipped_union_length([(-6.8, -5.7), (-6.3, -5.2)]), 1.6)
        self.assertEqual(clipped_union_length([(-9.0, -8.0), (-7.5, -6.5), (-6.75, -6.5), (-5.5, -4.0)]), 1.0)

    def test_analytic_pair_accepts_pixel_averaged_curved_cavities(self):
        result = compare_analytic_frames(self.frames())
        self.assertEqual(set(result["regions"]), set(ANALYTIC_ARMS))
        self.assertEqual(result["regions"]["cut"]["uncut_control"]["rgb"],
            result["regions"]["uncut"]["uncut_control"]["rgb"])
        self.assertGreater(result["regions"]["cut"]["capsule_body"]["rgb"][0],
            result["regions"]["cut"]["capsule_endcap"]["rgb"][0])

    def test_missing_curvature_axis_length_or_union_cannot_pass(self):
        for name, incorrect_length in (("ellipsoid_center", 1.0), ("capsule_endcap", 1.3),
            ("axial_capsule_center", 1.4), ("sphere_union_center", 0.0), ("uncut_control", 0.0)):
            with self.subTest(name=name):
                frames = self.frames()
                region = next(region for region in ANALYTIC_REGIONS if region[0] == name)
                value = tuple(round(channel) for channel in expected_rgb(incorrect_length, region[1], region[2], "directional"))
                for x, y in region_pixels(region, self.width, self.height, spacing=ANALYTIC_REGION_SPACING):
                    frames["cut"][2][y][x] = value
                with self.assertRaisesRegex(SmokeFailure, "cut/" + name):
                    compare_analytic_frames(frames)

    def test_uncut_atlas_cannot_pass_as_cut_or_missing_pair(self):
        frames = self.frames()
        frames["cut"] = frames["uncut"]
        with self.assertRaisesRegex(SmokeFailure, "cut/plane_left"):
            compare_analytic_frames(frames)
        del frames["uncut"]
        with self.assertRaisesRegex(SmokeFailure, "cut/uncut pair"):
            compare_analytic_frames(frames)

    def test_analytic_mode_is_explicit_directional_and_isolated_from_environment(self):
        arguments = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", ".", "--route", "hardware"]
        self.assertEqual(parse_args(arguments).atlas, "boxes")
        self.assertEqual(parse_args(arguments + ["--atlas", "analytic"]).atlas, "analytic")
        with self.assertRaises(SystemExit):
            parse_args(arguments + ["--atlas", "analytic", "--light", "point"])
        with patch.dict("os.environ", {"NWB_CSG_SHADOW_ATLAS": "boxes", "NWB_CSG_SHADOW_LIGHT": "point"}, clear=True):
            environment = capture_environment("cut", "directional", "analytic")
        self.assertEqual(environment["NWB_CSG_SHADOW_ATLAS"], "analytic")
        self.assertEqual(environment["NWB_CSG_SHADOW_LIGHT"], "directional")


if __name__ == "__main__":
    unittest.main()
