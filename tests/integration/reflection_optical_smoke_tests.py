#!/usr/bin/env python3
"""Independent physical identities and intentionally incorrect optical image/counter evidence."""


import contextlib
import io
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import reflection_optical_reference as reference
import reflection_optical_smoke as smoke
from reflection_smoke import STATISTICS_FIELDS, SmokeFailure


LIT_QUERY_LIMIT = "query_limit"
LIT_OPTICAL_CSG_DENSE = "optical_csg_dense"
LIT_PIXEL_REFERENCE = "pixel_reference"
LIT_OPTICAL_GROUP_GAP = "optical_group_gap"
LIT_OPTICAL_CSG_GROUP_GAP = "optical_csg_group_gap"
LIT_OPTICAL_GROUP_GAP_SUB_ULP = "optical_group_gap_sub_ulp"
LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP = "optical_csg_group_gap_sub_ulp"
LIT_OPTICAL_GROUP_ENTRY = "optical_group_entry"
LIT_OPTICAL_CSG_GROUP_ENTRY = "optical_csg_group_entry"
LIT_REFLECTIONSMOKEOPTICS = "ReflectionSmokeOptics: "
LIT_GENERATION = "generation"
LIT_HARDWARE_RAYS = "hardware_rays"
LIT_FRAME = "frame"
LIT_HARDWARE_READY = "hardware_ready"
LIT_HARDWARE_HITS = "hardware_hits"
LIT_GLASS_PIXELS = "glass_pixels"
LIT_FALLBACK_PIXELS = "fallback_pixels"
LIT_PARSE_STATISTICS = "parse_statistics"
LIT_VALIDATE_STATISTICS = "validate_statistics"
LIT_VALIDATE_SLICE_PACKETS = "validate_slice_packets"
LIT_TRANSPORT_ENABLED = "transport_enabled"
LIT_TRANSPARENT_PATHS = "transparent_paths"
LIT_BOOTSTRAP_EVENTS = "bootstrap_events"
LIT_UNSUPPORTED_PATHS = "unsupported_paths"
LIT_LIMITED_PATHS = "limited_paths"
LIT_AMBIGUOUS_PATHS = "ambiguous_paths"
LIT_MEDIUM_OVERFLOW_PATHS = "medium_overflow_paths"
LIT_OPTICS = "optics"
LIT_EVERY_ADMITTED_HARDWARE_RAY = "every admitted hardware ray"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_RUNS = "runs"
LIT_PROOF_TXT = "proof.txt"
LIT_RETAINED_EVIDENCE = "retained evidence"
LIT_UTF_8 = "utf-8"
LIT_OUTPUT_ROOT = "--output-root"
LIT_QUEUE_BOUNDARIES = "--queue-boundaries"
LIT_RUN_QUEUE_BOUNDARY_SUITE = "run_queue_boundary_suite"
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
LIT_QUERY_BUDGET_UNITS = "query_budget_units"
LIT_PHYSICAL_QUERIES = "physical_queries"
LIT_OPTICAL_REFERENCE = "optical_reference"
LIT_OUTPUT = "output"
LIT_APP_EXE = "app.exe"
LIT_RUNTIME = "runtime"
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
        self.assertEqual(limit[LIT_REASON], LIT_QUERY_LIMIT)
        self.assertEqual(limit[LIT_TIR_EVENTS], 1)

    def test_priority_replaces_medium_without_lifo_removal(self):
        first, _ = reference.trace_transmission(LIT_OPTICAL_PRIORITY_A, (0, 1.4, 0), (0, 0, -1))
        second, _ = reference.trace_transmission(LIT_OPTICAL_PRIORITY_B, (0, 1.4, 0), (0, 0, -1))
        self.assertGreater(second[0], first[0])
        self.assertLess(second[2], first[2])
        self.assertGreater(sum(abs(a - b) for a, b in zip(first, second)), 0.3)

    def test_mixed_modes_and_capacity_fail_without_environment(self):
        for case, reason in (("optical_mixed", LIT_AMBIGUOUS), (LIT_OPTICAL_OVERFLOW, "medium_overflow"), (LIT_OPTICAL_UNSPECIFIED, "unsupported")):
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
        frame = self.synthetic_frame(LIT_OPTICAL_CSG_DENSE)
        with patch.object(smoke, LIT_PIXEL_REFERENCE, return_value=((.9, .2, .05), {LIT_REASON: "opaque_dense", LIT_CROSSINGS: 16})):
            with self.assertRaisesRegex(SmokeFailure, "eighteen distinct"):
                self.analyze(frame, smoke.OpticalCapture(LIT_OPTICAL_CSG_DENSE, LIT_OPTICAL_CSG_DENSE))

    def test_unspecified_air_controls_cannot_qualify_black_or_misplaced_transport(self):
        for spec in smoke.CSG_REUSE_CAPTURES[-2:]:
            with self.subTest(case=spec.case):
                result = self.analyze(self.synthetic_frame(spec.case), spec)
                self.assertGreater(result["transmitted_chart_samples"], 150)
                for wrong in (self.synthetic_frame(spec.case, channel_scale=0.), self.synthetic_frame(spec.case, shift=12)):
                    with self.assertRaises(SmokeFailure):
                        self.analyze(wrong, spec)

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
        for case in (LIT_OPTICAL_GROUP_GAP, LIT_OPTICAL_CSG_GROUP_GAP, LIT_OPTICAL_GROUP_GAP_SUB_ULP, LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP, LIT_OPTICAL_GROUP_ENTRY, LIT_OPTICAL_CSG_GROUP_ENTRY):
            with self.subTest(case=case), self.assertRaises(SmokeFailure):
                self.analyze(self.synthetic_frame(LIT_OPTICAL_CLEAR), smoke.OpticalCapture(case, case))

    def test_half_energy_cannot_pass_using_relative_normalization(self):
        with self.assertRaises(SmokeFailure):
            self.analyze(self.synthetic_frame(LIT_OPTICAL_TINTED, channel_scale=0.5), smoke.OpticalCapture(LIT_OPTICAL_TINTED, LIT_OPTICAL_TINTED))

    def test_torus_cannot_pass_using_only_unobstructed_or_single_interval_samples(self):
        frame = (960, 720, [[(0, 0, 0)] * 960 for _ in range(720)])
        for crossings in (0, 2):
            with patch.object(smoke, LIT_PIXEL_REFERENCE, return_value=((0, 0, 0), {LIT_REASON: LIT_CHART, LIT_CROSSINGS: crossings})):
                with self.assertRaisesRegex(SmokeFailure, "re-entry coverage"):
                    self.analyze(frame, smoke.OpticalCapture(LIT_OPTICAL_TORUS, LIT_OPTICAL_TORUS))


