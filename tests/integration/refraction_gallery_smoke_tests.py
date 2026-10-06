#!/usr/bin/env python3
"""Check invalid selections, inherited capture state, and pixel thresholds."""

import argparse
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from refraction_gallery_smoke import CASES, capture_environment, frame_difference, parse_selection  # noqa: E402

LIT_CASES = "cases"
LIT_NESTED = "nested"
LIT_NWB_REFRACTION_SMOKE_CASE = "NWB_REFRACTION_SMOKE_CASE"
LIT_NWB_REFRACTION_SMOKE_GEOMETRY = "NWB_REFRACTION_SMOKE_GEOMETRY"
LIT_NWB_REFRACTION_SMOKE_ENABLED = "NWB_REFRACTION_SMOKE_ENABLED"
LIT_NWB_REFRACTION_SMOKE_HARDWARE = "NWB_REFRACTION_SMOKE_HARDWARE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT"
LIT_PATH = "PATH"
LIT_PRESERVED = "preserved"
LIT_PRISM = "prism"
LIT_MAIN = "__main__"


class RefractionGalleryTests(unittest.TestCase):
    def test_case_selection_rejects_unknown_duplicates_and_empty(self):
        for value in ("", "unknown", "nested,nested"):
            with self.assertRaises(argparse.ArgumentTypeError):
                parse_selection(value, CASES, LIT_CASES)

    def test_capture_environment_overrides_case_and_removes_stale_capture_controls(self):
        inherited = {
            LIT_NWB_REFRACTION_SMOKE_CASE: LIT_NESTED, LIT_NWB_REFRACTION_SMOKE_GEOMETRY: "1",
            LIT_NWB_REFRACTION_SMOKE_ENABLED: "0", LIT_NWB_REFRACTION_SMOKE_HARDWARE: "1",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "1", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: "old.bmp",
            LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO: "1", LIT_PATH: LIT_PRESERVED,
        }
        env = capture_environment(LIT_PRISM, "screen", inherited)
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_CASE], LIT_PRISM)
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_GEOMETRY], "0")
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_ENABLED], "1")
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_HARDWARE], "0")
        self.assertEqual(env[LIT_PATH], LIT_PRESERVED)
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, env)
        self.assertNotIn(LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH, env)
        self.assertNotIn(LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO, env)
        self.assertEqual(inherited[LIT_NWB_REFRACTION_SMOKE_CASE], LIT_NESTED)


    def test_difference_counts_only_pixels_above_channel_threshold(self):
        reference = (2, 1, [[(0, 0, 0), (10, 10, 10)]])
        output = (2, 1, [[(9, 0, 0), (18, 10, 10)]])
        result = frame_difference(reference, output)
        self.assertEqual(result["changed_pixels"], 1)


if __name__ == LIT_MAIN:
    unittest.main()
