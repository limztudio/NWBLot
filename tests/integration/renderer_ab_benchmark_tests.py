#!/usr/bin/env python3
"""Pure CPU tests for frozen-arm renderer A/B acquisition contracts."""


import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark
from smoke_volume_identity import volume_segment_filename
from renderer_ab_benchmark_test_support import scopes, trial_matrix


LIT_A = "a"
LIT_B = "b"
LIT_ELIGIBLE = "eligible"
LIT_RETAINED_REPORT_RANGE = "retained_report_range"
LIT_FRAMES = "frames"
LIT_SECONDS = "seconds"
LIT_MEAN_FRAME_MS = "mean_frame_ms"
LIT_MEAN_FPS = "mean_fps"
LIT_MIN_FRAME_MS = "min_frame_ms"
LIT_MAX_FRAME_MS = "max_frame_ms"
LIT_REFLECTION_OPTICAL_CSG_CAP = "reflection-optical-csg-cap"
LIT_WALL_FRAME = "wall_frame"
LIT_FRAME_MS_DELTA = "frame_ms_delta"
LIT_R = "r"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_MEAN_MS = "mean_ms"
LIT_SOURCE = "source"
LIT_REVISION = "revision"
LIT_SOURCE_CPP = "source.cpp"
LIT_UTF_8 = "utf-8"
LIT_SOURCE_JSON = "source.json"
LIT_FILES = "files"
LIT_RUNTIME = "runtime"
LIT_RES = "res"
LIT_AUTHORED_VOL = "authored.vol"
LIT_TRANSPARENT_MULTI = "transparent-multi"
LIT_AVBOIT_REFRACTION_ACCUMULATION = "avboit-refraction-accumulation"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_ARM = "arm"
LIT_BLOCK = "block"
LIT_POSITION = "position"
LIT_SCOPES = "scopes"
LIT_SAMPLE_RATIO = "sample ratio"
LIT_STATUS = "status"
LIT_RESOLVED_GPU_TIME_REDUCTION = "resolved_gpu_time_reduction"
LIT_CONTROL_DRIFT = "control_drift"
LIT_CONTROL_UNCERTAIN = "control_uncertain"
LIT_EVERY_PLANNED_TRIAL = "every planned trial"
LIT_SHADOW_ZERO_EXTENT = "shadow-zero-extent"
LIT_RUNTIME_PIPELINE_CACHE = "runtime_pipeline_cache"
LIT_REFLECTION_OPTICAL_CLEAR = "reflection-optical-clear"
LIT_CAUSTIC_POPULATED = "caustic-populated"
LIT_MAIN = "__main__"


def source_manifest(directory, text=LIT_SOURCE, revision=LIT_REVISION):
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / LIT_SOURCE_CPP
    source.write_text(text, encoding=LIT_UTF_8)
    manifest = directory / LIT_SOURCE_JSON
    manifest.write_text(json.dumps({LIT_REVISION: revision, LIT_FILES: {source.name: hashlib.sha256(source.read_bytes()).hexdigest()}}), encoding=LIT_UTF_8)
    return manifest


def make_arm(directory, name):
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / "renderer"
    executable.write_bytes(b"binary")
    runtime = directory / LIT_RUNTIME
    (runtime / LIT_RES).mkdir(parents=True)
    (runtime / LIT_RES / LIT_AUTHORED_VOL).write_bytes(b"authored asset")
    return benchmark.Arm(name, executable, runtime, source_manifest(directory / LIT_SOURCE))


