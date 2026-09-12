#!/usr/bin/env python3
"""CPU-only tests of reflection GPU benchmark analysis and launch contracts."""
import collections
import contextlib
import copy
import io
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import reflection_benchmark as benchmark
from name_symbols import known_name_symbols
from smoke_volume_identity import volume_segment_filename


def report(rows, cpu_frames=500):
    text = f"=== interval: {cpu_frames} frames / 0.500000s ===\n"
    for scope, total, samples in rows:
        text += (f"  {scope}: avg=999 min=888 max=1000 samples=2 total_ms={total} "
            f"gpu_samples={samples} sample_avg_ms=777\n")
    return text


def synthetic_scopes(variant, frame_ms=5.0, control_ms=.5, gpu_frames=200, mip_count=10):
    result = {}
    for scope in benchmark.required_scopes(variant):
        mean = frame_ms if scope == benchmark.FRAME else control_ms if scope in benchmark.OBSERVED_CONTROLS else .1
        samples = gpu_frames * (mip_count if scope == benchmark.DEPTH else 1)
        result[scope] = {"total_ms": mean * samples, "gpu_samples": samples, "reports": 16, "mean_ms": mean}
    return result


def matched_trials(frame_delta=-.5, control_delta=0.0):
    variants = benchmark.variants_for("floor")
    trials = []
    for block in range(6):
        for variant in variants:
            delta = frame_delta if variant.name == "hybrid" else 0.0
            controls = .5 + (control_delta if variant.name == "hybrid" else 0.0)
            scopes = synthetic_scopes(variant, 5.0 + block * .03 + delta, controls)
            trials.append({"block": block, "variant": benchmark.asdict(variant), "scopes": scopes,
                "kernel_work_ms": benchmark.kernel_work_ms(scopes, variant, 10)})
    return variants, trials


