#!/usr/bin/env python3
"""Reject black overflow stand-ins, pairing loss, stale clipping and broad warning allowances."""

from pathlib import Path
import sys
import unittest
import tempfile
from unittest.mock import patch
from contextlib import redirect_stderr
from io import StringIO

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import csg_shadow_open_smoke as smoke
from csg_shadow_open_smoke import CASES, COUNTS, LENGTHS, OPEN_CAPACITY_MESSAGE, REGIONS, compare_frames, expected_open_rgb, parse_args, validate_runtime
from csg_shadow_reference import expected_rgb, region_pixels
from generate_csg_shadow_open_meshes import open_sheets, validate_open_sheets
from window_capture_smoke import SmokeFailure

LIT_CAPTURE_SUFFIX = ".txt"

LIT_RETAINED = "retained"
LIT_HARDWARE = "hardware"
LIT_ENABLED = "enabled"
LIT_FRAMEBUFFERCAPTURE_GRAPHICS_SOURCE_FRA = "FramebufferCapture: graphics source frame 119"
LIT_SOFTWARE = "software"
LIT_REFERENCE = "reference"
LIT_TERMINAL65 = "terminal65"
LIT_REGIONS = "regions"
LIT_BOUNDARY64 = "boundary64"
LIT_BOUNDARY64_FIRST_RECEIVER = "boundary64/first_receiver"
LIT_DIAGNOSTIC_FLAGS = "diagnostic_flags"
LIT_DISABLED = "disabled"
LIT_DIAGNOSTIC_FLAGS_OBSERVED = "diagnostic_flags_observed"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_ROUTE = "--route"
LIT_WARNING_POLICY = "--warning-policy"
LIT_RUNS = "runs"
LIT_PROOF_TXT = "proof.txt"
LIT_RETAINED_EVIDENCE = "retained evidence"
LIT_UTF_8 = "utf-8"
LIT_APP_EXE = "app.exe"
LIT_RUNTIME = "runtime"
LIT_OUTPUT_ROOT = "--output-root"
LIT_CAPTURE_CASE = "capture_case"


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

    def log(self, case=LIT_RETAINED, route=LIT_HARDWARE, warning_policy=LIT_ENABLED):
        first, second = COUNTS[case]
        lines = ["FramebufferCapture: capture ready", LIT_FRAMEBUFFERCAPTURE_GRAPHICS_SOURCE_FRA, "CsgShadowSmokeProject: shutdown",
            f"CsgShadowSmokeProject: open_case={case} retained_events={first},{second} receiver_z=0 sheets_z_min=-7 sheets_z_max=-5 ior=1.5 unit_transmission=0.25,0.5,0.75 coverage=1",
            f"CsgShadowSmokeProject: atlas arm=1 light=directional hardware={int(route == LIT_HARDWARE)}",
            "SoftwareShadowSmoke: requested backend=0 directional_resolution=512 point_resolution=256 budget_bytes=268435456 coverage=0 blocker_search=0 capture_cadence=0"]
        if route == LIT_SOFTWARE:
            lines.append("Loader: hardware ray tracing disabled before device creation")
        if case in (LIT_REFERENCE, "clipped_reference"):
            lines.append("RendererSystem: dispatched hardware transparent shadow traversal" if route == LIT_HARDWARE else "RendererSystem: dispatched light-space shadow maps")
        else:
            lines.append(f"RendererSystem: dispatched CSG light-space shadows (hardware_compose={int(route == LIT_HARDWARE)}, views=1)")
        if case == LIT_TERMINAL65 and warning_policy == LIT_ENABLED:
            lines.extend(("00:00:01.250 [WARNING]:", "", OPEN_CAPACITY_MESSAGE, "", "00:00:01.251 [INFO]:", "", "later ordinary diagnostic"))
        return "\n".join(lines)

    def test_fifth_event_black_fallback_is_rejected(self):
        frames = self.frames()
        self.assertEqual(len(compare_frames(frames)[LIT_REGIONS]), 6)
        self.replace(frames, LIT_RETAINED, 0, (0, 0, 0))
        with self.assertRaisesRegex(SmokeFailure, "retained/first_receiver"):
            compare_frames(frames)

    def test_even_six_pairing_cannot_use_first_last_approximation(self):
        frames = self.frames()
        value = tuple(round(channel) for channel in expected_open_rgb(2., 6))
        self.replace(frames, LIT_RETAINED, 1, value)
        with self.assertRaisesRegex(SmokeFailure, "retained/six_sheet_control"):
            compare_frames(frames)

    def test_stale_cutter_and_all_black_controls_are_rejected(self):
        frames = self.frames()
        frames["clipped"] = frames[LIT_RETAINED]
        with self.assertRaisesRegex(SmokeFailure, "clipped/first_receiver"):
            compare_frames(frames)
        frames = self.frames()
        self.replace(frames, LIT_RETAINED, 2, (0, 0, 0))
        with self.assertRaisesRegex(SmokeFailure, "clear_receiver"):
            compare_frames(frames)

    def test_capacity64_must_remain_positive_and65_must_fail_conservatively(self):
        frames = self.frames()
        self.replace(frames, LIT_BOUNDARY64, 0, (0, 0, 0))
        with self.assertRaisesRegex(SmokeFailure, LIT_BOUNDARY64_FIRST_RECEIVER):
            compare_frames(frames)
        frames = self.frames()
        self.replace(frames, LIT_TERMINAL65, 0, (255, 255, 255))
        with self.assertRaisesRegex(SmokeFailure, "terminal65/first_receiver"):
            compare_frames(frames)

    def test_capacity64_cannot_drop_nonunit_ior_interface_product(self):
        frames = self.frames()
        beer_only = tuple(round(channel) for channel in expected_rgb(64. / 63., -1.2, 0., "directional"))
        self.replace(frames, LIT_BOUNDARY64, 0, beer_only)
        with self.assertRaisesRegex(SmokeFailure, LIT_BOUNDARY64_FIRST_RECEIVER):
            compare_frames(frames)

    def test_open_terminal_requires_exact_flag_and_rejects_other_capacity(self):
        self.assertEqual(validate_runtime(self.log(LIT_TERMINAL65), LIT_TERMINAL65, LIT_HARDWARE, 120)[LIT_DIAGNOSTIC_FLAGS], 16)
        for log in (self.log(LIT_TERMINAL65).replace("flags=16", "flags=2"), self.log(LIT_TERMINAL65) + "\n[WARNING]: unrelated", self.log(LIT_TERMINAL65).replace("[WARNING]", "[ERROR]")):
            with self.assertRaises(SmokeFailure):
                validate_runtime(log, LIT_TERMINAL65, LIT_HARDWARE, 120)

    def test_disabled_warning_policy_preserves_all_cases_without_fabricating_flags(self):
        for route in (LIT_HARDWARE, LIT_SOFTWARE):
            for case in CASES:
                receipt = validate_runtime(self.log(case, route, LIT_DISABLED), case, route, 120, LIT_DISABLED)
                self.assertIsNone(receipt[LIT_DIAGNOSTIC_FLAGS])
                self.assertFalse(receipt[LIT_DIAGNOSTIC_FLAGS_OBSERVED])
                self.assertFalse(receipt["capacity_diagnostic_available"])
                self.assertEqual(receipt["warning_policy"], LIT_DISABLED)
                self.assertEqual(receipt["expected_capacity_terminal"], case == LIT_TERMINAL65)
        self.assertEqual(len(compare_frames(self.frames())[LIT_REGIONS]), 6)

    def test_disabled_warning_policy_rejects_every_warning_and_validation_error(self):
        for case in CASES:
            for marker in ("00:00:01.250 [WARNING]:\n" + OPEN_CAPACITY_MESSAGE, "[WARNING]: unrelated", "[ERROR]: unrelated", "VUID-invalid"):
                with self.assertRaises(SmokeFailure):
                    validate_runtime(self.log(case, warning_policy=LIT_DISABLED) + "\n" + marker, case, LIT_HARDWARE, 120, LIT_DISABLED)
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log(LIT_TERMINAL65, warning_policy=LIT_DISABLED), LIT_TERMINAL65, LIT_HARDWARE, 120)

    def test_warning_policy_is_explicit_and_independent_of_gpu_validation(self):
        base = [LIT_EXECUTABLE, "smoke", LIT_WORKING_DIRECTORY, ".", LIT_OUTPUT_DIRECTORY, ".", LIT_ROUTE, LIT_SOFTWARE]
        for invalid in ([], [LIT_WARNING_POLICY, "unknown"]):
            with redirect_stderr(StringIO()), self.assertRaises(SystemExit):
                parse_args(base + invalid)
        disabled = parse_args(base + [LIT_WARNING_POLICY, LIT_DISABLED, "--gpu-validation"])
        receipt = validate_runtime(self.log(LIT_TERMINAL65, warning_policy=LIT_DISABLED), LIT_TERMINAL65, LIT_HARDWARE, 120, disabled.warning_policy)
        self.assertIsNone(receipt[LIT_DIAGNOSTIC_FLAGS])
        self.assertFalse(receipt[LIT_DIAGNOSTIC_FLAGS_OBSERVED])
        enabled = parse_args(base + [LIT_WARNING_POLICY, LIT_ENABLED, "--no-gpu-validation"])
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log(LIT_TERMINAL65, warning_policy=LIT_DISABLED), LIT_TERMINAL65, LIT_HARDWARE, 120, enabled.warning_policy)

    def test_multiline_warning_cannot_waive_prefix_suffix_or_detached_message(self):
        self.assertEqual(validate_runtime(self.log(LIT_TERMINAL65), LIT_TERMINAL65, LIT_HARDWARE, 120)[LIT_DIAGNOSTIC_FLAGS], 16)
        for changed in (self.log(LIT_TERMINAL65).replace("00:00:01.250 [WARNING]:", "unknown 00:00:01.250 [WARNING]:"), self.log(LIT_TERMINAL65).replace(OPEN_CAPACITY_MESSAGE, "prefix " + OPEN_CAPACITY_MESSAGE),
            self.log(LIT_TERMINAL65).replace(OPEN_CAPACITY_MESSAGE, OPEN_CAPACITY_MESSAGE + " suffix"), self.log(LIT_TERMINAL65).replace(OPEN_CAPACITY_MESSAGE, "unrelated warning") + "\n" + OPEN_CAPACITY_MESSAGE,
            self.log(LIT_TERMINAL65).replace("00:00:01.250 [WARNING]:\n\n", "[WARNING]: ")):
            with self.assertRaises(SmokeFailure):
                validate_runtime(changed, LIT_TERMINAL65, LIT_HARDWARE, 120)

    def test_valid_cases_and_terminal_reject_validation_errors(self):
        for case in (LIT_RETAINED, LIT_BOUNDARY64, LIT_TERMINAL65):
            with self.assertRaises(SmokeFailure):
                validate_runtime(self.log(case) + "\nVUID-invalid", case, LIT_HARDWARE, 120)
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log() + "\n[WARNING]: " + OPEN_CAPACITY_MESSAGE, LIT_RETAINED, LIT_HARDWARE, 120)

    def test_missing_stale_or_repeated_completed_source_is_rejected(self):
        log = self.log()
        for changed in (log.replace("frame 119", "frame 118"), log + "\nFramebufferCapture: graphics source frame 120", log.replace(LIT_FRAMEBUFFERCAPTURE_GRAPHICS_SOURCE_FRA, "")):
            with self.assertRaises(SmokeFailure):
                validate_runtime(changed, LIT_RETAINED, LIT_HARDWARE, 120)

    def test_ordinary_and_software_routes_cannot_claim_csg_or_hardware(self):
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log(LIT_REFERENCE) + "\nRendererSystem: dispatched CSG light-space shadows", LIT_REFERENCE, LIT_HARDWARE, 120)
        with self.assertRaises(SmokeFailure):
            validate_runtime(self.log(LIT_RETAINED, LIT_SOFTWARE) + "\nRendererSystem: dispatched hardware transparent shadow traversal", LIT_RETAINED, LIT_SOFTWARE, 120)

    def test_generator_rejects_a_quad_joined_to_the_next_component(self):
        mesh = open_sheets(65)
        self.assertEqual(len(mesh.indices), 130)
        mesh.indices[2] = mesh.indices[0]
        with self.assertRaisesRegex(AssertionError, "connected"):
            validate_open_sheets(mesh, 65)


