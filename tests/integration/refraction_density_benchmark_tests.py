#!/usr/bin/env python3
"""Reject malformed density fixtures, incomplete cost accounting, and contaminated wall timing."""

import contextlib
import io
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import refraction_density_benchmark as benchmark  # noqa: E402


class RefractionDensityBenchmarkTests(unittest.TestCase):
    def fixture(self, percent=1, bounds=(432, 324, 528, 396), pixels=6912, tiles=120, mode="visible"):
        return (f"RefractionDensity: extent 960x720 requested_percent {percent} admitted_pixels {pixels} total_pixels 691200 active_tiles {tiles} total_tiles 10800 glass_instances 1 capture_mode {mode}\n"
            + "RefractionDensity: pixel_bounds " + " ".join(map(str, bounds)) + "\n"
            + "RefractionDensity: tracing requested hardware ray_query_supported 1 refraction_enabled 1\n")

    def arguments(self):
        result = []
        for arm in ("baseline", "candidate"):
            for field in ("executable", "runtime", "logserver", "source-manifest"):
                result += [f"--{arm}-{field}", f"unused-{arm}-{field}"]
        return result + ["--output-directory", "unused-output"]

    def trial(self, arm, block):
        scopes = {scope: {"total_ms": 10, "gpu_samples": 100, "mean_ms": .1, "reports": 12} for scope in benchmark.OBSERVED}
        if arm == "baseline":
            scopes.pop(benchmark.CLASSIFY)
            scopes.pop(benchmark.CLEAR_ARGUMENTS)
        return {"arm": arm, "block": block, "scopes": scopes, "refraction_work_per_frame_ms": .1, "wall_frame": {"eligible": False, "reason": "synthetic test"}}

    def test_zero_density_requires_occlusion_without_removing_glass(self):
        cell = benchmark.Cell("960x720", 0, "hardware")
        text = self.fixture(0, (0, 0, 0, 0), 0, 0, "occluded")
        benchmark.validate_fixture(text, cell)
        for bad in (text.replace("glass_instances 1", "glass_instances 0"), text.replace("occluded", "visible"), text.replace("admitted_pixels 0", "admitted_pixels 1")):
            with self.subTest(log=bad), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_fixture(bad, cell)

    def test_requested_density_cannot_hide_incorrect_coverage_or_tile_bounds(self):
        cell = benchmark.Cell("960x720", 1, "hardware")
        text = self.fixture()
        benchmark.validate_fixture(text, cell)
        for bad in (text.replace("active_tiles 120", "active_tiles 119"), text.replace("admitted_pixels 6912", "admitted_pixels 6911"),
            self.fixture(1, (328, 246, 632, 474), 69312, 1140), text + text):
            with self.subTest(log=bad), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_fixture(bad, cell)

    def test_full_density_requires_framebuffer_tail_tiles(self):
        cell = benchmark.Cell("960x720", 100, "hardware")
        text = self.fixture(100, (0, 0, 960, 720), 691200, 10800)
        benchmark.validate_fixture(text, cell)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_fixture(self.fixture(100, (1, 1, 959, 719), 958 * 718, 10800), cell)

    def test_hardware_route_cannot_pass_without_capability(self):
        text = self.fixture().replace("ray_query_supported 1", "ray_query_supported 0")
        with self.assertRaisesRegex(benchmark.SmokeFailure, "hardware capability"):
            benchmark.validate_fixture(text, benchmark.Cell("960x720", 1, "hardware"))

    def test_argument_clear_regression_is_not_hidden_by_resolve_saving(self):
        trials = [self.trial(arm, block) for block in range(2) for arm in ("baseline", "candidate")]
        for trial in trials:
            if trial["arm"] == "candidate":
                trial["scopes"][benchmark.RESOLVE]["total_ms"] = 1
                trial["scopes"][benchmark.RESOLVE]["mean_ms"] = .01
                trial["scopes"][benchmark.CLEAR_ARGUMENTS]["total_ms"] = 100
        comparison = benchmark.comparison(trials, 2)
        aggregate = comparison["metrics"]["argument_clear_plus_classification_plus_resolve_plus_compositor_per_frame_ms"]
        self.assertGreater(aggregate["candidate_minus_baseline"], 0)
        self.assertLess(comparison["metrics"][benchmark.RESOLVE]["candidate_minus_baseline"], 0)

    def test_missing_clear_scope_or_tile_work_in_reference_fails_coverage(self):
        cell = benchmark.Cell("960x720", 0, "hardware")
        scopes = self.trial("candidate", 0)["scopes"]
        for value in scopes.values():
            value["gpu_samples"] = 120
        missing = {name: value for name, value in scopes.items() if name != benchmark.CLEAR_ARGUMENTS}
        with self.assertRaisesRegex(benchmark.SmokeFailure, "missing"):
            benchmark.ab.validate_coverage(missing, benchmark.workload_for(SimpleNamespace(name="candidate"), cell), 12, 100)
        with self.assertRaisesRegex(benchmark.SmokeFailure, "inactive"):
            benchmark.ab.validate_coverage(scopes, benchmark.workload_for(SimpleNamespace(name="baseline"), cell), 12, 100)

    def test_tiled_baseline_requires_each_tile_scope_on_both_arms(self):
        cell = benchmark.Cell("960x720", 0, "screen")
        args = benchmark.parse_args(self.arguments() + ["--baseline-scheduling", "tiled"])
        scopes = self.trial("candidate", 0)["scopes"]
        for arm in ("baseline", "candidate"):
            workload = benchmark.workload_for(SimpleNamespace(name=arm, scheduling=args.baseline_scheduling), cell)
            benchmark.ab.validate_coverage(scopes, workload, 12, 100)
            for missing in (benchmark.CLASSIFY, benchmark.CLEAR_ARGUMENTS):
                incomplete = {name: value for name, value in scopes.items() if name != missing}
                with self.subTest(arm=arm, missing=missing), self.assertRaisesRegex(benchmark.SmokeFailure, "missing"):
                    benchmark.ab.validate_coverage(incomplete, workload, 12, 100)
        full = benchmark.workload_for(SimpleNamespace(name="baseline", scheduling="full-frame"), cell)
        for unexpected in (benchmark.CLASSIFY, benchmark.CLEAR_ARGUMENTS):
            contaminated = self.trial("baseline", 0)["scopes"]
            contaminated[unexpected] = scopes[unexpected]
            with self.subTest(unexpected=unexpected), self.assertRaisesRegex(benchmark.SmokeFailure, "inactive"):
                benchmark.ab.validate_coverage(contaminated, full, 12, 100)

    def test_tiled_comparison_counts_baseline_cost_and_rejects_missing_or_mixed_strategy(self):
        trials = []
        for block in range(2):
            for arm in ("baseline", "candidate"):
                trial = self.trial("candidate", block)
                trial.update(arm=arm, scheduling="tiled", baseline_scheduling="tiled")
                if arm == "baseline":
                    trial["scopes"][benchmark.CLEAR_ARGUMENTS]["total_ms"] = 100
                trials.append(trial)
        result = benchmark.comparison(trials, 2, baseline_scheduling="tiled")
        aggregate = result["metrics"]["argument_clear_plus_classification_plus_resolve_plus_compositor_per_frame_ms"]
        self.assertAlmostEqual(aggregate["baseline"], 1.3)
        self.assertAlmostEqual(aggregate["candidate"], .4)
        self.assertEqual(result["baseline_scheduling"], "tiled")
        self.assertIn("both arms", result["reference_contract"])
        with self.assertRaisesRegex(benchmark.SmokeFailure, "mismatched scheduling"):
            benchmark.comparison(trials, 2)
        for missing in (benchmark.CLASSIFY, benchmark.CLEAR_ARGUMENTS):
            removed = trials[0]["scopes"].pop(missing)
            with self.subTest(missing=missing), self.assertRaisesRegex(benchmark.SmokeFailure, "lacks required"):
                benchmark.comparison(trials, 2, baseline_scheduling="tiled")
            trials[0]["scopes"][missing] = removed
        trials[0]["scheduling"] = "full-frame"
        with self.assertRaisesRegex(benchmark.SmokeFailure, "mismatched scheduling"):
            benchmark.comparison(trials, 2, baseline_scheduling="tiled")

    def test_stall_rejects_wall_timing_without_erasing_evidence(self):
        raw = {"eligible": True, "source": "fixture", "policy": "callback delta", "max_frame_ms": 44257.6, "mean_frame_ms": 29, "mean_fps": 34}
        with mock.patch.object(benchmark.ab, "summarize_wall_frames", return_value=raw.copy()):
            result = benchmark.wall_frames("", "", benchmark.workload_for(SimpleNamespace(name="candidate"), benchmark.Cell("960x720", 1, "screen")), 2, 14, 1000)
        self.assertFalse(result["eligible"])
        self.assertEqual(result["max_frame_ms"], raw["max_frame_ms"])
        self.assertIn("raw GPU and wall reports retained", result["reason"])

    def test_incomplete_or_duplicate_pairs_cannot_form_a_comparison(self):
        trials = [self.trial(arm, block) for block in range(2) for arm in ("baseline", "candidate")]
        for broken in (trials[:-1], trials + trials[:1]):
            with self.assertRaisesRegex(benchmark.SmokeFailure, "duplicate or incomplete"):
                benchmark.comparison(broken, 2)

    def test_mismatched_frozen_fixture_instrumentation_is_rejected(self):
        files = lambda arm, digest: {"source": {"files": {f"C:/{arm}/source/{path}": digest for path in benchmark.FIXTURE_SOURCE_FILES}}}
        benchmark.require_same_fixture_sources({"baseline": files("baseline", "same"), "candidate": files("candidate", "same")})
        with self.assertRaisesRegex(benchmark.SmokeFailure, "different refraction fixture"):
            benchmark.require_same_fixture_sources({"baseline": files("baseline", "before"), "candidate": files("candidate", "after")})

    def test_missing_timing_instrumentation_cannot_be_hidden_by_analysis_files(self):
        first = {"source": {"files": {f"C:/baseline/source/{path}": "same" for path in benchmark.FIXTURE_SOURCE_FILES}}}
        second = {"source": {"files": {"C:/candidate/source/tests/smoke/refraction_project.cpp": "same", "C:/candidate/source/tests/smoke/refraction_density_benchmark.py": "unrelated"}}}
        with self.assertRaisesRegex(benchmark.SmokeFailure, "avboit_timing_render_pass"):
            benchmark.require_same_fixture_sources({"baseline": first, "candidate": second})
        second["source"]["files"]["C:/candidate/source/tests/smoke/avboit_timing_render_pass.cpp"] = "same"
        benchmark.require_same_fixture_sources({"baseline": first, "candidate": second})

    def test_invalid_sampling_or_duplicate_matrix_selection_is_rejected(self):
        for extra in (("--blocks", "3"), ("--sample-intervals", "11"), ("--timeout", "nan"), ("--stall-threshold-ms", "inf"), ("--coverage", "1", "--coverage", "1"), ("--baseline-scheduling", "automatic")):
            with self.subTest(arguments=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(self.arguments() + list(extra))

    def test_unsupported_host_allocation_extent_requires_explicit_1080p_selection(self):
        # The qualified adapter's existing 1080p CSG target exceeds its allocation
        # cap; a default matrix must not silently request that unsupported case.
        selected = benchmark.parse_args(self.arguments()).extent
        self.assertEqual(selected, ["960x720", "1600x900"])
        self.assertEqual(benchmark.parse_args(self.arguments() + ["--extent", "1920x1080"]).extent, ["1920x1080"])

    def test_initial_workarea_clamp_requires_matching_final_resize(self):
        cell = benchmark.Cell("1920x1080", 1, "screen")
        text = ("Frame Win32 client size clamped from 1920x1080 to 1904x1011 for work area 1920x1050\n"
            "RendererSystem: deferred rendering targets ready (1904x1011, format)\n"
            "GraphicsRuntime: Back buffer resized to 1920x1080\n"
            "RendererSystem: deferred rendering targets ready (1920x1080, format)\n")
        benchmark.validate_extent_history(text, cell)
        for corrupted in (text.replace("Frame Win32 client size clamped", "Unrelated window size"), text.replace("Back buffer resized to 1920x1080", "Back buffer resized to 1904x1011"),
            text + "RendererSystem: deferred rendering targets ready (1280x720, format)\n", text.replace("from 1920x1080", "from 1600x900")):
            with self.subTest(log=corrupted), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_extent_history(corrupted, cell)

    def test_workarea_clamp_resizes_real_client_before_timing_admission(self):
        cell = benchmark.Cell("1920x1080", 1, "screen")
        text = ("Frame Win32 client size clamped from 1920x1080 to 1904x1011 for work area 1920x1050\n"
            "RendererSystem: deferred rendering targets ready (1904x1011, format)\n"
            "GraphicsRuntime: Back buffer resized to 1920x1080\n"
            "RendererSystem: deferred rendering targets ready (1920x1080, format)\n")
        backend = mock.Mock()
        backend.client_size.side_effect = ((1904, 1011), (1920, 1080))
        with mock.patch.object(benchmark, "ensure_process_running"):
            result = benchmark.prepare_target_extent(backend, 123, object(), cell, lambda: text, 1)
        backend.resize_client.assert_called_once_with(123, 1920, 1080, allow_outside_work_area=True)
        self.assertTrue(result["resize_requested"])
        self.assertTrue(result["allow_outside_work_area"])
        self.assertEqual(result["initial_client_extent"], [1904, 1011])

    def test_old_extent_and_unsealed_boundary_reports_cannot_count_as_target_warmup(self):
        sample = lambda frames: {benchmark.FRAME: benchmark.timing.ScopeSample(1, frames)}
        reports = [sample(10000), sample(10000), sample(10000), sample(20), sample(30), sample(50), sample(500)]
        # All three headers existed at resize confirmation, including the last
        # report which was still unsealed and can contain mixed-extent samples.
        start, retained = benchmark.retain_target_reports(reports, 3, 2, 100)
        self.assertEqual(start, 6)
        self.assertEqual(retained, reports[6:])

    def test_partially_written_startup_header_is_discarded_before_target_warmup(self):
        text = "=== interval: 100 frames / 0.5000s ===\n=== interval: 20 frames / "
        self.assertEqual(benchmark.startup_report_boundary(text), 2)
        self.assertEqual(benchmark.startup_report_boundary(""), 1)

    def test_elapsed_warmup_excludes_old_extent_and_requires_the_complete_target_interval(self):
        sample = {benchmark.FRAME: benchmark.timing.ScopeSample(1, 100)}
        reports = [sample] * 10
        durations = [100., 100., .5, .5, .5, .5, .5, .5, .5, .5]
        start, retained = benchmark.retain_target_reports(reports, 2, 2, 100, 3, durations)
        self.assertEqual(start, 8)
        self.assertEqual(retained, reports[8:])
        start, retained = benchmark.retain_target_reports(reports[:7], 2, 2, 100, 3, durations[:7])
        self.assertEqual(start, 7)
        self.assertEqual(retained, [])

    def test_elapsed_warmup_does_not_override_completed_gpu_frame_minimum(self):
        reports = [{benchmark.FRAME: benchmark.timing.ScopeSample(1, 10)}] * 13
        start, retained = benchmark.retain_target_reports(reports, 2, 2, 100, 1, [.5] * len(reports))
        self.assertEqual(start, 12)
        self.assertEqual(retained, reports[12:])

    def test_missing_or_invalid_publication_durations_cannot_establish_elapsed_warmup(self):
        reports = [{benchmark.FRAME: benchmark.timing.ScopeSample(1, 100)}] * 4
        for durations in (None, [.5] * 3, [.5, .5, float("nan"), .5], [.5, 0., .5, .5]):
            with self.subTest(durations=durations), self.assertRaisesRegex(benchmark.SmokeFailure, "positive durations"):
                benchmark.retain_target_reports(reports, 0, 2, 100, 1, durations)


if __name__ == "__main__":
    unittest.main()
