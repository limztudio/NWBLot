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
    LIT_HYBRID, LIT_IN_FLIGHT_RANGES_2, LIT_IN_FLIGHT_RANGES_32, LIT_MAIN, LIT_NAN,
    LIT_NWB_GPU_TIMING_FILE, LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS, LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH, LIT_NWB_SOFT_SHADOW_TEST_ANGLE,
    LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS, LIT_NWB_SOFT_SHADOW_TEST_TIMING, LIT_N_1280X900, LIT_N_960X720, LIT_OLD_BMP,
    LIT_PRESERVED, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA, LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE, LIT_SHADOW_FINITE_EXTENT,
    LIT_SHADOW_ROUTE, LIT_SHADOW_ZERO_EXTENT, LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN, LIT_SOFTWARE, LIT_TIMING_TXT,
    LIT_VK_LAYER_KHRONOS_VALIDATION, LIT_YES, soft_shadow_log_text,
)


class ShadowWorkloadPolicyTests(unittest.TestCase):
    def test_shadow_environment_clears_inherited_capture_and_extent_policy(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_SHADOW_ZERO_EXTENT]
        env, overrides = benchmark.configure_environment({LIT_NWB_SOFT_SHADOW_TEST_ANGLE: "0.2", LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS: "1", LIT_NWB_SOFT_SHADOW_TEST_TIMING: "0",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "360", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP, LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_PRESERVED: LIT_YES}, workload, Path(LIT_TIMING_TXT))
        self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **dict(workload.environment_overrides), LIT_NWB_GPU_TIMING_FILE: LIT_TIMING_TXT})

    def test_shadow_logs_require_actual_policy_route_extent_and_lifecycle(self):
        for name in (LIT_SHADOW_ZERO_EXTENT, LIT_SHADOW_FINITE_EXTENT):
            workload = RendererWorkloadRegistry.workloads()[name]
            text = soft_shadow_log_text(workload)
            expected = RendererWorkloadLogPolicy.soft_shadow_log(text, workload, True)
            self.assertEqual(expected, RendererWorkloadLogPolicy.soft_shadow_log(text.replace("\n", "\r\n"), workload, True))
            for altered in (text.replace(LIT_IN_FLIGHT_RANGES_32, LIT_IN_FLIGHT_RANGES_2), text.replace("caustic emission 0", "caustic emission 1"), text.replace(LIT_N_1280X900, LIT_N_960X720),
                text.replace(LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN, ""), text + "\nShadowTimingProbe: render unfocused 0", text.replace("natural shadow route hardware", "natural shadow route software")):
                with self.subTest(name=name, text=altered), self.assertRaises(benchmark.SmokeFailure):
                    RendererWorkloadLogPolicy.soft_shadow_log(altered, workload, True)

    def test_shadow_hardware_dispatch_evidence_is_required_and_retired_routes_are_rejected(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_SHADOW_ZERO_EXTENT]
        marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        text = soft_shadow_log_text(workload)
        for altered in (text.replace(marker, ""), text + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                RendererWorkloadLogPolicy.soft_shadow_log(altered, workload, True)
        retired = soft_shadow_log_text(workload, LIT_HYBRID)
        retired_route = f"ShadowTimingProbe: natural shadow route {LIT_HYBRID}"
        for altered in (retired, text + "\n" + retired_route):
            for require_hardware in (False, True):
                with self.subTest(text=altered, require_hardware=require_hardware), self.assertRaises(benchmark.SmokeFailure):
                    RendererWorkloadLogPolicy.soft_shadow_log(altered, workload, require_hardware)
        software_text = soft_shadow_log_text(workload, LIT_SOFTWARE)
        software = RendererWorkloadLogPolicy.soft_shadow_log(software_text, workload, False)
        self.assertEqual(software[LIT_SHADOW_ROUTE], LIT_SOFTWARE)
        with self.assertRaises(benchmark.SmokeFailure):
            RendererWorkloadLogPolicy.soft_shadow_log(software_text, workload, True)

    def test_shadow_logs_require_exact_material_indirect_response(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_SHADOW_ZERO_EXTENT]
        text = soft_shadow_log_text(workload)
        marker = LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE
        self.assertEqual(RendererWorkloadLogPolicy.soft_shadow_log(text, workload, True)["indirect_response"], "hemi-ambient")
        for altered in (text.replace(marker, ""), text.replace(marker, "ShadowTimingProbe: indirect response surfel"), text + "\n" + marker, text + "\nShadowTimingProbe: indirect response surfel"):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                RendererWorkloadLogPolicy.soft_shadow_log(altered, workload, True)

    def test_shadow_zero_policy_does_not_accept_tiny_nonzero_or_nonfinite(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_SHADOW_ZERO_EXTENT]
        text = soft_shadow_log_text(workload)
        for value in ("0.000000001", LIT_NAN, "inf", "-inf"):
            with self.subTest(value=value), self.assertRaises(benchmark.SmokeFailure):
                RendererWorkloadLogPolicy.soft_shadow_log(text.replace("angular=0 radius=0", f"angular={value} radius=0"), workload, True)

    def test_shadow_logs_reject_validation_capture_and_fallback_emission(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_SHADOW_ZERO_EXTENT]
        for marker in ("FramebufferCapture: ready", "Vulkan: enabled validation layer", LIT_VK_LAYER_KHRONOS_VALIDATION, "render submission suspended", "retaining all-lit visibility", "preserving opaque visibility"):
            with self.subTest(marker=marker), self.assertRaises(benchmark.SmokeFailure):
                RendererWorkloadLogPolicy.soft_shadow_log(soft_shadow_log_text(workload) + "\n" + marker, workload, True)

    def test_shadow_coverage_requires_each_phase_and_rejects_caustic_work(self):
        workload = RendererWorkloadRegistry.workloads()[LIT_SHADOW_ZERO_EXTENT]
        values = scopes(workload)
        benchmark.validate_coverage(values, workload, 6, 100)
        for name in SHADOW_PHASES:
            missing = copy.deepcopy(values)
            del missing[name]
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(missing, workload, 6, 100)
        for name in SHADOW_INACTIVE:
            unexpected = copy.deepcopy(values)
            unexpected[name] = dict(values[FRAME])
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_inactive_scopes(unexpected, workload)


if __name__ == LIT_MAIN:
    unittest.main()