class StatisticsTests(unittest.TestCase):
    def log(self, overrides=None):
        values = dict(zip(smoke.OPTICS_FIELDS, (9, 2, 16, 500, 500, 0, 100, 0, 0, 0, 0, 0, 1)))
        values.update(overrides or {})
        return LIT_REFLECTIONSMOKEOPTICS + " ".join(f"{name}={values[name]}" for name in smoke.OPTICS_FIELDS)

    def validate(self, text, spec=smoke.OpticalCapture(LIT_OPTICAL_CLEAR, LIT_OPTICAL_CLEAR), statistics_overrides=None):
        sample = {LIT_SEQUENCE: 9, LIT_GENERATION: 2, LIT_HARDWARE_RAYS: 100, LIT_FRAME: 9, LIT_HARDWARE_READY: 1, "candidates": 100, "effective_budget": smoke.DEFAULT_RAY_BUDGET, LIT_HARDWARE_HITS: 100, "opaque_pixels": 100, LIT_GLASS_PIXELS: 0, LIT_FALLBACK_PIXELS: 0}
        sample.update(statistics_overrides or {})
        statistics = [sample]
        with patch.object(smoke, LIT_PARSE_STATISTICS, return_value=statistics), patch.object(smoke, LIT_VALIDATE_STATISTICS), \
            patch.object(smoke, LIT_VALIDATE_SLICE_PACKETS, return_value=None):
            return smoke.validate_optics(text, spec)

    def test_dense_receiver_requires_exact_queries_without_replay_or_missing_hits(self):
        for case, queries in (("optical_dense", 100), (LIT_OPTICAL_CSG_DENSE, 200)):
            spec = smoke.OpticalCapture(case, case)
            values = {LIT_TRANSPORT_ENABLED: 0, LIT_TRANSPARENT_PATHS: 0, LIT_QUERY_BUDGET_UNITS: queries, LIT_PHYSICAL_QUERIES: queries}
            self.validate(self.log(values), spec)
            for wrong_queries in (queries - 1, queries + 1, queries * 2):
                with self.subTest(case=case, queries=wrong_queries), self.assertRaises(SmokeFailure):
                    self.validate(self.log({**values, LIT_QUERY_BUDGET_UNITS: wrong_queries}), spec)
            for change in ({LIT_HARDWARE_HITS: 99}, {LIT_HARDWARE_RAYS: 99}, {LIT_FALLBACK_PIXELS: 1}, {LIT_GLASS_PIXELS: 1}):
                with self.subTest(case=case, change=change), self.assertRaises(SmokeFailure):
                    self.validate(self.log(values), spec, change)

    def test_dense_opaque_plain_route_cannot_hide_optical_failures(self):
        values = {LIT_TRANSPORT_ENABLED: 0, LIT_TRANSPARENT_PATHS: 0, LIT_QUERY_BUDGET_UNITS: 200, LIT_PHYSICAL_QUERIES: 200}
        spec = smoke.OpticalCapture(LIT_OPTICAL_CSG_DENSE, LIT_OPTICAL_CSG_DENSE)
        for counter in (LIT_TRANSPORT_ENABLED, LIT_TRANSPARENT_PATHS, LIT_BOOTSTRAP_EVENTS, LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS):
            with self.subTest(counter=counter), self.assertRaises(SmokeFailure):
                self.validate(self.log({**values, counter: 1}), spec)

    def test_query_cap_violation_fails(self):
        with self.assertRaises(SmokeFailure):
            self.validate(self.log({LIT_QUERY_BUDGET_UNITS: 1601}))

    def test_physical_queries_cannot_exceed_the_reserved_budget(self):
        with self.assertRaisesRegex(SmokeFailure, "exceed reserved"):
            self.validate(self.log({LIT_QUERY_BUDGET_UNITS: 100, LIT_PHYSICAL_QUERIES: 101}))

    def test_receiver_reservation_without_physical_query_preserves_early_topology_rejection(self):
        result = self.validate(self.log({LIT_QUERY_BUDGET_UNITS: 200, LIT_PHYSICAL_QUERIES: 100, LIT_TRANSPARENT_PATHS: 0, LIT_UNSUPPORTED_PATHS: 100}), smoke.OpticalCapture(LIT_OPTICAL_UNSPECIFIED, LIT_OPTICAL_UNSPECIFIED, ray_budget=100))
        self.assertEqual(result[LIT_OPTICS][-1][LIT_PHYSICAL_QUERIES], 100)

    def test_receiver_reservation_without_physical_query_preserves_complete_cut_coverage(self):
        result = self.validate(self.log({LIT_QUERY_BUDGET_UNITS: 200, LIT_PHYSICAL_QUERIES: 100, LIT_TRANSPARENT_PATHS: 0}), smoke.OpticalCapture(LIT_OPTICAL_CLEAR, LIT_OPTICAL_CLEAR, ray_budget=100),
            statistics_overrides={LIT_HARDWARE_HITS: 0, LIT_FALLBACK_PIXELS: 100})
        self.assertEqual(result[LIT_OPTICS][-1][LIT_PHYSICAL_QUERIES], 100)

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
        for case in (LIT_OPTICAL_GROUP_GAP, LIT_OPTICAL_CSG_GROUP_GAP, LIT_OPTICAL_GROUP_GAP_SUB_ULP, LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP, LIT_OPTICAL_GROUP_ENTRY, LIT_OPTICAL_CSG_GROUP_ENTRY):
            spec = smoke.OpticalCapture(case, case)
            with self.subTest(case=case), self.assertRaisesRegex(SmokeFailure, "negative optical fixture"):
                self.validate(self.log({LIT_TRANSPARENT_PATHS: 0}), spec)
            self.validate(self.log({LIT_TRANSPARENT_PATHS: 0, LIT_AMBIGUOUS_PATHS: 100}), spec)

    def test_negative_volume_cannot_pass_with_partial_ambiguity(self):
        for case in ("optical_sliver", "optical_csg_sliver", "optical_sub_ulp", "optical_csg_sub_ulp", LIT_OPTICAL_GROUP_GAP, LIT_OPTICAL_CSG_GROUP_GAP, LIT_OPTICAL_GROUP_GAP_SUB_ULP, LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP,
            LIT_OPTICAL_GROUP_ENTRY, LIT_OPTICAL_CSG_GROUP_ENTRY):
            spec = smoke.OpticalCapture(case, case)
            for ambiguous in (1, 99):
                with self.subTest(case=case, ambiguous=ambiguous), self.assertRaisesRegex(SmokeFailure, LIT_EVERY_ADMITTED_HARDWARE_RAY):
                    self.validate(self.log({LIT_AMBIGUOUS_PATHS: ambiguous}), spec)
            self.validate(self.log({LIT_AMBIGUOUS_PATHS: 100}), spec)

    def test_last_negative_frame_cannot_hide_an_earlier_partial_rejection(self):
        statistics = [{LIT_SEQUENCE: sequence, LIT_GENERATION: 2, LIT_HARDWARE_RAYS: 100, LIT_FRAME: sequence, LIT_HARDWARE_READY: 1} for sequence in (9, 10)]
        text = self.log({LIT_AMBIGUOUS_PATHS: 99}) + "\n" + self.log({LIT_SEQUENCE: 10, LIT_AMBIGUOUS_PATHS: 100})
        spec = smoke.OpticalCapture(LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP, LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP)
        with patch.object(smoke, LIT_PARSE_STATISTICS, return_value=statistics), patch.object(smoke, LIT_VALIDATE_STATISTICS), \
            patch.object(smoke, LIT_VALIDATE_SLICE_PACKETS, return_value=None):
            with self.assertRaisesRegex(SmokeFailure, LIT_EVERY_ADMITTED_HARDWARE_RAY):
                smoke.validate_optics(text, spec)

    def test_malformed_or_duplicate_fields_fail(self):
        for text in ("", self.log() + " sequence=9", self.log().replace("limited_paths=0", "other=0")):
            with self.assertRaises(SmokeFailure):
                smoke.parse_optics(text)


