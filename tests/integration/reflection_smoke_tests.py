#!/usr/bin/env python3
"""Exercise reflection image oracles against correct geometry and false-positive failures."""

from html.parser import HTMLParser
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from reflection_image_analysis import ReflectionImageAnalysis, FOREGROUND_REGION, GLASS_REGION, OPAQUE_REGION  # noqa: E402
from reflection_smoke import DEFAULT_RAY_BUDGET, SmokeFailure, capture_environment, write_report  # noqa: E402
from window_capture_image import write_bmp_24  # noqa: E402

LIT_CAPTURES = "captures"

LIT_ONSCREEN = "onscreen"
LIT_SCREEN = "screen"
LIT_RED = "red"
LIT_GREEN = "green"
LIT_ONSCREEN_MOVED = "onscreen_moved"
LIT_BOUNDARY = "boundary"
LIT_FLOOR = "floor"
LIT_DISABLED = "disabled"
LIT_DIRECT = "direct"
LIT_REFLECTION = "reflection"
LIT_MOVED = "moved"
LIT_PROJECTED_AREA = "projected area"
LIT_GLASS_SPHERE = "glass sphere"
LIT_MISSING_RED_REFLECTION = "missing red reflection"
LIT_HYBRID = "hybrid"
LIT_HARDWARE = "hardware"
LIT_NWB_REFLECTION_SMOKE_TIMING = "NWB_REFLECTION_SMOKE_TIMING"
LIT_NWB_REFRACTION_SMOKE_ENABLED = "NWB_REFRACTION_SMOKE_ENABLED"
LIT_NWB_REFLECTION_SMOKE_DEBUG = "NWB_REFLECTION_SMOKE_DEBUG"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"


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
        rows = [[(160, 160, 160) if x % 20 < 10 else (10, 10, 10) for x in range(self.width)] for _ in range(self.height)]
        for x, y in ReflectionImageAnalysis.region_pixels(self.width, self.height, FOREGROUND_REGION):
            rows[y][x] = (10, 20, 220)
        reflected = [list(row) for row in rows]
        for region in (OPAQUE_REGION, GLASS_REGION):
            left, top = int(region[0] * self.width) + 8, int(region[1] * self.height) + 8
            for y in range(top, top + 15):
                for x in range(left, left + 20):
                    reflected[y][x] = (180, 5, 5)
        return (self.width, self.height, rows), (self.width, self.height, reflected)

    def panel_frame(self, case=LIT_ONSCREEN, mode=LIT_SCREEN, reflection_shift=None):
        rows = [[(20, 20, 20) for _ in range(self.width)] for _ in range(self.height)]
        # Independently rounded projections at 400x300, computed from authored world-space rectangles.
        direct = {LIT_RED: (53, 185, 26, 22), LIT_GREEN: (347, 98, 26, 30)}
        reflected = {LIT_RED: (151, 162, 9, 7), LIT_GREEN: (249, 133, 9, 10)}
        if case == LIT_ONSCREEN_MOVED:
            direct = {LIT_RED: (74, 185, 26, 22), LIT_GREEN: (326, 98, 26, 30)}
            reflected = {LIT_RED: (158, 162, 9, 7), LIT_GREEN: (242, 133, 9, 10)}
        elif case == LIT_BOUNDARY:
            direct.pop(LIT_GREEN)
            reflected[LIT_GREEN] = (287, 133, 9, 10)
            if mode == LIT_SCREEN:
                reflected.pop(LIT_GREEN)
        elif case == LIT_FLOOR:
            direct = {LIT_RED: (151, 133, 9, 7), LIT_GREEN: (249, 133, 9, 10)}
            reflected = {LIT_RED: (151, 248, 9, 7), LIT_GREEN: (249, 248, 9, 10)}
        if mode == LIT_DISABLED:
            reflected.clear()
        for label, regions in ((LIT_DIRECT, direct), (LIT_REFLECTION, reflected)):
            for color, (cx, cy, half_width, half_height) in regions.items():
                if label == LIT_REFLECTION and reflection_shift:
                    cx += reflection_shift[0]
                    cy += reflection_shift[1]
                # Half-strength provisional screen radiance still has to retain visible, localized marker color.
                value = 90 if mode == LIT_SCREEN and label == LIT_REFLECTION else 180
                rgb = (value, 5, 5) if color == LIT_RED else (5, value, 5)
                for y in range(cy - half_height, cy + half_height + 1):
                    for x in range(cx - half_width, cx + half_width + 1):
                        rows[y][x] = rgb
        return self.width, self.height, rows

    def test_flat_tint_cannot_pass_as_localized_reflection(self):
        frame = self.marker_frame()
        frame[2][:] = [[(180, 5, 5)] * self.width for _ in range(self.height)]
        with self.assertRaisesRegex(SmokeFailure, "predicted marker region"):
            ReflectionImageAnalysis.analyze_markers(frame)

    def test_missing_or_stationary_marker_fails(self):
        with self.assertRaises(SmokeFailure):
            ReflectionImageAnalysis.compare_marker_motion(self.marker_frame(), self.marker_frame())
        frame = self.marker_frame()
        for row in frame[2]:
            row[:] = [(20, 20, 20) if pixel[1] > 100 else pixel for pixel in row]
        with self.assertRaisesRegex(SmokeFailure, "missing.*green"):
            ReflectionImageAnalysis.analyze_markers(frame)

    def test_correctly_located_sparse_marker_pixels_fail_area_requirement(self):
        frame = self.marker_frame()
        kept = 0
        for row in frame[2]:
            for x, pixel in enumerate(row):
                if pixel[0] > 100:
                    kept += 1
                    if kept > 90:
                        row[x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, LIT_PROJECTED_AREA):
            ReflectionImageAnalysis.analyze_markers(frame)

    def test_offscreen_marker_cannot_exist_in_disabled_or_screen_frame(self):
        with self.assertRaisesRegex(SmokeFailure, "without a hardware"):
            ReflectionImageAnalysis.analyze_markers(self.marker_frame(), required=False)
        blank = (self.width, self.height, [[(20, 20, 20)] * self.width for _ in range(self.height)])
        self.assertEqual(ReflectionImageAnalysis.analyze_markers(blank, required=False)[LIT_RED]["pixels"], 0)

    def test_missing_glass_reflection_fails(self):
        before, after = self.glass_frames()
        for x, y in ReflectionImageAnalysis.region_pixels(self.width, self.height, GLASS_REGION):
            after[2][y][x] = before[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, LIT_GLASS_SPHERE):
            ReflectionImageAnalysis.compare_opaque_glass(before, after)

    def test_sparse_receiver_reflection_fails_region_coverage(self):
        before, after = self.glass_frames()
        kept = 0
        for x, y in ReflectionImageAnalysis.region_pixels(self.width, self.height, GLASS_REGION):
            if after[2][y][x] != before[2][y][x]:
                kept += 1
                if kept > 55:
                    after[2][y][x] = before[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, LIT_GLASS_SPHERE):
            ReflectionImageAnalysis.compare_opaque_glass(before, after)

    def test_expanded_foreground_fails_even_when_original_strip_is_retained(self):
        before, after = self.glass_frames()
        top = int(FOREGROUND_REGION[1] * self.height)
        for x, y in ReflectionImageAnalysis.region_pixels(self.width, self.height, FOREGROUND_REGION):
            if y < top + 4:
                before[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "expanded"):
            ReflectionImageAnalysis.compare_opaque_glass(before, after)

    def test_foreground_and_exterior_corruption_fail(self):
        before, after = self.glass_frames()
        for x, y in ReflectionImageAnalysis.region_pixels(self.width, self.height, FOREGROUND_REGION):
            after[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "foreground AVBOIT"):
            ReflectionImageAnalysis.compare_opaque_glass(before, after)
        before, after = self.glass_frames()
        for y in range(40, 80):
            for x in range(15, 55):
                after[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "outside"):
            ReflectionImageAnalysis.compare_opaque_glass(before, after)

    def test_malformed_rows_fail(self):
        frame = self.marker_frame()
        frame[2].pop()
        with self.assertRaisesRegex(SmokeFailure, "malformed"):
            ReflectionImageAnalysis.analyze_markers(frame)

    def test_direct_image_cannot_pass_as_screen_reflection(self):
        for case in (LIT_ONSCREEN, LIT_FLOOR):
            with self.subTest(case=case), self.assertRaisesRegex(SmokeFailure, LIT_MISSING_RED_REFLECTION):
                ReflectionImageAnalysis.analyze_panels(self.panel_frame(case, LIT_DISABLED), case, LIT_SCREEN)

    def test_floor_reflection_must_appear_below_its_direct_source(self):
        with self.assertRaisesRegex(SmokeFailure, LIT_MISSING_RED_REFLECTION):
            ReflectionImageAnalysis.analyze_panels(self.panel_frame(LIT_FLOOR, reflection_shift=(0, -115)), LIT_FLOOR, LIT_SCREEN)

    def test_displaced_reflection_cannot_pass_the_panel_projection(self):
        with self.assertRaisesRegex(SmokeFailure, "centroid|missing red reflection"):
            ReflectionImageAnalysis.analyze_panels(self.panel_frame(reflection_shift=(8, 0)), LIT_ONSCREEN, LIT_SCREEN)

    def test_sparse_correctly_located_panel_fails_projected_area(self):
        frame = self.panel_frame()
        for y in range(150, 174):
            for x in range(138, 164):
                if not (149 <= x <= 153 and 160 <= y <= 164):
                    frame[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, LIT_PROJECTED_AREA):
            ReflectionImageAnalysis.analyze_panels(frame, LIT_ONSCREEN, LIT_SCREEN)

    def test_boundary_screen_must_lose_offscreen_green_and_hybrid_must_retain_it(self):
        with self.assertRaisesRegex(SmokeFailure, "unexpected green reflection"):
            ReflectionImageAnalysis.analyze_panels(self.panel_frame(LIT_BOUNDARY, LIT_HYBRID), LIT_BOUNDARY, LIT_SCREEN)
        with self.assertRaisesRegex(SmokeFailure, "missing green reflection"):
            ReflectionImageAnalysis.analyze_panels(self.panel_frame(LIT_BOUNDARY, LIT_SCREEN), LIT_BOUNDARY, LIT_HYBRID)

    def test_panel_colors_outside_both_predicted_regions_fail(self):
        frame = self.panel_frame()
        for y in range(230, 250):
            for x in range(10, 30):
                frame[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "outside direct/reflected"):
            ReflectionImageAnalysis.analyze_panels(frame, LIT_ONSCREEN, LIT_SCREEN)

    def test_wrong_reflected_motion_fails_even_within_static_centroid_tolerance(self):
        moved = self.panel_frame(LIT_ONSCREEN_MOVED, reflection_shift=(-3, 0))
        ReflectionImageAnalysis.analyze_panels(moved, LIT_ONSCREEN_MOVED, LIT_SCREEN)
        with self.assertRaisesRegex(SmokeFailure, "authored inward movement"):
            ReflectionImageAnalysis.compare_panel_motion(self.panel_frame(), moved)

    def test_capture_environment_removes_inherited_fixture_controls(self):
        with patch.dict(os.environ, {LIT_NWB_REFLECTION_SMOKE_TIMING: "1", LIT_NWB_REFRACTION_SMOKE_ENABLED: "0", LIT_NWB_REFLECTION_SMOKE_DEBUG: "source", LIT_NWB_GPU_TIMING_FILE: "unexpected.txt", LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "1"}):
            result = capture_environment(LIT_MOVED, LIT_HARDWARE)
        self.assertNotIn(LIT_NWB_REFLECTION_SMOKE_TIMING, result)
        self.assertNotIn(LIT_NWB_REFLECTION_SMOKE_DEBUG, result)
        self.assertNotIn(LIT_NWB_REFRACTION_SMOKE_ENABLED, result)
        self.assertNotIn(LIT_NWB_GPU_TIMING_FILE, result)
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, result)

    def test_report_resolves_escaped_log_links_to_manifest_capture_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = SimpleNamespace(output_directory=root, frames=16, require_hardware=True, application_arg=[], suite="baseline")
            path = root / "odd&case_hybrid.bmp"
            write_bmp_24(path, 1, 1, [[(20, 40, 60)]])
            write_report(args, [("odd&case", LIT_HYBRID, DEFAULT_RAY_BUDGET)])
            manifest = json.loads((root / "reflection_manifest.json").read_text(encoding=LIT_UTF_8))
            document = (root / "reflection.html").read_text(encoding=LIT_UTF_8)
            elements = []
            parser = HTMLParser()
            parser.handle_starttag = lambda tag, attributes: elements.append((tag, dict(attributes)))
            parser.feed(document)
            links = [attributes["href"] for tag, attributes in elements if tag == "a"]
            self.assertEqual(links, [manifest[LIT_CAPTURES][0]["file"], manifest[LIT_CAPTURES][0]["log"]])
            self.assertIn('href="odd&amp;case_hybrid.log"', document)


if __name__ == LIT_MAIN:
    unittest.main()
