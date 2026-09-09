#!/usr/bin/env python3
"""Exercise the visual acceptance oracle with displaced, tinted, and corrupted frames."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from refraction_capture_smoke import (  # noqa: E402
    FOREGROUND_REGION, GLASS_REGION, SmokeFailure, compare_refraction_frames, region_pixels,
)


class RefractionCaptureAnalysisTests(unittest.TestCase):
    width = 400
    height = 300

    def make_frame(self):
        rows = [[(235, 235, 235) if (x // 8) % 2 else (20, 20, 20)
            for x in range(self.width)] for _ in range(self.height)]
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            if 181 <= y < 190:
                rows[y][x] = (225, 35, 35)
        return self.width, self.height, rows

    def displace_stripes(self, source):
        width, height, rows = source
        output = [list(row) for row in rows]
        for x, y in region_pixels(width, height, GLASS_REGION):
            output[y][x] = rows[y][x + 8]
        return width, height, output

    def test_displaced_stripes_preserve_foreground_and_exterior(self):
        source = self.make_frame()
        metrics = compare_refraction_frames(source, self.displace_stripes(source))
        self.assertGreater(metrics["dark_to_light_pixels"], 100)
        self.assertGreater(metrics["light_to_dark_pixels"], 100)
        self.assertEqual(metrics["exterior_changed_pixels"], 0)
        self.assertEqual(metrics["foreground_reference_pixels"], metrics["foreground_retained_pixels"])

    def test_identical_frame_cannot_pass_as_refraction(self):
        source = self.make_frame()
        with self.assertRaisesRegex(SmokeFailure, "did not displace"):
            compare_refraction_frames(source, source)

    def test_attenuation_cannot_pass_as_displaced_edges(self):
        source = self.make_frame()
        output = [list(row) for row in source[2]]
        for x, y in region_pixels(self.width, self.height, GLASS_REGION):
            output[y][x] = tuple(int(channel * 0.2) for channel in output[y][x])
        with self.assertRaisesRegex(SmokeFailure, "did not displace"):
            compare_refraction_frames(source, (self.width, self.height, output))

    def test_whole_screen_shift_is_not_local_refraction(self):
        source = self.make_frame()
        output = self.displace_stripes(source)
        for y in range(110, 170):
            for x in range(117, 137):
                output[2][y][x] = (255, 0, 255)
        with self.assertRaisesRegex(SmokeFailure, "outside the glass"):
            compare_refraction_frames(source, output)

    def test_lost_foreground_transparency_fails(self):
        source = self.make_frame()
        output = self.displace_stripes(source)
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            output[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "foreground AVBOIT panel"):
            compare_refraction_frames(source, output)


if __name__ == "__main__":
    unittest.main()
