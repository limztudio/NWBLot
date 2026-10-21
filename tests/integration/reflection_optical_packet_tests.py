#!/usr/bin/env python3
"""Reflection/optical queue coverage, collection reuse, slice packet, and context transition analysis."""


from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import reflection_optical_reference as reference
import reflection_optical_smoke as smoke
import reflection_csg_context_smoke as context
from reflection_roughness_smoke import HISTORY_FIELDS
from reflection_smoke import STATISTICS_FIELDS, SmokeFailure


LIT_QUERY_LIMIT = "query_limit"
LIT_REFLECTIONSMOKEOPTICS = "ReflectionSmokeOptics: "
LIT_HARDWARE_RAYS = "hardware_rays"
LIT_HARDWARE_HITS = "hardware_hits"
LIT_GLASS_PIXELS = "glass_pixels"
LIT_FALLBACK_PIXELS = "fallback_pixels"
LIT_BOOTSTRAP_EVENTS = "bootstrap_events"
LIT_UNSUPPORTED_PATHS = "unsupported_paths"
LIT_LIMITED_PATHS = "limited_paths"
LIT_AMBIGUOUS_PATHS = "ambiguous_paths"
LIT_MEDIUM_OVERFLOW_PATHS = "medium_overflow_paths"
LIT_OPTICS = "optics"
LIT_OPTICAL_CSG_CAP = "optical_csg_cap"
LIT_STATISTICS = "statistics"
LIT_SLICE_PACKETS = "slice_packets"
LIT_COMPLETED_FRAMES = "completed_frames"
LIT_ACCEPTED_PACKETS = "accepted_packets"
LIT_OPTICAL_CSG_CAVITY = "optical_csg_cavity"
LIT_REFLECTIONSMOKESLICEPACKETS = "ReflectionSmokeSlicePackets:"
LIT_DISTINCT_SINGLE_TASK = "distinct single-task"
LIT_HARDWARE_NODES = "hardware_nodes"
LIT_INDEXED_SLICES = "indexed_slices"
LIT_COMPILED_NODES = "compiled_nodes"
LIT_UNIQUE_PACKETS = "unique_packets"
LIT_SINGLE_TASK_PACKETS = "single_task_packets"
LIT_GRAPHICS_PACKETS = "graphics_packets"
LIT_SMALL_CAP_PHASE = "small cap phase"
LIT_CROSSINGS = "crossings"
LIT_REASON = "reason"
LIT_CHART = "chart"
LIT_TIR_EVENTS = "tir_events"
LIT_QUERIES = "queries"
LIT_SEQUENCE = "sequence"
LIT_MAIN = "__main__"


class QueueCoverageTests(unittest.TestCase):
    def log(self, budget, ray_delta=0, sequence=9, queries=None, physical_queries=None, transparent=None):
        admitted = min(217668, budget)
        rays = admitted + ray_delta
        ready = int(budget > 0)
        sample = dict(sequence=sequence, generation=2, frame=sequence, mode=2, width=960, height=720, requested_budget=budget, effective_budget=budget, queue_capacity=max(1, budget), hardware_requested=1, hardware_available=ready, hardware_ready=ready,
            token_queue=0, token_value=sequence + 100, physical_queue=0, device_generation=1, candidates=217668 if budget else 0, hardware_rays=rays, hardware_hits=0, opaque_pixels=217668, glass_pixels=0,
            fallback_pixels=217668, screen_attempts=0, screen_hits=0)
        budget_units = rays if queries is None else queries
        values = dict(zip(smoke.OPTICS_FIELDS, (sequence, 2, 16, budget_units, budget_units if physical_queries is None else physical_queries, 0, min(rays, 100) if transparent is None else transparent, 0, 0, 0, 0, 0, ready)))
        history = dict(zip(HISTORY_FIELDS, (sequence, 2, sequence - 1, 1, 0, 0, sequence - 1, 0, 0, 0, 0, 0)))
        expected = (budget + smoke.CSG_TRACE_SLICE_RAYS - 1) // smoke.CSG_TRACE_SLICE_RAYS
        packets = dict(zip(smoke.SLICE_PACKET_FIELDS, (sequence - 1, 23, 1, 0, 1, budget, expected, expected, expected, expected, expected, expected, expected, expected)))
        return "ReflectionSmokeStatistics: " + " ".join(f"{name}={sample[name]}" for name in STATISTICS_FIELDS) + "\n" \
            + LIT_REFLECTIONSMOKEOPTICS + " ".join(f"{name}={values[name]}" for name in smoke.OPTICS_FIELDS) + "\n" \
            + "ReflectionSmokeHistory: " + " ".join(f"{name}={history[name]}" for name in HISTORY_FIELDS) + "\n" \
            + "ReflectionSmokeSlicePackets: " + " ".join(f"{name}={packets[name]}" for name in smoke.SLICE_PACKET_FIELDS)

    def validate(self, budget, **changes):
        spec = smoke.OpticalCapture(f"budget_{budget}", LIT_OPTICAL_CSG_CAP, ray_budget=budget)
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
        spec = smoke.OpticalCapture("tail", LIT_OPTICAL_CSG_CAP, ray_budget=8193)
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


