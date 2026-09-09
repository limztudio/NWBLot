#!/usr/bin/env python3
"""Exercise reflection image oracles against correct geometry and false-positive failures."""

import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from reflection_smoke import (  # noqa: E402
    BASELINE_CAPTURES, CAPTURES, FOREGROUND_REGION, GLASS_REGION, OPAQUE_REGION, SCREEN_CAPTURES,
    SmokeFailure, analyze_markers, analyze_panels, capture_environment, compare_marker_motion,
    compare_opaque_glass, compare_panel_motion, region_pixels,
)


class ReflectionSmokeAnalysisTests(unittest.TestCase):
    width = 400
    height = 300

    def marker_frame(self, moved=False):
        rows = [[(20, 20, 20) for _ in range(self.width)] for _ in range(self.height)]
        # Independent projected positions for a 60-degree vertical FOV, 14-unit virtual-source distance.
        shift = 17 if moved else 0
        for cx, cy, color in ((170 + shift, 157, (180, 5, 5)), (230 + shift, 139, (5, 180, 5))):
            for y in range(cy - 9, cy + 10):
                for x in range(cx - 9, cx + 10):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= 81:
                        rows[y][x] = color
        return self.width, self.height, rows

    def glass_frames(self):
        rows = [[(160, 160, 160) if x % 20 < 10 else (10, 10, 10)
            for x in range(self.width)] for _ in range(self.height)]
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            rows[y][x] = (10, 20, 220)
        reflected = [list(row) for row in rows]
        for region in (OPAQUE_REGION, GLASS_REGION):
            left, top = int(region[0] * self.width) + 8, int(region[1] * self.height) + 8
            for y in range(top, top + 15):
                for x in range(left, left + 20):
                    reflected[y][x] = (180, 5, 5)
        return (self.width, self.height, rows), (self.width, self.height, reflected)

    def panel_frame(self, case="onscreen", mode="screen", reflection_shift=None):
        rows = [[(20, 20, 20) for _ in range(self.width)] for _ in range(self.height)]
        # Independently rounded projections at 400x300, computed from authored world-space rectangles.
        direct = {"red": (53, 185, 26, 22), "green": (347, 98, 26, 30)}
        reflected = {"red": (151, 162, 9, 7), "green": (249, 133, 9, 10)}
        if case == "onscreen_moved":
            direct = {"red": (74, 185, 26, 22), "green": (326, 98, 26, 30)}
            reflected = {"red": (158, 162, 9, 7), "green": (242, 133, 9, 10)}
        elif case == "boundary":
            direct.pop("green")
            reflected["green"] = (287, 133, 9, 10)
            if mode == "screen":
                reflected.pop("green")
        elif case == "floor":
            direct = {"red": (151, 133, 9, 7), "green": (249, 133, 9, 10)}
            reflected = {"red": (151, 248, 9, 7), "green": (249, 248, 9, 10)}
        if mode == "disabled":
            reflected.clear()
        for label, regions in (("direct", direct), ("reflection", reflected)):
            for color, (cx, cy, half_width, half_height) in regions.items():
                if label == "reflection" and reflection_shift:
                    cx += reflection_shift[0]
                    cy += reflection_shift[1]
                # Half-strength provisional screen radiance still has to retain visible, localized marker color.
                value = 90 if mode == "screen" and label == "reflection" else 180
                rgb = (value, 5, 5) if color == "red" else (5, value, 5)
                for y in range(cy - half_height, cy + half_height + 1):
                    for x in range(cx - half_width, cx + half_width + 1):
                        rows[y][x] = rgb
        return self.width, self.height, rows

    def test_projected_two_color_markers_and_known_motion_pass(self):
        result = compare_marker_motion(self.marker_frame(), self.marker_frame(moved=True))
        self.assertEqual(result["moved"]["red"]["measured_motion"], [17.0, 0.0])

    def test_flat_tint_cannot_pass_as_localized_reflection(self):
        frame = self.marker_frame()
        frame[2][:] = [[(180, 5, 5)] * self.width for _ in range(self.height)]
        with self.assertRaisesRegex(SmokeFailure, "predicted marker region"):
            analyze_markers(frame)

    def test_missing_or_stationary_marker_fails(self):
        with self.assertRaises(SmokeFailure):
            compare_marker_motion(self.marker_frame(), self.marker_frame())
        frame = self.marker_frame()
        for row in frame[2]:
            row[:] = [(20, 20, 20) if pixel[1] > 100 else pixel for pixel in row]
        with self.assertRaisesRegex(SmokeFailure, "missing.*green"):
            analyze_markers(frame)

    def test_correctly_located_sparse_marker_pixels_fail_area_requirement(self):
        frame = self.marker_frame()
        kept = 0
        for row in frame[2]:
            for x, pixel in enumerate(row):
                if pixel[0] > 100:
                    kept += 1
                    if kept > 90:
                        row[x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "projected area"):
            analyze_markers(frame)

    def test_offscreen_marker_cannot_exist_in_disabled_or_screen_frame(self):
        with self.assertRaisesRegex(SmokeFailure, "without a hardware"):
            analyze_markers(self.marker_frame(), required=False)
        blank = (self.width, self.height, [[(20, 20, 20)] * self.width for _ in range(self.height)])
        self.assertEqual(analyze_markers(blank, required=False)["red"]["pixels"], 0)

    def test_both_receiver_types_preserve_foreground_and_exterior(self):
        result = compare_opaque_glass(*self.glass_frames())
        self.assertEqual(result["opaque_new_reflected_pixels"], 300)
        self.assertEqual(result["glass_new_reflected_pixels"], 300)
        self.assertEqual(result["foreground_pixels"], result["foreground_retained"])
        self.assertEqual(result["exterior_changed"], 0)

    def test_missing_glass_reflection_fails(self):
        before, after = self.glass_frames()
        for x, y in region_pixels(self.width, self.height, GLASS_REGION):
            after[2][y][x] = before[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "glass sphere"):
            compare_opaque_glass(before, after)

    def test_sparse_receiver_reflection_fails_region_coverage(self):
        before, after = self.glass_frames()
        kept = 0
        for x, y in region_pixels(self.width, self.height, GLASS_REGION):
            if after[2][y][x] != before[2][y][x]:
                kept += 1
                if kept > 55:
                    after[2][y][x] = before[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "glass sphere"):
            compare_opaque_glass(before, after)

    def test_expanded_foreground_fails_even_when_original_strip_is_retained(self):
        before, after = self.glass_frames()
        top = int(FOREGROUND_REGION[1] * self.height)
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            if y < top + 4:
                before[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "expanded"):
            compare_opaque_glass(before, after)

    def test_foreground_and_exterior_corruption_fail(self):
        before, after = self.glass_frames()
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            after[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "foreground AVBOIT"):
            compare_opaque_glass(before, after)
        before, after = self.glass_frames()
        for y in range(40, 80):
            for x in range(15, 55):
                after[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "outside"):
            compare_opaque_glass(before, after)

    def test_malformed_rows_fail(self):
        frame = self.marker_frame()
        frame[2].pop()
        with self.assertRaisesRegex(SmokeFailure, "malformed"):
            analyze_markers(frame)

    def test_all_panel_route_geometries_pass(self):
        for case, mode in SCREEN_CAPTURES:
            with self.subTest(case=case, mode=mode):
                result = analyze_panels(self.panel_frame(case, mode), case, mode)
                self.assertEqual(result["red"]["outside_predicted_regions"], 0)
                self.assertEqual(result["green"]["outside_predicted_regions"], 0)

    def test_direct_image_cannot_pass_as_screen_reflection(self):
        for case in ("onscreen", "floor"):
            with self.subTest(case=case), self.assertRaisesRegex(SmokeFailure, "missing red reflection"):
                analyze_panels(self.panel_frame(case, "disabled"), case, "screen")

    def test_floor_reflection_must_appear_below_its_direct_source(self):
        with self.assertRaisesRegex(SmokeFailure, "missing red reflection"):
            analyze_panels(self.panel_frame("floor", reflection_shift=(0, -115)), "floor", "screen")

    def test_displaced_reflection_cannot_pass_the_panel_projection(self):
        with self.assertRaisesRegex(SmokeFailure, "centroid|missing red reflection"):
            analyze_panels(self.panel_frame(reflection_shift=(8, 0)), "onscreen", "screen")

    def test_sparse_correctly_located_panel_fails_projected_area(self):
        frame = self.panel_frame()
        for y in range(150, 174):
            for x in range(138, 164):
                if not (149 <= x <= 153 and 160 <= y <= 164):
                    frame[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "projected area"):
            analyze_panels(frame, "onscreen", "screen")

    def test_boundary_screen_must_lose_offscreen_green_and_hybrid_must_retain_it(self):
        with self.assertRaisesRegex(SmokeFailure, "unexpected green reflection"):
            analyze_panels(self.panel_frame("boundary", "hybrid"), "boundary", "screen")
        with self.assertRaisesRegex(SmokeFailure, "missing green reflection"):
            analyze_panels(self.panel_frame("boundary", "screen"), "boundary", "hybrid")

    def test_panel_colors_outside_both_predicted_regions_fail(self):
        frame = self.panel_frame()
        for y in range(230, 250):
            for x in range(10, 30):
                frame[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "outside direct/reflected"):
            analyze_panels(frame, "onscreen", "screen")

    def test_inward_panel_motion_matches_distinct_direct_and_reflected_scales(self):
        result = compare_panel_motion(self.panel_frame(), self.panel_frame("onscreen_moved"))
        self.assertEqual(result["moved"]["red"]["direct"]["measured_motion"], [21.0, 0.0])
        self.assertEqual(result["moved"]["red"]["reflection"]["measured_motion"], [7.0, 0.0])
        self.assertEqual(result["moved"]["green"]["direct"]["measured_motion"], [-21.0, 0.0])
        self.assertEqual(result["moved"]["green"]["reflection"]["measured_motion"], [-7.0, 0.0])

    def test_wrong_reflected_motion_fails_even_within_static_centroid_tolerance(self):
        moved = self.panel_frame("onscreen_moved", reflection_shift=(-3, 0))
        analyze_panels(moved, "onscreen_moved", "screen")
        with self.assertRaisesRegex(SmokeFailure, "authored inward movement"):
            compare_panel_motion(self.panel_frame(), moved)

    def test_capture_matrix_keeps_baseline_and_four_floor_routes(self):
        self.assertEqual(len(CAPTURES), 20)
        self.assertEqual(len(set(CAPTURES)), len(CAPTURES))
        self.assertEqual(set(CAPTURES), set(BASELINE_CAPTURES) | set(SCREEN_CAPTURES))
        self.assertEqual({mode for case, mode in CAPTURES if case == "floor"},
            {"disabled", "screen", "hardware", "hybrid"})
        self.assertIn(("offscreen", "hybrid"), CAPTURES)

    def test_capture_environment_removes_inherited_fixture_controls(self):
        with patch.dict(os.environ, {"NWB_REFLECTION_SMOKE_TIMING": "1", "NWB_REFRACTION_SMOKE_ENABLED": "0",
            "NWB_REFLECTION_SMOKE_DEBUG": "source",
            "NWB_GPU_TIMING_FILE": "unexpected.txt", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "1"}):
            result = capture_environment("moved", "hardware")
        self.assertEqual(result["NWB_REFLECTION_SMOKE_CASE"], "moved")
        self.assertEqual(result["NWB_REFLECTION_SMOKE_MODE"], "hardware")
        self.assertEqual(result["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"], "0.016666667")
        self.assertNotIn("NWB_REFLECTION_SMOKE_TIMING", result)
        self.assertNotIn("NWB_REFLECTION_SMOKE_DEBUG", result)
        self.assertNotIn("NWB_REFRACTION_SMOKE_ENABLED", result)
        self.assertNotIn("NWB_GPU_TIMING_FILE", result)
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", result)


if __name__ == "__main__":
    unittest.main()
