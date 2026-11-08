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
    LIT_BASELINE_EXECUTABLE, LIT_BASELINE_RUNTIME, LIT_BASELINE_SOURCE_MANIFEST, LIT_BLOCK, LIT_BLOCKS,
    LIT_BR, LIT_BS_JSON, LIT_CANDIDATE, LIT_CANDIDATE_EXECUTABLE, LIT_CANDIDATE_RUNTIME,
    LIT_CANDIDATE_SOURCE_MANIFEST, LIT_CONTROLS, LIT_CONTROL_DRIFT, LIT_CONTROL_UNCERTAIN, LIT_EVERY_PLANNED_TRIAL,
    LIT_GPU_SAMPLES, LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU, LIT_HARDWARE, LIT_IN_FLIGHT_RANGES_2, LIT_IN_FLIGHT_RANGES_32,
    LIT_LOGGER, LIT_LOGSERVER_EXECUTABLE, LIT_MAIN, LIT_MEAN_MS, LIT_MISSING_COMPLETED_GPU_SCOPES,
    LIT_NAN, LIT_NATURAL_HARDWARE_SHADOW_ROUTE, LIT_NATURAL_HYBRID_SHADOW_ROUTE, LIT_NWB_CAUSTIC_SMOKE_ENABLED, LIT_NWB_GPU_TIMING_FILE,
    LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS, LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH, LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE, LIT_N_1280X900,
    LIT_N_960X720, LIT_OLD_BMP, LIT_OLD_TXT, LIT_OUTPUT, LIT_OUTPUT_DIRECTORY,
    LIT_PRESERVED, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA, LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED, LIT_RENDER_DEFERRED_COMPOSITE,
    LIT_RENDER_DEFERRED_PRESENT, LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_SHADOW_VISIBILITY, LIT_RESOLVED_GPU_TIME_REDUCTION, LIT_SCOPES,
    LIT_SECONDARY_SCOPE, LIT_SELECTED, LIT_SHADOW_ROUTE, LIT_SOFTWARE, LIT_STATUS,
    LIT_TIMING_TXT, LIT_TOTAL_MS, LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN, LIT_TRANSPARENT_MULTI, LIT_VK_INSTANCE_LAYERS,
    LIT_VK_LAYER_KHRONOS_VALIDATION, LIT_VK_LOADER_LAYERS_ENABLE, LIT_YES, log_text,
)


