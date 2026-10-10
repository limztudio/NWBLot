#!/usr/bin/env python3
"""Reject black overflow stand-ins, pairing loss, stale clipping and broad warning allowances."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from csg_shadow_open_smoke import CASES, COUNTS, LENGTHS, OPEN_CAPACITY_MESSAGE, REGIONS, compare_frames, expected_open_rgb, validate_runtime
from csg_shadow_reference import expected_rgb, region_pixels
from generate_csg_shadow_open_meshes import open_sheets, validate_open_sheets
from window_capture_smoke import SmokeFailure


class CsgOpenShadowTests(unittest.TestCase):
    def frames(self):
        frames = {}
        for case in CASES:
            width, height = 480, 360
            rows = [[(255, 255, 255)] * width for _ in range(height)]
            for index, region in enumerate(REGIONS):
                length = LENGTHS[case][index] if index < 2 else 0.
                rgb = tuple(round(value) for value in expected_open_rgb(length, COUNTS[case][index] if index < 2 else 0))
                for x, y in region_pixels(region, width, height):
                    rows[y][x] = rgb
            frames[case] = width, height, rows
        return frames

    def replace(self, frames, case, region_index, value):
        frame = frames[case]
        for x, y in region_pixels(REGIONS[region_index], frame[0], frame[1]):
            frame[2][y][x] = value

    def log(self, case="retained", route="hardware"):
        first, second = COUNTS[case]
        lines = ["FramebufferCapture: capture ready", "FramebufferCapture: graphics source frame 119",
            "CsgShadowSmokeProject: shutdown",
            f"CsgShadowSmokeProject: open_case={case} retained_events={first},{second} receiver_z=0 sheets_z_min=-7 sheets_z_max=-5 ior=1.5 unit_transmission=0.25,0.5,0.75 coverage=1",
            f"CsgShadowSmokeProject: atlas arm=1 light=directional hardware={int(route == 'hardware')}",
            "SoftwareShadowSmoke: requested backend=0 directional_resolution=512 point_resolution=256 budget_bytes=268435456 coverage=0 blocker_search=0 capture_cadence=0"]
        if route == "software":
            lines.append("Loader: hardware ray tracing disabled before device creation")
        if case in ("reference", "clipped_reference"):
            lines.append("RendererSystem: dispatched hardware transparent shadow traversal" if route == "hardware"
                else "RendererSystem: dispatched light-space shadow maps")
        else:
            lines.append(f"RendererSystem: dispatched CSG light-space shadows (hardware_compose={int(route == 'hardware')}, views=1)")
        if case == "terminal65":
            lines.extend(("00:00:01.250 [WARNING]:", "", OPEN_CAPACITY_MESSAGE, "",
                "00:00:01.251 [INFO]:", "", "later ordinary diagnostic"))
        return "\n".join(lines)

    def test_fifth_event_black_fallback_is_rejected(self):
        frames = self.frames()
        self.assertEqual(len(compare_frames(frames)["regions"]), 6)
        self.replace(frames, "retained", 0, (0, 0, 0))
        with self.assertRaisesRegex(SmokeFailure, "retained/first_receiver"):
            compare_frames(frames)

    def test_even_six_pairing_cannot_use_first_last_approximation(self):
        frames = self.frames()
        value = tuple(round(channel) for channel in expected_open_rgb(2., 6))
        self.replace(frames, "retained", 1, value)
        with self.assertRaisesRegex(SmokeFailure, "retained/six_sheet_control"):
            compare_frames(frames)

    def test_stale_cutter_and_all_black_controls_are_rejected(self):
        frames = self.frames()
        frames["clipped"] = frames["retained"]
        with self.assertRaisesRegex(SmokeFailure, "clipped/first_receiver"):
            compare_frames(frames)
        frames = self.frames()
        self.replace(frames, "retained", 2, (0, 0, 0))
        with self.assertRaisesRegex(SmokeFailure, "clear_receiver"):
            compare_frames(frames)

    def test_capacity64_must_remain_positive_and65_must_fail_conservatively(self):
        frames = self.frames()
        self.replace(frames, "boundary64", 0, (0, 0, 0))
        with self.assertRaisesRegex(SmokeFailure, "boundary64/first_receiver"):
            compare_frames(frames)
        frames = self.frames()
        self.replace(frames, "terminal65", 0, (255, 255, 255))
        with self.assertRaisesRegex(SmokeFailure, "terminal65/first_receiver"):
            compare_frames(frames)

    def test_capacity64_cannot_drop_nonunit_ior_interface_product(self):
        frames = self.frames()
        beer_only = tuple(round(channel) for channel in expected_rgb(64. / 63., -1.2, 0., "directional"))
        self.replace(frames, "boundary64", 0, beer_only)
        with self.assertRaisesRegex(SmokeFailure, "boundary64/first_receiver"):
            compare_frames(frames)

    def test_open_terminal_requires_exact_flag_and_rejects_other_capacity(self):
        self.assertEqual(validate_runtime(self.log("terminal65"), "terminal65", "hardware", 120)["diagnostic_flags"], 16)
        for log in (self.log("terminal65").replace("flags=16", "flags=2"),
            self.log("terminal65") + "\n[WARNING]: unrelated", self.log("terminal65").replace("[WARNING]", "[ERROR]")):
            with self.assertRaises(SmokeFailure):
                validate_runtime(log, "terminal65", "hardware", 120)

    def test_multiline_warning_cannot_waive_prefix_suffix_or_detached_message(self):
        self.assertEqual(validate_runtime(self.log("terminal65"), "terminal65", "hardware", 120)["diagnostic_flags"], 16)
        for changed in (
            self.log("terminal65").replace("00:00:01.250 [WARNING]:", "unknown 00:00:01.250 [WARNING]:"),
            self.log("terminal65").replace(OPEN_CAPACITY_MESSAGE, "prefix " + OPEN_CAPACITY_MESSAGE),
            self.log("terminal65").replace(OPEN_CAPACITY_MESSAGE, OPEN_CAPACITY_MESSAGE + " suffix"),
            self.log("terminal65").replace(OPEN_CAPACITY_MESSAGE, "unrelated warning") + "\n" + OPEN_CAPACITY_MESSAGE,
            self.log("terminal65").replace("00:00:01.250 [WARNING]:\n\n", "[WARNING]: ")
        ):
            with self.assertRaises(SmokeFailure):
                validate_runtime(changed, "terminal65", "hardware", 120)

    def test_valid_cases_and_terminal_reject_validation_errors(self):
        for case in ("retained", "boundary64", "terminal65"):
            with self.assertRaises(SmokeFailure):
                validate_runtime(self.log(case) + "\nVUID-invalid", case, "hardware", 120)
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log() + "\n[WARNING]: " + OPEN_CAPACITY_MESSAGE, "retained", "hardware", 120)

    def test_missing_stale_or_repeated_completed_source_is_rejected(self):
        log = self.log()
        for changed in (log.replace("frame 119", "frame 118"), log + "\nFramebufferCapture: graphics source frame 120",
            log.replace("FramebufferCapture: graphics source frame 119", "")):
            with self.assertRaises(SmokeFailure):
                validate_runtime(changed, "retained", "hardware", 120)

    def test_ordinary_and_software_routes_cannot_claim_csg_or_hardware(self):
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log("reference") + "\nRendererSystem: dispatched CSG light-space shadows", "reference", "hardware", 120)
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log("retained", "software") + "\nRendererSystem: dispatched hardware transparent shadow traversal", "retained", "software", 120)

    def test_generator_rejects_a_quad_joined_to_the_next_component(self):
        mesh = open_sheets(65)
        self.assertEqual(len(mesh.indices), 130)
        mesh.indices[2] = mesh.indices[0]
        with self.assertRaisesRegex(AssertionError, "connected"):
            validate_open_sheets(mesh, 65)


if __name__ == "__main__":
    unittest.main()
