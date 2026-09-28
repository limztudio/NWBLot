#!/usr/bin/env python3
"""Independent physical identities and intentionally incorrect optical image/counter evidence."""

import contextlib
import io
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

# Shared literals (no inline hardcodes below this block).
LIT_OPTICAL_CLEAR = "optical_clear"
LIT_CROSSINGS = "crossings"
LIT_REASON = "reason"
LIT_CHART = "chart"
LIT_OPTICAL_TINTED = "optical_tinted"
LIT_OPTICAL_TIR = "optical_tir"
LIT_TIR_EVENTS = "tir_events"
LIT_OPTICAL_PRIORITY_A = "optical_priority_a"
LIT_OPTICAL_PRIORITY_B = "optical_priority_b"
LIT_AMBIGUOUS = "ambiguous"
LIT_OPTICAL_OVERFLOW = "optical_overflow"
LIT_OPTICAL_UNSPECIFIED = "optical_unspecified"
LIT_QUERIES = "queries"
LIT_OPTICAL_NESTED3 = "optical_nested3"
LIT_OPTICAL_ALPHA_BEFORE = "optical_alpha_before"
LIT_OPTICAL_TORUS = "optical_torus"
LIT_SEQUENCE = "sequence"
LIT_HARDWARE_QUERIES = "hardware_queries"
LIT_OPTICAL_REFERENCE = "optical_reference"
LIT_ANALYZE_EXTERIOR_REFLECTION = "analyze_exterior_reflection"
LIT_REFLECTION_DISABLED = "reflection_disabled"
LIT_REFRACTION_DISABLED = "refraction_disabled"
LIT_CAUSTICS_DISABLED = "caustics_disabled"
LIT_CHANGED_SPHERE_PIXELS = "changed_sphere_pixels"
LIT_CHANGED_GROUND_PIXELS = "changed_ground_pixels"
LIT_COMBINED = "combined"
LIT_NWB_REFRACTION_SMOKE_CASE = "NWB_REFRACTION_SMOKE_CASE"
LIT_NESTED = "nested"
LIT_NWB_REFLECTION_SMOKE_TEMPORAL = "NWB_REFLECTION_SMOKE_TEMPORAL"
LIT_NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BA = "NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BACKDROP"
LIT_NWB_REFLECTION_SMOKE_MODE = "NWB_REFLECTION_SMOKE_MODE"
LIT_DISABLED = "disabled"
LIT_NWB_CAUSTIC_SMOKE_ENABLED = "NWB_CAUSTIC_SMOKE_ENABLED"
LIT_OUTPUT = "output"
LIT_APP_EXE = "app.exe"
LIT_RUNTIME = "runtime"
LIT_RUN = "run"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA = "RendererSystem: dispatched software shadow traversal"
LIT_SKIP_LOG_MESSAGE = "--skip-log-message"
LIT_AVBOIT_REFRACTION_RESOLVE_SCREEN_SPACE = "AVBOIT refraction resolve: screen-space"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
LIT_AVBOIT_REFRACTION_RESOLVE_HARDWARE = "AVBOIT refraction resolve: hardware"
LIT_EXTERIOR_ENVIRONMENT = "exterior_environment"
LIT_PROOF = "proof"
LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_CAU = "RendererSystem: dispatched software caustic producer"
LIT_SCREEN = "screen"
LIT_NWB_REFRACTION_SMOKE_HARDWARE = "NWB_REFRACTION_SMOKE_HARDWARE"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_SOFTWARE_RAY_TRACING = "--software-ray-tracing"
LIT_REQUIRE_HARDWARE = "--require-hardware"
LIT_PREDICTOR = "predictor"
LIT_MISSING_EXTERIOR = "missing exterior"
LIT_MAIN = "__main__"


