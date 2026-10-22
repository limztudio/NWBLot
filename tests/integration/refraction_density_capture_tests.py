#!/usr/bin/env python3
"""Reject incomplete, shifted, stale, and physically unrelated density captures."""

import argparse
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from refraction_density_capture_smoke import (  # noqa: E402
    SmokeFailure, analyze_density_frame, expected_levels, projected_bounds, selection, validate_backdrop, validate_extent_evidence,
)


class RefractionDensityCaptureTests(unittest.TestCase):
    width = 160
    height = 120

    def reference(self):
        row = [(68, 68, 68) if (x // 8) % 2 == 0 else (201, 201, 201) for x in range(self.width)]
        return self.width, self.height, [list(row) for _ in range(self.height)]

    def refracted(self, reference, percent=50, route="hardware"):
        width, height, rows = reference
        output = [list(row) for row in rows]
        left, top, right, bottom = projected_bounds(width, height, percent)
        for y in range(top, bottom):
            for x in range(left, right):
                levels = expected_levels(width, height, x, y, route)
                level = round(levels[rows[y][x][0] > 130])
                output[y][x] = (level, level, level)
        return width, height, output

    def test_missing_glass_cannot_pass_as_admitted_coverage(self):
        reference = self.reference()
        validate_backdrop(reference)
        with self.assertRaisesRegex(SmokeFailure, "changed bounds"):
            analyze_density_frame(reference, reference, 50, "hardware")

    def test_partial_glass_region_is_rejected(self):
        reference = self.reference()
        candidate = self.refracted(reference)
        left, top, right, bottom = projected_bounds(self.width, self.height, 50)
        for y in range(top, bottom):
            for x in range((left + right) // 2, right):
                candidate[2][y][x] = reference[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "changed bounds|interior"):
            analyze_density_frame(reference, candidate, 50, "hardware")

    def test_one_missing_tile_fails_even_when_global_coverage_exceeds_99_percent(self):
        reference = self.reference()
        candidate = self.refracted(reference)
        accepted = analyze_density_frame(reference, candidate, 50, "hardware")
        self.assertEqual(accepted["actual_changed_pixels"], accepted["analytical_pixels"])
        left, top, _, _ = projected_bounds(self.width, self.height, 50)
        for y in range(top + 9, top + 17):
            for x in range(left + 9, left + 17):
                candidate[2][y][x] = reference[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "missing local coverage"):
            analyze_density_frame(reference, candidate, 50, "hardware")

    def test_pixels_outside_projected_mask_are_rejected(self):
        reference = self.reference()
        candidate = self.refracted(reference)
        left, top, _, _ = projected_bounds(self.width, self.height, 50)
        candidate[2][top][left - 2] = (120, 120, 120)
        with self.assertRaisesRegex(SmokeFailure, "outside its projected mask"):
            analyze_density_frame(reference, candidate, 50, "hardware")

    def test_wrong_pitch_or_trapezoid_cannot_use_marker_bounds_as_image_proof(self):
        reference = self.reference()
        candidate = self.refracted(reference)
        left, top, right, bottom = projected_bounds(self.width, self.height, 50)
        for y in range(top, (top + bottom) // 2):
            for x in range(left, left + 12):
                candidate[2][y][x] = reference[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "interior|local coverage"):
            analyze_density_frame(reference, candidate, 50, "hardware")

    def test_black_tint_and_excessive_attenuation_fail_despite_full_changed_mask(self):
        reference = self.reference()
        for color in ((0, 0, 0), (55, 40, 55), (20, 20, 20)):
            candidate = self.refracted(reference)
            left, top, right, bottom = projected_bounds(self.width, self.height, 50)
            for y in range(top, bottom):
                for x in range(left, right):
                    candidate[2][y][x] = color
            with self.subTest(color=color), self.assertRaisesRegex(SmokeFailure, "neutral nonblack Fresnel stripes"):
                analyze_density_frame(reference, candidate, 50, "hardware")

    def test_one_flat_polarity_cannot_replace_both_background_stripes(self):
        reference = self.reference()
        candidate = self.refracted(reference)
        left, top, right, bottom = projected_bounds(self.width, self.height, 50)
        for y in range(top, bottom):
            for x in range(left, right):
                value = round(expected_levels(self.width, self.height, x, y, "hardware")[0])
                candidate[2][y][x] = (value, value, value)
        with self.assertRaisesRegex(SmokeFailure, "neutral nonblack Fresnel stripes"):
            analyze_density_frame(reference, candidate, 50, "hardware")

    def test_black_tile_cannot_hide_inside_global_fresnel_tolerance(self):
        reference = self.reference()
        candidate = self.refracted(reference)
        left, top, _, _ = projected_bounds(self.width, self.height, 50)
        for y in range(top + 17, top + 25):
            for x in range(left + 17, left + 25):
                candidate[2][y][x] = (0, 0, 0)
        with self.assertRaisesRegex(SmokeFailure, "locally invalid Fresnel output"):
            analyze_density_frame(reference, candidate, 50, "hardware")

    def test_zero_density_reference_rejects_visible_glass_and_nonvertical_output(self):
        reference = self.reference()
        candidate = self.refracted(reference, 50)
        with self.assertRaisesRegex(SmokeFailure, "zero-density backdrop"):
            validate_backdrop(candidate)
        reference[2][0][0] = (220, 0, 0)
        with self.assertRaisesRegex(SmokeFailure, "zero-density backdrop"):
            validate_backdrop(reference)

    def test_full_frame_still_checks_every_boundary_and_small_density_has_physical_core(self):
        for percent, route in ((1, "screen"), (100, "hardware"), (100, "screen")):
            reference = self.reference()
            candidate = self.refracted(reference, percent, route)
            metrics = analyze_density_frame(reference, candidate, percent, route)
            self.assertEqual(metrics["outside_changed_pixels"], 0)
            if percent == 100:
                self.assertEqual(metrics["actual_changed_bounds"], [0, 0, self.width, self.height])
                self.assertEqual(metrics["actual_changed_pixels"], self.width * self.height)
                for y in range(self.height):
                    candidate[2][y][-8:] = reference[2][y][-8:]
                with self.assertRaisesRegex(SmokeFailure, "changed bounds"):
                    analyze_density_frame(reference, candidate, percent, route)

    def test_dimension_mismatch_and_invalid_selection_are_rejected(self):
        reference = self.reference()
        with self.assertRaisesRegex(SmokeFailure, "extent"):
            analyze_density_frame(reference, (1, 1, [[(1, 1, 1)]]), 1, "hardware")
        with self.assertRaisesRegex(SmokeFailure, "unsupported density"):
            projected_bounds(960, 720, 12)
        for value in ("", "screen,screen", "unknown"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                selection(value, ("screen", "hardware"), "routes")

    def test_initial_clamp_requires_final_exact_framebuffer_before_readback(self):
        text = "GraphicsRuntime: Back buffer resized to 1904x1011\nGraphicsRuntime: Back buffer resized to 1920x1080\nFramebufferCapture: capture ready\n"
        metrics = validate_extent_evidence(text, (1920, 1080), (1904, 1011))
        self.assertTrue(metrics["client_resize_requested"])
        self.assertEqual(metrics["confirmed_client_extent"], [1920, 1080])
        with self.assertRaisesRegex(SmokeFailure, "finish at requested extent"):
            validate_extent_evidence(text.replace("GraphicsRuntime: Back buffer resized to 1920x1080\n", ""), (1920, 1080), (1904, 1011))
        with self.assertRaisesRegex(SmokeFailure, "before the final target"):
            validate_extent_evidence("FramebufferCapture: capture ready\n" + text.replace("FramebufferCapture: capture ready\n", ""), (1920, 1080), (1904, 1011))

    def test_unrelated_resize_and_post_capture_resize_are_rejected(self):
        valid = "GraphicsRuntime: Back buffer resized to 960x720\nFramebufferCapture: capture ready\n"
        metrics = validate_extent_evidence(valid, (960, 720), (960, 720))
        self.assertFalse(metrics["client_resize_requested"])
        for text in ("GraphicsRuntime: Back buffer resized to 640x480\n" + valid, valid + "GraphicsRuntime: Back buffer resized to 960x720\n"):
            with self.subTest(text=text), self.assertRaises(SmokeFailure):
                validate_extent_evidence(text, (960, 720), (960, 720))


if __name__ == "__main__":
    unittest.main()
