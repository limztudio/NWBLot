#!/usr/bin/env python3
"""Exercise the duplicate-volume image oracle without a GPU."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from refraction_duplicate_smoke import (  # noqa: E402
    MATCHES, MAXIMUM_CHANNEL_ERROR, MINIMUM_DISTINCT_PIXELS, RETAINED_CASES,
    pixel_difference, require_distinct, require_match, validate_captures,
)
from window_capture_smoke import SmokeFailure  # noqa: E402


class RefractionDuplicateAnalysisTests(unittest.TestCase):
    @staticmethod
    def frame(width=16, height=8):
        return width, height, [[(40 if x % 2 else 200, 90, 120) for x in range(width)] for _ in range(height)]

    def test_identical_pixels_match_exactly(self):
        frame = self.frame()
        result = require_match(frame, deepcopy(frame), "exact")
        self.assertEqual(result["maximum_channel_difference"], 0)
        self.assertEqual(result["mean_absolute_rgb_difference"], 0)

    def test_sparse_rounding_is_allowed_but_large_local_error_fails(self):
        reference = self.frame()
        output = deepcopy(reference)
        output[2][0][0] = (201, 90, 120)
        require_match(reference, output, "one rounding step")
        output[2][0][0] = (200 + MAXIMUM_CHANNEL_ERROR + 1, 90, 120)
        with self.assertRaisesRegex(SmokeFailure, "does not match"):
            require_match(reference, output, "local artifact")

    def test_systematic_small_tint_shift_fails_mean_error_budget(self):
        reference = self.frame()
        output = deepcopy(reference)
        for row in output[2]:
            for x, pixel in enumerate(row):
                row[x] = (pixel[0] + 1, pixel[1], pixel[2])
        with self.assertRaisesRegex(SmokeFailure, "does not match"):
            require_match(reference, output, "wrong tint")

    def test_distinct_control_rejects_identical_and_insufficient_changes(self):
        reference = self.frame()
        with self.assertRaisesRegex(SmokeFailure, "not distinguishable"):
            require_distinct(reference, deepcopy(reference), "lost optical effect")
        output = deepcopy(reference)
        for index in range(MINIMUM_DISTINCT_PIXELS - 1):
            y, x = divmod(index, reference[0])
            output[2][y][x] = (0, 0, 0)
        with self.assertRaisesRegex(SmokeFailure, "not distinguishable"):
            require_distinct(reference, output, "too few changed pixels")
        y, x = divmod(MINIMUM_DISTINCT_PIXELS - 1, reference[0])
        output[2][y][x] = (0, 0, 0)
        self.assertEqual(require_distinct(reference, output, "retained second volume")["pixels_over_channel_tolerance"], MINIMUM_DISTINCT_PIXELS)

    def test_mismatched_or_truncated_frames_fail_instead_of_comparing_prefixes(self):
        with self.assertRaises(SmokeFailure):
            pixel_difference(self.frame(), self.frame(15, 8))
        frame = self.frame()
        for malformed in ((16, 8, frame[2][:-1]), (16, 8, [row[:-1] for row in frame[2]])):
            with self.assertRaises(SmokeFailure):
                pixel_difference(frame, malformed)

    def test_suite_checks_priority_winner_and_disabled_grouping_against_correct_references(self):
        def shifted(red=0, green=0, blue=0):
            width, height, rows = self.frame()
            return width, height, [[(r + red, g + green, b + blue) for r, g, b in row] for row in rows]

        captures = {}
        for variant in ("automatic", "screen"):
            captures[f"duplicate_single_cool_{variant}"] = shifted()
            captures[f"duplicate_single_warm_{variant}"] = shifted(red=8)
            captures[f"duplicate_single_cool_tinted_{variant}"] = shifted(green=10)
            captures[f"duplicate_single_warm_tinted_{variant}"] = shifted(red=8, green=10)
            for candidate, reference in MATCHES.items():
                captures[f"{candidate}_{variant}"] = captures[f"{reference}_{variant}"]
            for candidate in RETAINED_CASES:
                captures[f"{candidate}_{variant}"] = shifted(blue=30)
        captures["duplicate_single_cool_disabled"] = shifted(blue=20)
        captures["duplicate_single_cool_tinted_disabled"] = shifted(green=10, blue=20)
        for candidate in ("coincident_tinted_identical", "coincident_tinted"):
            captures[f"{candidate}_disabled"] = captures["duplicate_single_cool_tinted_disabled"]
        captures["coincident_preserved_disabled"] = shifted(blue=40)
        with patch("refraction_duplicate_smoke.read_bmp_24_rows", side_effect=lambda path: captures[path.stem]):
            self.assertEqual(len(validate_captures(Path("unused"), ("automatic", "screen"))), 29)
            captures["coincident_priority_swap_screen"] = captures["duplicate_single_cool_screen"]
            with self.assertRaisesRegex(SmokeFailure, "coincident_priority_swap/screen.*does not match"):
                validate_captures(Path("unused"), ("automatic", "screen"))


if __name__ == "__main__":
    unittest.main()
