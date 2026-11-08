#!/usr/bin/env python3
"""Renderer A/B coverage and wall-frame evidence tests."""

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark
from renderer_trial_comparison import RendererTrialComparison
from renderer_workload_registry import RendererWorkloadRegistry, FRAME, CONTROLS
from renderer_wall_frame_evidence import RendererWallFrameEvidence
from gpu_timing_evidence import GpuTimingEvidence
from renderer_ab_benchmark_test_support import scopes, trial_matrix

TIMING_EVIDENCE = GpuTimingEvidence()

LIT_ARM = 'arm'
LIT_AVBOIT_REFRACTION_ACCUMULATION = 'avboit-refraction-accumulation'
LIT_BASELINE = 'baseline'
LIT_CANDIDATE = 'candidate'
LIT_CAUSTIC_POPULATED = 'caustic-populated'
LIT_ELIGIBLE = 'eligible'
LIT_EVERY_PLANNED_TRIAL = 'every planned trial'
LIT_FRAMES = 'frames'
LIT_FRAME_MS_DELTA = 'frame_ms_delta'
LIT_GPU_SAMPLES = 'gpu_samples'
LIT_MAX_FRAME_MS = 'max_frame_ms'
LIT_MEAN_FPS = 'mean_fps'
LIT_MEAN_FRAME_MS = 'mean_frame_ms'
LIT_MEAN_MS = 'mean_ms'
LIT_MIN_FRAME_MS = 'min_frame_ms'
LIT_REFLECTION_OPTICAL_CLEAR = 'reflection-optical-clear'
LIT_REFLECTION_OPTICAL_CSG_CAP = 'reflection-optical-csg-cap'
LIT_REPORTS = 'reports'
LIT_RESOLVED_GPU_TIME_REDUCTION = 'resolved_gpu_time_reduction'
LIT_RETAINED_REPORT_RANGE = 'retained_report_range'
LIT_SAMPLE_RATIO = 'sample ratio'
LIT_SCOPES = 'scopes'
LIT_SECONDS = 'seconds'
LIT_SHADOW_ZERO_EXTENT = 'shadow-zero-extent'
LIT_STATUS = 'status'
LIT_TRANSPARENT_MULTI = 'transparent-multi'
LIT_WALL_FRAME = 'wall_frame'