class TimingNormalizationTests(unittest.TestCase):
    def test_weights_actual_gpu_samples_and_ignores_published_averages(self):
        text = report([(benchmark.FRAME, 90, 10)], 2000) + report([(benchmark.FRAME, 100, 100)], 1)
        intervals = benchmark.parse_intervals(text, finalized=True)
        summary = benchmark.summarize_intervals(intervals)[benchmark.FRAME]
        self.assertAlmostEqual(summary["mean_ms"], 190 / 110)
        self.assertEqual(summary["gpu_samples"], 110)
        self.assertNotAlmostEqual(summary["mean_ms"], 5)

    def test_live_partial_tail_is_not_used_or_parsed(self):
        text = report([(benchmark.FRAME, 2, 1)]) + "=== interval: 1 frames / 0.5s ===\n  render.frame: total_ms="
        self.assertEqual(len(benchmark.parse_intervals(text)), 1)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.parse_intervals(text + "\n=== interval: 1 frames / 0.5s ===\n")

    def test_crlf_and_matching_name_hash_decode(self):
        symbols = known_name_symbols([benchmark.FRAME])
        token = next(iter(symbols))
        parsed = benchmark.parse_intervals(report([(token, 2, 1)]).replace("\n", "\r\n"), symbols, finalized=True)
        self.assertIn(benchmark.FRAME, parsed[0])

    def test_legacy_only_nonfinite_empty_and_duplicate_samples_fail(self):
        bad_rows = [
            "  render.frame: avg=1 min=1 max=1 samples=2\n",
            "  render.frame: total_ms=NaN gpu_samples=3\n",
            "  render.frame: total_ms=-1 gpu_samples=3\n",
            "  render.frame: total_ms=1 gpu_samples=0\n",
            "  render.frame: total_ms=1 gpu_samples=1.5\n",
            "  render.frame: total_ms=1 gpu_samples=2 gpu_samples=3\n",
        ]
        for row in bad_rows:
            with self.subTest(row=row), self.assertRaises(benchmark.SmokeFailure):
                benchmark.parse_intervals("=== interval: 1 frames / 0.5s ===\n" + row, finalized=True)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.parse_intervals(report([(benchmark.FRAME, 1, 1), (benchmark.FRAME, 1, 1)]), finalized=True)

    def test_zero_duration_is_valid_for_a_sampled_kernel(self):
        parsed = benchmark.parse_intervals(report([(benchmark.CLASSIFY, 0, 5)]), finalized=True)
        self.assertEqual(benchmark.summarize_intervals(parsed)[benchmark.CLASSIFY]["mean_ms"], 0)

    def test_depth_is_per_mip_and_count_ratio_is_required(self):
        variant = benchmark.Variant("hybrid", "hybrid")
        scopes = synthetic_scopes(variant)
        benchmark.validate_coverage(scopes, variant, 16, 100, 10)
        self.assertAlmostEqual(benchmark.kernel_work_ms(scopes, variant, 10), 1.3)
        scopes[benchmark.DEPTH]["gpu_samples"] = 200
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_coverage(scopes, variant, 16, 100, 10)

    def test_missing_scopes_and_sparse_publications_fail(self):
        variant = benchmark.Variant("hardware", "hardware")
        scopes = synthetic_scopes(variant)
        for scope in (benchmark.HARDWARE, benchmark.FRAME, benchmark.CONTROLS[0]):
            missing = copy.deepcopy(scopes)
            del missing[scope]
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(missing, variant, 16, 100, 10)
        scopes[benchmark.CLASSIFY]["reports"] = 2
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_coverage(scopes, variant, 16, 100, 10)

    def test_single_sample_temporal_requires_zero_dispatches_and_preserves_other_coverage(self):
        variant = benchmark.Variant("hardware_temporal", "hardware", temporal=True)
        scopes = synthetic_scopes(variant)
        with self.assertRaisesRegex(benchmark.SmokeFailure, "unexpected native dispatch"):
            benchmark.validate_coverage(scopes, variant, 16, 100, 10, history_samples=1)
        del scopes[benchmark.TEMPORAL]
        benchmark.validate_coverage(scopes, variant, 16, 100, 10, history_samples=1)
        self.assertAlmostEqual(benchmark.kernel_work_ms(scopes, variant, 10, history_samples=1), .3)
        for scope in (benchmark.FRAME, benchmark.CLASSIFY, benchmark.HARDWARE, benchmark.CONTROLS[0]):
            missing = copy.deepcopy(scopes)
            del missing[scope]
            with self.subTest(scope=scope), self.assertRaisesRegex(benchmark.SmokeFailure, "missing GPU scopes"):
                benchmark.validate_coverage(missing, variant, 16, 100, 10, history_samples=1)

    def test_accumulating_history_still_requires_full_temporal_dispatch_coverage(self):
        variant = benchmark.Variant("hardware_temporal", "hardware", temporal=True)
        for history_samples in (2, 16, 256):
            with self.subTest(history_samples=history_samples):
                scopes = synthetic_scopes(variant)
                benchmark.validate_coverage(scopes, variant, 16, 100, 10, history_samples=history_samples)
                scopes[benchmark.TEMPORAL]["gpu_samples"] = 1
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.validate_coverage(scopes, variant, 16, 100, 10, history_samples=history_samples)
                del scopes[benchmark.TEMPORAL]
                with self.assertRaisesRegex(benchmark.SmokeFailure, "missing GPU scopes"):
                    benchmark.validate_coverage(scopes, variant, 16, 100, 10, history_samples=history_samples)

    def test_feedback_warmup_uses_completed_gpu_samples_before_measurement(self):
        reports = [{benchmark.FRAME: benchmark.ScopeSample(1, 3)} for _ in range(15)]
        start, retained = benchmark.retain_after_warmup(reports, 6, 32)
        self.assertEqual(start, 11)
        self.assertEqual(len(retained), 4)
        start, retained = benchmark.retain_after_warmup(reports[:7], 6, 32)
        self.assertEqual(start, 7)
        self.assertEqual(retained, [])
        self.assertEqual(benchmark.retain_after_warmup(reports, 6)[0], 6)


