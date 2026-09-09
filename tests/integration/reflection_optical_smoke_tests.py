#!/usr/bin/env python3
"""Independent physical identities and intentionally incorrect optical image/counter evidence."""

import math
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import reflection_optical_reference as reference
import reflection_optical_smoke as smoke
import caustic_optical_smoke as caustic
import caustic_optical_reference as caustic_reference
from reflection_smoke import SmokeFailure


class ReferenceTests(unittest.TestCase):
    def test_normal_air_glass_air_has_two_fresnel_crossings(self):
        clear, details = reference.trace_transmission("optical_clear", (0, 1.4, 0), (0, 0, -1))
        expected = reference.CHART_RADIANCE * 0.96 ** 2
        self.assertAlmostEqual(clear[0], expected, places=10)
        self.assertEqual(details["crossings"], 2)
        self.assertEqual(details["reason"], "chart")

    def test_beer_uses_geometric_distance_and_half_authored_values(self):
        clear, _ = reference.trace_transmission("optical_clear", (0, 1.4, 0), (0, 0, -1))
        tinted, _ = reference.trace_transmission("optical_tinted", (0, 1.4, 0), (0, 0, -1))
        self.assertAlmostEqual(tinted[0] / clear[0], reference.half(0.55) ** 2, places=5)
        self.assertEqual(tinted[2], clear[2])

    def test_inside_origin_retains_radiance_eta_squared(self):
        actual, details = reference.trace_transmission("optical_inside", (0, 1.4, 0), (0, 0, -1))
        self.assertAlmostEqual(actual[0], reference.CHART_RADIANCE * 0.96 * 1.5 ** 2, places=10)
        self.assertEqual(details["crossings"], 1)
        self.assertGreater(actual[0], 1.0)

    def test_oblique_snell_and_exact_fresnel(self):
        direction = (0.6, 0.0, -0.8)
        fresnel, transmitted = reference.fresnel_snell(direction, (0, 0, 1), 1, 1.5)
        self.assertAlmostEqual(transmitted[0], 0.4, places=12)
        self.assertAlmostEqual(transmitted[2], -math.sqrt(0.84), places=12)
        rs = (0.8 - 1.5 * math.sqrt(0.84)) / (0.8 + 1.5 * math.sqrt(0.84))
        rp = (1.5 * 0.8 - math.sqrt(0.84)) / (1.5 * 0.8 + math.sqrt(0.84))
        self.assertAlmostEqual(fresnel, (rs * rs + rp * rp) / 2, places=12)
        self.assertNotAlmostEqual(fresnel, 0.04 + 0.96 * 0.2 ** 5, places=5)

    def test_tir_and_unresolved_residual_have_distinct_energy(self):
        angle = math.sqrt(0.5)
        fresnel, transmitted = reference.fresnel_snell((angle, 0, -angle), (0, 0, 1), 1.5, 1)
        self.assertEqual((fresnel, transmitted), (1.0, None))
        complete, details = reference.trace_transmission("optical_tir", (0, 1.4, 0), (0, 0, -1), 16)
        limited, limit = reference.trace_transmission("optical_tir", (0, 1.4, 0), (0, 0, -1), 3)
        self.assertEqual(details["tir_events"], 1)
        self.assertGreater(complete[0], 1.0)
        self.assertEqual(limited, (0.0, 0.0, 0.0))
        self.assertEqual(limit["reason"], "query_limit")
        self.assertEqual(limit["tir_events"], 1)

    def test_priority_replaces_medium_without_lifo_removal(self):
        first, _ = reference.trace_transmission("optical_priority_a", (0, 1.4, 0), (0, 0, -1))
        second, _ = reference.trace_transmission("optical_priority_b", (0, 1.4, 0), (0, 0, -1))
        self.assertGreater(second[0], first[0])
        self.assertLess(second[2], first[2])
        self.assertGreater(sum(abs(a - b) for a, b in zip(first, second)), 0.3)

    def test_mixed_modes_and_capacity_fail_without_environment(self):
        for case, reason in (("optical_mixed", "ambiguous"), ("optical_overflow", "medium_overflow"),
            ("optical_unspecified", "unsupported")):
            actual, details = reference.trace_transmission(case, (0, 1.4, 0), (0, 0, -1))
            self.assertEqual(actual, (0.0, 0.0, 0.0))
            self.assertEqual(details["reason"], reason)

    def test_disconnected_single_instance_has_two_intervals(self):
        actual, details = reference.trace_transmission("optical_same_mesh", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["crossings"], 4)
        self.assertAlmostEqual(actual[2], reference.CHART_RADIANCE * 0.96 ** 4, places=10)

    def test_overlapping_components_integrate_union_not_double_absorption(self):
        single, _ = reference.trace_transmission("optical_union_single", (0, 1.4, 0), (0, 0, -1))
        union, details = reference.trace_transmission("optical_union_same_mesh", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["crossings"], 4)
        for a, b in zip(single, union):
            self.assertAlmostEqual(a, b, places=5)
        self.assertAlmostEqual(union[2], reference.CHART_RADIANCE * 0.96 ** 2, places=10)

    def test_priority_ties_follow_creation_identity_not_boundary_entry_order(self):
        for priority, tie in (("optical_priority_a", "optical_priority_tie_a"), ("optical_priority_b", "optical_priority_tie_b")):
            expected, _ = reference.trace_transmission(priority, (0, 1.4, 0), (0, 0, -1))
            actual, _ = reference.trace_transmission(tie, (0, 1.4, 0), (0, 0, -1))
            self.assertEqual(actual, expected)

    def test_independent_coincident_boundaries_are_not_an_arbitrary_winner(self):
        actual, details = reference.trace_transmission("optical_coincident_independent", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(actual, (0, 0, 0))
        self.assertEqual(details["reason"], "ambiguous")

    def test_interface_validation_is_counted_before_transport(self):
        _, details = reference.trace_transmission("optical_clear", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["queries"], 5)
        _, details = reference.trace_transmission("optical_disconnected", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["queries"], 9)
        _, details = reference.trace_transmission("optical_nested3", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["queries"], 13)
        _, details = reference.trace_transmission("optical_unspecified", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["queries"], 1)
        _, details = reference.trace_transmission("optical_alpha_before", (3, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["queries"], 2)
        self.assertEqual(details["reason"], "opaque_alpha")

    def test_nested_fixtures_are_strictly_contained_on_all_axes(self):
        for case in ("optical_inside_nested", "optical_nested2", "optical_nested3", "optical_overflow"):
            objects = reference.boundaries(case)
            for outer, inner in zip(objects, objects[1:]):
                for (normal_a, extent_a), (normal_b, extent_b) in zip(outer.planes, inner.planes):
                    self.assertEqual(normal_a, normal_b)
                    self.assertLess(extent_b, extent_a)

    def test_torus_uses_authored_geometric_triangles_and_reentry(self):
        self.assertEqual(len(reference.authored_torus_triangles()), 1280)
        _, details = reference.trace_transmission("optical_torus", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details["crossings"], 4)

    def test_alpha_order_matters_inside_known_transmission(self):
        before, _ = reference.trace_transmission("optical_alpha_before", (0, 1.4, 0), (0, 0, -1))
        after, _ = reference.trace_transmission("optical_alpha_after", (0, 1.4, 0), (0, 0, -1))
        self.assertGreater(before[0] - after[0], 0.3)
        self.assertGreater(before[1] - after[1], 0.1)
        self.assertAlmostEqual(before[2], after[2], places=10)

    def test_presentation_round_trip_allows_physical_inside_energy(self):
        for value in (0.0, 0.1, 0.8, 1.85, 2.06):
            self.assertLess(abs(reference.decode_radiance(reference.encode_radiance(value)) - value), 0.035)


class ImageOracleTests(unittest.TestCase):
    def synthetic_frame(self, case, queries=16, channel_scale=1.0, shift=0):
        # Synthetic pixels exercise the analyzer; the ReferenceTests above separately establish physical identities.
        rows = [[(0, 0, 0)] * 960 for _ in range(720)]
        for x, y in self.points:
            value, _ = reference.pixel_reference(case, x + shift + 0.5, y + 0.5, max_queries=queries)
            rows[y][x] = tuple(reference.encode_radiance(channel * channel_scale) for channel in value)
        return 960, 720, rows

    def setUp(self):
        self.points = tuple((x, y) for y in range(204, 509, 16) for x in range(214, 749, 16))

    def analyze(self, frame, spec):
        with patch.object(smoke, "reference_points", return_value=iter(self.points)):
            return smoke.analyze_image(frame, spec)

    def test_quantized_reference_passes(self):
        spec = smoke.OpticalCapture("optical_tinted", "optical_tinted")
        result = self.analyze(self.synthetic_frame(spec.case), spec)
        self.assertGreater(result["stable_reference_pixels"], 150)
        self.assertLess(result["linear_rgb_mae"], 0.006)

    def test_absorption_omission_fails(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame("optical_clear"), smoke.OpticalCapture("optical_tinted", "optical_tinted"))

    def test_shifted_stripes_fail_even_when_color_energy_is_similar(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame("optical_tinted", shift=12), smoke.OpticalCapture("optical_tinted", "optical_tinted"))

    def test_straight_environment_leak_after_query_limit_fails(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame("optical_clear"), smoke.OpticalCapture("optical_query_limit", "optical_clear", 1))

    def test_half_energy_cannot_pass_using_relative_normalization(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame("optical_tinted", channel_scale=0.5), smoke.OpticalCapture("optical_tinted", "optical_tinted"))

    def test_torus_cannot_pass_using_only_unobstructed_or_single_interval_samples(self):
        frame = (960, 720, [[(0, 0, 0)] * 960 for _ in range(720)])
        for crossings in (0, 2):
            with patch.object(smoke, "pixel_reference", return_value=((0, 0, 0), {"reason": "chart", "crossings": crossings})):
                with self.assertRaisesRegex(SmokeFailure, "re-entry coverage"):
                    self.analyze(frame, smoke.OpticalCapture("optical_torus", "optical_torus"))


class StatisticsTests(unittest.TestCase):
    def log(self, overrides=None):
        values = dict(zip(smoke.OPTICS_FIELDS, (9, 2, 16, 500, 0, 100, 0, 0, 0, 0, 0, 1)))
        values.update(overrides or {})
        return "ReflectionSmokeOptics: " + " ".join(f"{name}={values[name]}" for name in smoke.OPTICS_FIELDS)

    def validate(self, text, spec=smoke.OpticalCapture("optical_clear", "optical_clear")):
        statistics = [{"sequence": 9, "generation": 2, "hardware_rays": 100, "frame": 9}]
        with patch.object(smoke, "parse_statistics", return_value=statistics), patch.object(smoke, "validate_statistics"):
            return smoke.validate_optics(text, spec)

    def test_actual_queries_are_distinct_from_primary_paths(self):
        result = self.validate(self.log())
        self.assertEqual(result["stable_optics"]["hardware_queries"], 500)

    def test_query_cap_violation_fails(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log({"hardware_queries": 1601}))

    def test_plain_variant_required_for_opaque_only_scene(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log(), smoke.OpticalCapture("optical_reference", "optical_reference"))

    def test_frozen_budget_mismatch_fails(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log({"max_queries": 8}))

    def test_counter_source_mismatch_fails(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log({"sequence": 8}))

    def test_negative_case_requires_actual_reason(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log(), smoke.OpticalCapture("optical_unspecified", "optical_unspecified"))

    def test_malformed_or_duplicate_fields_fail(self):
        for text in ("", self.log() + " sequence=9", self.log().replace("limited_paths=0", "other=0")):
            with self.assertRaises(SmokeFailure):
                smoke.parse_optics(text)


class CombinedCausticTests(unittest.TestCase):
    def setUp(self):
        # These small synthetic images isolate the broad toggle/ground checks. The mesh/radiometric oracle has
        # separate full-resolution positive and deliberately missing-patch tests below.
        isolate_exterior = patch.object(caustic, "analyze_exterior_reflection", return_value={"isolated_in_test": True})
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
                frames["reflection_disabled"][2][y][x] = (60, 60, 60)
                frames["refraction_disabled"][2][y][x] = (100, 100, 80)
        for y in range(250, 275):
            for x in range(185, 215):
                frames["caustics_disabled"][2][y][x] = (60, 60, 60)
        return frames

    def test_distinct_sphere_and_ground_contributions_pass(self):
        result = caustic.analyze_frames(self.frames())
        self.assertEqual(result["reflection_disabled"]["changed_sphere_pixels"], 1500)
        self.assertEqual(result["caustics_disabled"]["changed_ground_pixels"], 750)

    def test_no_visible_reflection_fails(self):
        frames = self.frames()
        frames["reflection_disabled"] = frames["combined"]
        with self.assertRaises(SmokeFailure):
            caustic.analyze_frames(frames)

    def test_global_brightness_cannot_pass_as_reflection(self):
        frames = self.frames()
        frames["reflection_disabled"] = (400, 300, [[(0, 0, 0)] * 400 for _ in range(300)])
        with self.assertRaises(SmokeFailure):
            caustic.analyze_frames(frames)

    def test_missing_caustic_ground_contribution_fails(self):
        frames = self.frames()
        frames["caustics_disabled"] = frames["combined"]
        with self.assertRaises(SmokeFailure):
            caustic.analyze_frames(frames)

    def test_capture_flags_clear_inherited_optical_state(self):
        with patch.dict(caustic.os.environ, {"NWB_REFRACTION_SMOKE_CASE": "nested", "NWB_REFLECTION_SMOKE_TEMPORAL": "1"}):
            environment = caustic.capture_environment("reflection_disabled")
        self.assertNotIn("NWB_REFRACTION_SMOKE_CASE", environment)
        self.assertNotIn("NWB_REFLECTION_SMOKE_TEMPORAL", environment)
        self.assertEqual(environment["NWB_REFLECTION_SMOKE_MODE"], "disabled")
        self.assertEqual(environment["NWB_CAUSTIC_SMOKE_ENABLED"], "1")

    def test_disabled_refraction_retains_glass_reflection_composition_pass(self):
        args = SimpleNamespace(output_directory=Path("output"), executable=Path("app.exe"),
            working_directory=Path("runtime"), logserver_executable=None, timeout=60,
            require_hardware=True, application_arg=[])
        with patch.object(caustic.subprocess, "run", return_value=SimpleNamespace(returncode=77)) as run:
            self.assertIsNone(caustic.capture(args, "refraction_disabled"))
        command = run.call_args.args[0]
        pairs = set(zip(command, command[1:]))
        self.assertIn(("--expect-log-message", "CausticSphereSmokeProject: camera refraction disabled"), pairs)
        self.assertIn(("--expect-log-message", "AVBOIT refraction resolve: screen-space"), pairs)
        self.assertIn(("--reject-log-message", "AVBOIT refraction resolve: hardware"), pairs)
        self.assertNotIn(("--reject-log-message", "AVBOIT refraction resolve:"), pairs)


class CausticExteriorTests(unittest.TestCase):
    def frames(self):
        disabled = (1280, 900, [[(75, 90, 110)] * 1280 for _ in range(900)])
        rows = [list(row) for row in disabled[2]]
        for x, y, fresnel in caustic_reference.exterior_samples():
            rows[y][x] = tuple(round(value) for value in caustic_reference.expected_environment_color(disabled[2][y][x], fresnel))
        return (1280, 900, rows), disabled

    def test_mesh_selected_quantized_environment_passes(self):
        combined, disabled = self.frames()
        result = caustic.analyze_exterior_reflection(combined, disabled)
        self.assertGreater(result["tested_samples"], 6000)
        self.assertEqual(result["missing_samples"], 0)
        self.assertLess(result["rgb_byte_mae"], 0.3)
        self.assertEqual(result["predictor"]["environment"], (0.6, 0.7, 1.0))
        self.assertEqual(len(result["predictor"]["mesh_lf_sha256"]), 64)

    def test_single_stable_missing_sample_cannot_hide_in_global_change_count(self):
        combined, disabled = self.frames()
        x, y, _ = caustic_reference.exterior_samples()[len(caustic_reference.exterior_samples()) // 3]
        combined[2][y][x] = disabled[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "missing exterior"):
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
        with self.assertRaisesRegex(SmokeFailure, "missing exterior"):
            caustic.analyze_exterior_reflection(combined, disabled)

    def test_two_byte_rounding_margin_passes(self):
        combined, disabled = self.frames()
        for x, y, _ in caustic_reference.exterior_samples():
            combined[2][y][x] = tuple(max(0, value - 1) for value in combined[2][y][x])
        caustic.analyze_exterior_reflection(combined, disabled)

    def test_nonreflecting_or_saturated_scene_cannot_evade_sample_coverage(self):
        combined, disabled = self.frames()
        with self.assertRaisesRegex(SmokeFailure, "missing exterior"):
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

    def test_ground_occlusion_is_geometry_checked(self):
        self.assertTrue(caustic_reference.hits_ground((0, 0.85, -0.7), (0, -1, 0)))
        self.assertFalse(caustic_reference.hits_ground((0, 0.85, -0.7), (0, 1, 0)))
        self.assertFalse(caustic_reference.hits_ground((4, 0.85, -0.7), (0, -1, 0)))

    def test_normal_incidence_uses_physical_ior_f0_and_presentation(self):
        expected = caustic_reference.expected_environment_color((0, 0, 0), 0.04)
        for encoded, environment in zip(expected, caustic_reference.ENVIRONMENT):
            self.assertAlmostEqual(caustic_reference.decode_scene_radiance(encoded), 0.04 * environment, places=12)


if __name__ == "__main__":
    unittest.main()
