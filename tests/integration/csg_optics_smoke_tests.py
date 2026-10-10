#!/usr/bin/env python3
"""Reject optical CSG leak evidence and unrelated-frame comparisons."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import csg_optics_smoke as smoke
from window_capture_smoke import SmokeFailure


def framebuffer(color=(128, 128, 128)):
    return (960, 720, [[color for _ in range(960)] for _ in range(720)])


class CsgOpticsEvidenceTests(unittest.TestCase):
    def test_generated_wall_match_rejects_missing_transmitted_energy(self):
        reference = framebuffer()
        missing_wall = framebuffer()
        for y in range(270, 400):
            for x in range(350, 550):
                missing_wall[2][y][x] = (0, 0, 0)
        with self.assertRaises(SmokeFailure):
            smoke.require_refraction_match(reference, missing_wall)

    def test_controls_must_change_retained_solid_pixels(self):
        reference = framebuffer()
        unrelated = framebuffer()
        for y in range(30):
            for x in range(30):
                unrelated[2][y][x] = (0, 0, 0)
        with self.assertRaises(SmokeFailure):
            smoke.require_refraction_distinct(reference, unrelated, "unrelated background")

    def test_comparison_rejects_resized_and_mismatched_captures(self):
        with self.assertRaises(SmokeFailure):
            smoke.refraction_difference(framebuffer(), (959, 720, []))
        with self.assertRaises(SmokeFailure):
            smoke.refraction_difference((2, 1, [[(0, 0, 0)] * 2]), (2, 1, [[(0, 0, 0)] * 2]))


if __name__ == "__main__":
    unittest.main()