class CoverageTests(unittest.TestCase):
    def test_actual_gpu_counts_ignore_cpu_frame_and_displayed_average_fields(self):
        text = ("=== interval: 99999 frames / 0.5s ===\n" "  render.frame: window_avg_ms=123 window_min_ms=123 window_max_ms=123 published_windows=1 total_ms=90 gpu_samples=10 sample_avg_ms=9\n" "=== interval: 1 frames / 0.5s ===\n"
            "  render.frame: window_avg_ms=456 window_min_ms=456 window_max_ms=456 published_windows=1 total_ms=100 gpu_samples=100 sample_avg_ms=1\n")
        result = benchmark.summarize_intervals(benchmark.parse_intervals(text, finalized=True))
        self.assertEqual(result[benchmark.FRAME][LIT_GPU_SAMPLES], 110)
        self.assertAlmostEqual(result[benchmark.FRAME][LIT_MEAN_MS], 190 / 110)

    def test_partial_live_tail_is_not_counted(self):
        text = ("=== interval: 1 frames / 0.5s ===\n" "  render.frame: window_avg_ms=4 window_min_ms=4 window_max_ms=4 published_windows=1 total_ms=4 gpu_samples=1 sample_avg_ms=4\n" "=== interval: 1 frames / 0.5s ===\n  render.frame: total_ms=")
        self.assertEqual(len(benchmark.parse_intervals(text)), 1)

    def test_all_avboit_and_control_ranges_are_required(self):
        for workload_name in (LIT_TRANSPARENT_MULTI, LIT_AVBOIT_REFRACTION_ACCUMULATION):
            workload = benchmark.workloads()[workload_name]
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
            workload = benchmark.workloads()[workload_name]
            for frames, tolerance in ((100, 2), (1000, 20)):
                for scope in (workload.secondary_scope, benchmark.CONTROLS[0]):
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
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (30, .6), (900, .6)))
        value = benchmark.summarize_wall_frames(text.replace("\n", "\r\n"), timing, workload, [2, 4])
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
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CSG_CAP]
        text, timing = self.reports(((30, .5), (30, .5), (2, .6), (1, .9), (900, .6)))
        value = benchmark.summarize_wall_frames(text, timing, workload, [2, 4])
        self.assertEqual(value[LIT_RETAINED_REPORT_RANGE], [2, 4])
        self.assertEqual(value[LIT_FRAMES], 3)
        self.assertEqual(value[LIT_REPORTS], 2)
        self.assertAlmostEqual(value[LIT_SECONDS], 1.5)
        self.assertAlmostEqual(value[LIT_MEAN_FRAME_MS], 500)
        self.assertAlmostEqual(value[LIT_MEAN_FPS], 2)
        self.assertAlmostEqual(value[LIT_MIN_FRAME_MS], 150)
        self.assertAlmostEqual(value[LIT_MAX_FRAME_MS], 1800)

    def test_long_positive_maximum_does_not_discard_an_otherwise_consistent_interval(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (100, .5)))
        value = benchmark.summarize_wall_frames(text.replace("max=10", "max=251", 1), timing, workload, [2, 4])
        self.assertEqual(value[LIT_FRAMES], 200)
        self.assertAlmostEqual(value[LIT_MEAN_FRAME_MS], 5)
        self.assertEqual(value[LIT_MAX_FRAME_MS], 251)

    def test_missing_malformed_nonfinite_and_inconsistent_reports_are_refused(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (100, .5)))
        for changed in ("\n".join(text.splitlines()[:-1]), text.replace("fps avg=200", "fps avg=nan", 1), text.replace("frame_ms avg=5", "frame_ms avg=inf", 1), text.replace("frames=100", "frames=0", 1),
            text.replace("fps avg=200", "fps avg=100", 1), text.replace(" min=2.5", "", 1), text.replace("max=10", "max=4", 1), text.replace("max=10", "max=0", 1), text.replace("min=2.5", "min=-2.5", 1), text.replace("seconds=0.5", "seconds=0", 1)):
            with self.subTest(text=changed), self.assertRaises(benchmark.SmokeFailure):
                benchmark.summarize_wall_frames(changed, timing, workload, [2, 4])

    def test_gpu_header_rounding_is_accepted_but_shifted_windows_are_refused(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .500049), (100, .5)))
        benchmark.summarize_wall_frames(text, timing, workload, [2, 4])
        for changed in (timing.replace("100 frames", "101 frames", 1), timing.replace("100 frames / 0.5000", "100 frames / 0.5002", 1)):
            with self.subTest(timing=changed), self.assertRaisesRegex(benchmark.SmokeFailure, "windows do not match"):
                benchmark.summarize_wall_frames(text, changed, workload, [2, 4])

    def test_fixed_simulation_delta_workloads_explicitly_omit_wall_frame_evidence(self):
        for name in (LIT_TRANSPARENT_MULTI, LIT_SHADOW_ZERO_EXTENT, LIT_CAUSTIC_POPULATED):
            value = benchmark.summarize_wall_frames("", "", benchmark.workloads()[name], [2, 8])
            with self.subTest(workload=name):
                self.assertFalse(value[LIT_ELIGIBLE])
                self.assertIn("fixed simulation delta", value["reason"])
        workload, orders, trials = trial_matrix()
        for trial in trials:
            trial[LIT_WALL_FRAME] = {LIT_ELIGIBLE: True, LIT_MEAN_FRAME_MS: 1, LIT_MEAN_FPS: 1000}
        self.assertFalse(benchmark.compare_trials(trials, orders, workload)[LIT_WALL_FRAME][LIT_ELIGIBLE])

    def test_ten_positive_frame_reports_in_one_trial_cannot_resolve_a_change(self):
        workload, orders, trials = trial_matrix()
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)[LIT_STATUS], LIT_RESOLVED_GPU_TIME_REDUCTION)
        trials[-1][benchmark.LIT_POSITIVE_FRAME_REPORTS] = 10
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)[LIT_STATUS], "inconclusive_interval_count")

    def test_wall_frame_comparison_uses_complete_paired_blocks_and_correct_units(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        _, orders, trials = trial_matrix()
        for trial in trials:
            trial[LIT_SCOPES] = scopes(workload)
            frame_ms = 5 if trial[LIT_ARM] == LIT_BASELINE else 4
            trial[LIT_WALL_FRAME] = {LIT_ELIGIBLE: True, LIT_MEAN_FRAME_MS: frame_ms, LIT_MEAN_FPS: 1000 / frame_ms}
        value = benchmark.compare_trials(trials, orders, workload)[LIT_WALL_FRAME]
        self.assertTrue(value[LIT_ELIGIBLE])
        self.assertEqual(value[LIT_FRAME_MS_DELTA]["blocks"], 8)
        self.assertEqual(value[LIT_FRAME_MS_DELTA]["ci95_mean_ms"], [-1, -1])
        self.assertEqual(value["fps_delta"]["ci95_mean_fps"], [50, 50])
        self.assertEqual(value["means"][LIT_CANDIDATE], {"frame_ms": 4, "fps": 250})
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            benchmark.compare_trials(trials[:-1], orders, workload)
        for omitted in (1, len(trials)):
            invalid = copy.deepcopy(trials)
            for trial in invalid[:omitted]:
                del trial[LIT_WALL_FRAME]
            with self.subTest(omitted=omitted), self.assertRaisesRegex(benchmark.SmokeFailure, "every planned trial.*qualified reports"):
                benchmark.compare_trials(invalid, orders, workload)


class FrozenIdentityTests(unittest.TestCase):
    def test_source_content_and_manifest_are_both_frozen(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = source_manifest(root)
            benchmark.source_identity(manifest)
            (root / LIT_SOURCE_CPP).write_text("changed", encoding=LIT_UTF_8)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "source bytes"):
                benchmark.source_identity(manifest)
            for document in ([], {LIT_REVISION: LIT_R, LIT_FILES: {}}, {LIT_REVISION: LIT_R, LIT_FILES: {"x": "invalid"}}):
                manifest.write_text(json.dumps(document), encoding=LIT_UTF_8)
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.source_identity(manifest)

    def test_only_exact_contiguous_pipeline_cache_segments_can_mutate(self):
        with tempfile.TemporaryDirectory() as temporary:
            arm = make_arm(Path(temporary), LIT_BASELINE)
            before = benchmark.freeze_arm(arm)
            cache = arm.runtime / LIT_RES / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)
            cache.write_bytes(b"runtime cache")
            self.assertEqual(before, benchmark.freeze_arm(arm))
            cache.write_bytes(b"updated runtime cache")
            self.assertEqual(before, benchmark.freeze_arm(arm))
            unrelated = arm.runtime / LIT_RES / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 2)
            unrelated.write_bytes(b"gap means authored/unknown")
            self.assertNotEqual(before, benchmark.freeze_arm(arm))

    def test_binary_authored_source_and_shared_tool_changes_abort(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            arm = make_arm(root / LIT_ARM, LIT_BASELINE)
            other = make_arm(root / "other", LIT_CANDIDATE)
            tool = root / "tool.py"
            tool.write_bytes(b"tool")
            identities = {item.name: benchmark.freeze_arm(item) for item in (arm, other)}
            shared = {str(tool): benchmark.file_identity(tool)}
            benchmark.verify_frozen((arm, other), identities, shared)
            for path in (arm.executable, arm.runtime / LIT_RES / LIT_AUTHORED_VOL, tool):
                original = path.read_bytes()
                path.write_bytes(b"changed")
                with self.subTest(path=path), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.verify_frozen((arm, other), identities, shared)
                path.write_bytes(original)
            dependency = arm.executable.parent / "runtime.dll"
            dependency.write_bytes(b"new loader dependency")
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.verify_frozen((arm, other), identities, shared)

    def test_shared_or_hard_linked_caches_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = make_arm(root / LIT_A, LIT_BASELINE)
            second = make_arm(root / LIT_B, LIT_CANDIDATE)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "distinct"):
                benchmark.validate_arm_separation((first, first))
            benchmark.validate_arm_separation((first, second))
            filename = volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)
            first_cache = first.runtime / LIT_RES / filename
            second_cache = second.runtime / LIT_RES / filename
            first_cache.write_bytes(b"shared cache")
            second_cache.hardlink_to(first_cache)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "alias"):
                benchmark.validate_arm_separation((first, second))

    def test_missing_windows_crash_helper_fails_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "smoke.exe"
            executable.write_bytes(b"exe")
            with self.assertRaisesRegex(benchmark.SmokeFailure, "crash_handler"):
                benchmark.binary_identity(executable)


class PairedInferenceTests(unittest.TestCase):

    def test_missing_duplicate_and_wrong_order_trials_are_not_silently_dropped(self):
        workload, orders, trials = trial_matrix()
        changed = copy.deepcopy(trials)
        changed[0][LIT_POSITION] = 1
        for invalid in (trials[:-1], trials + [trials[0]], changed):
            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
                benchmark.compare_trials(invalid, orders, workload)

    def test_small_effect_and_uncertain_controls_cannot_claim_speedup(self):
        workload, orders, trials = trial_matrix(-.01)
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)[LIT_STATUS], "unresolved")
        workload, orders, trials = trial_matrix(-.5)
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][benchmark.CONTROLS[0]][LIT_MEAN_MS] += .2 if trial[LIT_BLOCK] % 2 else -.2
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result[LIT_STATUS], LIT_CONTROL_UNCERTAIN)
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][benchmark.CONTROLS[0]][LIT_MEAN_MS] = .3
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)[LIT_STATUS], LIT_CONTROL_DRIFT)


if __name__ == LIT_MAIN:
    unittest.main()