class OpticalEvidenceOutputTests(unittest.TestCase):
    def test_repeated_output_root_runs_keep_prior_evidence_and_use_disjoint_children(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / LIT_RUNS
            retained = root / "completed" / LIT_PROOF_TXT
            retained.parent.mkdir(parents=True)
            retained.write_text(LIT_RETAINED_EVIDENCE, encoding=LIT_UTF_8)
            destinations = []

            def record_capture(args):
                destination = args.output_directory
                self.assertFalse(any(destination.iterdir()))
                (destination / LIT_PROOF_TXT).write_text(f"new evidence {len(destinations)}", encoding=LIT_UTF_8)
                destinations.append(destination)
                return 0

            required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_ROOT, str(root), LIT_QUEUE_BOUNDARIES]
            with patch.object(smoke, LIT_RUN_QUEUE_BOUNDARY_SUITE, side_effect=record_capture):
                self.assertEqual(smoke.main(required), 0)
                self.assertEqual(smoke.main(required), 0)
            self.assertEqual(len(set(destinations)), 2)
            self.assertTrue(all(destination.parent == root for destination in destinations))
            self.assertEqual(retained.read_text(encoding=LIT_UTF_8), LIT_RETAINED_EVIDENCE)
            for index, destination in enumerate(destinations):
                self.assertEqual((destination / LIT_PROOF_TXT).read_text(encoding=LIT_UTF_8), f"new evidence {index}")

    def test_nonempty_exact_output_directory_is_rejected_without_overwriting_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            retained = output / LIT_PROOF_TXT
            retained.write_text(LIT_RETAINED_EVIDENCE, encoding=LIT_UTF_8)
            required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_DIRECTORY, str(output), LIT_QUEUE_BOUNDARIES]
            with (
                patch.object(smoke, LIT_RUN_QUEUE_BOUNDARY_SUITE) as capture,
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaises(SystemExit) as rejection,
            ):
                smoke.main(required)
            self.assertEqual(rejection.exception.code, 2)
            capture.assert_not_called()
            self.assertEqual(retained.read_text(encoding=LIT_UTF_8), LIT_RETAINED_EVIDENCE)

    def test_exact_directory_and_output_root_cannot_be_requested_together(self):
        required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT, LIT_OUTPUT_ROOT, LIT_RUNS]
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as rejection:
            smoke.main(required)
        self.assertEqual(rejection.exception.code, 2)


if __name__ == LIT_MAIN:
    unittest.main()
