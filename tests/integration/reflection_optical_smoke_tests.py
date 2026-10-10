#!/usr/bin/env python3
"""Independent physical identities and intentionally incorrect optical image/counter evidence."""

import contextlib
import io
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import reflection_optical_reference as reference
import reflection_optical_smoke as smoke
import reflection_csg_context_smoke as context
import caustic_optical_smoke as caustic
import caustic_optical_reference as caustic_reference
from reflection_roughness_smoke import HISTORY_FIELDS
from reflection_smoke import STATISTICS_FIELDS, SmokeFailure

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
LIT_OPTICAL_ALPHA_BEFORE = "optical_alpha_before"
LIT_OPTICAL_TORUS = "optical_torus"
LIT_SEQUENCE = "sequence"
LIT_HARDWARE_QUERIES = "hardware_queries"
LIT_OPTICAL_REFERENCE = "optical_reference"
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


class ReferenceTests(unittest.TestCase):
    def test_dense_receiver_closure_includes_eighteen_crossings_beyond_the_ordinary_chart(self):
        for x, y in ((352.5, 280.5), (608.5, 440.5), (472.5, 352.5)):
            focal = 720 / (2. * math.tan(math.pi / 6.))
            origin = ((x - 480) * 6. / focal, 1.4 - (y - 360) * 6. / focal, 0.)
            direction = reference.normalized((origin[0], origin[1] - 1.4, -6.))
            crossings = reference.dense_receiver_intersections(origin, direction)
            self.assertEqual(len(crossings), 18)
            self.assertTrue(all(second - first >= 1. for first, second in zip(crossings, crossings[1:])))
            self.assertAlmostEqual(reference.add(origin, reference.scale(direction, crossings[0]))[2], -8.)
            self.assertAlmostEqual(reference.add(origin, reference.scale(direction, crossings[-1]))[2], -25.)
            self.assertGreater(crossings[-1], -14. / direction[2])
            for case in reference.DENSE_CASES:
                actual, evidence = reference.pixel_reference(case, x, y)
                self.assertEqual(evidence[LIT_CROSSINGS], 18)
                self.assertTrue(all(value > 0. for value in actual))
                self.assertLess(reference.add(origin, reference.scale(direction, crossings[-1]))[0], 11.)

    def test_inside_origin_retains_radiance_eta_squared(self):
        actual, details = reference.trace_transmission("optical_inside", (0, 1.4, 0), (0, 0, -1))
        self.assertAlmostEqual(actual[0], reference.CHART_RADIANCE * 0.96 * 1.5 ** 2, places=10)
        self.assertEqual(details[LIT_CROSSINGS], 1)
        self.assertGreater(actual[0], 1.0)

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

    def test_unsupported_medium_and_opaque_alpha_abort_before_transport(self):
        _, details = reference.trace_transmission(LIT_OPTICAL_UNSPECIFIED, (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_QUERIES], 1)
        _, details = reference.trace_transmission(LIT_OPTICAL_ALPHA_BEFORE, (3, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_QUERIES], 2)
        self.assertEqual(details[LIT_REASON], "opaque_alpha")

    def test_torus_uses_authored_geometric_triangles_and_reentry(self):
        _, details = reference.trace_transmission(LIT_OPTICAL_TORUS, (0, 1.4, 0), (0, 0, -1))
        self.assertEqual(details[LIT_CROSSINGS], 4)

    def test_alpha_order_matters_inside_known_transmission(self):
        before, _ = reference.trace_transmission(LIT_OPTICAL_ALPHA_BEFORE, (0, 1.4, 0), (0, 0, -1))
        after, _ = reference.trace_transmission("optical_alpha_after", (0, 1.4, 0), (0, 0, -1))
        self.assertGreater(before[0] - after[0], 0.3)
        self.assertGreater(before[1] - after[1], 0.1)
        self.assertAlmostEqual(before[2], after[2], places=10)

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

    def test_dense_receiver_capacity_loss_cannot_pass_as_black_or_transmitted_chart(self):
        for case in reference.DENSE_CASES:
            spec = smoke.OpticalCapture(case, case)
            self.points = tuple(smoke.reference_points(spec))
            metrics = self.analyze(self.synthetic_frame(case), spec)
            self.assertEqual(metrics["dense_receiver_crossings"]["minimum"], 18)
            for wrong in (self.synthetic_frame(case, channel_scale=0.), self.synthetic_frame(LIT_OPTICAL_CLEAR)):
                with self.subTest(case=case), self.assertRaises(SmokeFailure):
                    self.analyze(wrong, spec)

    def test_dense_receiver_cannot_claim_a_shorter_closed_collection(self):
        frame = self.synthetic_frame("optical_csg_dense")
        with patch.object(smoke, "pixel_reference", return_value=((.9, .2, .05),
            {LIT_REASON: "opaque_dense", LIT_CROSSINGS: 16})):
            with self.assertRaisesRegex(SmokeFailure, "eighteen distinct"):
                self.analyze(frame, smoke.OpticalCapture("optical_csg_dense", "optical_csg_dense"))

    def test_absorption_omission_fails(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_CLEAR), smoke.OpticalCapture(LIT_OPTICAL_TINTED, LIT_OPTICAL_TINTED))

    def test_shifted_stripes_fail_even_when_color_energy_is_similar(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_TINTED, shift=12), smoke.OpticalCapture(LIT_OPTICAL_TINTED, LIT_OPTICAL_TINTED))

    def test_straight_environment_leak_after_query_limit_fails(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_CLEAR), smoke.OpticalCapture("optical_query_limit", LIT_OPTICAL_CLEAR, 1))

    def test_grouped_gap_cannot_pass_with_lost_interfaces_and_environment_radiance(self):
        for case in ("optical_group_gap", "optical_csg_group_gap",
            "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp", "optical_group_entry", "optical_csg_group_entry"):
            with self.subTest(case=case), self.assertRaises(SmokeFailure):
                self.analyze(self.synthetic_frame(LIT_OPTICAL_CLEAR), smoke.OpticalCapture(case, case))

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

    def validate(self, text, spec=smoke.OpticalCapture(LIT_OPTICAL_CLEAR, LIT_OPTICAL_CLEAR), statistics_overrides=None):
        sample = {LIT_SEQUENCE: 9, "generation": 2, "hardware_rays": 100, "frame": 9, "hardware_ready": 1,
            "candidates": 100, "effective_budget": smoke.DEFAULT_RAY_BUDGET, "hardware_hits": 100,
            "opaque_pixels": 100, "glass_pixels": 0, "fallback_pixels": 0}
        sample.update(statistics_overrides or {})
        statistics = [sample]
        with patch.object(smoke, "parse_statistics", return_value=statistics), patch.object(smoke, "validate_statistics"), \
            patch.object(smoke, "validate_slice_packets", return_value=None):
            return smoke.validate_optics(text, spec)

    def test_dense_receiver_requires_exact_queries_without_replay_or_missing_hits(self):
        for case, queries in (("optical_dense", 100), ("optical_csg_dense", 200)):
            spec = smoke.OpticalCapture(case, case)
            values = {"transport_enabled": 0, "transparent_paths": 0, LIT_HARDWARE_QUERIES: queries}
            self.validate(self.log(values), spec)
            for wrong_queries in (queries - 1, queries + 1, queries * 2):
                with self.subTest(case=case, queries=wrong_queries), self.assertRaises(SmokeFailure):
                    self.validate(self.log({**values, LIT_HARDWARE_QUERIES: wrong_queries}), spec)
            for change in ({"hardware_hits": 99}, {"hardware_rays": 99}, {"fallback_pixels": 1}, {"glass_pixels": 1}):
                with self.subTest(case=case, change=change), self.assertRaises(SmokeFailure):
                    self.validate(self.log(values), spec, change)

    def test_dense_opaque_plain_route_cannot_hide_optical_failures(self):
        values = {"transport_enabled": 0, "transparent_paths": 0, LIT_HARDWARE_QUERIES: 200}
        spec = smoke.OpticalCapture("optical_csg_dense", "optical_csg_dense")
        for counter in ("transport_enabled", "transparent_paths", "bootstrap_events", "unsupported_paths",
            "limited_paths", "ambiguous_paths", "tir_events", "medium_overflow_paths"):
            with self.subTest(counter=counter), self.assertRaises(SmokeFailure):
                self.validate(self.log({**values, counter: 1}), spec)

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

    def test_grouped_gap_requires_completed_ambiguity_even_without_transparent_paths(self):
        for case in ("optical_group_gap", "optical_csg_group_gap",
            "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp", "optical_group_entry", "optical_csg_group_entry"):
            spec = smoke.OpticalCapture(case, case)
            with self.subTest(case=case), self.assertRaisesRegex(SmokeFailure, "negative optical fixture"):
                self.validate(self.log({"transparent_paths": 0}), spec)
            self.validate(self.log({"transparent_paths": 0, "ambiguous_paths": 100}), spec)

    def test_negative_volume_cannot_pass_with_partial_ambiguity(self):
        for case in ("optical_sliver", "optical_csg_sliver", "optical_sub_ulp", "optical_csg_sub_ulp",
            "optical_group_gap", "optical_csg_group_gap", "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp",
            "optical_group_entry", "optical_csg_group_entry"):
            spec = smoke.OpticalCapture(case, case)
            for ambiguous in (1, 99):
                with self.subTest(case=case, ambiguous=ambiguous), self.assertRaisesRegex(SmokeFailure, "every admitted hardware ray"):
                    self.validate(self.log({"ambiguous_paths": ambiguous}), spec)
            self.validate(self.log({"ambiguous_paths": 100}), spec)

    def test_last_negative_frame_cannot_hide_an_earlier_partial_rejection(self):
        statistics = [{LIT_SEQUENCE: sequence, "generation": 2, "hardware_rays": 100, "frame": sequence, "hardware_ready": 1} for sequence in (9, 10)]
        text = self.log({"ambiguous_paths": 99}) + "\n" + self.log({LIT_SEQUENCE: 10, "ambiguous_paths": 100})
        spec = smoke.OpticalCapture("optical_csg_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp")
        with patch.object(smoke, "parse_statistics", return_value=statistics), patch.object(smoke, "validate_statistics"), \
            patch.object(smoke, "validate_slice_packets", return_value=None):
            with self.assertRaisesRegex(SmokeFailure, "every admitted hardware ray"):
                smoke.validate_optics(text, spec)

    def test_malformed_or_duplicate_fields_fail(self):
        for text in ("", self.log() + " sequence=9", self.log().replace("limited_paths=0", "other=0")):
            with self.assertRaises(SmokeFailure):
                smoke.parse_optics(text)