class ReferenceTests(unittest.TestCase):
    def test_normal_air_glass_air_has_two_fresnel_crossings(self):
        clear, details = reference.trace_transmission(LIT_OPTICAL_CLEAR, (0, 1.4, 0), (0, 0, -1))
        expected = reference.CHART_RADIANCE * 0.96 ** 2
        self.assertAlmostEqual(clear[0], expected, places=10)
        self.assertEqual(details[LIT_CROSSINGS], 2)
        self.assertEqual(details[LIT_REASON], LIT_CHART)

    def test_beer_uses_geometric_distance_and_half_authored_values(self):
        clear, _ = reference.trace_transmission(LIT_OPTICAL_CLEAR, (0, 1.4, 0), (0, 0, -1))
        tinted, _ = reference.trace_transmission(LIT_OPTICAL_TINTED, (0, 1.4, 0), (0, 0, -1))
        self.assertAlmostEqual(tinted[0] / clear[0], reference.half(0.55) ** 2, places=5)
        self.assertEqual(tinted[2], clear[2])

    def test_inside_origin_retains_radiance_eta_squared(self):
        actual, details = reference.trace_transmission("optical_inside", (0, 1.4, 0), (0, 0, -1))
        self.assertAlmostEqual(actual[0], reference.CHART_RADIANCE * 0.96 * 1.5 ** 2, places=10)
        self.assertEqual(details[LIT_CROSSINGS], 1)
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
        complete, details = reference.trace_transmission(LIT_OPTICAL_TIR, (0, 1.4, 0), (0, 0, -1), 16)
        limited, limit = reference.trace_transmission(LIT_OPTICAL_TIR, (0, 1.4, 0), (0, 0, -1), 3)
        self.assertEqual(details[LIT_TIR_EVENTS], 1)
        self.assertGreater(complete[0], 1.0)
        self.assertEqual(limited, (0.0, 0.0, 0.0))
        self.assertEqual(limit[LIT_REASON], "query_limit")
        self.assertEqual(limit[LIT_TIR_EVENTS], 1)

    def test_priority_replaces_medium_without_lifo_removal(self):
        first, _ = reference.trace_transmission(LIT_OPTICAL_PRIORITY_A, (0, 1.4, 0), (0, 0, -1))
        second, _ = reference.trace_transmission(LIT_OPTICAL_PRIORITY_B, (0, 1.4, 0), (0, 0, -1))
        self.assertGreater(second[0], first[0])
        self.assertLess(second[2], first[2])
        self.assertGreater(sum(abs(a - b) for a, b in zip(first, second)), 0.3)

    def test_mixed_modes_and_capacity_fail_without_environment(self):
        for case, reason in (("optical_mixed", LIT_AMBIGUOUS), (LIT_OPTICAL_OVERFLOW, "medium_overflow"),
            (LIT_OPTICAL_UNSPECIFIED, "unsupported")):
            actual, details = reference.trace_transmission(case, (0, 1.4, 0), (0, 0, -1))
            self.assertEqual(actual, (0.0, 0.0, 0.0))
            self.assertEqual(details[LIT_REASON], reason)

    def test_disconnected_single_instance_has_two_intervals(self):
        actual, details = reference.trace_transmission("optical_same_mesh", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_CROSSINGS], 4)
        self.assertAlmostEqual(actual[2], reference.CHART_RADIANCE * 0.96 ** 4, places=10)

    def test_overlapping_components_integrate_union_not_double_absorption(self):
        single, _ = reference.trace_transmission("optical_union_single", (0, 1.4, 0), (0, 0, -1))
        union, details = reference.trace_transmission("optical_union_same_mesh", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_CROSSINGS], 4)
        for a, b in zip(single, union):
            self.assertAlmostEqual(a, b, places=5)
        self.assertAlmostEqual(union[2], reference.CHART_RADIANCE * 0.96 ** 2, places=10)

    def test_priority_ties_follow_creation_identity_not_boundary_entry_order(self):
        for priority, tie in ((LIT_OPTICAL_PRIORITY_A, "optical_priority_tie_a"), (LIT_OPTICAL_PRIORITY_B, "optical_priority_tie_b")):
            expected, _ = reference.trace_transmission(priority, (0, 1.4, 0), (0, 0, -1))
            actual, _ = reference.trace_transmission(tie, (0, 1.4, 0), (0, 0, -1))
            self.assertEqual(actual, expected)

    def test_independent_coincident_boundaries_are_not_an_arbitrary_winner(self):
        actual, details = reference.trace_transmission("optical_coincident_independent", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(actual, (0, 0, 0))
        self.assertEqual(details[LIT_REASON], LIT_AMBIGUOUS)

    def test_interface_validation_is_counted_before_transport(self):
        _, details = reference.trace_transmission(LIT_OPTICAL_CLEAR, (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_QUERIES], 5)
        _, details = reference.trace_transmission("optical_disconnected", (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_QUERIES], 9)
        _, details = reference.trace_transmission(LIT_OPTICAL_NESTED3, (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_QUERIES], 13)
        _, details = reference.trace_transmission(LIT_OPTICAL_UNSPECIFIED, (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_QUERIES], 1)
        _, details = reference.trace_transmission(LIT_OPTICAL_ALPHA_BEFORE, (3, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_QUERIES], 2)
        self.assertEqual(details[LIT_REASON], "opaque_alpha")

    def test_nested_fixtures_are_strictly_contained_on_all_axes(self):
        for case in ("optical_inside_nested", "optical_nested2", LIT_OPTICAL_NESTED3, LIT_OPTICAL_OVERFLOW):
            objects = reference.boundaries(case)
            for outer, inner in zip(objects, objects[1:]):
                for (normal_a, extent_a), (normal_b, extent_b) in zip(outer.planes, inner.planes):
                    self.assertEqual(normal_a, normal_b)
                    self.assertLess(extent_b, extent_a)

    def test_torus_uses_authored_geometric_triangles_and_reentry(self):
        self.assertEqual(len(reference.authored_torus_triangles()), 1280)
        _, details = reference.trace_transmission(LIT_OPTICAL_TORUS, (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_CROSSINGS], 4)

    def test_alpha_order_matters_inside_known_transmission(self):
        before, _ = reference.trace_transmission(LIT_OPTICAL_ALPHA_BEFORE, (0, 1.4, 0), (0, 0, -1))
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
        spec = smoke.OpticalCapture(LIT_OPTICAL_TINTED, LIT_OPTICAL_TINTED)
        result = self.analyze(self.synthetic_frame(spec.case), spec)
        self.assertGreater(result["stable_reference_pixels"], 150)
        self.assertLess(result["linear_rgb_mae"], 0.006)

    def test_absorption_omission_fails(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_CLEAR), smoke.OpticalCapture(LIT_OPTICAL_TINTED, LIT_OPTICAL_TINTED))

    def test_shifted_stripes_fail_even_when_color_energy_is_similar(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_TINTED, shift=12), smoke.OpticalCapture(LIT_OPTICAL_TINTED, LIT_OPTICAL_TINTED))

    def test_straight_environment_leak_after_query_limit_fails(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_CLEAR), smoke.OpticalCapture("optical_query_limit", LIT_OPTICAL_CLEAR, 1))

    def test_half_energy_cannot_pass_using_relative_normalization(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_TINTED, channel_scale=0.5), smoke.OpticalCapture(LIT_OPTICAL_TINTED, LIT_OPTICAL_TINTED))

    def test_torus_cannot_pass_using_only_unobstructed_or_single_interval_samples(self):
        frame = (960, 720, [[(0, 0, 0)] * 960 for _ in range(720)])
        for crossings in (0, 2):
            with patch.object(smoke, "pixel_reference", return_value=((0, 0, 0), {LIT_REASON: LIT_CHART, LIT_CROSSINGS: crossings})):
                with self.assertRaisesRegex(SmokeFailure, "re-entry coverage"):
                    self.analyze(frame, smoke.OpticalCapture(LIT_OPTICAL_TORUS, LIT_OPTICAL_TORUS))


