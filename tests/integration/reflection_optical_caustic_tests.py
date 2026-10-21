#!/usr/bin/env python3
"""Combined and exterior caustic evidence analysis for the reflection/optical smoke."""


import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import caustic_optical_smoke as caustic
import caustic_optical_reference as caustic_reference
from reflection_smoke import STATISTICS_FIELDS, SmokeFailure


LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_ANALYZE_EXTERIOR_REFLECTION = "analyze_exterior_reflection"
LIT_REFLECTION_DISABLED = "reflection_disabled"
LIT_REFRACTION_DISABLED = "refraction_disabled"
LIT_CAUSTICS_DISABLED = "caustics_disabled"
LIT_COMBINED = "combined"
LIT_NWB_REFRACTION_SMOKE_CASE = "NWB_REFRACTION_SMOKE_CASE"
LIT_NESTED = "nested"
LIT_NWB_REFLECTION_SMOKE_TEMPORAL = "NWB_REFLECTION_SMOKE_TEMPORAL"
LIT_NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BA = "NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BACKDROP"
LIT_NWB_REFLECTION_SMOKE_MODE = "NWB_REFLECTION_SMOKE_MODE"
LIT_OUTPUT = "output"
LIT_APP_EXE = "app.exe"
LIT_RUNTIME = "runtime"
LIT_EXTERIOR_ENVIRONMENT = "exterior_environment"
LIT_SCREEN = "screen"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_SOFTWARE_RAY_TRACING = "--software-ray-tracing"
LIT_REQUIRE_HARDWARE = "--require-hardware"
LIT_MISSING_EXTERIOR = "missing exterior"
LIT_MAIN = "__main__"


class CombinedCausticTests(unittest.TestCase):
    def setUp(self):
        # These small synthetic images isolate the broad toggle/ground checks.
        # The mesh/radiometric oracle has separate full-resolution positive and deliberately missing-patch tests below.
        isolate_exterior = patch.object(caustic, LIT_ANALYZE_EXTERIOR_REFLECTION, return_value={"isolated_in_test": True})
        isolate_exterior.start()
        self.addCleanup(isolate_exterior.stop)

    def frames(self):
        rows = [[(60, 60, 60)] * 400 for _ in range(300)]
        for y in range(125, 155):
            for x in range(175, 225):
                rows[y][x] = (80, 90, 110)
        for y in range(250, 275):
            for x in range(185, 215):
                rows[y][x] = (100, 110, 120)
        frames = {name: (400, 300, [list(row) for row in rows]) for name in caustic.VARIANTS}
        for y in range(125, 155):
            for x in range(175, 225):
                frames[LIT_REFLECTION_DISABLED][2][y][x] = (60, 60, 60)
                frames[LIT_REFRACTION_DISABLED][2][y][x] = (100, 100, 80)
        for y in range(250, 275):
            for x in range(185, 215):
                frames[LIT_CAUSTICS_DISABLED][2][y][x] = (60, 60, 60)
        return frames

    def test_no_visible_reflection_fails(self):
        frames = self.frames()
        frames[LIT_REFLECTION_DISABLED] = frames[LIT_COMBINED]
        with self.assertRaises(SmokeFailure):
            caustic.analyze_frames(frames)

    def test_global_brightness_cannot_pass_as_reflection(self):
        frames = self.frames()
        frames[LIT_REFLECTION_DISABLED] = (400, 300, [[(0, 0, 0)] * 400 for _ in range(300)])
        with self.assertRaises(SmokeFailure):
            caustic.analyze_frames(frames)

    def test_missing_caustic_ground_contribution_fails(self):
        frames = self.frames()
        frames[LIT_CAUSTICS_DISABLED] = frames[LIT_COMBINED]
        with self.assertRaises(SmokeFailure):
            caustic.analyze_frames(frames)

    def test_capture_flags_clear_inherited_optical_state(self):
        with patch.dict(caustic.os.environ, {LIT_NWB_REFRACTION_SMOKE_CASE: LIT_NESTED, LIT_NWB_REFLECTION_SMOKE_TEMPORAL: "1", LIT_NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BA: "1"}):
            environment = caustic.capture_environment(LIT_REFLECTION_DISABLED)
        self.assertNotIn(LIT_NWB_REFRACTION_SMOKE_CASE, environment)
        self.assertNotIn(LIT_NWB_REFLECTION_SMOKE_TEMPORAL, environment)
        self.assertNotIn(LIT_NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BA, environment)


    def test_software_contributions_keep_thresholds_and_exclude_hardware_radiometry(self):
        with patch.object(caustic, LIT_ANALYZE_EXTERIOR_REFLECTION) as exterior:
            result = caustic.analyze_frames(self.frames(), software_ray_tracing=True)
        exterior.assert_not_called()
        self.assertFalse(result[LIT_REFLECTION_DISABLED][LIT_EXTERIOR_ENVIRONMENT]["evaluated"])
        for disabled in caustic.VARIANTS[1:]:
            frames = self.frames()
            frames[disabled] = frames[LIT_COMBINED]
            with self.subTest(disabled=disabled), self.assertRaises(SmokeFailure):
                caustic.analyze_frames(frames, software_ray_tracing=True)


    def test_software_environment_drops_inherited_hardware_or_scene_override(self):
        with patch.dict(caustic.os.environ, {LIT_NWB_REFRACTION_SMOKE_CASE: LIT_NESTED, LIT_NWB_REFLECTION_SMOKE_MODE: "hardware", LIT_NWB_GPU_TIMING_FILE: "inherited.csv"}):
            environment = caustic.capture_environment(LIT_COMBINED, software_ray_tracing=True)
        self.assertNotIn(LIT_NWB_REFRACTION_SMOKE_CASE, environment)
        self.assertNotIn(LIT_NWB_GPU_TIMING_FILE, environment)
        self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_MODE], LIT_SCREEN)

    def test_device_policy_arguments_are_explicit_and_mutually_exclusive(self):
        required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            caustic.parse_args(required + [LIT_REQUIRE_HARDWARE, LIT_SOFTWARE_RAY_TRACING])
        self.assertEqual(error.exception.code, 2)


