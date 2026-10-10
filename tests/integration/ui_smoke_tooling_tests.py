#!/usr/bin/env python3
"""Reject inherited UI fixture state and preserve independent SDR and atlas edge references."""

import pathlib
import sys
import unittest
from unittest.mock import patch

SMOKE = pathlib.Path(__file__).resolve().parents[1] / "smoke"
sys.path[:0] = [str(SMOKE / "ui_layer"), str(SMOKE)]

from fixture_environment import (  # noqa: E402
    AUXILIARY_SWITCHES, CAPTURE_ENVIRONMENT, FIXTURE_FLAGS, SKIN_FIXTURES, build_fixture_environment,
)
from probe_reference import (  # noqa: E402
    authored_texel, compose, encoded_marker, linear_channels, linear_rgb_bytes, sampled_region, sampled_tile,
)


class UiFixtureEnvironmentTests(unittest.TestCase):
    def test_inherited_selectors_and_capture_controls_cannot_override_selected_fixture(self):
        inherited = {f"NWB_UI_LAYER_{fixture}": "1" for fixture in FIXTURE_FLAGS}
        inherited.update({f"NWB_UI_LAYER_{fixture}_SKIN": "1" for fixture in SKIN_FIXTURES})
        inherited.update({name: "1" for name in AUXILIARY_SWITCHES})
        inherited.update({name: "stale" for name in CAPTURE_ENVIRONMENT})
        inherited.update({"PATH": "loader-path", "NWB_LINUX_BACKEND": "wayland"})
        frozen = dict(inherited)
        for fixture in (None, *FIXTURE_FLAGS):
            with self.subTest(fixture=fixture):
                skin = "alternate" if fixture in SKIN_FIXTURES else "default"
                environment = build_fixture_environment(inherited, fixture, skin=skin)
                active = [name for name in FIXTURE_FLAGS if environment[f"NWB_UI_LAYER_{name}"] == "1"]
                self.assertEqual(active, [] if fixture is None else [fixture])
                active_skins = [name for name in SKIN_FIXTURES if environment[f"NWB_UI_LAYER_{name}_SKIN"] == "1"]
                self.assertEqual(active_skins, [fixture] if fixture in SKIN_FIXTURES else [])
                self.assertTrue(all(environment[name] == "0" for name in AUXILIARY_SWITCHES))
                self.assertTrue(all(name not in environment for name in CAPTURE_ENVIRONMENT))
                self.assertEqual(environment["PATH"], "loader-path")
                self.assertEqual(environment["NWB_LINUX_BACKEND"], "wayland")
                self.assertEqual(inherited, frozen)

    def test_unknown_or_incoherent_selection_fails_without_mutating_inherited_state(self):
        inherited = {"PATH": "preserved"}
        for fixture, skin in (("missing", "default"), ("image", "default"), ("", "default"),
                ("IMAGE", "unknown"), (None, "alternate"), ("EDIT", "alternate"), ("INTERACTIVE", "alternate")):
            with self.subTest(fixture=fixture, skin=skin), self.assertRaises(ValueError):
                build_fixture_environment(inherited, fixture, skin=skin)
        self.assertEqual(inherited, {"PATH": "preserved"})

    def test_x11_admission_is_explicit_and_does_not_change_other_platform_selection(self):
        inherited = {"NWB_LINUX_BACKEND": "wayland"}
        with patch("fixture_environment.platform.system", return_value="Linux"):
            self.assertEqual(build_fixture_environment(inherited, "EDIT")["NWB_LINUX_BACKEND"], "wayland")
            self.assertEqual(build_fixture_environment(inherited, "EDIT", force_x11=True)["NWB_LINUX_BACKEND"], "x11")
        with patch("fixture_environment.platform.system", return_value="Windows"):
            self.assertEqual(build_fixture_environment(inherited, "EDIT", force_x11=True)["NWB_LINUX_BACKEND"], "wayland")
        self.assertEqual(inherited["NWB_LINUX_BACKEND"], "wayland")

    def test_replay_scene_re_admits_only_its_explicit_current_switch(self):
        from raster_ir_parity_smoke import capture_environment
        from types import SimpleNamespace
        inherited = {f"NWB_UI_LAYER_{name}": "1" for name in FIXTURE_FLAGS}
        inherited.update({"NWB_UI_IR_REPLAY": "1", "NWB_UI_LAYER_RESIZE_CAPTURE": "1"})
        args = SimpleNamespace(mode_environment="NWB_UI_IR_REPLAY")
        with patch.dict("os.environ", inherited, clear=True):
            direct = capture_environment(args, "paint", "direct")
            replay = capture_environment(args, "texture_image", "replay")
        self.assertTrue(all(direct[f"NWB_UI_LAYER_{name}"] == "0" for name in FIXTURE_FLAGS))
        self.assertEqual(direct["NWB_UI_IR_REPLAY"], "0")
        self.assertEqual(replay["NWB_UI_IR_REPLAY"], "1")
        self.assertEqual(replay["NWB_UI_LAYER_TEXTURE_IMAGE"], "1")
        self.assertEqual(replay["NWB_UI_LAYER_TEXTURE_IMAGE_SKIN"], "1")
        self.assertEqual(replay["NWB_UI_LAYER_RESIZE_CAPTURE"], "0")
        self.assertTrue(all(replay[f"NWB_UI_LAYER_{name}"] == "0" for name in FIXTURE_FLAGS if name != "TEXTURE_IMAGE"))


