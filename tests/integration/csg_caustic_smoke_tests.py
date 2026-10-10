#!/usr/bin/env python3
"""Reject missing cap interfaces, stale uncut photons, and disabled ordinary multi-refractor deposition."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from csg_caustic_reference import REGIONS, analyze_route, compare_routes, expected_split_ratio  # noqa: E402
from csg_shadow_reference import region_pixels  # noqa: E402
from window_capture_smoke import SmokeFailure  # noqa: E402


def encode_srgb(value):
    return round(255 * (12.92 * value if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055))


class CsgCausticOracleTests(unittest.TestCase):
    def frames(self):
        width, height = 480, 360
        uncut = (0.01, 0.04, 0.09)
        cut = tuple(base * ratio for base, ratio in zip(uncut, expected_split_ratio()))
        frames = {}
        for arm in ("reference", "cut", "uncut", "disabled"):
            rows = [[(0, 0, 0)] * width for _ in range(height)]
            for region in REGIONS:
                linear = (0, 0, 0) if arm == "disabled" else (cut if region[0] == "split" and arm != "uncut" else uncut)
                value = tuple(encode_srgb(channel) for channel in linear)
                for x, y in region_pixels(region, width, height, spacing=0.045):
                    rows[y][x] = value
            frames[arm] = width, height, rows
        return frames

    def test_uncut_geometry_cannot_pass_retained_photon_optics(self):
        frames = self.frames()
        analyze_route(frames)
        frames["cut"] = frames["uncut"]
        with self.assertRaises(SmokeFailure):
            analyze_route(frames)

    def test_four_step_budget_cannot_pass_ordinary_split_reference(self):
        frames = self.frames()
        frames["reference"] = frames["disabled"]
        with self.assertRaisesRegex(SmokeFailure, "missing or saturated photon signal"):
            analyze_route(frames)

    def test_missing_generated_interfaces_fail_fresnel_beer_ratio(self):
        frames = self.frames()
        # Removing glass without the two cavity interfaces leaves the Beer ratio alone.
        for arm in ("reference", "cut"):
            for x, y in region_pixels(REGIONS[0], 480, 360, spacing=0.045):
                frames[arm][2][y][x] = (encode_srgb(0.04), encode_srgb(0.08), encode_srgb(0.12))
        with self.assertRaisesRegex(SmokeFailure, "Beer-Fresnel ratio"):
            analyze_route(frames)

    def test_disabled_lighting_contamination_and_hardware_mismatch_are_rejected(self):
        frames = self.frames()
        result = analyze_route(frames)
        compare_routes({"hardware": result, "software": result})
        altered = analyze_route(self.frames())
        altered["linear_irradiance"]["cut"]["split"] = (0.2, 0.2, 0.2)
        with self.assertRaisesRegex(SmokeFailure, "hardware/software"):
            compare_routes({"hardware": altered, "software": result})
        frames["disabled"] = frames["uncut"]
        with self.assertRaisesRegex(SmokeFailure, "disabled capture"):
            analyze_route(frames)


if __name__ == "__main__":
    unittest.main()
