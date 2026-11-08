#!/usr/bin/env python3
"""Reflection optical statistics and evidence output regression tests."""

import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import reflection_optical_smoke as smoke
from reflection_optical_evidence import ReflectionOpticalEvidence, ReflectionStatistics as ReflectionOpticalEvidenceStatistics
from reflection_optical_evidence import OPTICS_FIELDS
from reflection_smoke import SmokeFailure

LIT_AMBIGUOUS_PATHS = 'ambiguous_paths'
LIT_APP_EXE = 'app.exe'
LIT_BOOTSTRAP_EVENTS = 'bootstrap_events'
LIT_EVERY_ADMITTED_HARDWARE_RAY = 'every admitted hardware ray'
LIT_EXECUTABLE = '--executable'
LIT_FALLBACK_PIXELS = 'fallback_pixels'
LIT_FRAME = 'frame'
LIT_GENERATION = 'generation'
LIT_GLASS_PIXELS = 'glass_pixels'
LIT_HARDWARE_HITS = 'hardware_hits'
LIT_HARDWARE_RAYS = 'hardware_rays'
LIT_HARDWARE_READY = 'hardware_ready'
LIT_LIMITED_PATHS = 'limited_paths'
LIT_MEDIUM_OVERFLOW_PATHS = 'medium_overflow_paths'
LIT_OPTICAL_CLEAR = 'optical_clear'
LIT_OPTICAL_CSG_DENSE = 'optical_csg_dense'
LIT_OPTICAL_CSG_GROUP_ENTRY = 'optical_csg_group_entry'
LIT_OPTICAL_CSG_GROUP_GAP = 'optical_csg_group_gap'
LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP = 'optical_csg_group_gap_sub_ulp'
LIT_OPTICAL_GROUP_ENTRY = 'optical_group_entry'
LIT_OPTICAL_GROUP_GAP = 'optical_group_gap'
LIT_OPTICAL_GROUP_GAP_SUB_ULP = 'optical_group_gap_sub_ulp'
LIT_OPTICAL_REFERENCE = 'optical_reference'
LIT_OPTICAL_UNSPECIFIED = 'optical_unspecified'
LIT_OPTICS = 'optics'
LIT_OUTPUT = 'output'
LIT_OUTPUT_DIRECTORY = '--output-directory'
LIT_OUTPUT_ROOT = '--output-root'
LIT_PARSE_STATISTICS = 'parse_statistics'
LIT_PHYSICAL_QUERIES = 'physical_queries'
LIT_PROOF_TXT = 'proof.txt'
LIT_QUERY_BUDGET_UNITS = 'query_budget_units'
LIT_QUEUE_BOUNDARIES = '--queue-boundaries'
LIT_REFLECTIONSMOKEOPTICS = 'ReflectionSmokeOptics: '
LIT_RETAINED_EVIDENCE = 'retained evidence'
LIT_RUNS = 'runs'
LIT_RUNTIME = 'runtime'
LIT_RUN_QUEUE_BOUNDARY_SUITE = 'run_queue_boundary_suite'
LIT_SEQUENCE = 'sequence'
LIT_TIR_EVENTS = 'tir_events'
LIT_TRANSPARENT_PATHS = 'transparent_paths'
LIT_TRANSPORT_ENABLED = 'transport_enabled'
LIT_UNSUPPORTED_PATHS = 'unsupported_paths'
LIT_UTF_8 = 'utf-8'
LIT_VALIDATE_SLICE_PACKETS = 'validate_slice_packets'
LIT_VALIDATE_STATISTICS = 'validate_statistics'
LIT_WORKING_DIRECTORY = '--working-directory'


class StatisticsTests(unittest.TestCase):
    def log(self, overrides=None):
        values = dict(zip(OPTICS_FIELDS, (9, 2, 16, 500, 500, 0, 100, 0, 0, 0, 0, 0, 1)))
        values.update(overrides or {})
        return LIT_REFLECTIONSMOKEOPTICS + " ".join(f"{name}={values[name]}" for name in OPTICS_FIELDS)

    def validate(self, text, spec=smoke.OpticalCapture(LIT_OPTICAL_CLEAR, LIT_OPTICAL_CLEAR), statistics_overrides=None):
        sample = {LIT_SEQUENCE: 9, LIT_GENERATION: 2, LIT_HARDWARE_RAYS: 100, LIT_FRAME: 9, LIT_HARDWARE_READY: 1, "candidates": 100, "effective_budget": smoke.DEFAULT_RAY_BUDGET, LIT_HARDWARE_HITS: 100, "opaque_pixels": 100, LIT_GLASS_PIXELS: 0, LIT_FALLBACK_PIXELS: 0}
        sample.update(statistics_overrides or {})
        statistics = [sample]
        with patch.object(ReflectionOpticalEvidenceStatistics, LIT_PARSE_STATISTICS, return_value=statistics), patch.object(ReflectionOpticalEvidenceStatistics, LIT_VALIDATE_STATISTICS), \
            patch.object(ReflectionOpticalEvidence, LIT_VALIDATE_SLICE_PACKETS, return_value=None):
            return ReflectionOpticalEvidence.validate_optics(text, spec)

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
        with patch.object(ReflectionOpticalEvidenceStatistics, LIT_PARSE_STATISTICS, return_value=statistics), patch.object(ReflectionOpticalEvidenceStatistics, LIT_VALIDATE_STATISTICS), \
            patch.object(ReflectionOpticalEvidence, LIT_VALIDATE_SLICE_PACKETS, return_value=None):
            with self.assertRaisesRegex(SmokeFailure, LIT_EVERY_ADMITTED_HARDWARE_RAY):
                ReflectionOpticalEvidence.validate_optics(text, spec)

    def test_malformed_or_duplicate_fields_fail(self):
        for text in ("", self.log() + " sequence=9", self.log().replace("limited_paths=0", "other=0")):
            with self.assertRaises(SmokeFailure):
                ReflectionOpticalEvidence.parse_optics(text)


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


if __name__ == "__main__":
    unittest.main()