class QueueCoverageTests(unittest.TestCase):
    def log(self, budget, ray_delta=0, sequence=9, queries=None, transparent=None):
        admitted = min(217668, budget)
        rays = admitted + ray_delta
        ready = int(budget > 0)
        sample = dict(sequence=sequence, generation=2, frame=sequence, mode=2, width=960, height=720,
            requested_budget=budget, effective_budget=budget, queue_capacity=max(1, budget),
            hardware_requested=1, hardware_available=ready, hardware_ready=ready,
            token_queue=0, token_value=sequence + 100, physical_queue=0, device_generation=1,
            candidates=217668 if budget else 0, hardware_rays=rays, hardware_hits=0, opaque_pixels=217668, glass_pixels=0,
            fallback_pixels=217668, screen_attempts=0, screen_hits=0)
        values = dict(zip(smoke.OPTICS_FIELDS, (sequence, 2, 16, rays if queries is None else queries,
            0, min(rays, 100) if transparent is None else transparent, 0, 0, 0, 0, 0, ready)))
        history = dict(zip(HISTORY_FIELDS, (sequence, 2, sequence - 1, 1, 0, 0, sequence - 1, 0, 0, 0, 0, 0)))
        expected = (budget + smoke.CSG_TRACE_SLICE_RAYS - 1) // smoke.CSG_TRACE_SLICE_RAYS
        packets = dict(zip(smoke.SLICE_PACKET_FIELDS, (sequence - 1, 23, 1, 0, 1, budget, expected,
            expected, expected, expected, expected, expected, expected, expected)))
        return "ReflectionSmokeStatistics: " + " ".join(f"{name}={sample[name]}" for name in STATISTICS_FIELDS) + "\n" \
            + "ReflectionSmokeOptics: " + " ".join(f"{name}={values[name]}" for name in smoke.OPTICS_FIELDS) + "\n" \
            + "ReflectionSmokeHistory: " + " ".join(f"{name}={history[name]}" for name in HISTORY_FIELDS) + "\n" \
            + "ReflectionSmokeSlicePackets: " + " ".join(f"{name}={packets[name]}" for name in smoke.SLICE_PACKET_FIELDS)

    def validate(self, budget, **changes):
        spec = smoke.OpticalCapture(f"budget_{budget}", "optical_csg_cap", ray_budget=budget)
        return smoke.validate_optics(self.log(budget, **changes), spec)

    def test_missing_group_or_slice_tail_cannot_pass_per_frame_coverage(self):
        for budget in (1, 63, 64, 65, 8191, 8192, 8193):
            with self.subTest(budget=budget):
                self.validate(budget)
                with self.assertRaises(SmokeFailure):
                    self.validate(budget, ray_delta=-1)
                with self.assertRaises(SmokeFailure):
                    self.validate(budget, ray_delta=1)
        with self.assertRaisesRegex(SmokeFailure, "every admitted queue entry"):
            self.validate(8193, ray_delta=-8192)

    def test_complete_final_frame_cannot_hide_an_earlier_missing_slice_tail(self):
        spec = smoke.OpticalCapture("tail", "optical_csg_cap", ray_budget=8193)
        log = self.log(8193, ray_delta=-1) + "\n" + self.log(8193, sequence=10)
        with self.assertRaisesRegex(SmokeFailure, "each stable frame"):
            smoke.validate_optics(log, spec)

    def test_full_budget_stops_at_candidates_and_still_requires_transparent_transport(self):
        self.validate(smoke.DEFAULT_RAY_BUDGET)
        with self.assertRaises(SmokeFailure):
            self.validate(smoke.DEFAULT_RAY_BUDGET, ray_delta=1)
        with self.assertRaisesRegex(SmokeFailure, "transparent reflected paths"):
            self.validate(smoke.DEFAULT_RAY_BUDGET, transparent=0)

    def test_zero_budget_cannot_admit_queries_or_paths_without_primary_rays(self):
        self.validate(0)
        with self.assertRaisesRegex(SmokeFailure, "per-path query bound"):
            self.validate(0, queries=1)
        with self.assertRaisesRegex(SmokeFailure, "admitted primary paths"):
            self.validate(0, transparent=1)
        with self.assertRaises(SmokeFailure):
            self.validate(0, ray_delta=1)

    def test_limited_compacted_queue_can_miss_glass_without_weakening_full_oracle(self):
        for budget in (1, 8193):
            with self.subTest(budget=budget):
                self.validate(budget, transparent=0)