class StatisticsTests(unittest.TestCase):
    def log(self, overrides=None):
        values = dict(zip(smoke.OPTICS_FIELDS, (9, 2, 16, 500, 0, 100, 0, 0, 0, 0, 0, 1)))
        values.update(overrides or {})
        return "ReflectionSmokeOptics: " + " ".join(f"{name}={values[name]}" for name in smoke.OPTICS_FIELDS)

    def validate(self, text, spec=smoke.OpticalCapture(LIT_OPTICAL_CLEAR, LIT_OPTICAL_CLEAR)):
        statistics = [{LIT_SEQUENCE: 9, "generation": 2, "hardware_rays": 100, "frame": 9}]
        with patch.object(smoke, "parse_statistics", return_value=statistics), patch.object(smoke, "validate_statistics"):
            return smoke.validate_optics(text, spec)

    def test_actual_queries_are_distinct_from_primary_paths(self):
        result = self.validate(self.log())
        self.assertEqual(result["stable_optics"][LIT_HARDWARE_QUERIES], 500)

    def test_query_cap_violation_fails(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log({LIT_HARDWARE_QUERIES: 1601}))

    def test_plain_variant_required_for_opaque_only_scene(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log(), smoke.OpticalCapture(LIT_OPTICAL_REFERENCE, LIT_OPTICAL_REFERENCE))

    def test_frozen_budget_mismatch_fails(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log({"max_queries": 8}))

    def test_counter_source_mismatch_fails(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log({LIT_SEQUENCE: 8}))

    def test_negative_case_requires_actual_reason(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log(), smoke.OpticalCapture(LIT_OPTICAL_UNSPECIFIED, LIT_OPTICAL_UNSPECIFIED))

    def test_malformed_or_duplicate_fields_fail(self):
        for text in ("", self.log() + " sequence=9", self.log().replace("limited_paths=0", "other=0")):
            with self.assertRaises(SmokeFailure):
                smoke.parse_optics(text)


