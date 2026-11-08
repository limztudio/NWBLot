#!/usr/bin/env python3
"""Independent physical identities and optical image evidence."""


import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import reflection_optical_reference as reference
import reflection_optical_geometry as geometry
import reflection_optical_smoke as smoke
import reflection_optical_image_evidence as optical_image
from reflection_optical_image_evidence import ReflectionOpticalImageEvidence
from reflection_smoke import SmokeFailure


LIT_QUERY_LIMIT = "query_limit"
LIT_OPTICAL_CSG_DENSE = "optical_csg_dense"
LIT_PIXEL_REFERENCE = "pixel_reference"
LIT_OPTICAL_GROUP_GAP = "optical_group_gap"
LIT_OPTICAL_CSG_GROUP_GAP = "optical_csg_group_gap"
LIT_OPTICAL_GROUP_GAP_SUB_ULP = "optical_group_gap_sub_ulp"
LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP = "optical_csg_group_gap_sub_ulp"
LIT_OPTICAL_GROUP_ENTRY = "optical_group_entry"
LIT_OPTICAL_CSG_GROUP_ENTRY = "optical_csg_group_entry"
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
LIT_MAIN = "__main__"


class ReferenceTests(unittest.TestCase):
    def test_dense_receiver_closure_includes_eighteen_crossings_beyond_the_ordinary_chart(self):
        for x, y in ((352.5, 280.5), (608.5, 440.5), (472.5, 352.5)):
            focal = 720 / (2. * math.tan(math.pi / 6.))
            origin = ((x - 480) * 6. / focal, 1.4 - (y - 360) * 6. / focal, 0.)
            direction = geometry.normalized((origin[0], origin[1] - 1.4, -6.))
            crossings = reference.dense_receiver_intersections(origin, direction)
            self.assertEqual(len(crossings), 18)
            self.assertTrue(all(second - first >= 1. for first, second in zip(crossings, crossings[1:])))
            self.assertAlmostEqual(geometry.add(origin, geometry.scale(direction, crossings[0]))[2], -8.)
            self.assertAlmostEqual(geometry.add(origin, geometry.scale(direction, crossings[-1]))[2], -25.)
            self.assertGreater(crossings[-1], -14. / direction[2])
            for case in reference.DENSE_CASES:
                actual, evidence = reference.pixel_reference(case, x, y)
                self.assertEqual(evidence[LIT_CROSSINGS], 18)
                self.assertTrue(all(value > 0. for value in actual))
                self.assertLess(geometry.add(origin, geometry.scale(direction, crossings[-1]))[0], 11.)

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
        # Synthetic pixels test the image analyzer.
        rows = [[(0, 0, 0)] * 960 for _ in range(720)]
        for x, y in self.points:
            value, _ = reference.pixel_reference(case, x + shift + 0.5, y + 0.5, max_queries=queries)
            rows[y][x] = tuple(reference.encode_radiance(channel * channel_scale) for channel in value)
        return 960, 720, rows

    def setUp(self):
        self.points = tuple((x, y) for y in range(204, 509, 16) for x in range(214, 749, 16))

    def analyze(self, frame, spec):
        with patch.object(ReflectionOpticalImageEvidence, "reference_points", return_value=iter(self.points)):
            return ReflectionOpticalImageEvidence.analyze_image(frame, spec)

    def test_dense_receiver_capacity_loss_cannot_pass_as_black_or_transmitted_chart(self):
        for case in reference.DENSE_CASES:
            spec = smoke.OpticalCapture(case, case)
            self.points = tuple(ReflectionOpticalImageEvidence.reference_points(spec))
            metrics = self.analyze(self.synthetic_frame(case), spec)
            self.assertEqual(metrics["dense_receiver_crossings"]["minimum"], 18)
            for wrong in (self.synthetic_frame(case, channel_scale=0.), self.synthetic_frame(LIT_OPTICAL_CLEAR)):
                with self.subTest(case=case), self.assertRaises(SmokeFailure):
                    self.analyze(wrong, spec)

    def test_dense_receiver_cannot_claim_a_shorter_closed_collection(self):
        frame = self.synthetic_frame(LIT_OPTICAL_CSG_DENSE)
        with patch.object(optical_image, LIT_PIXEL_REFERENCE, return_value=((.9, .2, .05), {LIT_REASON: "opaque_dense", LIT_CROSSINGS: 16})):
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
            with patch.object(optical_image, LIT_PIXEL_REFERENCE, return_value=((0, 0, 0), {LIT_REASON: LIT_CHART, LIT_CROSSINGS: crossings})):
                with self.assertRaisesRegex(SmokeFailure, "re-entry coverage"):
                    self.analyze(frame, smoke.OpticalCapture(LIT_OPTICAL_TORUS, LIT_OPTICAL_TORUS))


if __name__ == LIT_MAIN:
    unittest.main()
