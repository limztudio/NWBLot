#!/usr/bin/env python3
"""CPU-only tests of reflection benchmark timing analysis."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import reflection_benchmark as benchmark
from reflection_gpu_coverage import ReflectionGpuCoverage
from gpu_timing_evidence import GpuTimingEvidence, ScopeSample

TIMING_EVIDENCE = GpuTimingEvidence()
from experiment_statistics import PairedExperimentAnalysis
from reflection_experiment_analysis import ReflectionExperimentAnalysis
from reflection_benchmark_test_support import report

LIT_UNRESOLVED = "unresolved"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_MEAN_MS = "mean_ms"
LIT_FLOOR = "floor"
LIT_HYBRID = "hybrid"
LIT_BLOCK = "block"
LIT_VARIANT = "variant"
LIT_SCOPES = "scopes"
LIT_KERNEL_WORK_MS = "kernel_work_ms"
LIT_HARDWARE = "hardware"
LIT_HARDWARE_TEMPORAL = "hardware_temporal"
LIT_MISSING_GPU_SCOPES = "missing GPU scopes"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_STATUS = "status"
LIT_RESOLVED_GPU_TIME_REDUCTION = "resolved_gpu_time_reduction"
LIT_NAME = "name"
LIT_MAIN = "__main__"


def synthetic_scopes(variant, frame_ms=5.0, control_ms=.5, gpu_frames=200, mip_count=10):
    result = {}
    for scope in ReflectionGpuCoverage.required_scopes(variant):
        mean = frame_ms if scope == benchmark.FRAME else control_ms if scope in benchmark.OBSERVED_CONTROLS else .1
        samples = gpu_frames * (mip_count if scope == benchmark.DEPTH else 1)
        result[scope] = {"total_ms": mean * samples, LIT_GPU_SAMPLES: samples, LIT_REPORTS: 16, LIT_MEAN_MS: mean}
    return result


def matched_trials(frame_delta=-.5, control_delta=0.0):
    variants = benchmark.variants_for(LIT_FLOOR)
    trials = []
    for block in range(6):
        for variant in variants:
            delta = frame_delta if variant.name == LIT_HYBRID else 0.0
            controls = .5 + (control_delta if variant.name == LIT_HYBRID else 0.0)
            scopes = synthetic_scopes(variant, 5.0 + block * .03 + delta, controls)
            trials.append({LIT_BLOCK: block, LIT_VARIANT: benchmark.asdict(variant), benchmark.LIT_POSITIVE_FRAME_REPORTS: 16, LIT_SCOPES: scopes, LIT_KERNEL_WORK_MS: ReflectionGpuCoverage.kernel_work_ms(scopes, variant, 10)})
    return variants, trials


class TimingNormalizationTests(unittest.TestCase):
    def test_weights_actual_gpu_samples_and_ignores_published_averages(self):
        text = report([(benchmark.FRAME, 90, 10)], 2000) + report([(benchmark.FRAME, 100, 100)], 1)
        intervals = TIMING_EVIDENCE.parse_intervals(text, finalized=True)
        summary = TIMING_EVIDENCE.summarize_intervals(intervals)[benchmark.FRAME]
        self.assertAlmostEqual(summary[LIT_MEAN_MS], 190 / 110)
        self.assertEqual(summary[LIT_GPU_SAMPLES], 110)
        self.assertNotAlmostEqual(summary[LIT_MEAN_MS], 5)

    def test_live_partial_tail_is_not_used_or_parsed(self):
        text = report([(benchmark.FRAME, 2, 1)]) + "=== interval: 1 frames / 0.5s ===\n  render.frame: total_ms="
        self.assertEqual(len(TIMING_EVIDENCE.parse_intervals(text)), 1)
        with self.assertRaises(benchmark.SmokeFailure):
            TIMING_EVIDENCE.parse_intervals(text + "\n=== interval: 1 frames / 0.5s ===\n")

    def test_retired_partial_nonfinite_empty_and_duplicate_rows_fail_then_recover(self):
        current = report([(benchmark.FRAME, 1, 2)])
        retired = current.replace("window_avg_ms=", "avg=").replace("window_min_ms=", "min=")
        retired = retired.replace("window_max_ms=", "max=").replace("published_windows=", "samples=")
        invalid = [retired, current.replace(" sample_avg_ms=777", "")]
        invalid.extend(current.replace(old, new) for old, new in (("window_avg_ms=999", "avg=999"), ("window_min_ms=888", "min=888"), ("window_max_ms=1000", "max=1000"), ("published_windows=2", "samples=2"),
            ("total_ms=1", "total_ms=NaN"), ("total_ms=1", "total_ms=-1"), ("gpu_samples=2", "gpu_samples=0"), ("gpu_samples=2", "gpu_samples=1.5"), ("gpu_samples=2", "gpu_samples=2 gpu_samples=3"), ("published_windows=2", "published_windows=0"),
            ("published_windows=2", "published_windows=4294967296"), ("gpu_samples=2", "gpu_samples=18446744073709551616"), ("window_min_ms=888", "window_min_ms=1001"), ("sample_avg_ms=777", "sample_avg_ms=1e999"),))
        invalid.append(report([(benchmark.FRAME, 1, 1), (benchmark.FRAME, 1, 1)]))
        for text in invalid:
            for evidence in (text, current + text):
                with self.subTest(evidence=evidence), self.assertRaises(benchmark.SmokeFailure):
                    TIMING_EVIDENCE.parse_intervals(evidence, finalized=True)
            recovered = TIMING_EVIDENCE.parse_intervals(current, finalized=True)
            self.assertEqual(recovered[0][benchmark.FRAME], ScopeSample(1, 2))

    def test_zero_duration_is_valid_for_a_sampled_kernel(self):
        parsed = TIMING_EVIDENCE.parse_intervals(report([(benchmark.CLASSIFY, 0, 5)]), finalized=True)
        self.assertEqual(TIMING_EVIDENCE.summarize_intervals(parsed)[benchmark.CLASSIFY][LIT_MEAN_MS], 0)

    def test_depth_is_per_mip_and_count_ratio_is_required(self):
        variant = benchmark.Variant(LIT_HYBRID, LIT_HYBRID)
        scopes = synthetic_scopes(variant)
        ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10)
        scopes[benchmark.DEPTH][LIT_GPU_SAMPLES] = 200
        with self.assertRaises(benchmark.SmokeFailure):
            ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10)

    def test_missing_scopes_and_sparse_publications_fail(self):
        variant = benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE)
        scopes = synthetic_scopes(variant)
        for scope in (benchmark.HARDWARE, benchmark.FRAME, benchmark.CONTROLS[0]):
            missing = copy.deepcopy(scopes)
            del missing[scope]
            with self.assertRaises(benchmark.SmokeFailure):
                ReflectionGpuCoverage.validate_coverage(missing, variant, 16, 100, 10)
        scopes[benchmark.CLASSIFY][LIT_REPORTS] = 2
        with self.assertRaises(benchmark.SmokeFailure):
            ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10)

    def test_single_sample_temporal_requires_zero_dispatches_and_preserves_other_coverage(self):
        variant = benchmark.Variant(LIT_HARDWARE_TEMPORAL, LIT_HARDWARE, temporal=True)
        scopes = synthetic_scopes(variant)
        with self.assertRaisesRegex(benchmark.SmokeFailure, "unexpected native dispatch"):
            ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10, history_samples=1)
        del scopes[benchmark.TEMPORAL]
        ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10, history_samples=1)
        self.assertAlmostEqual(ReflectionGpuCoverage.kernel_work_ms(scopes, variant, 10, history_samples=1), .3)
        for scope in (benchmark.FRAME, benchmark.CLASSIFY, benchmark.HARDWARE, benchmark.CONTROLS[0]):
            missing = copy.deepcopy(scopes)
            del missing[scope]
            with self.subTest(scope=scope), self.assertRaisesRegex(benchmark.SmokeFailure, LIT_MISSING_GPU_SCOPES):
                ReflectionGpuCoverage.validate_coverage(missing, variant, 16, 100, 10, history_samples=1)

    def test_accumulating_history_still_requires_full_temporal_dispatch_coverage(self):
        variant = benchmark.Variant(LIT_HARDWARE_TEMPORAL, LIT_HARDWARE, temporal=True)
        for history_samples in (2, 16, 256):
            with self.subTest(history_samples=history_samples):
                scopes = synthetic_scopes(variant)
                ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10, history_samples=history_samples)
                scopes[benchmark.TEMPORAL][LIT_GPU_SAMPLES] = 1
                with self.assertRaises(benchmark.SmokeFailure):
                    ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10, history_samples=history_samples)
                del scopes[benchmark.TEMPORAL]
                with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_MISSING_GPU_SCOPES):
                    ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10, history_samples=history_samples)

    def test_feedback_warmup_uses_completed_gpu_samples_before_measurement(self):
        reports = [{benchmark.FRAME: ScopeSample(1, 3)} for _ in range(15)]
        start, retained = TIMING_EVIDENCE.retain_after_warmup(reports, 6, 32)
        self.assertEqual(start, 11)
        self.assertEqual(len(retained), 4)
        start, retained = TIMING_EVIDENCE.retain_after_warmup(reports[:7], 6, 32)
        self.assertEqual(start, 7)
        self.assertEqual(retained, [])


class PairedInferenceTests(unittest.TestCase):
    def test_incomplete_cycle_and_fewer_than_five_independent_units_fail(self):
        with self.assertRaises(benchmark.SmokeFailure):
            PairedExperimentAnalysis(0).balanced_orders(benchmark.variants_for(LIT_FLOOR), 5)
        with self.assertRaises(benchmark.SmokeFailure):
            PairedExperimentAnalysis().paired_statistics([1, 2, 3, 4])

    def test_small_change_and_material_control_drift_cannot_claim_speedup(self):
        for delta, control, status in ((-.5, 0, LIT_RESOLVED_GPU_TIME_REDUCTION), (-.01, 0, LIT_UNRESOLVED), (-.5, -.1, "control_drift")):
            variants, trials = matched_trials(delta, control)
            results = ReflectionExperimentAnalysis(benchmark.FRAME, benchmark.KERNELS, benchmark.CONTROLS).compare(trials, variants)
            pair = next(item for item in results if item[LIT_BASELINE] == LIT_HARDWARE and item[LIT_CANDIDATE] == LIT_HYBRID)
            self.assertEqual(pair[LIT_STATUS], status)

    def test_ten_positive_frame_reports_in_one_trial_cannot_resolve_its_pairs(self):
        variants, trials = matched_trials()
        next(trial for trial in trials if trial[LIT_VARIANT][LIT_NAME] == LIT_HYBRID)[benchmark.LIT_POSITIVE_FRAME_REPORTS] = 10
        statuses = {(item[LIT_BASELINE], item[LIT_CANDIDATE]): item[LIT_STATUS] for item in ReflectionExperimentAnalysis(benchmark.FRAME, benchmark.KERNELS, benchmark.CONTROLS).compare(trials, variants)}
        self.assertEqual(statuses[(LIT_HARDWARE, LIT_HYBRID)], "inconclusive_interval_count")
        self.assertEqual(statuses[("disabled", LIT_HARDWARE)], LIT_UNRESOLVED)

    def test_positive_frame_reports_ignore_missing_and_zero_frame_rows(self):
        text = report([(benchmark.FRAME, 1, 2)]) + report([(benchmark.CLASSIFY, 1, 2)]) + report([(benchmark.FRAME, 0, 2)]) + report([])
        self.assertEqual(TIMING_EVIDENCE.positive_frame_reports(TIMING_EVIDENCE.parse_intervals(text, finalized=True)), 1)

    def test_noisy_controls_require_equivalence_before_a_speedup_claim(self):
        variants, trials = matched_trials()
        for trial in trials:
            if trial[LIT_VARIANT][LIT_NAME] == LIT_HYBRID:
                trial[LIT_SCOPES][benchmark.CONTROLS[0]][LIT_MEAN_MS] += .2 if trial[LIT_BLOCK] % 2 else -.2
        results = ReflectionExperimentAnalysis(benchmark.FRAME, benchmark.KERNELS, benchmark.CONTROLS).compare(trials, variants)
        pair = next(item for item in results if item[LIT_BASELINE] == LIT_HARDWARE and item[LIT_CANDIDATE] == LIT_HYBRID)
        self.assertEqual(pair[LIT_STATUS], "control_uncertain")
        self.assertFalse(pair["controls"][benchmark.CONTROLS[0]]["equivalent_within_tolerance"])

    def test_missing_or_duplicate_block_members_are_never_discarded_silently(self):
        variants, trials = matched_trials()
        with self.assertRaises(benchmark.SmokeFailure):
            ReflectionExperimentAnalysis(benchmark.FRAME, benchmark.KERNELS, benchmark.CONTROLS).compare(trials[:-1], variants)
        with self.assertRaises(benchmark.SmokeFailure):
            ReflectionExperimentAnalysis(benchmark.FRAME, benchmark.KERNELS, benchmark.CONTROLS).compare(trials + [trials[0]], variants)


class BenchmarkCoverageBoundaryTests(unittest.TestCase):
    def test_capacity_prepared_scopes_allow_only_two_frames_or_two_percent_boundary_skew(self):
        variant = benchmark.Variant(LIT_HYBRID, LIT_HYBRID)
        for frames, allowed in ((100, 2), (1000, 20)):
            for scope, multiplier in ((benchmark.CLASSIFY, 1), (benchmark.DEPTH, 10)):
                for sign in (-1, 1):
                    with self.subTest(frames=frames, scope=scope, sign=sign):
                        scopes = synthetic_scopes(variant, gpu_frames=frames)
                        scopes[scope][LIT_GPU_SAMPLES] += sign * allowed * multiplier
                        ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10)
                        scopes[scope][LIT_GPU_SAMPLES] += sign
                        with self.assertRaisesRegex(benchmark.SmokeFailure, "sample ratio"):
                            ReflectionGpuCoverage.validate_coverage(scopes, variant, 16, 100, 10)


if __name__ == LIT_MAIN:
    unittest.main()