class WorkloadPolicyTests(unittest.TestCase):
    def test_inherited_smoke_capture_diagnostics_and_pose_are_cleared(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_TRANSPARENT_MULTI]
        inherited = {LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP, LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_NWB_CAUSTIC_SMOKE_ENABLED: "1", "NWB_TRANSPARENT_CSG_DISABLE_CUTTER": "1",
            LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE: "2", LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "5", LIT_NWB_GPU_TIMING_FILE: LIT_OLD_TXT, LIT_PRESERVED: LIT_YES}
        env, overrides = benchmark.configure_environment(inherited, workload, Path(LIT_TIMING_TXT))
        self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **dict(workload.environment_overrides), LIT_NWB_GPU_TIMING_FILE: LIT_TIMING_TXT})
        self.assertEqual(overrides[LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE], "0")
        self.assertEqual(inherited[LIT_NWB_GPU_TIMING_FILE], LIT_OLD_TXT)

    def test_explicit_validation_is_rejected_not_silently_disabled(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_TRANSPARENT_MULTI]
        for key in (LIT_VK_INSTANCE_LAYERS, LIT_VK_LOADER_LAYERS_ENABLE):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: LIT_VK_LAYER_KHRONOS_VALIDATION}, workload, Path("t"))

    def test_logs_require_actual_route_extent_policy_device_and_shutdown(self):
        self.assertEqual(RendererWorkloadLogPolicy.device_material_signature(log_text()), RendererWorkloadLogPolicy.device_material_signature(log_text().replace("\n", "\r\n")))
        workload = RendererWorkloadRegistry.workloads()[LIT_TRANSPARENT_MULTI]
        expected = RendererWorkloadLogPolicy.transparent_multi_log(log_text(), workload, True)
        self.assertEqual(expected[LIT_SHADOW_ROUTE], LIT_HARDWARE)
        self.assertEqual(expected, RendererWorkloadLogPolicy.transparent_multi_log(log_text().replace("\n", "\r\n"), workload, True))
        software = RendererWorkloadLogPolicy.transparent_multi_log(log_text(LIT_SOFTWARE), workload, False)
        self.assertEqual(software[LIT_SHADOW_ROUTE], LIT_SOFTWARE)
        with self.assertRaises(benchmark.SmokeFailure):
            RendererWorkloadLogPolicy.transparent_multi_log(log_text(LIT_SOFTWARE), workload, True)
        for altered in (log_text().replace(LIT_N_1280X900, LIT_N_960X720), log_text().replace(LIT_IN_FLIGHT_RANGES_32, LIT_IN_FLIGHT_RANGES_2), log_text().replace("render unfocused 1", "render unfocused 0"),
            log_text().replace(LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN, ""), log_text() + "\nAvboitTimingProbe: in-flight ranges 32", log_text() + "\n[ERROR]: rejected",
            log_text() + "\nFramebufferCapture: capture ready", log_text().replace("GraphicsRuntime: created device", "device")):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                RendererWorkloadLogPolicy.transparent_multi_log(altered, workload, True)

    def test_material_signature_preserves_mixed_routes_independent_of_creation_order(self):
        indexed = "RendererSystem: material 'glass' selected VertexIndexed + PS from persistent object-space geometry"
        compute = LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED
        device = LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU
        mixed = RendererWorkloadLogPolicy.device_material_signature("\n".join((device, compute, indexed, compute)))
        reordered = RendererWorkloadLogPolicy.device_material_signature("\n".join((device, indexed, compute)))
        self.assertEqual(mixed, reordered)
        self.assertEqual(mixed["material_routes"]["glass"], [compute.split(LIT_SELECTED)[1], indexed.split(LIT_SELECTED)[1]])
        self.assertNotEqual(mixed, RendererWorkloadLogPolicy.device_material_signature("\n".join((device, indexed))))
        with self.assertRaises(benchmark.SmokeFailure):
            RendererWorkloadLogPolicy.device_material_signature(device)

    def test_current_hardware_route_requires_dispatch_and_rejects_retired_routes(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_TRANSPARENT_MULTI]
        marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        for altered in (log_text().replace(marker, ""), log_text() + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                RendererWorkloadLogPolicy.transparent_multi_log(altered, workload, True)
        retired = log_text().replace(LIT_NATURAL_HARDWARE_SHADOW_ROUTE, LIT_NATURAL_HYBRID_SHADOW_ROUTE)
        retired_route = next(line for line in retired.splitlines() if LIT_NATURAL_HYBRID_SHADOW_ROUTE in line)
        for altered in (retired, log_text() + "\n" + retired_route):
            for require_hardware in (False, True):
                with self.subTest(hardware=require_hardware), self.assertRaises(benchmark.SmokeFailure):
                    RendererWorkloadLogPolicy.transparent_multi_log(altered, workload, require_hardware)

    def test_cli_rejects_lower_coverage_and_unbalanced_plans(self):
        common = [LIT_BASELINE_EXECUTABLE, LIT_A, LIT_BASELINE_RUNTIME, LIT_AR, LIT_BASELINE_SOURCE_MANIFEST, LIT_AS_JSON, LIT_CANDIDATE_EXECUTABLE, LIT_B, LIT_CANDIDATE_RUNTIME, LIT_BR, LIT_CANDIDATE_SOURCE_MANIFEST, LIT_BS_JSON,
            LIT_LOGSERVER_EXECUTABLE, LIT_LOGGER, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        for extra in ((LIT_BLOCKS, "7"), (LIT_BLOCKS, "6"), ("--warmup-intervals", "1"), ("--sample-intervals", "10"), ("--minimum-frame-samples", "99"), ("--timeout", LIT_NAN), ("--application-arg=--gpudbg",)):
            with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + list(extra))


class WorkloadControlSelectionTests(unittest.TestCase):
    SHADOW_CONTROLS = (LIT_RENDER_OPAQUE_REGULAR, "render.deferred_lighting", LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT)

    def shadow_workload(self, **changes):
        from dataclasses import replace
        values = {"name": "test-shadow-controls", LIT_SECONDARY_SCOPE: LIT_RENDER_SHADOW_VISIBILITY, "control_scopes": self.SHADOW_CONTROLS}
        values.update(changes)
        return replace(RendererWorkloadRegistry.workloads()[LIT_TRANSPARENT_MULTI], **values)

    def test_shadow_target_change_is_not_misclassified_as_control_drift(self):
        _, orders, trials = trial_matrix()
        workload = self.shadow_workload()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_MEAN_MS] = .2
                trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_TOTAL_MS] = .2 * trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_GPU_SAMPLES]
        result = RendererTrialComparison.compare_trials(trials, orders, workload)
        self.assertEqual(result[LIT_STATUS], LIT_RESOLVED_GPU_TIME_REDUCTION)

    def test_composite_and_present_drift_remain_controls(self):
        for changed_scope in (LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT):
            _, orders, trials = trial_matrix()
            for trial in trials:
                if trial[LIT_ARM] == LIT_CANDIDATE:
                    trial[LIT_SCOPES][changed_scope][LIT_MEAN_MS] = .8
            with self.subTest(scope=changed_scope):
                result = RendererTrialComparison.compare_trials(trials, orders, self.shadow_workload())
                self.assertEqual(result[LIT_STATUS], LIT_CONTROL_DRIFT)
                self.assertTrue(result[LIT_CONTROLS][changed_scope]["material_drift"])

    def test_uncertain_present_control_cannot_claim_reduction(self):
        _, orders, trials = trial_matrix()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][LIT_RENDER_DEFERRED_PRESENT][LIT_MEAN_MS] += .2 if trial[LIT_BLOCK] % 2 else -.2
        result = RendererTrialComparison.compare_trials(trials, orders, self.shadow_workload())
        self.assertEqual(result[LIT_STATUS], LIT_CONTROL_UNCERTAIN)

    def test_empty_duplicate_or_mutable_controls_are_rejected(self):
        for controls in ((), (LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_OPAQUE_REGULAR), [LIT_RENDER_OPAQUE_REGULAR]):
            with self.subTest(controls=controls), self.assertRaisesRegex(benchmark.SmokeFailure, "nonempty unique tuple"):
                self.shadow_workload(control_scopes=controls)

    def test_unobserved_control_is_rejected(self):
        with self.assertRaisesRegex(benchmark.SmokeFailure, "not an observed scope"):
            self.shadow_workload(control_scopes=("render.not_measured",))

    def test_primary_and_secondary_targets_cannot_be_controls(self):
        for control in (FRAME, LIT_RENDER_SHADOW_VISIBILITY):
            with self.subTest(control=control), self.assertRaisesRegex(benchmark.SmokeFailure, "target cannot also be a control"):
                self.shadow_workload(control_scopes=(control,))

    def test_shadow_controls_still_require_complete_scope_coverage(self):
        workload = self.shadow_workload()
        for control in self.SHADOW_CONTROLS:
            values = scopes(workload)
            del values[control]
            with self.subTest(control=control), self.assertRaisesRegex(benchmark.SmokeFailure, LIT_MISSING_COMPLETED_GPU_SCOPES):
                benchmark.validate_coverage(values, workload, 6, 100)

    def test_per_workload_controls_do_not_relax_complete_trial_requirement(self):
        _, orders, trials = trial_matrix()
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            RendererTrialComparison.compare_trials(trials[:-1], orders, self.shadow_workload())


if __name__ == LIT_MAIN:
    unittest.main()