class CombinedCausticTests(unittest.TestCase):
    def setUp(self):
        # These small synthetic images isolate the broad toggle/ground checks. The mesh/radiometric oracle has
        # separate full-resolution positive and deliberately missing-patch tests below.
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

    def test_distinct_sphere_and_ground_contributions_pass(self):
        result = caustic.analyze_frames(self.frames())
        self.assertEqual(result[LIT_REFLECTION_DISABLED][LIT_CHANGED_SPHERE_PIXELS], 1500)
        self.assertEqual(result[LIT_CAUSTICS_DISABLED][LIT_CHANGED_GROUND_PIXELS], 750)

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
        with patch.dict(caustic.os.environ, {LIT_NWB_REFRACTION_SMOKE_CASE: LIT_NESTED, LIT_NWB_REFLECTION_SMOKE_TEMPORAL: "1",
            LIT_NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BA: "1"}):
            environment = caustic.capture_environment(LIT_REFLECTION_DISABLED)
        self.assertNotIn(LIT_NWB_REFRACTION_SMOKE_CASE, environment)
        self.assertNotIn(LIT_NWB_REFLECTION_SMOKE_TEMPORAL, environment)
        self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_MODE], LIT_DISABLED)
        self.assertEqual(environment[LIT_NWB_CAUSTIC_SMOKE_ENABLED], "1")
        self.assertNotIn(LIT_NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BA, environment)

    def test_caustic_capture_requires_hardware_traversal_when_missing_hardware_can_skip(self):
        for require_hardware in (False, True):
            args = SimpleNamespace(output_directory=Path(LIT_OUTPUT), executable=Path(LIT_APP_EXE),
                working_directory=Path(LIT_RUNTIME), logserver_executable=None, timeout=60,
                require_hardware=require_hardware, software_ray_tracing=False, caustic_photon_grid_divisor=1, application_arg=[])
            with self.subTest(require_hardware=require_hardware), patch.object(caustic.subprocess, LIT_RUN,
                return_value=SimpleNamespace(returncode=77)) as run:
                self.assertIsNone(caustic.capture(args, LIT_COMBINED))
            command = run.call_args.args[0]
            pairs = set(zip(command, command[1:]))
            self.assertIn((LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA), pairs)
            self.assertNotIn((LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA), pairs)
            if require_hardware:
                self.assertIn((LIT_EXPECT_LOG_MESSAGE, "natural hardware shadow route selected on RayQuery-capable hardware"), pairs)
            else:
                self.assertIn((LIT_SKIP_LOG_MESSAGE, "natural software-only shadow route selected because RayQuery-capable hardware is unavailable"), pairs)

    def test_disabled_refraction_retains_glass_reflection_composition_pass(self):
        args = SimpleNamespace(output_directory=Path(LIT_OUTPUT), executable=Path(LIT_APP_EXE),
            working_directory=Path(LIT_RUNTIME), logserver_executable=None, timeout=60,
            require_hardware=True, software_ray_tracing=False, caustic_photon_grid_divisor=1, application_arg=[])
        with patch.object(caustic.subprocess, LIT_RUN, return_value=SimpleNamespace(returncode=77)) as run:
            self.assertIsNone(caustic.capture(args, LIT_REFRACTION_DISABLED))
        command = run.call_args.args[0]
        pairs = set(zip(command, command[1:]))
        self.assertIn((LIT_EXPECT_LOG_MESSAGE, "CausticSphereSmokeProject: camera refraction disabled"), pairs)
        self.assertIn((LIT_EXPECT_LOG_MESSAGE, LIT_AVBOIT_REFRACTION_RESOLVE_SCREEN_SPACE), pairs)
        self.assertIn((LIT_REJECT_LOG_MESSAGE, LIT_AVBOIT_REFRACTION_RESOLVE_HARDWARE), pairs)
        self.assertNotIn((LIT_REJECT_LOG_MESSAGE, "AVBOIT refraction resolve:"), pairs)


    def test_software_contributions_keep_thresholds_and_exclude_hardware_radiometry(self):
        with patch.object(caustic, LIT_ANALYZE_EXTERIOR_REFLECTION) as exterior:
            result = caustic.analyze_frames(self.frames(), software_ray_tracing=True)
        exterior.assert_not_called()
        self.assertEqual(result[LIT_REFLECTION_DISABLED][LIT_CHANGED_SPHERE_PIXELS], 1500)
        self.assertEqual(result[LIT_CAUSTICS_DISABLED][LIT_CHANGED_GROUND_PIXELS], 750)
        self.assertFalse(result[LIT_REFLECTION_DISABLED][LIT_EXTERIOR_ENVIRONMENT]["evaluated"])
        for disabled in caustic.VARIANTS[1:]:
            frames = self.frames()
            frames[disabled] = frames[LIT_COMBINED]
            with self.subTest(disabled=disabled), self.assertRaises(SmokeFailure):
                caustic.analyze_frames(frames, software_ray_tracing=True)

    def test_hardware_mode_keeps_exterior_radiometric_oracle(self):
        with patch.object(caustic, LIT_ANALYZE_EXTERIOR_REFLECTION, return_value={LIT_PROOF: True}) as exterior:
            result = caustic.analyze_frames(self.frames())
        exterior.assert_called_once()
        self.assertEqual(result[LIT_REFLECTION_DISABLED][LIT_EXTERIOR_ENVIRONMENT], {LIT_PROOF: True})

    def test_capture_software_route_requires_real_disabled_device_for_every_toggle(self):
        args = SimpleNamespace(output_directory=Path(LIT_OUTPUT), executable=Path(LIT_APP_EXE),
            working_directory=Path(LIT_RUNTIME), logserver_executable=None, timeout=150,
            require_hardware=False, software_ray_tracing=True, caustic_photon_grid_divisor=1, application_arg=[])
        for variant in caustic.VARIANTS:
            with self.subTest(variant=variant), patch.object(caustic.subprocess, LIT_RUN,
                return_value=SimpleNamespace(returncode=77)) as run:
                self.assertIsNone(caustic.capture(args, variant))
            command = run.call_args.args[0]
            pairs = set(zip(command, command[1:]))
            self.assertIn("--application-arg=--disable-hardware-ray-tracing", command)
            self.assertIn("--application-arg=--gpudbg", command)
            self.assertNotIn(LIT_SKIP_LOG_MESSAGE, command)
            self.assertIn((LIT_EXPECT_LOG_MESSAGE,
                "CausticSphereSmokeProject: screen refraction striped backdrop created (24 opaque strips)"), pairs)
            for message in ("Loader: hardware ray tracing disabled before device creation",
                "Vulkan: hardware ray tracing policy=disabled",
                "RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0",
                LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA, LIT_AVBOIT_REFRACTION_RESOLVE_SCREEN_SPACE):
                self.assertIn((LIT_EXPECT_LOG_MESSAGE, message), pairs)
            reflection_route = LIT_DISABLED if variant == LIT_REFLECTION_DISABLED else "screen-space"
            self.assertIn((LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: " + reflection_route), pairs)
            for message in ("Reflection resolve: hardware", LIT_AVBOIT_REFRACTION_RESOLVE_HARDWARE,
                LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA,
                "RendererSystem: dispatched hardware caustic producer",
                "RendererSystem: created surfel HW trace compute pipeline",
                "RendererSystem: created refraction resolve pipeline (hardware ray query)"):
                self.assertIn((LIT_REJECT_LOG_MESSAGE, message), pairs)
                self.assertNotIn((LIT_EXPECT_LOG_MESSAGE, message), pairs)
            if variant == LIT_CAUSTICS_DISABLED:
                self.assertIn((LIT_REJECT_LOG_MESSAGE, "caustic producer ("), pairs)
                self.assertNotIn((LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_CAU), pairs)
            else:
                self.assertIn((LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_CAU), pairs)
            environment = run.call_args.kwargs["env"]
            self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_MODE],
                LIT_DISABLED if variant == LIT_REFLECTION_DISABLED else LIT_SCREEN)
            self.assertEqual(environment[LIT_NWB_REFRACTION_SMOKE_HARDWARE], "1")
            self.assertEqual(environment[LIT_NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BA], "1")
            self.assertEqual(environment["NWB_REFRACTION_SMOKE_ENABLED"],
                "0" if variant == LIT_REFRACTION_DISABLED else "1")
            self.assertEqual(environment[LIT_NWB_CAUSTIC_SMOKE_ENABLED],
                "0" if variant == LIT_CAUSTICS_DISABLED else "1")

    def test_software_environment_drops_inherited_hardware_or_scene_override(self):
        with patch.dict(caustic.os.environ, {LIT_NWB_REFRACTION_SMOKE_CASE: LIT_NESTED,
            LIT_NWB_REFLECTION_SMOKE_MODE: "hardware", LIT_NWB_GPU_TIMING_FILE: "inherited.csv"}):
            environment = caustic.capture_environment(LIT_COMBINED, software_ray_tracing=True)
        self.assertNotIn(LIT_NWB_REFRACTION_SMOKE_CASE, environment)
        self.assertNotIn(LIT_NWB_GPU_TIMING_FILE, environment)
        self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_MODE], LIT_SCREEN)
        self.assertEqual(environment[LIT_NWB_REFRACTION_SMOKE_HARDWARE], "1")

    def test_device_policy_arguments_are_explicit_and_mutually_exclusive(self):
        required = ["--executable", LIT_APP_EXE, "--working-directory", LIT_RUNTIME, "--output-directory", LIT_OUTPUT]
        default = caustic.parse_args(required)
        self.assertFalse(default.require_hardware)
        self.assertFalse(default.software_ray_tracing)
        software = caustic.parse_args(required + [LIT_SOFTWARE_RAY_TRACING])
        self.assertTrue(software.software_ray_tracing)
        self.assertFalse(software.require_hardware)
        hardware = caustic.parse_args(required + [LIT_REQUIRE_HARDWARE])
        self.assertTrue(hardware.require_hardware)
        self.assertFalse(hardware.software_ray_tracing)
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

    def test_mesh_selected_quantized_environment_passes(self):
        combined, disabled = self.frames()
        result = caustic.analyze_exterior_reflection(combined, disabled)
        self.assertGreater(result["tested_samples"], 6000)
        self.assertEqual(result["missing_samples"], 0)
        self.assertLess(result["rgb_byte_mae"], 0.3)
        self.assertEqual(result[LIT_PREDICTOR]["environment"], (0.6, 0.7, 1.0))
        self.assertEqual(len(result[LIT_PREDICTOR]["mesh_lf_sha256"]), 64)

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

    def test_ground_occlusion_is_geometry_checked(self):
        self.assertTrue(caustic_reference.hits_ground((0, 0.85, -0.7), (0, -1, 0)))
        self.assertFalse(caustic_reference.hits_ground((0, 0.85, -0.7), (0, 1, 0)))
        self.assertFalse(caustic_reference.hits_ground((4, 0.85, -0.7), (0, -1, 0)))

    def test_normal_incidence_uses_physical_ior_f0_and_presentation(self):
        expected = caustic_reference.expected_environment_color((0, 0, 0), 0.04)
        for encoded, environment in zip(expected, caustic_reference.ENVIRONMENT):
            self.assertAlmostEqual(caustic_reference.decode_scene_radiance(encoded), 0.04 * environment, places=12)


if __name__ == LIT_MAIN:
    unittest.main()