class CsgOpenShadowOutputTests(unittest.TestCase):
    def test_repeated_output_root_runs_preserve_prior_and_first_capture_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / LIT_RUNS
            retained = root / "completed" / LIT_PROOF_TXT
            retained.parent.mkdir(parents=True)
            retained.write_text(LIT_RETAINED_EVIDENCE, encoding=LIT_UTF_8)
            destinations = []

            def capture_case(args, case):
                if case == CASES[0]:
                    self.assertFalse(any(args.output_directory.iterdir()))
                    destinations.append(args.output_directory)
                (args.output_directory / (case + LIT_CAPTURE_SUFFIX)).write_text(f"capture {len(destinations)}", encoding=LIT_UTF_8)
                return None, {"case": case}

            required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_ROOT, str(root), LIT_ROUTE, LIT_SOFTWARE, LIT_WARNING_POLICY, LIT_ENABLED]
            with patch.object(smoke, LIT_CAPTURE_CASE, side_effect=capture_case), patch.object(smoke, "compare_frames", side_effect=lambda frames: {}):
                self.assertEqual(smoke.main(required), 0)
                self.assertEqual(smoke.main(required), 0)
            self.assertEqual(len(set(destinations)), 2)
            self.assertTrue(all(destination.parent == root for destination in destinations))
            self.assertEqual(retained.read_text(encoding=LIT_UTF_8), LIT_RETAINED_EVIDENCE)
            for index, destination in enumerate(destinations, 1):
                self.assertEqual((destination / (CASES[0] + LIT_CAPTURE_SUFFIX)).read_text(encoding=LIT_UTF_8), f"capture {index}")

    def test_nonempty_exact_output_directory_still_rejects_without_capture_or_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            retained = output / LIT_PROOF_TXT
            retained.write_text(LIT_RETAINED_EVIDENCE, encoding=LIT_UTF_8)
            required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_DIRECTORY, str(output), LIT_ROUTE, LIT_SOFTWARE, LIT_WARNING_POLICY, LIT_ENABLED]
            with patch.object(smoke, LIT_CAPTURE_CASE) as capture, redirect_stderr(StringIO()):
                self.assertEqual(smoke.main(required), 1)
            capture.assert_not_called()
            self.assertEqual(retained.read_text(encoding=LIT_UTF_8), LIT_RETAINED_EVIDENCE)

    def test_exact_output_directory_and_root_are_mutually_exclusive(self):
        required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_DIRECTORY, "output", LIT_OUTPUT_ROOT, LIT_RUNS, LIT_ROUTE, LIT_SOFTWARE, LIT_WARNING_POLICY, LIT_ENABLED]
        with redirect_stderr(StringIO()), self.assertRaises(SystemExit) as rejection:
            smoke.parse_args(required)
        self.assertEqual(rejection.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