class CausticExteriorTests(unittest.TestCase):
    def frames(self):
        disabled = (1280, 900, [[(75, 90, 110)] * 1280 for _ in range(900)])
        rows = [list(row) for row in disabled[2]]
        for x, y, fresnel in caustic_reference.exterior_samples():
            rows[y][x] = tuple(round(value) for value in caustic_reference.expected_environment_color(disabled[2][y][x], fresnel))
        return (1280, 900, rows), disabled

    def test_single_stable_missing_sample_cannot_hide_in_global_change_count(self):
        combined, disabled = self.frames()
        x, y, _ = caustic_reference.exterior_samples()[len(caustic_reference.exterior_samples()) // 3]
        combined[2][y][x] = disabled[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, LIT_MISSING_EXTERIOR):
            caustic.analyze_exterior_reflection(combined, disabled)

    def test_small_missing_patch_fails_while_most_reflection_is_correct(self):
        combined, disabled = self.frames()
        points = caustic_reference.exterior_samples()
        center_x, center_y, _ = points[len(points) // 2]
        changed = 0
        for x, y, _ in points:
            if abs(x - center_x) <= 8 and abs(y - center_y) <= 8:
                combined[2][y][x] = disabled[2][y][x]
                changed += 1
        self.assertGreater(changed, 2)
        self.assertLess(changed, len(points) * 0.01)
        with self.assertRaisesRegex(SmokeFailure, LIT_MISSING_EXTERIOR):
            caustic.analyze_exterior_reflection(combined, disabled)

    def test_two_byte_rounding_margin_passes(self):
        combined, disabled = self.frames()
        for x, y, _ in caustic_reference.exterior_samples():
            combined[2][y][x] = tuple(max(0, value - 1) for value in combined[2][y][x])
        caustic.analyze_exterior_reflection(combined, disabled)

    def test_nonreflecting_or_saturated_scene_cannot_evade_sample_coverage(self):
        combined, disabled = self.frames()
        with self.assertRaisesRegex(SmokeFailure, LIT_MISSING_EXTERIOR):
            caustic.analyze_exterior_reflection(disabled, disabled)
        saturated = (1280, 900, [[(255, 255, 255)] * 1280 for _ in range(900)])
        with self.assertRaisesRegex(SmokeFailure, "too few"):
            caustic.analyze_exterior_reflection(saturated, saturated)

    def test_reflection_multiplier_cannot_replace_physical_prediction(self):
        combined, disabled = self.frames()
        for x, y, _ in caustic_reference.exterior_samples():
            combined[2][y][x] = (200, 210, 220)
        with self.assertRaisesRegex(SmokeFailure, "radiometric"):
            caustic.analyze_exterior_reflection(combined, disabled)

    def test_nearest_mesh_hit_and_outward_escape_are_distinct(self):
        mesh = caustic_reference.sphere_mesh()
        hit = mesh.intersect(caustic_reference.CAMERA, (0.0, 0.0, 1.0))
        self.assertAlmostEqual(hit[0], 1.5, places=5)
        self.assertLess(hit[3].normal[2], -0.9)
        self.assertIsNone(mesh.intersect((0, 0.85, -0.701), (0.0, 0.0, -1.0)))
        self.assertIsNone(mesh.intersect((2.0, 0.85, -2.2), (0.0, 0.0, 1.0)))


if __name__ == LIT_MAIN:
    unittest.main()