class SlicePacketTests(unittest.TestCase):
    def log(self, budget=8193, sequence=9):
        return QueueCoverageTests().log(budget, sequence=sequence)

    def validate(self, text, budget=8193):
        return smoke.validate_optics(text, smoke.OpticalCapture("packet_test", "optical_csg_cap", ray_budget=budget))

    def test_each_boundary_budget_has_exact_accepted_single_task_packets(self):
        for budget in smoke.QUEUE_BOUNDARY_BUDGETS:
            with self.subTest(budget=budget):
                record = self.validate(self.log(budget), budget)["slice_packets"]["completed_frames"][0]
                self.assertEqual(record["accepted_packets"], (budget + 8191) // 8192)
        self.assertEqual(self.validate(self.log(smoke.DEFAULT_RAY_BUDGET), smoke.DEFAULT_RAY_BUDGET)
            ["slice_packets"]["completed_frames"][0]["accepted_packets"], 169)

    def test_cavity_requires_the_same_accepted_packet_proof(self):
        spec = smoke.OpticalCapture("optical_csg_cavity", "optical_csg_cavity", ray_budget=8193)
        result = smoke.validate_optics(self.log(), spec)
        self.assertEqual(result["slice_packets"]["completed_frames"][0]["accepted_packets"], 2)
        text = "\n".join(line for line in self.log().splitlines() if "ReflectionSmokeSlicePackets:" not in line)
        with self.assertRaisesRegex(SmokeFailure, "exact source frame"):
            smoke.validate_optics(text, spec)

    def test_all_indirect_calls_in_one_packet_cannot_pass(self):
        with self.assertRaisesRegex(SmokeFailure, "distinct single-task"):
            self.validate(self.log().replace("unique_packets=2", "unique_packets=1")
                .replace("single_task_packets=2", "single_task_packets=0"))

    def test_unaccepted_recovery_merged_or_wrong_queue_packet_fails(self):
        for name in ("hardware_nodes", "indexed_slices", "compiled_nodes", "unique_packets", "accepted_packets", "single_task_packets", "graphics_packets"):
            with self.subTest(name=name), self.assertRaisesRegex(SmokeFailure, "distinct single-task"):
                self.validate(self.log().replace(name + "=2", name + "=1"))

    def test_stale_frame_plan_or_physical_generation_cannot_pass(self):
        for old, new in (("source_frame=8", "source_frame=7"), ("runtime_present=1", "runtime_present=0"),
            ("plan_generation=23", "plan_generation=0"), ("graphics_queue=0", "graphics_queue=65535")):
            with self.subTest(old=old), self.assertRaises(SmokeFailure):
                self.validate(self.log().replace(old, new))
        text = self.log().replace("ReflectionSmokeSlicePackets: source_frame=8 plan_generation=23 device_generation=1",
            "ReflectionSmokeSlicePackets: source_frame=8 plan_generation=23 device_generation=2")
        with self.assertRaisesRegex(SmokeFailure, "queue identity"):
            self.validate(text)

    def test_readback_may_use_a_different_queue_from_primary_hardware_packets(self):
        text = self.log().replace("physical_queue=0", "physical_queue=1")
        self.validate(text)

    def test_budget_or_slice_count_cannot_be_relabelled(self):
        for old, new in (("ray_capacity=8193", "ray_capacity=8192"), ("expected_slices=2", "expected_slices=1")):
            with self.subTest(old=old), self.assertRaisesRegex(SmokeFailure, "frozen queue capacity"):
                self.validate(self.log().replace(old, new))

    def test_zero_budget_cannot_submit_a_slice(self):
        with self.assertRaisesRegex(SmokeFailure, "distinct single-task"):
            self.validate(self.log(0).replace("hardware_nodes=0", "hardware_nodes=1"), 0)

    def test_complete_final_frame_cannot_hide_an_earlier_merged_packet(self):
        text = self.log().replace("unique_packets=2", "unique_packets=1") + "\n" + self.log(sequence=10)
        with self.assertRaisesRegex(SmokeFailure, "distinct single-task"):
            self.validate(text)

    def test_packet_evidence_requires_exact_completed_source_metadata(self):
        for marker in ("ReflectionSmokeSlicePackets:", "ReflectionSmokeHistory:"):
            text = "\n".join(line for line in self.log().splitlines() if marker not in line)
            with self.subTest(marker=marker), self.assertRaises(SmokeFailure):
                self.validate(text)

    def test_duplicate_or_malformed_packet_identity_fails(self):
        line = self.log().splitlines()[-1]
        for text in (self.log() + "\n" + line, self.log() + " hardware_nodes=2",
            self.log().replace("compiled_nodes=2", "compiled_nodes=-1"), self.log() + " junk"):
            with self.subTest(text=text), self.assertRaises(SmokeFailure):
                self.validate(text)


class ContextTransitionTests(unittest.TestCase):
    def log(self):
        lines = []
        for phase, (_, case, cut_receiver_primitives, csg) in enumerate(context.PHASES):
            begin = 8 + phase * 24
            lines.append(f"ReflectionCsgContext: phase={phase} source_frame={begin} cut_receiver_primitives={cut_receiver_primitives} csg={csg}")
            for index in range(18):
                queries = 217668 * (11 if cut_receiver_primitives == 12 else 2 if csg else 1)
                sample = QueueCoverageTests().log(smoke.DEFAULT_RAY_BUDGET, sequence=begin + index + 1,
                    queries=queries, transparent=217668 if cut_receiver_primitives == 12 else 0)
                sample = sample.replace("hardware_hits=0", "hardware_hits=217668").replace("fallback_pixels=217668", "fallback_pixels=0")
                if cut_receiver_primitives != 12:
                    sample = sample.replace("transport_enabled=1", "transport_enabled=0")
                if not csg:
                    for field in ("expected_slices", "hardware_nodes", "indexed_slices", "compiled_nodes", "unique_packets",
                        "accepted_packets", "single_task_packets", "graphics_packets"):
                        sample = sample.replace(field + "=169", field + "=1")
                lines.append(sample)
            source = begin + 16
            lines.append(f"FramebufferCapture: graphics source frame {source}")
            lines.append(f"ReflectionCsgContext: captured phase={phase} source_frame={source} completed_frames=18")
        lines.append("ReflectionCsgContext: complete captures=5")
        return "\n".join(lines)

    def test_current_context_return_cannot_retain_sliced_ordinary_or_short_dense_work(self):
        evidence = context.analyze_evidence(self.log())
        self.assertEqual([item["begin"]["cut_receiver_primitives"] for item in evidence], [12, 108, 0, 12, 108])
        for field, value in (("hardware_nodes", 168), ("indexed_slices", 168), ("accepted_packets", 168)):
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                context.analyze_evidence(self.log().replace(field + "=169", field + "=" + str(value)))
        ordinary = self.log().replace("expected_slices=1 hardware_nodes=1", "expected_slices=169 hardware_nodes=169")
        with self.assertRaisesRegex(SmokeFailure, "stale CSG slices"):
            context.analyze_evidence(ordinary)

    def test_context_capture_cannot_use_frames_from_before_its_live_edit(self):
        text = self.log().replace("captured phase=1 source_frame=48", "captured phase=1 source_frame=47")
        text = text.replace("graphics source frame 48", "graphics source frame 47")
        with self.assertRaisesRegex(SmokeFailure, "sixteen prior"):
            context.analyze_evidence(text)
        with self.assertRaises(SmokeFailure):
            context.analyze_evidence(self.log().replace("phase=3 source_frame=80 cut_receiver_primitives=12", "phase=3 source_frame=105 cut_receiver_primitives=12"))

    def test_context_capture_source_requires_its_own_completed_metadata(self):
        text = self.log().replace("captured phase=4 source_frame=120", "captured phase=4 source_frame=123")
        text = text.replace("graphics source frame 120", "graphics source frame 123")
        with self.assertRaisesRegex(SmokeFailure, "exact captured source"):
            context.analyze_evidence(text)

    def test_small_phase_query_change_cannot_hide_behind_valid_final_dense_frame(self):
        with self.assertRaisesRegex(SmokeFailure, "small cap phase"):
            context.analyze_evidence(self.log().replace("hardware_queries=2394348", "hardware_queries=2394347", 1))
        with self.assertRaises(SmokeFailure):
            context.analyze_evidence(self.log().replace("transparent_paths=217668", "transparent_paths=0", 1))

    def test_context_phase_identity_or_completion_count_cannot_be_replayed(self):
        for old, new in (("phase=3 source_frame=80 cut_receiver_primitives=12 csg=1", "phase=3 source_frame=80 cut_receiver_primitives=108 csg=1"),
            ("completed_frames=18", "completed_frames=15"), ("complete captures=5", "complete captures=4")):
            with self.subTest(old=old), self.assertRaises(SmokeFailure):
                context.analyze_evidence(self.log().replace(old, new, 1))
        with self.assertRaises(SmokeFailure):
            context.analyze_evidence(self.log() + "\nFramebufferCapture: graphics source frame 120")


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
        with patch.dict(caustic.os.environ, {LIT_NWB_REFRACTION_SMOKE_CASE: LIT_NESTED,
            LIT_NWB_REFLECTION_SMOKE_MODE: "hardware", LIT_NWB_GPU_TIMING_FILE: "inherited.csv"}):
            environment = caustic.capture_environment(LIT_COMBINED, software_ray_tracing=True)
        self.assertNotIn(LIT_NWB_REFRACTION_SMOKE_CASE, environment)
        self.assertNotIn(LIT_NWB_GPU_TIMING_FILE, environment)
        self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_MODE], LIT_SCREEN)

    def test_device_policy_arguments_are_explicit_and_mutually_exclusive(self):
        required = ["--executable", LIT_APP_EXE, "--working-directory", LIT_RUNTIME, "--output-directory", LIT_OUTPUT]
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
