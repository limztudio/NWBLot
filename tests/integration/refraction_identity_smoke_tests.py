#!/usr/bin/env python3
"""Reject wrong receiver association, partial leaks, and vacuous optical controls."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import refraction_identity_smoke as smoke
from window_capture_smoke import SmokeFailure


def framebuffer(value=130):
    color = (value, value, value)
    return 960, 720, [[color] * 960 for _ in range(720)]


def fill_rectangle(frame, rectangle, value):
    left, top, right, bottom = rectangle
    for y in range(top, bottom):
        for x in range(left, right):
            frame[2][y][x] = (value, value, value)


class RefractionIdentityEvidenceTests(unittest.TestCase):
    def frames(self):
        frames = {spec: framebuffer() for spec in smoke.IDENTITY_CAPTURES + smoke.FAILURE_CAPTURES}
        frames[("identity_reference", "automatic")] = framebuffer(90)
        fill_rectangle(frames[("crossing_overflow", "automatic")], smoke.FAILURE_RECTANGLE, 0)
        return frames

    def test_same_ior_foreign_exit_is_rejected_while_different_ior_control_still_matches(self):
        frames = self.frames()
        frames[("identity_same_ior", "automatic")] = framebuffer(160)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_frames(frames)

    def test_wrong_capture_primary_is_rejected_even_when_both_routes_share_it(self):
        frames = self.frames()
        frames[("identity_same_ior", "automatic")] = framebuffer(160)
        frames[("identity_same_ior", "screen")] = framebuffer(160)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_frames(frames)

    def test_one_leaked_capacity_pixel_fails_conservative_rejection(self):
        frames = self.frames()
        hardware = frames[("crossing_overflow", "automatic")]
        left, top, _, _ = smoke.FAILURE_RECTANGLE
        hardware[2][top][left] = (smoke.MAXIMUM_BLACK_CHANNEL + 1, 0, 0)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_frames(frames)

    def test_black_screen_control_cannot_qualify_a_missing_optical_result(self):
        frames = self.frames()
        fill_rectangle(frames[("crossing_overflow", "screen")], smoke.FAILURE_RECTANGLE, 0)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_frames(frames)

    def test_all_screen_fallback_cannot_qualify_the_valid_closed_volume_control(self):
        frames = self.frames()
        frames[("identity_reference", "automatic")] = framebuffer()
        with self.assertRaises(SmokeFailure):
            smoke.analyze_frames(frames)


class NearAirThicknessEvidenceTests(unittest.TestCase):
    def frames(self):
        return {spec: framebuffer(80 if spec[1] == "automatic" else 130) for spec in smoke.NEAR_AIR_CAPTURES}

    def test_first_half_ior_above_air_retains_positive_longer_path(self):
        metrics = smoke.analyze_near_air_frames(self.frames())
        self.assertEqual(metrics["near_air_ordinary_measured_thickness"]["minimum_red_attenuation"], 50)

    def test_identical_screen_fallback_cannot_qualify_valid_near_air_volume(self):
        frames = self.frames()
        for case in ("near_air_ordinary", "near_air_csg"):
            frames[(case, "automatic")] = framebuffer(130)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_near_air_frames(frames)

    def test_black_matched_hardware_volumes_cannot_qualify_positive_transmission(self):
        frames = self.frames()
        for case in ("near_air_ordinary", "near_air_csg"):
            frames[(case, "automatic")] = framebuffer(0)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_near_air_frames(frames)

    def test_brighter_hardware_cannot_qualify_a_longer_absorption_path(self):
        frames = self.frames()
        for case in ("near_air_ordinary", "near_air_csg"):
            frames[(case, "automatic")] = framebuffer(180)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_near_air_frames(frames)

    def test_one_missing_transmission_probe_cannot_be_hidden_by_matching_images(self):
        frames = self.frames()
        left, top, _, _ = smoke.NEAR_AIR_RECTANGLE
        for case in ("near_air_ordinary", "near_air_csg"):
            frames[(case, "automatic")][2][top][left] = (0, 0, 0)
        with self.assertRaises(SmokeFailure):
            smoke.analyze_near_air_frames(frames)


if __name__ == "__main__":
    unittest.main()
