#!/usr/bin/env python3
"""CPU checks for the temporal recording proof's whole-log and control coverage policy."""

from dataclasses import asdict
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import reflection_temporal_omission_smoke as proof
from reflection_temporal_omission_evidence import VARIANT, ReflectionTemporalOmissionEvidence
from reflection_gpu_coverage import ReflectionGpuCoverage
from gpu_timing_evidence import GpuTimingEvidence

TIMING_EVIDENCE = GpuTimingEvidence()

LIT_N = "\n"
LIT_SCOPES = "scopes"
LIT_REFLECTIONSMOKEPROJECT_HARDWARE_UNAVAI = "ReflectionSmokeProject: hardware unavailable\n"
LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"


class TemporalOmissionEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name)

    def trial(self, cap, warmup_temporal=False, tail_temporal=False, control_count=100):
        benchmark = proof.benchmark
        def report(temporal, frames):
            rows = [f"=== interval: {frames} frames / 0.5s ==="]
            for scope in ReflectionGpuCoverage.required_scopes(VARIANT, cap):
                count = control_count if scope in benchmark.OBSERVED_CONTROLS else frames
                rows.append(f"  {scope}: window_avg_ms=1 window_min_ms=1 window_max_ms=1 published_windows=1 total_ms=1 gpu_samples={count} sample_avg_ms={1/count}")
            if temporal and cap == 1:
                rows.append(f"  {benchmark.TEMPORAL}: window_avg_ms=1 window_min_ms=1 window_max_ms=1 published_windows=1 total_ms=1 gpu_samples=1 sample_avg_ms=1")
            return LIT_N.join(rows) + LIT_N
        text = report(warmup_temporal, 100) + report(False, 100) + report(tail_temporal, 100)
        (self.path / "gpu_timing.txt").write_text(text, encoding=LIT_UTF_8)
        reports = TIMING_EVIDENCE.parse_intervals(text, finalized=True)
        return {"variant": asdict(VARIANT), "artifacts": str(self.path), "retained_report_range": [1, 2], "reports": 1, LIT_SCOPES: TIMING_EVIDENCE.summarize_intervals(reports[1:2]), "mip_count": 10}

    def test_cap_one_requires_no_temporal_ranges_in_any_completed_report(self):
        result = ReflectionTemporalOmissionEvidence.validate_recording_evidence(self.trial(1), 1)
        self.assertEqual(result["all_completed_temporal_ranges"], 0)

    def test_cap_one_cannot_hide_work_in_warmup(self):
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "warm-up"):
            ReflectionTemporalOmissionEvidence.validate_recording_evidence(self.trial(1, warmup_temporal=True), 1)

    def test_cap_one_cannot_hide_work_in_final_shutdown_report(self):
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "shutdown"):
            ReflectionTemporalOmissionEvidence.validate_recording_evidence(self.trial(1, tail_temporal=True), 1)

    def test_selective_control_range_loss_is_rejected(self):
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "control scope coverage"):
            ReflectionTemporalOmissionEvidence.validate_recording_evidence(self.trial(1, control_count=97), 1)

    def test_two_range_publication_skew_is_retained(self):
        ReflectionTemporalOmissionEvidence.validate_recording_evidence(self.trial(16, control_count=98), 16)

    def test_changed_summary_is_rejected(self):
        trial = self.trial(1)
        trial[LIT_SCOPES][proof.benchmark.FRAME]["gpu_samples"] += 1
        with self.assertRaisesRegex(proof.benchmark.SmokeFailure, "raw timing"):
            ReflectionTemporalOmissionEvidence.validate_recording_evidence(trial, 1)


class TemporalOmissionCapabilityTests(unittest.TestCase):
    def test_available_and_unavailable_are_contradictory(self):
        self.assertFalse(ReflectionTemporalOmissionEvidence.hardware_unavailable_without_errors("ReflectionSmokeProject: hardware unavailable\nReflectionSmokeProject: hardware available\n"))

    def test_repeated_unavailable_marker_is_not_a_capability_proof(self):
        self.assertTrue(ReflectionTemporalOmissionEvidence.hardware_unavailable_without_errors(LIT_REFLECTIONSMOKEPROJECT_HARDWARE_UNAVAI + "ReflectionSmokeProject: shutdown\n"))
        self.assertFalse(ReflectionTemporalOmissionEvidence.hardware_unavailable_without_errors(LIT_REFLECTIONSMOKEPROJECT_HARDWARE_UNAVAI * 2))

    def test_missing_cleanup_artifact_cannot_prove_cleanup_succeeded(self):
        with tempfile.TemporaryDirectory() as root:
            log = Path(root) / "runtime.log"
            log.write_text(LIT_REFLECTIONSMOKEPROJECT_HARDWARE_UNAVAI, encoding=LIT_UTF_8)
            error = proof.benchmark.SmokeFailure("GPU benchmark timed out: no hardware")
            cleanup = proof.benchmark.TrialCleanupStatus()
            self.assertFalse(ReflectionTemporalOmissionEvidence.hardware_skip_is_proven(error, log, cleanup))
            cleanup.succeeded = True
            self.assertTrue(ReflectionTemporalOmissionEvidence.hardware_skip_is_proven(error, log, cleanup))

    def test_source_identity_includes_capture_validation_helpers(self):
        files = {Path(path).name for path in proof.source_identity(None)}
        self.assertTrue({"window_capture_logs.py", "window_capture_validation.py", "smoke_failure_reporting.py", "renderer_benchmark_identity.py", "reflection_workload_qualification.py", "reflection_gpu_coverage.py", "gpu_timing_evidence.py", "reflection_temporal_omission_evidence.py"}.issubset(files))

    def test_no_marker_cannot_hide_an_acquisition_failure(self):
        self.assertFalse(ReflectionTemporalOmissionEvidence.hardware_unavailable_without_errors("ReflectionSmokeProject: shutdown\n"))

    def test_validation_or_error_messages_are_never_masked(self):
        for error in proof.benchmark.STRICT_LOG_FAILURE_MESSAGES:
            with self.subTest(error=error):
                self.assertFalse(ReflectionTemporalOmissionEvidence.hardware_unavailable_without_errors(LIT_REFLECTIONSMOKEPROJECT_HARDWARE_UNAVAI + error + " detected\n"))


if __name__ == LIT_MAIN:
    unittest.main()