class PairedInferenceTests(unittest.TestCase):
    def test_feedback_pair_isolates_toggle_and_keeps_normal_comparisons(self):
        variants = benchmark.variants_for("offscreen", include_screen=True, include_feedback=True)
        trials = []
        for block in range(10):
            for variant in variants:
                scopes = synthetic_scopes(variant, frame_ms=4.8 if variant.feedback else 5.0)
                trials.append({"block": block, "variant": benchmark.asdict(variant), "scopes": scopes,
                    "kernel_work_ms": benchmark.kernel_work_ms(scopes, variant, 10)})
        comparisons = benchmark.compare_trials(trials, variants)
        pairs = {(comparison["baseline"], comparison["candidate"]) for comparison in comparisons}
        self.assertIn(("hardware", "hybrid"), pairs)
        self.assertIn(("disabled", "screen"), pairs)
        paired = next(comparison for comparison in comparisons if comparison["baseline"] == "hybrid")
        self.assertEqual(paired["candidate"], "hybrid_feedback")
        self.assertEqual(paired["status"], "resolved_gpu_time_reduction")
        self.assertIn(benchmark.CLASSIFY, paired["dispatch_scopes"])
        self.assertIn(benchmark.DEPTH, paired["dispatch_scopes"])

    def test_williams_order_balances_positions_and_preceding_treatments(self):
        for count in (3, 4, 5, 7):
            variants = tuple(benchmark.Variant(str(index), "hardware") for index in range(count))
            cycle = count if count % 2 == 0 else 2 * count
            blocks = cycle if cycle >= 5 else 2 * cycle
            rows = benchmark.balanced_orders(variants, blocks, 7)
            positions = collections.Counter((position, variant.name) for row in rows for position, variant in enumerate(row))
            self.assertEqual(len(set(positions.values())), 1)
            predecessors = collections.Counter((left.name, right.name) for row in rows for left, right in zip(row, row[1:]))
            self.assertEqual(len(predecessors), count * (count - 1))
            self.assertEqual(len(set(predecessors.values())), 1)
            self.assertEqual(rows, benchmark.balanced_orders(variants, blocks, 7))

    def test_incomplete_cycle_and_fewer_than_five_independent_units_fail(self):
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.balanced_orders(benchmark.variants_for("floor"), 5, 0)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.paired_statistics([1, 2, 3, 4])

    def test_clear_frame_reduction_requires_flat_controls(self):
        variants, trials = matched_trials()
        results = benchmark.compare_trials(trials, variants)
        pair = next(item for item in results if item["baseline"] == "hardware" and item["candidate"] == "hybrid")
        self.assertEqual(pair["status"], "resolved_gpu_time_reduction")
        self.assertAlmostEqual(pair["frame"]["mean_ms"], -.5)
        self.assertEqual(pair["frame"]["blocks"], 6)
        self.assertEqual(len(pair["frame"]["block_differences_ms"]), 6)

    def test_small_change_and_material_control_drift_cannot_claim_speedup(self):
        for delta, control, status in ((-.01, 0, "unresolved"), (-.5, -.1, "control_drift")):
            variants, trials = matched_trials(delta, control)
            results = benchmark.compare_trials(trials, variants)
            pair = next(item for item in results if item["baseline"] == "hardware" and item["candidate"] == "hybrid")
            self.assertEqual(pair["status"], status)

    def test_noisy_controls_require_equivalence_before_a_speedup_claim(self):
        variants, trials = matched_trials()
        for trial in trials:
            if trial["variant"]["name"] == "hybrid":
                trial["scopes"][benchmark.CONTROLS[0]]["mean_ms"] += .2 if trial["block"] % 2 else -.2
        results = benchmark.compare_trials(trials, variants)
        pair = next(item for item in results if item["baseline"] == "hardware" and item["candidate"] == "hybrid")
        self.assertEqual(pair["status"], "control_uncertain")
        self.assertFalse(pair["controls"][benchmark.CONTROLS[0]]["equivalent_within_tolerance"])

    def test_missing_or_duplicate_block_members_are_never_discarded_silently(self):
        variants, trials = matched_trials()
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.compare_trials(trials[:-1], variants)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.compare_trials(trials + [trials[0]], variants)


class BenchmarkCoverageBoundaryTests(unittest.TestCase):
    def test_capacity_prepared_scopes_allow_only_two_frames_or_two_percent_boundary_skew(self):
        variant = benchmark.Variant("hybrid", "hybrid")
        for frames, allowed in ((100, 2), (1000, 20)):
            for scope, multiplier in ((benchmark.CLASSIFY, 1), (benchmark.DEPTH, 10)):
                for sign in (-1, 1):
                    with self.subTest(frames=frames, scope=scope, sign=sign):
                        scopes = synthetic_scopes(variant, gpu_frames=frames)
                        scopes[scope]["gpu_samples"] += sign * allowed * multiplier
                        benchmark.validate_coverage(scopes, variant, 16, 100, 10)
                        scopes[scope]["gpu_samples"] += sign
                        with self.assertRaisesRegex(benchmark.SmokeFailure, "sample ratio"):
                            benchmark.validate_coverage(scopes, variant, 16, 100, 10)


