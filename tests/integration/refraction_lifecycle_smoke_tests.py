#!/usr/bin/env python3
"""Check that lifecycle acceptance rejects stale pixels, missed edge tiles, and reordered readbacks."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from refraction_lifecycle_smoke import (  # noqa: E402
    MOVED_NEW_REGION, PHASES, SmokeFailure, require_displacement, require_foreground, require_partial_tile_displacement, require_unchanged, validate_lifecycle_log,
)
from refraction_capture_smoke import FOREGROUND_REGION, region_pixels  # noqa: E402


class RefractionLifecycleAnalysisTests(unittest.TestCase):
    def frame(self, width=160, height=120):
        rows = [[(235, 235, 235) if (x // 4) % 2 else (20, 20, 20) for x in range(width)] for _ in range(height)]
        for x, y in region_pixels(width, height, FOREGROUND_REGION):
            rows[y][x] = (225, 35, 35)
        return width, height, rows

    def test_stale_pixels_in_empty_or_disabled_frame_fail(self):
        baseline = self.frame()
        require_unchanged(baseline, baseline, (0, 0, 1, 1), "empty frame")
        corrupted = self.frame()
        for y in range(40, 70):
            for x in range(65, 90):
                corrupted[2][y][x] = (255, 0, 255)
        with self.assertRaisesRegex(SmokeFailure, "stale"):
            require_unchanged(baseline, corrupted, (0, 0, 1, 1), "empty frame")

    def test_departed_glass_tiles_must_return_to_background(self):
        baseline, moved = self.frame(), self.frame()
        for y in range(45, 68):
            for x in range(68, 88):
                moved[2][y][x] = baseline[2][y][x + 4]
        with self.assertRaisesRegex(SmokeFailure, "original tiles"):
            require_unchanged(baseline, moved, (.42, .38, .55, .58), "moved glass original tiles")

    def test_attenuation_cannot_pass_as_edge_displacement(self):
        baseline, edge = self.frame(), self.frame()
        for x, y in region_pixels(160, 120, (.94, .38, 1, .58)):
            edge[2][y][x] = tuple(int(channel * .2) for channel in edge[2][y][x])
        with self.assertRaisesRegex(SmokeFailure, "both stripe directions"):
            require_displacement(baseline, edge, (.94, .38, 1, .58), "edge glass")

    def test_missing_partial_right_tile_fails_despite_interior_displacement(self):
        baseline, edge = self.frame(width=967), self.frame(width=967)
        for x, y in region_pixels(967, 120, (.94, .38, 960 / 967, .58)):
            edge[2][y][x] = baseline[2][y][x + 4]
        require_displacement(baseline, edge, (.94, .38, 1, .58), "edge glass")
        with self.assertRaisesRegex(SmokeFailure, "partial right tile"):
            require_partial_tile_displacement(baseline, edge)

    def test_single_stripe_tail_requires_only_its_available_polarity(self):
        baseline, edge = self.frame(width=967), self.frame(width=967)
        for x, y in region_pixels(967, 120, (960 / 967, .38, 1, .58)):
            baseline[2][y][x] = (20, 20, 20)
            edge[2][y][x] = (235, 235, 235)
        metrics = require_partial_tile_displacement(baseline, edge)
        self.assertEqual(metrics["available_light_pixels"], 0)
        with self.assertRaisesRegex(SmokeFailure, "partial right tile"):
            require_partial_tile_displacement(baseline, baseline)

    def test_screen_sheet_fallback_rejects_missing_black_or_tinted_tail(self):
        width, height = 967, 727
        baseline = (width, height, [[(68, 68, 68)] * width] * height)
        fallback = (width, height, [[(66, 66, 66)] * width] * height)
        metrics = require_partial_tile_displacement(baseline, fallback, allow_screen_fallback=True)
        self.assertEqual(metrics["mode"], "bounded_sheet_fallback")
        # Clear-output retirement, a flat black replacement, an unrelated tint,
        # and excessive neutral attenuation must not imitate the Fresnel sheet.
        for pixel in ((68, 68, 68), (0, 0, 0), (66, 60, 66), (50, 50, 50)):
            corrupted = (width, height, [[pixel] * width] * height)
            with self.subTest(pixel=pixel), self.assertRaisesRegex(SmokeFailure, "partial right tile"):
                require_partial_tile_displacement(baseline, corrupted, allow_screen_fallback=True)
        with self.assertRaisesRegex(SmokeFailure, "partial right tile"):
            require_partial_tile_displacement(baseline, fallback)

    def test_lost_foreground_is_rejected_after_tile_movement(self):
        baseline, moved = self.frame(), self.frame()
        for x, y in region_pixels(160, 120, FOREGROUND_REGION):
            moved[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "foreground AVBOIT"):
            require_foreground(baseline, moved, "moved")

    def test_original_glass_pixels_cannot_establish_newly_active_tiles(self):
        baseline, stale = self.frame(), self.frame()
        for x, y in region_pixels(160, 120, (.57, .38, .63, .58)):
            stale[2][y][x] = baseline[2][y][x + 4]
        with self.assertRaisesRegex(SmokeFailure, "newly active"):
            require_displacement(baseline, stale, MOVED_NEW_REGION, "newly active moved tiles")

    def test_reordered_or_repeated_source_frames_are_rejected(self):
        valid = "\n".join(f"RefractionLifecycle: captured {phase} graphics frame {16 * (index + 1)}" for index, phase in enumerate(PHASES))
        self.assertEqual(list(validate_lifecycle_log(valid)), list(PHASES))
        with self.assertRaisesRegex(SmokeFailure, "reordered"):
            validate_lifecycle_log(valid.replace("captured moved", "captured active"))
        with self.assertRaisesRegex(SmokeFailure, "advance"):
            validate_lifecycle_log(valid.replace("graphics frame 32", "graphics frame 16"))


if __name__ == "__main__":
    unittest.main()
