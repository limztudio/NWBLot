#!/usr/bin/env python3
"""CPU checks for the temporal recording proof's whole-log and control coverage policy."""

from dataclasses import asdict
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import reflection_temporal_omission_smoke as proof


class TemporalOmissionEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name)

    def trial(self, cap, warmup_temporal=False, tail_temporal=False, control_count=100):
        benchmark = proof.benchmark
        def report(temporal, frames):
            rows = [f"=== interval: {frames} frames / 0.5s ==="]
            for scope in benchmark.required_scopes(proof.VARIANT, cap):
                count = control_count if scope in benchmark.OBSERVED_CONTROLS else frames
                rows.append(f"  {scope}: total_ms=1 gpu_samples={count}")
            if temporal and cap == 1:
                rows.append(f"  {benchmark.TEMPORAL}: total_ms=1 gpu_samples=1")
            return "\n".join(rows) + "\n"
        text = report(warmup_temporal, 100) + report(False, 100) + report(tail_temporal, 100)
        (self.path / "gpu_timing.txt").write_text(text, encoding="utf-8")
        reports = benchmark.parse_intervals(text, finalized=True)
        return {"variant": asdict(proof.VARIANT), "artifacts": str(self.path), "retained_report_range": [1, 2],
            "reports": 1, "scopes": benchmark.summarize_intervals(reports[1:2]), "mip_count": 10}

    def test_cap_one_requires_no_temporal_ranges_in_any_completed_report(self):
        result = proof.validate_recording_evidence(self.trial(1), 1)
        self.assertEqual(result["all_completed_temporal_ranges"], 0)
        self.assertEqual(result["completed_gpu_frames"], 100)

    def test_cap_sixteen_preserves_active_temporal_coverage(self):
        result = proof.validate_recording_evidence(self.trial(16), 16)
        self.assertEqual(result["retained_temporal_ranges"], 100)

    def test_cap_one_cannot_hide_work_in_warmup(self):
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "warm-up"):
            proof.validate_recording_evidence(self.trial(1, warmup_temporal=True), 1)

    def test_cap_one_cannot_hide_work_in_final_shutdown_report(self):
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "shutdown"):
            proof.validate_recording_evidence(self.trial(1, tail_temporal=True), 1)

    def test_selective_control_range_loss_is_rejected(self):
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "control scope coverage"):
            proof.validate_recording_evidence(self.trial(1, control_count=97), 1)

    def test_two_range_publication_skew_is_retained(self):
        proof.validate_recording_evidence(self.trial(16, control_count=98), 16)

    def test_changed_summary_is_rejected(self):
        trial = self.trial(1)
        trial["scopes"][proof.benchmark.FRAME]["gpu_samples"] += 1
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "raw timing"):
            proof.validate_recording_evidence(trial, 1)


class TemporalOmissionCapabilityTests(unittest.TestCase):
    def test_one_explicit_clean_unavailable_marker_can_skip(self):
        self.assertTrue(proof.hardware_unavailable_without_errors(
            "ReflectionSmokeProject: hardware unavailable\nReflectionSmokeProject: shutdown\n"))

    def test_available_and_unavailable_are_contradictory(self):
        self.assertFalse(proof.hardware_unavailable_without_errors(
            "ReflectionSmokeProject: hardware unavailable\nReflectionSmokeProject: hardware available\n"))

    def test_repeated_unavailable_marker_is_not_a_capability_proof(self):
        self.assertFalse(proof.hardware_unavailable_without_errors(
            "ReflectionSmokeProject: hardware unavailable\n" * 2))

    def test_no_marker_cannot_hide_an_acquisition_failure(self):
        self.assertFalse(proof.hardware_unavailable_without_errors("ReflectionSmokeProject: shutdown\n"))

    def test_validation_or_error_messages_are_never_masked(self):
        for error in proof.benchmark.STRICT_LOG_FAILURE_MESSAGES:
            with self.subTest(error=error):
                self.assertFalse(proof.hardware_unavailable_without_errors(
                    "ReflectionSmokeProject: hardware unavailable\n" + error + " detected\n"))


if __name__ == "__main__":
    unittest.main()