class BenchmarkEnvironmentTests(unittest.TestCase):
    def test_diagnostics_and_capture_controls_are_sanitized_then_explicit(self):
        args = SimpleNamespace(family="rough", ray_budget=100, history_samples=16, roughness=.4,
            sampling_seed=0, optical_queries=8, screen_steps=96)
        inherited = {"NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1", "NWB_REFLECTION_SMOKE_FINAL_STATE": "1",
            "NWB_REFRACTION_SMOKE_GALLERY": "1", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old.bmp",
            "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT": "4", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "10",
            "NWB_GPU_TIMING_FILE": "old.txt", "NWB_REFLECTION_SMOKE_FEEDBACK": "1", "PRESERVED": "yes"}
        env, overrides = benchmark.timed_environment(inherited, args, benchmark.Variant("raw", "hardware"), Path("new.txt"))
        self.assertEqual(env["NWB_REFLECTION_SMOKE_DIAGNOSTICS"], "0")
        self.assertEqual(env["NWB_REFLECTION_SMOKE_TEMPORAL"], "0")
        self.assertEqual(env["NWB_REFLECTION_SMOKE_SPATIAL"], "0")
        self.assertEqual(env["NWB_REFLECTION_SMOKE_FEEDBACK"], "0")
        self.assertEqual(env["NWB_REFLECTION_SMOKE_SCREEN_STEPS"], "96")
        self.assertEqual(env["NWB_REFLECTION_SMOKE_FINAL_STATE"], "0")
        self.assertEqual(env["NWB_GPU_TIMING_FILE"], "new.txt")
        self.assertEqual(env["PRESERVED"], "yes")
        self.assertNotIn("PRESERVED", overrides)
        self.assertFalse(any(key.startswith("NWB_SMOKE_FRAMEBUFFER_CAPTURE_") for key in env))
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", env)
        self.assertEqual(inherited["NWB_GPU_TIMING_FILE"], "old.txt")

    def test_cli_defaults_select_complete_blocks_and_reject_capture(self):
        common = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", "output"]
        args = benchmark.parse_args(common)
        self.assertEqual(args.blocks, 6)
        self.assertEqual(args.mip_count, 10)
        self.assertEqual(args.optical_queries, 16)
        self.assertFalse(args.include_feedback)
        self.assertEqual(args.screen_steps, 96)
        self.assertEqual(benchmark.parse_args(common + ["--family", "rough"]).blocks, 10)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark.parse_args(common + ["--application-arg=--gpudbg"])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark.parse_args(common + ["--ray-budget", "0"])

    def test_feedback_pairs_preserve_all_baseline_routes_and_balanced_cycles(self):
        common = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", "output"]
        for family in ("floor", "offscreen", "feedback_long_miss", "rough", "optical_clear"):
            with self.subTest(family=family):
                plain = benchmark.variants_for(family)
                self.assertTrue(all(not variant.feedback for variant in plain))
                paired = benchmark.variants_for(family, include_feedback=True)
                self.assertEqual(paired[:len(plain)], plain)
                enabled = [variant for variant in paired if variant.feedback]
                self.assertEqual(enabled, [benchmark.Variant("hybrid_feedback", "hybrid", feedback=True)])
                args = benchmark.parse_args(common + ["--family", family, "--include-feedback"])
                rows = benchmark.balanced_orders(paired, args.blocks, 0)
                self.assertTrue(all(set(row) == set(paired) for row in rows))
                self.assertEqual(args.blocks, 14 if family == "rough" else 8)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark.parse_args(common + ["--include-feedback", "--minimum-frame-samples", "63"])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark.parse_args(common + ["--screen-steps", "257"])

    def test_predeclared_five_family_matrix_has_164_independent_trial_launches(self):
        common = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", "output",
            "--warmup-intervals", "2", "--sample-intervals", "6", "--minimum-frame-samples", "100",
            "--ray-budget", "1382400", "--sampling-seed", "0", "--optical-queries", "16"]
        total_trials = 0
        for family in ("offscreen", "feedback_long_miss", "floor", "rough", "optical_clear"):
            rough = family == "rough"
            feedback = family not in ("rough", "optical_clear")
            options = ["--family", family, "--blocks", "10" if rough else "8" if feedback else "6"]
            args = benchmark.parse_args(common + options + (["--include-feedback"] if feedback else []))
            variants = benchmark.variants_for(family, args.include_screen, args.include_feedback)
            rows = benchmark.balanced_orders(variants, args.blocks, args.order_seed)
            total_trials += sum(len(row) for row in rows)
            self.assertEqual((args.warmup_intervals, args.sample_intervals, args.minimum_frame_samples), (2, 6, 100))
            self.assertEqual(args.screen_steps, 16 if family == "feedback_long_miss" else 96)
            self.assertEqual(sum(variant.feedback for variant in variants), int(feedback))
            for variant in variants:
                required = benchmark.required_scopes(variant)
                self.assertTrue(set(benchmark.OBSERVED_CONTROLS).issubset(required))
                self.assertIn(benchmark.FRAME, required)
        self.assertEqual(total_trials, 164)

    def test_optical_family_keeps_three_matched_routes_and_checks_its_query_bound(self):
        args = benchmark.parse_args(["--executable", "test.exe", "--working-directory", ".", "--output-directory", "output",
            "--family", "optical_clear", "--require-hardware"])
        self.assertEqual(args.blocks, 6)
        self.assertEqual(benchmark.variants_for(args.family), benchmark.variants_for("floor"))
        self.assertIsNone(benchmark.validate_long_miss_qualification(args, {}))
        variant = benchmark.Variant("hardware", "hardware")
        _, controls = benchmark.timed_environment({}, args, variant, Path("optical.txt"))
        self.assertEqual(controls["NWB_REFLECTION_SMOKE_CASE"], "optical_clear")
        self.assertEqual(controls["NWB_REFLECTION_SMOKE_OPTICAL_QUERIES"], "16")
        self.assertEqual(controls["NWB_REFLECTION_SMOKE_DIAGNOSTICS"], "0")
        text = ("ReflectionSmokeProject: case optical_clear created\nReflectionSmokeProject: shutdown\n"
            "ReflectionSmokeProject: timing render unfocused 1\n"
            "ReflectionSmokeProject: timing in-flight ranges 32\n"
            "ReflectionSmokeProject: timing depth mip count 10\n"
            "ReflectionSmokeProject: reflection mode hardware\nReflectionSmokeProject: hardware ray budget 1382400\n"
            "ReflectionSmokeProject: screen feedback 0\nReflectionSmokeProject: screen steps 96\n"
            "ReflectionSmokeProject: optical query limit 16\n"
            "Reflection resolve: hardware\nReflectionSmokeProject: hardware available\n"
            "RendererSystem: deferred rendering targets ready (960x720, frame)\n")
        benchmark.validate_trial_log(text, args, variant)
        for bad in (text.replace("optical query limit 16", "optical query limit 1"),
                text.replace("ReflectionSmokeProject: optical query limit 16\n", "")):
            with self.subTest(text=bad), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_trial_log(bad, args, variant)

    def test_cli_rejects_abbreviated_controls_and_invalid_screen_step_limits(self):
        common = ["--executable", "test.exe", "--working-directory", ".", "--output-directory", "output"]
        for options in (["--warmup", "2"], ["--screen-steps", "7"], ["--screen-steps", "257"]):
            with self.subTest(options=options), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + options)
        for steps in (8, 256):
            self.assertEqual(benchmark.parse_args(common + ["--screen-steps", str(steps)]).screen_steps, steps)

    def test_feedback_on_changes_only_the_explicit_scheduling_toggle(self):
        args = SimpleNamespace(family="feedback_long_miss", ray_budget=1382400, history_samples=16, roughness=.4,
            sampling_seed=0, optical_queries=16, screen_steps=192)
        ordinary, off = benchmark.timed_environment({}, args, benchmark.Variant("hybrid", "hybrid"), Path("timing.txt"))
        enabled, on = benchmark.timed_environment({}, args,
            benchmark.Variant("hybrid_feedback", "hybrid", feedback=True), Path("timing.txt"))
        self.assertEqual({key for key in ordinary if ordinary[key] != enabled[key]}, {"NWB_REFLECTION_SMOKE_FEEDBACK"})
        self.assertEqual(on["NWB_REFLECTION_SMOKE_FEEDBACK"], "1")
        self.assertEqual(off["NWB_REFLECTION_SMOKE_FEEDBACK"], "0")
        self.assertEqual(on["NWB_REFLECTION_SMOKE_DIAGNOSTICS"], "0")
        self.assertEqual(on["NWB_REFLECTION_SMOKE_CASE"], "feedback_long_miss")

    def test_route_dimensions_and_diagnostics_are_verified_from_runtime_logs(self):
        args = SimpleNamespace(family="floor", ray_budget=100, require_hardware=True, width=960, height=720, screen_steps=96, mip_count=10)
        variant = benchmark.Variant("hardware", "hardware")
        text = ("ReflectionSmokeProject: case floor created\nReflectionSmokeProject: shutdown\n"
            "ReflectionSmokeProject: timing render unfocused 1\n"
            "ReflectionSmokeProject: timing in-flight ranges 32\n"
            "ReflectionSmokeProject: timing depth mip count 10\n"
            "ReflectionSmokeProject: reflection mode hardware\nReflectionSmokeProject: hardware ray budget 100\n"
            "ReflectionSmokeProject: screen feedback 0\nReflectionSmokeProject: screen steps 96\n"
            "Reflection resolve: hardware\nReflectionSmokeProject: hardware available\n"
            "RendererSystem: deferred rendering targets ready (960x720, frame)\n")
        benchmark.validate_trial_log(text, args, variant)
        benchmark.validate_trial_log(text.replace("\n", "\r\n"), args, variant)
        for bad in (text.replace("960x720", "1280x720"), text.replace("Reflection resolve: hardware", "Reflection resolve: disabled"),
                text.replace("screen feedback 0", "screen feedback 1"), text.replace("screen steps 96", "screen steps 16"),
                text.replace("ray budget 100", "ray budget 1000"), text.replace("screen steps 96", "screen steps 960"),
                text.replace("reflection mode hardware", "reflection mode hardware_unknown"),
                text + "ReflectionSmokeProject: hardware ray budget 200\n", text + "Reflection resolve: disabled\n",
                text + "ReflectionSmokeProject: screen feedback 0\n",
                text.replace("ReflectionSmokeProject: timing render unfocused 1\n", ""),
                text.replace("timing render unfocused 1", "timing render unfocused 0"),
                text.replace("ReflectionSmokeProject: timing in-flight ranges 32\n", ""),
                text.replace("timing in-flight ranges 32", "timing in-flight ranges 2"),
                text.replace("timing depth mip count 10", "timing depth mip count 9"),
                text + "ReflectionSmokeStatistics: sequence=3\n", text + "ReflectionSmokeFeedback: sequence=3\n",
                text + "ReflectionSmokeProject: hardware unavailable\n"):
            with self.subTest(text=bad), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_trial_log(bad, args, variant)


class CostlyMissQualificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "res").mkdir()
        (self.root / "res" / "authored.vol").write_bytes(b"exact shader and mesh cook")
        executable = self.root / "app.exe"
        executable.write_bytes(b"exact optimized executable")
        self.args = benchmark.parse_args(["--executable", str(executable), "--working-directory", str(self.root),
            "--output-directory", str(self.root / "output"), "--family", "feedback_long_miss", "--include-feedback",
            "--qualification", str(self.root / "qualified.json")])
        self.identity = benchmark.file_identity(executable)
        self.evidence = {"temporal": False, "spatial_filter": False, "fixed_delta_seconds": 1 / 60, "optical_queries": 16,
            "build_identity": {"executable_sha256": self.identity["sha256"],
                "asset_volumes": benchmark.authored_volume_hashes(self.root)},
            "captures": [{"name": "long_miss_feedback" if enabled else "long_miss_baseline", "enabled": enabled,
                "case": "feedback_long_miss", "mode": "hybrid", "hardware_budget": self.args.ray_budget,
                "screen_steps": 16, "seed": 0, "roughness": 0.0, "final_state": False, "mutation": False, "extent": "native"}
                for enabled in (False, True)],
            "metrics": {"long_miss_geometry": {"passed": True}, "long_miss": {"images": {"passed": True}},
                "costly_miss_qualification": {"completed_observations": 8, "actual_attempts": 100,
                    "actual_iterations": 1300, "mean_iterations_per_attempt": 13.0,
                    "actual_step_limit_misses": 30, "step_limit_miss_fraction": .3}}}

    def write(self, evidence):
        self.args.qualification.write_text(json.dumps(evidence), encoding="utf-8")

    def test_accepts_matching_qualified_build_and_excludes_only_mutable_cache(self):
        cache = self.root / "res" / volume_segment_filename("runtime_pipeline_cache", 0)
        cache.write_bytes(b"driver state")
        self.write(self.evidence)
        result = benchmark.validate_long_miss_qualification(self.args, self.identity)
        self.assertTrue(result["settings_match"])
        self.assertEqual(self.args.screen_steps, 16)
        cache.write_bytes(b"new mutable driver state")
        benchmark.validate_long_miss_qualification(self.args, self.identity)

    def test_rejects_missing_unqualified_or_incompatible_evidence(self):
        variations = []
        for key, value in (("completed_observations", 7), ("actual_iterations", 100), ("actual_step_limit_misses", 1),
                ("mean_iterations_per_attempt", 99.0), ("actual_attempts", 0)):
            changed = copy.deepcopy(self.evidence)
            changed["metrics"]["costly_miss_qualification"][key] = value
            variations.append(changed)
        for key, value in (("seed", 1), ("hardware_budget", 64), ("screen_steps", 96), ("extent", "npot")):
            changed = copy.deepcopy(self.evidence)
            changed["captures"][0][key] = value
            variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        changed["build_identity"]["executable_sha256"] = "different build"
        variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        changed["temporal"] = True
        variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        changed["optical_queries"] = 8
        variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        del changed["metrics"]["long_miss_geometry"]
        variations.append(changed)
        variations.extend(([], {"build_identity": []}))
        for changed in variations:
            with self.subTest(evidence=changed):
                self.write(changed)
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.validate_long_miss_qualification(self.args, self.identity)
        self.args.qualification = None
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_long_miss_qualification(self.args, self.identity)

    def test_rejects_changed_authored_volume_even_when_same_filename_remains(self):
        self.write(self.evidence)
        (self.root / "res" / "authored.vol").write_bytes(b"later shader cook")
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_long_miss_qualification(self.args, self.identity)