class CollectionReuseTests(unittest.TestCase):
    def evidence(self, spec, units, physical, limited=0, sequence=9):
        rays = 217668
        statistics = dict(sequence=sequence, generation=2, frame=sequence, candidates=rays, effective_budget=rays, hardware_rays=rays, hardware_hits=rays, opaque_pixels=rays, glass_pixels=0, fallback_pixels=0)
        optical = dict(zip(smoke.OPTICS_FIELDS, (sequence, 2, spec.queries, units * rays, physical * rays, 0, rays, 0, limited * rays, 0, 0, 0, 1)))
        return {LIT_STATISTICS: [statistics], LIT_OPTICS: [optical]}

    def test_closed_open_and_exhausted_controls_require_exact_physical_work(self):
        expected = ((11, 7, 0), (12, 12, 0), (8, 5, 1), (5, 5, 0), (11, 7, 0))
        self.assertEqual(len(smoke.CSG_REUSE_CAPTURES) - 1, len(expected))
        for spec, (units, physical, limited) in zip(smoke.CSG_REUSE_CAPTURES[1:], expected):
            with self.subTest(case=spec.name):
                result = smoke.validate_collection_reuse(self.evidence(spec, units, physical, limited), spec)
                self.assertEqual(result["physical_queries_per_ray"], physical)
                self.assertEqual(result["reserved_units_per_ray"], units)
                for delta in (-1, 1):
                    with self.assertRaises(SmokeFailure):
                        smoke.validate_collection_reuse(self.evidence(spec, units, physical + delta, limited), spec)
                    with self.assertRaises(SmokeFailure):
                        smoke.validate_collection_reuse(self.evidence(spec, units + delta, physical, limited), spec)

    def test_geometry_failure_and_fallback_cannot_qualify_query_reduction(self):
        spec = smoke.CSG_REUSE_CAPTURES[1]
        for field, value in ((LIT_HARDWARE_HITS, 217667), (LIT_FALLBACK_PIXELS, 1), (LIT_GLASS_PIXELS, 1), (LIT_HARDWARE_RAYS, 0)):
            evidence = self.evidence(spec, 11, 7)
            evidence[LIT_STATISTICS][0][field] = value
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                smoke.validate_collection_reuse(evidence, spec)
        for field in (LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS, LIT_BOOTSTRAP_EVENTS):
            evidence = self.evidence(spec, 11, 7)
            evidence[LIT_OPTICS][0][field] = 1
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                smoke.validate_collection_reuse(evidence, spec)

    def test_exact_completed_source_and_all_stable_frames_are_required(self):
        spec = smoke.CSG_REUSE_CAPTURES[1]
        evidence = self.evidence(spec, 11, 7)
        evidence[LIT_OPTICS][0][LIT_SEQUENCE] += 1
        with self.assertRaises(SmokeFailure):
            smoke.validate_collection_reuse(evidence, spec)
        evidence = self.evidence(spec, 11, 8)
        final = self.evidence(spec, 11, 7, sequence=10)
        evidence[LIT_STATISTICS] += final[LIT_STATISTICS]
        evidence[LIT_OPTICS] += final[LIT_OPTICS]
        with self.assertRaises(SmokeFailure):
            smoke.validate_collection_reuse(evidence, spec)

    def test_valid_air_unspecified_controls_keep_positive_transport_and_geometric_checks(self):
        reference_color, ordinary = reference.trace_transmission("optical_unspecified_reference", (0., 1.4, 0.), (0., 0., -1.))
        csg_color, retained = reference.trace_transmission("optical_csg_unspecified_cap", (0., 1.4, 0.), (0., 0., -1.))
        self.assertEqual(reference_color, csg_color)
        self.assertGreater(min(csg_color), 0.)
        self.assertEqual((ordinary[LIT_QUERIES], retained[LIT_QUERIES]), (5, 11))
        for details in (ordinary, retained):
            self.assertEqual((details[LIT_CROSSINGS], details[LIT_REASON], details[LIT_TIR_EVENTS]), (2, LIT_CHART, 0))
        for spec, units, physical in ((smoke.CSG_REUSE_CAPTURES[-2], 5, 5), (smoke.CSG_REUSE_CAPTURES[-1], 11, 7)):
            for field in (LIT_UNSUPPORTED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_LIMITED_PATHS, LIT_BOOTSTRAP_EVENTS):
                evidence = self.evidence(spec, units, physical)
                evidence[LIT_OPTICS][0][field] = 1
                with self.subTest(case=spec.case, field=field), self.assertRaises(SmokeFailure):
                    smoke.validate_collection_reuse(evidence, spec)
            evidence = self.evidence(spec, units, physical)
            evidence[LIT_STATISTICS][0][LIT_FALLBACK_PIXELS] = 1
            with self.subTest(case=spec.case), self.assertRaises(SmokeFailure):
                smoke.validate_collection_reuse(evidence, spec)

    def test_independent_geometry_and_reservation_oracle_preserve_limit(self):
        closed, closed_details = reference.trace_transmission(LIT_OPTICAL_CSG_CAP, (0., 1.4, 0.), (0., 0., -1.))
        opened, open_details = reference.trace_transmission("optical_csg_open_tail", (0., 1.4, 0.), (0., 0., -1.))
        self.assertEqual(closed, opened)
        self.assertGreater(sum(closed), 0.)
        self.assertEqual((closed_details[LIT_QUERIES], open_details[LIT_QUERIES]), (11, 12))
        limited, details = reference.trace_transmission(LIT_OPTICAL_CSG_CAP, (0., 1.4, 0.), (0., 0., -1.), 8)
        self.assertEqual(limited, (0., 0., 0.))
        self.assertEqual((details[LIT_QUERIES], details[LIT_REASON]), (8, LIT_QUERY_LIMIT))