class CoverageTests(unittest.TestCase):
    def test_actual_gpu_counts_ignore_cpu_frame_and_displayed_average_fields(self):
        text = ("=== interval: 99999 frames / 0.5s ===\n" "  render.frame: window_avg_ms=123 window_min_ms=123 window_max_ms=123 published_windows=1 total_ms=90 gpu_samples=10 sample_avg_ms=9\n" "=== interval: 1 frames / 0.5s ===\n"
            "  render.frame: window_avg_ms=456 window_min_ms=456 window_max_ms=456 published_windows=1 total_ms=100 gpu_samples=100 sample_avg_ms=1\n")
        result = TIMING_EVIDENCE.summarize_intervals(TIMING_EVIDENCE.parse_intervals(text, finalized=True))
        self.assertEqual(result[FRAME][LIT_GPU_SAMPLES], 110)
        self.assertAlmostEqual(result[FRAME][LIT_MEAN_MS], 190 / 110)

    def test_partial_live_tail_is_not_counted(self):
        text = ("=== interval: 1 frames / 0.5s ===\n" "  render.frame: window_avg_ms=4 window_min_ms=4 window_max_ms=4 published_windows=1 total_ms=4 gpu_samples=1 sample_avg_ms=4\n" "=== interval: 1 frames / 0.5s ===\n  render.frame: total_ms=")
        self.assertEqual(len(TIMING_EVIDENCE.parse_intervals(text)), 1)

    def test_all_avboit_and_control_ranges_are_required(self):
        for workload_name in (LIT_TRANSPARENT_MULTI, LIT_AVBOIT_REFRACTION_ACCUMULATION):
            workload = RendererWorkloadRegistry.workloads()[workload_name]
            complete = scopes(workload)
            benchmark.validate_coverage(complete, workload, 6, 100)
            for name in workload.scopes:
                with self.subTest(workload=workload_name, scope=name):
                    missing = copy.deepcopy(complete)
                    del missing[name]
                    with self.assertRaisesRegex(benchmark.SmokeFailure, "missing .*GPU scopes"):
                        benchmark.validate_coverage(missing, workload, 6, 100)

    def test_ratio_boundary_and_sparse_reports_are_rejected(self):
        for workload_name in (LIT_TRANSPARENT_MULTI, LIT_AVBOIT_REFRACTION_ACCUMULATION):
            workload = RendererWorkloadRegistry.workloads()[workload_name]
            for frames, tolerance in ((100, 2), (1000, 20)):
                for scope in (workload.secondary_scope, CONTROLS[0]):
                    for sign in (-1, 1):
                        with self.subTest(workload=workload_name, scope=scope, frames=frames, sign=sign):
                            value = scopes(workload, frames)
                            value[scope][LIT_GPU_SAMPLES] += sign * tolerance
                            benchmark.validate_coverage(value, workload, 6, 100)
                            value[scope][LIT_GPU_SAMPLES] += sign
                            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_SAMPLE_RATIO):
                                benchmark.validate_coverage(value, workload, 6, 100)
            value = scopes(workload)
            value[workload.secondary_scope][LIT_REPORTS] = 5
            benchmark.validate_coverage(value, workload, 6, 100)
            value[workload.secondary_scope][LIT_REPORTS] = 4
            with self.subTest(workload=workload_name), self.assertRaisesRegex(benchmark.SmokeFailure, "publications"):
                benchmark.validate_coverage(value, workload, 6, 100)
            with self.subTest(workload=workload_name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(scopes(workload, 99), workload, 6, 100)


class WallFrameEvidenceTests(unittest.TestCase):
    @staticmethod
    def reports(intervals):
        fps = []
        gpu = []
        for frames, seconds in intervals:
            average = seconds * 1000 / frames
            fps.append(f"ReflectionSmokeProject: fps avg={frames / seconds:.17g} frame_ms avg={average:.17g} " f"min={average / 2:.17g} max={average * 2:.17g} frames={frames} seconds={seconds:.17g}")
            gpu.append(f"=== interval: {frames} frames / {seconds:.4f}s ===")
        return "\n".join(fps), "\n".join(gpu)

    def test_retained_intervals_weight_frames_and_ignore_shutdown_tail(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (30, .6), (900, .6)))
        value = RendererWallFrameEvidence.summarize_wall_frames(text.replace("\n", "\r\n"), timing, workload, [2, 4])
        self.assertTrue(value[LIT_ELIGIBLE])
        self.assertEqual(value[LIT_RETAINED_REPORT_RANGE], [2, 4])
        self.assertEqual(value[LIT_FRAMES], 130)
        self.assertEqual(value[LIT_REPORTS], 2)
        self.assertAlmostEqual(value[LIT_SECONDS], 1.1)
        self.assertAlmostEqual(value[LIT_MEAN_FRAME_MS], 1100 / 130)
        self.assertAlmostEqual(value[LIT_MEAN_FPS], 130 / 1.1)
        self.assertEqual(value[LIT_MIN_FRAME_MS], 2.5)
        self.assertEqual(value[LIT_MAX_FRAME_MS], 40)

    def test_long_positive_frames_are_retained_and_weighted_without_a_duration_cutoff(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_CSG_CAP]
        text, timing = self.reports(((30, .5), (30, .5), (2, .6), (1, .9), (900, .6)))
        value = RendererWallFrameEvidence.summarize_wall_frames(text, timing, workload, [2, 4])
        self.assertEqual(value[LIT_RETAINED_REPORT_RANGE], [2, 4])
        self.assertEqual(value[LIT_FRAMES], 3)
        self.assertEqual(value[LIT_REPORTS], 2)
        self.assertAlmostEqual(value[LIT_SECONDS], 1.5)
        self.assertAlmostEqual(value[LIT_MEAN_FRAME_MS], 500)
        self.assertAlmostEqual(value[LIT_MEAN_FPS], 2)
        self.assertAlmostEqual(value[LIT_MIN_FRAME_MS], 150)
        self.assertAlmostEqual(value[LIT_MAX_FRAME_MS], 1800)

    def test_long_positive_maximum_does_not_discard_an_otherwise_consistent_interval(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (100, .5)))
        value = RendererWallFrameEvidence.summarize_wall_frames(text.replace("max=10", "max=251", 1), timing, workload, [2, 4])
        self.assertEqual(value[LIT_FRAMES], 200)
        self.assertAlmostEqual(value[LIT_MEAN_FRAME_MS], 5)
        self.assertEqual(value[LIT_MAX_FRAME_MS], 251)

    def test_missing_malformed_nonfinite_and_inconsistent_reports_are_refused(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (100, .5)))
        for changed in ("\n".join(text.splitlines()[:-1]), text.replace("fps avg=200", "fps avg=nan", 1), text.replace("frame_ms avg=5", "frame_ms avg=inf", 1), text.replace("frames=100", "frames=0", 1),
            text.replace("fps avg=200", "fps avg=100", 1), text.replace(" min=2.5", "", 1), text.replace("max=10", "max=4", 1), text.replace("max=10", "max=0", 1), text.replace("min=2.5", "min=-2.5", 1), text.replace("seconds=0.5", "seconds=0", 1)):
            with self.subTest(text=changed), self.assertRaises(benchmark.SmokeFailure):
                RendererWallFrameEvidence.summarize_wall_frames(changed, timing, workload, [2, 4])

    def test_gpu_header_rounding_is_accepted_but_shifted_windows_are_refused(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .500049), (100, .5)))
        RendererWallFrameEvidence.summarize_wall_frames(text, timing, workload, [2, 4])
        for changed in (timing.replace("100 frames", "101 frames", 1), timing.replace("100 frames / 0.5000", "100 frames / 0.5002", 1)):
            with self.subTest(timing=changed), self.assertRaisesRegex(benchmark.SmokeFailure, "windows do not match"):
                RendererWallFrameEvidence.summarize_wall_frames(text, changed, workload, [2, 4])

    def test_fixed_simulation_delta_workloads_explicitly_omit_wall_frame_evidence(self):
        for name in (LIT_TRANSPARENT_MULTI, LIT_SHADOW_ZERO_EXTENT, LIT_CAUSTIC_POPULATED):
            value = RendererWallFrameEvidence.summarize_wall_frames("", "", RendererWorkloadRegistry.workloads()[name], [2, 8])
            with self.subTest(workload=name):
                self.assertFalse(value[LIT_ELIGIBLE])
                self.assertIn("fixed simulation delta", value["reason"])
        workload, orders, trials = trial_matrix()
        for trial in trials:
            trial[LIT_WALL_FRAME] = {LIT_ELIGIBLE: True, LIT_MEAN_FRAME_MS: 1, LIT_MEAN_FPS: 1000}
        self.assertFalse(RendererTrialComparison.compare_trials(trials, orders, workload)[LIT_WALL_FRAME][LIT_ELIGIBLE])

    def test_ten_positive_frame_reports_in_one_trial_cannot_resolve_a_change(self):
        workload, orders, trials = trial_matrix()
        self.assertEqual(RendererTrialComparison.compare_trials(trials, orders, workload)[LIT_STATUS], LIT_RESOLVED_GPU_TIME_REDUCTION)
        trials[-1][benchmark.LIT_POSITIVE_FRAME_REPORTS] = 10
        self.assertEqual(RendererTrialComparison.compare_trials(trials, orders, workload)[LIT_STATUS], "inconclusive_interval_count")

    def test_wall_frame_comparison_uses_complete_paired_blocks_and_correct_units(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        _, orders, trials = trial_matrix()
        for trial in trials:
            trial[LIT_SCOPES] = scopes(workload)
            frame_ms = 5 if trial[LIT_ARM] == LIT_BASELINE else 4
            trial[LIT_WALL_FRAME] = {LIT_ELIGIBLE: True, LIT_MEAN_FRAME_MS: frame_ms, LIT_MEAN_FPS: 1000 / frame_ms}
        value = RendererTrialComparison.compare_trials(trials, orders, workload)[LIT_WALL_FRAME]
        self.assertTrue(value[LIT_ELIGIBLE])
        self.assertEqual(value[LIT_FRAME_MS_DELTA]["blocks"], 8)
        self.assertEqual(value[LIT_FRAME_MS_DELTA]["ci95_mean_ms"], [-1, -1])
        self.assertEqual(value["fps_delta"]["ci95_mean_fps"], [50, 50])
        self.assertEqual(value["means"][LIT_CANDIDATE], {"frame_ms": 4, "fps": 250})
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            RendererTrialComparison.compare_trials(trials[:-1], orders, workload)
        for omitted in (1, len(trials)):
            invalid = copy.deepcopy(trials)
            for trial in invalid[:omitted]:
                del trial[LIT_WALL_FRAME]
            with self.subTest(omitted=omitted), self.assertRaisesRegex(benchmark.SmokeFailure, "every planned trial.*qualified reports"):
                RendererTrialComparison.compare_trials(invalid, orders, workload)


if __name__ == "__main__":
    unittest.main()