class BenchmarkLifecycleTests(unittest.TestCase):
    def test_only_canonical_runtime_pipeline_cache_segments_are_mutable(self):
        self.assertEqual(volume_segment_filename("runtime_pipeline_cache", 0), "1f98ed5c238bf1c3.vol")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            resources = root / "res"
            resources.mkdir()
            authored = resources / "authored.vol"
            authored.write_bytes(b"fixed cooked assets")
            similar = resources / "runtime_pipeline_cache_fake.vol"
            similar.write_bytes(b"must remain authored")
            caches = [resources / volume_segment_filename("runtime_pipeline_cache", index) for index in range(2)]
            for cache in caches:
                cache.write_bytes(b"runtime")
            original = benchmark.runtime_asset_identity(root)
            cache_before = benchmark.runtime_pipeline_cache_identity(root)
            self.assertEqual(len(original), 2)
            self.assertEqual(len(cache_before), 2)
            caches[0].write_bytes(b"updated driver cache")
            caches[1].write_bytes(b"updated driver segment")
            self.assertEqual(benchmark.runtime_asset_identity(root), original)
            self.assertNotEqual(benchmark.runtime_pipeline_cache_identity(root), cache_before)
            authored.write_bytes(b"different cook")
            self.assertNotEqual(benchmark.runtime_asset_identity(root), original)

    def test_runtime_cache_alone_does_not_qualify_as_cooked_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            resources = root / "res"
            resources.mkdir()
            (resources / volume_segment_filename("runtime_pipeline_cache", 0)).write_bytes(b"driver cache")
            with self.assertRaisesRegex(benchmark.SmokeFailure, "no authored resources"):
                benchmark.runtime_asset_identity(root)

    def test_existing_artifacts_are_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "app.exe"
            executable.write_bytes(b"exe")
            output = root / "output"
            output.mkdir()
            plan = output / "plan.json"
            plan.write_text("original", encoding="utf-8")
            args = SimpleNamespace(executable=executable, working_directory=root, output_directory=output)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "must be empty"):
                benchmark.run(args)
            self.assertEqual(plan.read_text(encoding="utf-8"), "original")

    def test_plan_records_build_decoder_and_common_control_identity_before_any_trial(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "app.exe"
            executable.write_bytes(b"immutable build")
            resources = root / "res"
            resources.mkdir()
            (resources / "authored.vol").write_bytes(b"immutable assets")
            namesym = root / "app.namesym"
            token = next(iter(known_name_symbols([benchmark.FRAME])))
            namesym.write_text(f"{token}\t0\t{benchmark.FRAME}\n", encoding="utf-8")
            args = benchmark.parse_args(["--executable", str(executable), "--working-directory", str(root),
                "--output-directory", str(root / "output"), "--namesym", str(namesym), "--family", "optical_clear"])
            with patch.object(benchmark, "run_trial", side_effect=benchmark.SmokeFailure("stopped before launch")), \
                    patch.object(benchmark, "write_status"), self.assertRaisesRegex(benchmark.SmokeFailure, "stopped before launch"):
                benchmark.run(args)
            plan = json.loads((args.output_directory / "plan.json").read_text(encoding="utf-8"))
            self.assertEqual(plan["executable_identity"], benchmark.file_identity(executable))
            self.assertEqual(plan["authored_volume_hashes"], benchmark.authored_volume_hashes(root))
            self.assertEqual(plan["namesym_identity"]["sha256"], benchmark.file_identity(namesym)["sha256"])
            self.assertEqual(plan["primary_scope"], benchmark.FRAME)
            self.assertEqual(plan["timing_in_flight_ranges"], 32)
            self.assertEqual(plan["control_scopes"], list(benchmark.CONTROLS))
            self.assertEqual(plan["observed_control_scopes"], list(benchmark.OBSERVED_CONTROLS))
            self.assertEqual(sum(len(row) for row in plan["orders"]), 18)
            failure = json.loads((args.output_directory / "failure.json").read_text(encoding="utf-8"))
            self.assertEqual(failure["completed_trials"], 0)

    def test_failed_launch_cleans_logserver_and_preserves_original_failure(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            args = benchmark.parse_args(["--executable", str(root / "app.exe"), "--working-directory", str(root),
                "--output-directory", str(root / "output")])
            backend = Mock()
            backend.close.side_effect = OSError("backend close failed")
            logserver = object()
            stack.enter_context(patch.object(benchmark, "build_launch_environment", return_value={}))
            stack.enter_context(patch.object(benchmark, "runtime_asset_identity", return_value=[]))
            stack.enter_context(patch.object(benchmark, "create_capture_backend", return_value=backend))
            stack.enter_context(patch.object(benchmark, "launch_logserver", return_value=(logserver, 1, root, {}, "logs")))
            stack.enter_context(patch.object(benchmark, "launch_testbed", side_effect=benchmark.SmokeFailure("launch failed")))
            stack.enter_context(patch.object(benchmark, "shutdown_logserver_and_collect", return_value="original log"))
            stop = stack.enter_context(patch.object(benchmark, "terminate_process", return_value=(0, "")))
            stack.enter_context(patch.object(benchmark, "write_status"))
            with self.assertRaisesRegex(benchmark.SmokeFailure, "launch failed"):
                benchmark.run_trial(args, benchmark.Variant("hardware", "hardware"), 0, 0, {}, {})
            stop.assert_any_call(logserver, "logserver")
            backend.close.assert_called_once()
            directory = args.output_directory / "block_00_00_hardware"
            self.assertEqual((directory / "runtime.log").read_text(encoding="utf-8"), "original log")
            self.assertIn("backend close failed", (directory / "cleanup_error.txt").read_text(encoding="utf-8"))

    def test_empty_completed_timing_reports_explain_absent_gpu_samples(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            args = benchmark.parse_args(["--executable", str(root / "app.exe"), "--working-directory", str(root),
                "--output-directory", str(root / "output")])
            runtime, logserver, backend = Mock(), Mock(), Mock()
            backend.wait_for_window.return_value = 1
            def launch(*_):
                timing = args.output_directory / "block_00_00_hardware" / "gpu_timing.txt"
                timing.write_text(report([], cpu_frames=50000) * 2, encoding="utf-8")
                return runtime
            stack.enter_context(patch.object(benchmark, "build_launch_environment", return_value={}))
            stack.enter_context(patch.object(benchmark, "runtime_asset_identity", return_value=[]))
            stack.enter_context(patch.object(benchmark, "create_capture_backend", return_value=backend))
            stack.enter_context(patch.object(benchmark, "launch_logserver", return_value=(logserver, 1, root, {}, "logs")))
            stack.enter_context(patch.object(benchmark, "launch_testbed", side_effect=launch))
            stack.enter_context(patch.object(benchmark, "ensure_process_running"))
            stack.enter_context(patch.object(benchmark, "terminate_process", return_value=(0, "")))
            stack.enter_context(patch.object(benchmark, "shutdown_logserver_and_collect", return_value="empty GPU reports"))
            stack.enter_context(patch.object(benchmark.time, "monotonic", side_effect=(0.0, 0.0, 100.0)))
            stack.enter_context(patch.object(benchmark.time, "sleep"))
            with self.assertRaisesRegex(benchmark.SmokeFailure, "timing reports contain no completed GPU frame samples"):
                benchmark.run_trial(args, benchmark.Variant("hardware", "hardware"), 0, 0, {}, {})
            self.assertFalse((args.output_directory / "block_00_00_hardware" / "summary.json").exists())


if __name__ == "__main__":
    unittest.main()