class SlicePacketTests(unittest.TestCase):
    def log(self, budget=8193, sequence=9):
        return QueueCoverageTests().log(budget, sequence=sequence)

    def validate(self, text, budget=8193):
        return smoke.validate_optics(text, smoke.OpticalCapture("packet_test", LIT_OPTICAL_CSG_CAP, ray_budget=budget))

    def test_each_boundary_budget_has_exact_accepted_single_task_packets(self):
        for budget in smoke.QUEUE_BOUNDARY_BUDGETS:
            with self.subTest(budget=budget):
                record = self.validate(self.log(budget), budget)[LIT_SLICE_PACKETS][LIT_COMPLETED_FRAMES][0]
                self.assertEqual(record[LIT_ACCEPTED_PACKETS], (budget + 8191) // 8192)
        self.assertEqual(self.validate(self.log(smoke.DEFAULT_RAY_BUDGET), smoke.DEFAULT_RAY_BUDGET) [LIT_SLICE_PACKETS][LIT_COMPLETED_FRAMES][0][LIT_ACCEPTED_PACKETS], 169)

    def test_cavity_requires_the_same_accepted_packet_proof(self):
        spec = smoke.OpticalCapture(LIT_OPTICAL_CSG_CAVITY, LIT_OPTICAL_CSG_CAVITY, ray_budget=8193)
        result = smoke.validate_optics(self.log(), spec)
        self.assertEqual(result[LIT_SLICE_PACKETS][LIT_COMPLETED_FRAMES][0][LIT_ACCEPTED_PACKETS], 2)
        text = "\n".join(line for line in self.log().splitlines() if LIT_REFLECTIONSMOKESLICEPACKETS not in line)
        with self.assertRaisesRegex(SmokeFailure, "exact source frame"):
            smoke.validate_optics(text, spec)

    def test_all_indirect_calls_in_one_packet_cannot_pass(self):
        with self.assertRaisesRegex(SmokeFailure, LIT_DISTINCT_SINGLE_TASK):
            self.validate(self.log().replace("unique_packets=2", "unique_packets=1") .replace("single_task_packets=2", "single_task_packets=0"))

    def test_unaccepted_recovery_merged_or_wrong_queue_packet_fails(self):
        for name in (LIT_HARDWARE_NODES, LIT_INDEXED_SLICES, LIT_COMPILED_NODES, LIT_UNIQUE_PACKETS, LIT_ACCEPTED_PACKETS, LIT_SINGLE_TASK_PACKETS, LIT_GRAPHICS_PACKETS):
            with self.subTest(name=name), self.assertRaisesRegex(SmokeFailure, LIT_DISTINCT_SINGLE_TASK):
                self.validate(self.log().replace(name + "=2", name + "=1"))

    def test_stale_frame_plan_or_physical_generation_cannot_pass(self):
        for old, new in (("source_frame=8", "source_frame=7"), ("runtime_present=1", "runtime_present=0"), ("plan_generation=23", "plan_generation=0"), ("graphics_queue=0", "graphics_queue=65535")):
            with self.subTest(old=old), self.assertRaises(SmokeFailure):
                self.validate(self.log().replace(old, new))
        text = self.log().replace("ReflectionSmokeSlicePackets: source_frame=8 plan_generation=23 device_generation=1", "ReflectionSmokeSlicePackets: source_frame=8 plan_generation=23 device_generation=2")
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
        with self.assertRaisesRegex(SmokeFailure, LIT_DISTINCT_SINGLE_TASK):
            self.validate(self.log(0).replace("hardware_nodes=0", "hardware_nodes=1"), 0)

    def test_complete_final_frame_cannot_hide_an_earlier_merged_packet(self):
        text = self.log().replace("unique_packets=2", "unique_packets=1") + "\n" + self.log(sequence=10)
        with self.assertRaisesRegex(SmokeFailure, LIT_DISTINCT_SINGLE_TASK):
            self.validate(text)

    def test_packet_evidence_requires_exact_completed_source_metadata(self):
        for marker in (LIT_REFLECTIONSMOKESLICEPACKETS, "ReflectionSmokeHistory:"):
            text = "\n".join(line for line in self.log().splitlines() if marker not in line)
            with self.subTest(marker=marker), self.assertRaises(SmokeFailure):
                self.validate(text)

    def test_duplicate_or_malformed_packet_identity_fails(self):
        line = self.log().splitlines()[-1]
        for text in (self.log() + "\n" + line, self.log() + " hardware_nodes=2", self.log().replace("compiled_nodes=2", "compiled_nodes=-1"), self.log() + " junk"):
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
                sample = QueueCoverageTests().log(smoke.DEFAULT_RAY_BUDGET, sequence=begin + index + 1, queries=queries, physical_queries=217668 * (7 if cut_receiver_primitives == 12 else 2 if csg else 1),
                    transparent=217668 if cut_receiver_primitives == 12 else 0)
                sample = sample.replace("hardware_hits=0", "hardware_hits=217668").replace("fallback_pixels=217668", "fallback_pixels=0")
                if cut_receiver_primitives != 12:
                    sample = sample.replace("transport_enabled=1", "transport_enabled=0")
                if not csg:
                    for field in ("expected_slices", LIT_HARDWARE_NODES, LIT_INDEXED_SLICES, LIT_COMPILED_NODES, LIT_UNIQUE_PACKETS, LIT_ACCEPTED_PACKETS, LIT_SINGLE_TASK_PACKETS, LIT_GRAPHICS_PACKETS):
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
        for field, value in ((LIT_HARDWARE_NODES, 168), (LIT_INDEXED_SLICES, 168), (LIT_ACCEPTED_PACKETS, 168)):
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                context.analyze_evidence(self.log().replace(field + "=169", field + "=" + str(value)))
        ordinary = self.log().replace("expected_slices=1 hardware_nodes=1", "expected_slices=169 hardware_nodes=169")
        with self.assertRaisesRegex(SmokeFailure, "stale CSG slices"):
            context.analyze_evidence(ordinary)

    def test_ordinary_matched_control_cannot_use_a_different_hardware_queue(self):
        text = self.log().replace("single_task_packets=1 graphics_packets=1", "single_task_packets=1 graphics_packets=0", 1)
        self.assertNotEqual(text, self.log())
        with self.assertRaisesRegex(SmokeFailure, "ordinary matched control"):
            context.analyze_evidence(text)

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
        with self.assertRaisesRegex(SmokeFailure, LIT_SMALL_CAP_PHASE):
            context.analyze_evidence(self.log().replace("query_budget_units=2394348", "query_budget_units=2394347", 1))
        with self.assertRaises(SmokeFailure):
            context.analyze_evidence(self.log().replace("transparent_paths=217668", "transparent_paths=0", 1))

    def test_small_phase_physical_replay_cannot_hide_behind_unchanged_budget_units(self):
        for wrong in (6, 8, 11):
            with self.subTest(physical_queries_per_ray=wrong), self.assertRaisesRegex(SmokeFailure, LIT_SMALL_CAP_PHASE):
                context.analyze_evidence(self.log().replace("physical_queries=1523676", f"physical_queries={217668 * wrong}", 1))

    def test_context_phase_identity_or_completion_count_cannot_be_replayed(self):
        for old, new in (("phase=3 source_frame=80 cut_receiver_primitives=12 csg=1", "phase=3 source_frame=80 cut_receiver_primitives=108 csg=1"), ("completed_frames=18", "completed_frames=15"), ("complete captures=5", "complete captures=4")):
            with self.subTest(old=old), self.assertRaises(SmokeFailure):
                context.analyze_evidence(self.log().replace(old, new, 1))
        with self.assertRaises(SmokeFailure):
            context.analyze_evidence(self.log() + "\nFramebufferCapture: graphics source frame 120")


if __name__ == LIT_MAIN:
    unittest.main()
