#!/usr/bin/env python3
"""Renderer A/B workload policy, control selection, shadow, and reflection workload analysis."""


import contextlib
import copy
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from renderer_workload_log_policy import RendererWorkloadLogPolicy, SHADOW_ROUTES
import renderer_ab_benchmark as benchmark
from renderer_trial_comparison import LIT_MEANS_MS, RendererTrialComparison
from renderer_workload_registry import RendererWorkloadRegistry, FRAME, CONTROLS, SHADOW_INACTIVE, SHADOW_PHASES
from gpu_timing_evidence import GpuTimingEvidence

TIMING_EVIDENCE = GpuTimingEvidence()
from renderer_ab_benchmark_test_support import scopes, trial_matrix


from renderer_ab_workload_fixture import (
    LIT_A, LIT_AR, LIT_ARM, LIT_AS_JSON, LIT_B,
    LIT_BASELINE, LIT_BASELINE_EXECUTABLE, LIT_BASELINE_RUNTIME, LIT_BASELINE_SOURCE_MANIFEST, LIT_BR,
    LIT_BS_JSON, LIT_CANDIDATE, LIT_CANDIDATE_EXECUTABLE, LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, LIT_CANDIDATE_RUNTIME,
    LIT_CANDIDATE_SOURCE_MANIFEST, LIT_ELIGIBLE, LIT_EVERY_PLANNED_TRIAL, LIT_GPU_SAMPLES, LIT_INACTIVE_REFLECTION_SCOPES,
    LIT_LOGGER, LIT_LOGSERVER_EXECUTABLE, LIT_MAIN, LIT_MEAN_FPS, LIT_MEAN_FRAME_MS,
    LIT_MEAN_MS, LIT_NEW_TIMING_TXT, LIT_NWB_AVBOIT_SMOKE_TIMING, LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS, LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES,
    LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO, LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH, LIT_N_1280X900, LIT_N_16, LIT_N_960X720,
    LIT_OLD_BMP, LIT_OUTPUT, LIT_OUTPUT_DIRECTORY, LIT_PRESERVED, LIT_REFLECTIONSMOKEPROJECT_SHUTDOWN,
    LIT_REFLECTION_MIRROR_SPATIAL, LIT_REFLECTION_OPTICAL_CSG_CAP, LIT_REFLECTION_OPTICAL_INSIDE, LIT_REFLECTION_ROUGH_FILTERED, LIT_REFLECTION_ROUGH_SPATIAL,
    LIT_REFLECTION_SCREEN_DEPTH, LIT_SCOPES, LIT_TOTAL_MS, LIT_TRANSPARENT_MULTI, LIT_VK_INSTANCE_LAYERS,
    LIT_VK_LOADER_LAYERS_ENABLE, LIT_WALL_FRAME, LIT_WORKLOAD, LIT_YES, reflection_log_text,
)