class UiProbeReferenceTests(unittest.TestCase):
    def test_srgb_byte_roundtrip_preserves_all_code_values_across_transfer_breakpoint(self):
        for value in range(256):
            with self.subTest(value=value):
                channels = (value, 255 - value, value // 2)
                self.assertEqual(linear_rgb_bytes(linear_channels(channels)), channels)
        self.assertEqual(linear_rgb_bytes((0.0031308, 0.0031308001, 0.0)), (10, 10, 0))

    def test_marker_uses_only_selected_twelve_bits_and_applies_linear_dimming(self):
        self.assertEqual(encoded_marker(0x1000), (0, 0, 0))
        self.assertEqual(encoded_marker(0x10F0), (0, 255, 0))
        self.assertEqual(encoded_marker(0xF0000F), (255, 0, 0))
        self.assertEqual(encoded_marker(0xFFF, 0.6), (203, 203, 203))

    def test_transparent_sample_or_tint_cannot_contribute_color(self):
        for alpha, tint in ((0.0, (1.0, 1.0, 1.0, 1.0)), (1.0, (1.0, 1.0, 1.0, 0.0))):
            for background in ((24, 29, 37), (0, 0, 255), (255, 255, 255)):
                self.assertEqual(compose((0.9, 0.1, 0.3), alpha, tint, background), background)

    def test_texture_coverage_and_tint_opacity_multiply_before_linear_blending(self):
        self.assertEqual(compose((1.0, 0.0, 0.0), 0.5, (1.0, 1.0, 1.0, 0.5), (0, 0, 255)), (137, 0, 225))

    def test_outside_authored_tile_and_transparent_corners_have_zero_color_and_alpha(self):
        for kind in ("dot", "panel"):
            for x, y in ((-1, 12), (24, 12), (12, -1), (12, 24), (0, 0)):
                self.assertEqual(authored_texel(kind, x, y, (53, 131, 192), (97, 176, 233)), ((0.0, 0.0, 0.0), 0.0))

    def test_partial_dot_edge_keeps_straight_color_with_quantized_coverage(self):
        source, alpha = authored_texel("dot", 7, 9, (0, 0, 0), (0, 0, 0))
        self.assertEqual(linear_rgb_bytes(source), (235, 245, 255))
        self.assertEqual(alpha, 80 / 255)
        self.assertGreater(alpha, 0.0)
        self.assertLess(alpha, 1.0)

    def test_pixel_center_and_dpi_mapping_reach_same_authored_texel(self):
        fill, border = (53, 131, 192), (97, 176, 233)
        for kind in ("dot", "panel"):
            expected = authored_texel(kind, 11, 11, fill, border)
            self.assertEqual(sampled_tile(kind, (0, 0, 24, 24), (11, 11), (1, 1), fill, border), expected)
            self.assertEqual(sampled_tile(kind, (0, 0, 12, 12), (11, 11), (2, 2), fill, border), expected)

    def test_partial_uv_crop_matches_corresponding_full_tile_pixels(self):
        fill, border = (53, 131, 192), (97, 176, 233)
        for kind in ("dot", "panel"):
            for x, y in ((0, 0), (3, 4), (6, 7), (11, 11)):
                cropped = sampled_tile(kind, (0, 0, 12, 12), (x, y), (1, 1), fill, border, crop=(12, 12, 12, 12))
                full = sampled_tile(kind, (0, 0, 24, 24), (x + 12, y + 12), (1, 1), fill, border)
                self.assertEqual(cropped, full)

    def test_nine_slice_rejects_pixel_centers_outside_half_open_bounds(self):
        for pixel in ((-1, 12), (24, 12), (12, -1), (12, 24)):
            self.assertEqual(sampled_region((0, 0, 24, 24), pixel, (1, 1), (53, 131, 192), (97, 176, 233)),
                ((0.0, 0.0, 0.0), 0.0))

    def test_small_nine_slice_has_no_center_division_when_insets_meet(self):
        fill, border = (53, 131, 192), (97, 176, 233)
        for rectangle in ((0, 0, 5, 11), (0, 0, 12, 12)):
            for pixel in ((0, 0), (2, 4), (4, 10)):
                source, alpha = sampled_region(rectangle, pixel, (1, 1), fill, border)
                self.assertTrue(all(0.0 <= channel <= 1.0 for channel in source))
                self.assertGreaterEqual(alpha, 0.0)
                self.assertLessEqual(alpha, 1.0)
        left = sampled_region((0, 0, 5, 11), (0, 5), (1, 1), fill, border)
        right = sampled_region((0, 0, 5, 11), (4, 5), (1, 1), fill, border)
        for before, after in zip((*left[0], left[1]), (*right[0], right[1])):
            self.assertAlmostEqual(before, after, places=14)


if __name__ == "__main__":
    unittest.main()