class ReflectionWorkloadTests(unittest.TestCase):
    def test_reflection_environment_reuses_fixed_production_controls_after_clearing_inheritance(self):
        inherited = {LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES: "1", LIT_NWB_AVBOIT_SMOKE_TIMING: "1", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP,
            LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO: "5", LIT_PRESERVED: LIT_YES}
        for workload in RendererWorkloadRegistry.workloads().values():
            if workload.reflection_policy is None:
                continue
            with self.subTest(workload=workload.name):
                env, overrides = benchmark.configure_environment(inherited, workload, Path(LIT_NEW_TIMING_TXT))
                self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **overrides})
                self.assertEqual(overrides[LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS], "0")
                self.assertEqual(overrides[LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES], LIT_N_16)
                self.assertEqual(overrides[LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO], "0.016666667")
        self.assertEqual(inherited[LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS], "1")

    def test_reflection_cannot_override_explicit_vulkan_validation(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_ROUGH_SPATIAL]
        for key in (LIT_VK_INSTANCE_LAYERS, LIT_VK_LOADER_LAYERS_ENABLE):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: "validation"}, workload, Path("timing"))

    def test_screen_depth_rejects_incomplete_or_excess_mip_coverage(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        for multiplier in (1, 9, 11):
            changed = scopes(workload)
            changed[benchmark.reflection.DEPTH][LIT_GPU_SAMPLES] = 200 * multiplier
            with self.subTest(multiplier=multiplier), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_mirror_rejects_missing_spatial_range(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_MIRROR_SPATIAL]
        changed = scopes(workload)
        del changed[benchmark.reflection.SPATIAL]
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_coverage(changed, workload, 6, 100)

    def test_filtered_retains_temporal_one_per_frame_and_both_controls(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_ROUGH_FILTERED]
        for missing in (benchmark.reflection.TEMPORAL, benchmark.reflection.SPATIAL, CONTROLS[1]):
            changed = scopes(workload)
            del changed[missing]
            with self.subTest(scope=missing), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_inactive_ranges_are_rejected_even_outside_the_retained_window(self):
        for workload in RendererWorkloadRegistry.workloads().values():
            for inactive in workload.inactive_scopes:
                with self.subTest(workload=workload.name, scope=inactive), self.assertRaisesRegex(benchmark.SmokeFailure, LIT_INACTIVE_REFLECTION_SCOPES):
                    benchmark.validate_inactive_scopes({inactive: {LIT_GPU_SAMPLES: 1}}, workload)

    def test_inactive_hashed_scope_is_decoded_and_rejected(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        symbols = benchmark.load_name_symbols(None, workload.observed_scopes)
        token = next(token for token, name in symbols.items() if name == benchmark.reflection.HARDWARE)
        text = f"=== interval: 1 frames / 0.5s ===\n  {token}: window_avg_ms=1 window_min_ms=1 window_max_ms=1 published_windows=1 total_ms=1 gpu_samples=1 sample_avg_ms=1\n"
        parsed = TIMING_EVIDENCE.summarize_intervals(GpuTimingEvidence(symbols).parse_intervals(text, finalized=True))
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_INACTIVE_REFLECTION_SCOPES):
            benchmark.validate_inactive_scopes(parsed, workload)

    def test_reflection_runtime_signature_uses_production_log_validation(self):
        for workload in RendererWorkloadRegistry.workloads().values():
            if workload.reflection_policy is None:
                continue
            with self.subTest(workload=workload.name):
                text = reflection_log_text(workload)
                signature = workload.validate_log(text, workload, True)
                self.assertEqual(signature["reflection_route"], workload.reflection_policy.variant.mode)
                self.assertEqual(signature["reflection_policy"]["roughness"], workload.reflection_policy.roughness)
                self.assertEqual(signature, workload.validate_log(text.replace("\n", "\r\n"), workload, True))
                for altered in (text.replace(LIT_N_960X720, LIT_N_1280X900), text.replace("timing in-flight ranges 32", "timing in-flight ranges 2"), text + "\nReflectionSmokeProject: screen steps 16", text + "\nVUID-rejected",
                    text + "\nFramebufferCapture: capture ready", text.replace("hardware available", "hardware unavailable"), text.replace(LIT_REFLECTIONSMOKEPROJECT_SHUTDOWN, "")):
                    with self.assertRaises(benchmark.SmokeFailure):
                        workload.validate_log(altered, workload, True)

    def test_inside_query_cap_is_strict_despite_shared_benchmark_only_checking_clear(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_INSIDE]
        text = reflection_log_text(workload)
        for changed in (text.replace("optical query limit 16", "optical query limit 8"), text + "\nReflectionSmokeProject: optical query limit 16"):
            with self.assertRaisesRegex(benchmark.SmokeFailure, "optical query limit"):
                workload.validate_log(changed, workload, True)

    def test_declared_native_dispatch_count_never_changes_range_coverage(self):
        common = [LIT_BASELINE_EXECUTABLE, LIT_A, LIT_BASELINE_RUNTIME, LIT_AR, LIT_BASELINE_SOURCE_MANIFEST, LIT_AS_JSON, LIT_CANDIDATE_EXECUTABLE, LIT_B, LIT_CANDIDATE_RUNTIME, LIT_BR, LIT_CANDIDATE_SOURCE_MANIFEST, LIT_BS_JSON,
            LIT_LOGSERVER_EXECUTABLE, LIT_LOGGER, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        for count in (2, 169):
            args = benchmark.parse_args(common + [LIT_WORKLOAD, LIT_REFLECTION_OPTICAL_CSG_CAP, LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, str(count)])
            workload = RendererWorkloadRegistry.workloads()[args.workload]
            value = scopes(workload)
            benchmark.validate_coverage(value, workload, 6, 100)
            value[benchmark.reflection.HARDWARE][LIT_GPU_SAMPLES] *= count
            with self.subTest(native_calls=count), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(value, workload, 6, 100)
        for invalid in ("0", "-1", "1382401"):
            with self.subTest(native_calls=invalid), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + [LIT_WORKLOAD, LIT_REFLECTION_OPTICAL_CSG_CAP, LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, invalid])
        for name in (LIT_TRANSPARENT_MULTI, LIT_REFLECTION_SCREEN_DEPTH, LIT_REFLECTION_ROUGH_FILTERED):
            with self.subTest(workload=name), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + [LIT_WORKLOAD, name, LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, "2"])

    def test_sliced_long_hardware_range_retains_all_work_in_per_frame_and_paired_times(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_OPTICAL_CSG_CAP]
        _, orders, trials = trial_matrix()
        for trial in trials:
            candidate = trial[LIT_ARM] == LIT_CANDIDATE
            trial["native_hardware_dispatches_per_range"] = 169 if candidate else 1
            frame_ms = 2400 if candidate else 100
            trial[LIT_SCOPES] = scopes(workload, frame_ms=frame_ms)
            trial[LIT_WALL_FRAME] = {LIT_ELIGIBLE: True, LIT_MEAN_FRAME_MS: frame_ms, LIT_MEAN_FPS: 1000 / frame_ms}
            hardware = trial[LIT_SCOPES][benchmark.reflection.HARDWARE]
            hardware[LIT_MEAN_MS] = 2351 if candidate else 50
            hardware[LIT_TOTAL_MS] = hardware[LIT_MEAN_MS] * hardware[LIT_GPU_SAMPLES]
            benchmark.validate_coverage(trial[LIT_SCOPES], workload, 6, 100)
        result = RendererTrialComparison.compare_trials(trials, orders, workload)
        self.assertEqual(result[LIT_MEANS_MS][LIT_CANDIDATE][benchmark.reflection.HARDWARE], 2351)
        self.assertEqual(result["secondary_per_frame_work"][LIT_MEANS_MS][LIT_CANDIDATE], 2351)
        self.assertEqual(result["secondary"][LIT_MEAN_MS], 2301)
        self.assertEqual(result["frame"][LIT_MEAN_MS], 2300)

    def test_depth_secondary_rejects_an_incomplete_trial_matrix(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        _, orders, trials = trial_matrix()
        for trial in trials:
            frame_ms = 5 if trial[LIT_ARM] == LIT_BASELINE else 4.5
            trial[LIT_SCOPES] = scopes(workload, frame_ms=frame_ms)
            trial[LIT_WALL_FRAME] = {LIT_ELIGIBLE: True, LIT_MEAN_FRAME_MS: frame_ms, LIT_MEAN_FPS: 1000 / frame_ms}
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][benchmark.reflection.DEPTH][LIT_MEAN_MS] = .4
                trial[LIT_SCOPES][benchmark.reflection.DEPTH][LIT_TOTAL_MS] = .4 * 200 * 10
        RendererTrialComparison.compare_trials(trials, orders, workload)
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            RendererTrialComparison.compare_trials(trials[:-1], orders, workload)


if __name__ == LIT_MAIN:
    unittest.main()
