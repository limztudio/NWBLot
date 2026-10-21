#!/usr/bin/env python3
"""Renderer A/B workload policy, control selection, shadow, and reflection workload analysis."""


import contextlib
import copy
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark
from renderer_ab_benchmark_test_support import scopes, trial_matrix


LIT_A = "a"
LIT_B = "b"
LIT_ELIGIBLE = "eligible"
LIT_MEAN_FRAME_MS = "mean_frame_ms"
LIT_MEAN_FPS = "mean_fps"
LIT_REFLECTION_OPTICAL_CSG_CAP = "reflection-optical-csg-cap"
LIT_WALL_FRAME = "wall_frame"
LIT_NATURAL_HARDWARE_SHADOW_ROUTE = "natural hardware shadow route"
LIT_NATURAL_HYBRID_SHADOW_ROUTE = "natural hybrid shadow route"
LIT_TOTAL_MS = "total_ms"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_MEAN_MS = "mean_ms"
LIT_HARDWARE = "hardware"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA = "RendererSystem: dispatched software shadow traversal"
LIT_AVBOITTIMINGPROBE_IN_FLIGHT_RANGES_32 = "AvboitTimingProbe: in-flight ranges 32"
LIT_AVBOITTIMINGPROBE_RENDER_UNFOCUSED_1 = "AvboitTimingProbe: render unfocused 1"
LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG = "RendererSystem: deferred rendering targets ready (1280x900, samples=1)"
LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU = "GraphicsRuntime: created device 'Example GPU'"
LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED = "RendererSystem: material 'glass' selected CS + PS through compute emulation"
LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN = "TransparentMultiSmokeProject: shutdown"
LIT_TRANSPARENT_MULTI = "transparent-multi"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_ARM = "arm"
LIT_BLOCK = "block"
LIT_SCOPES = "scopes"
LIT_MISSING_COMPLETED_GPU_SCOPES = "missing completed GPU scopes"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
LIT_OLD_BMP = "old.bmp"
LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS = "NWB_REFLECTION_SMOKE_DIAGNOSTICS"
LIT_NWB_CAUSTIC_SMOKE_ENABLED = "NWB_CAUSTIC_SMOKE_ENABLED"
LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE = "NWB_TRANSPARENT_MULTI_SPIN_ANGLE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_OLD_TXT = "old.txt"
LIT_PRESERVED = "PRESERVED"
LIT_YES = "yes"
LIT_TIMING_TXT = "timing.txt"
LIT_NWB_AVBOIT_SMOKE_TIMING = "NWB_AVBOIT_SMOKE_TIMING"
LIT_VK_INSTANCE_LAYERS = "VK_INSTANCE_LAYERS"
LIT_VK_LOADER_LAYERS_ENABLE = "VK_LOADER_LAYERS_ENABLE"
LIT_VK_LAYER_KHRONOS_VALIDATION = "VK_LAYER_KHRONOS_validation"
LIT_SHADOW_ROUTE = "shadow_route"
LIT_SOFTWARE = "software"
LIT_N_1280X900 = "1280x900"
LIT_N_960X720 = "960x720"
LIT_IN_FLIGHT_RANGES_32 = "in-flight ranges 32"
LIT_IN_FLIGHT_RANGES_2 = "in-flight ranges 2"
LIT_SELECTED = " selected "
LIT_HYBRID = "hybrid"
LIT_BASELINE_EXECUTABLE = "--baseline-executable"
LIT_BASELINE_RUNTIME = "--baseline-runtime"
LIT_AR = "ar"
LIT_BASELINE_SOURCE_MANIFEST = "--baseline-source-manifest"
LIT_AS_JSON = "as.json"
LIT_CANDIDATE_EXECUTABLE = "--candidate-executable"
LIT_CANDIDATE_RUNTIME = "--candidate-runtime"
LIT_BR = "br"
LIT_CANDIDATE_SOURCE_MANIFEST = "--candidate-source-manifest"
LIT_BS_JSON = "bs.json"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_LOGGER = "logger"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_OUTPUT = "output"
LIT_BLOCKS = "--blocks"
LIT_NAN = "nan"
LIT_RENDER_OPAQUE_REGULAR = "render.opaque_regular"
LIT_RENDER_DEFERRED_COMPOSITE = "render.deferred_composite"
LIT_RENDER_DEFERRED_PRESENT = "render.deferred_present"
LIT_SECONDARY_SCOPE = "secondary_scope"
LIT_RENDER_SHADOW_VISIBILITY = "render.shadow_visibility"
LIT_STATUS = "status"
LIT_RESOLVED_GPU_TIME_REDUCTION = "resolved_gpu_time_reduction"
LIT_CONTROLS = "controls"
LIT_CONTROL_DRIFT = "control_drift"
LIT_CONTROL_UNCERTAIN = "control_uncertain"
LIT_EVERY_PLANNED_TRIAL = "every planned trial"
LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE = "ShadowTimingProbe: indirect response hemi-ambient"
LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN = "SoftShadowTestSmokeProject: shutdown"
LIT_SHADOW_ZERO_EXTENT = "shadow-zero-extent"
LIT_SHADOW_FINITE_EXTENT = "shadow-finite-extent"
LIT_NWB_SOFT_SHADOW_TEST_ANGLE = "NWB_SOFT_SHADOW_TEST_ANGLE"
LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS = "NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS"
LIT_NWB_SOFT_SHADOW_TEST_TIMING = "NWB_SOFT_SHADOW_TEST_TIMING"
LIT_SCREEN = "screen"
LIT_REFLECTIONSMOKEPROJECT_SHUTDOWN = "ReflectionSmokeProject: shutdown"
LIT_REFLECTION_ROUGH_SPATIAL = "reflection-rough-spatial"
LIT_REFLECTION_MIRROR_SPATIAL = "reflection-mirror-spatial"
LIT_REFLECTION_ROUGH_FILTERED = "reflection-rough-filtered"
LIT_REFLECTION_SCREEN_DEPTH = "reflection-screen-depth"
LIT_REFLECTION_OPTICAL_INSIDE = "reflection-optical-inside"
LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES = "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES"
LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO = "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"
LIT_NEW_TIMING_TXT = "new_timing.txt"
LIT_N_16 = "16"
LIT_INACTIVE_REFLECTION_SCOPES = "inactive reflection scopes"
LIT_WORKLOAD = "--workload"
LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG = "--candidate-hardware-dispatches-per-range"
LIT_MAIN = "__main__"


def log_text(route=LIT_HARDWARE):
    natural = next(message for message, name in benchmark.SHADOW_ROUTES.items() if name == route)
    return "\n".join((natural, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA if route == LIT_HARDWARE else LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA, LIT_AVBOITTIMINGPROBE_IN_FLIGHT_RANGES_32, LIT_AVBOITTIMINGPROBE_RENDER_UNFOCUSED_1,
        "TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created", LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG, LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU, LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED,
        LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN))


def soft_shadow_log_text(workload, route=LIT_HARDWARE):
    values = dict(workload.environment_overrides)
    return "\n".join(("ShadowTimingProbe: in-flight ranges 32", "ShadowTimingProbe: render unfocused 1", "ShadowTimingProbe: caustic emission 0", LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE, f"ShadowTimingProbe: natural shadow route {route}",
        LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA if route == LIT_HARDWARE else LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA,
        f"ShadowTimingProbe: source extents angular={values[LIT_NWB_SOFT_SHADOW_TEST_ANGLE]} radius={values[LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS]}",
        "SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius=0 rad", LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG, LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU, LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED,
        LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN))


def reflection_log_text(workload):
    policy = workload.reflection_policy
    route = "screen-space" if policy.variant.mode == LIT_SCREEN else LIT_HARDWARE
    lines = [f"ReflectionSmokeProject: case {policy.family} created", f"ReflectionSmokeProject: reflection mode {policy.variant.mode}", f"ReflectionSmokeProject: hardware ray budget {policy.ray_budget}", "ReflectionSmokeProject: screen feedback 0",
        f"ReflectionSmokeProject: screen steps {policy.screen_steps}", "ReflectionSmokeProject: timing render unfocused 1", "ReflectionSmokeProject: timing in-flight ranges 32", "ReflectionSmokeProject: timing depth mip count 10",
        "ReflectionSmokeProject: hardware available", LIT_REFLECTIONSMOKEPROJECT_SHUTDOWN, f"Reflection resolve: {route}", LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU, "RendererSystem: material 'receiver' selected CS + PS through compute emulation",
        "RendererSystem: deferred rendering targets ready (960x720, samples=1)"]
    if policy.family.startswith("optical_"):
        lines.append(f"ReflectionSmokeProject: optical query limit {policy.optical_queries}")
    return "\n".join(lines)


class WorkloadPolicyTests(unittest.TestCase):
    def test_inherited_smoke_capture_diagnostics_and_pose_are_cleared(self):
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        inherited = {LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP, LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_NWB_CAUSTIC_SMOKE_ENABLED: "1", "NWB_TRANSPARENT_CSG_DISABLE_CUTTER": "1",
            LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE: "2", LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "5", LIT_NWB_GPU_TIMING_FILE: LIT_OLD_TXT, LIT_PRESERVED: LIT_YES}
        env, overrides = benchmark.configure_environment(inherited, workload, Path(LIT_TIMING_TXT))
        self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **dict(workload.environment_overrides), LIT_NWB_GPU_TIMING_FILE: LIT_TIMING_TXT})
        self.assertEqual(overrides[LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE], "0")
        self.assertEqual(inherited[LIT_NWB_GPU_TIMING_FILE], LIT_OLD_TXT)

    def test_explicit_validation_is_rejected_not_silently_disabled(self):
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        for key in (LIT_VK_INSTANCE_LAYERS, LIT_VK_LOADER_LAYERS_ENABLE):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: LIT_VK_LAYER_KHRONOS_VALIDATION}, workload, Path("t"))

    def test_logs_require_actual_route_extent_policy_device_and_shutdown(self):
        self.assertEqual(benchmark.device_material_signature(log_text()), benchmark.device_material_signature(log_text().replace("\n", "\r\n")))
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        expected = benchmark.transparent_multi_log(log_text(), workload, True)
        self.assertEqual(expected[LIT_SHADOW_ROUTE], LIT_HARDWARE)
        self.assertEqual(expected, benchmark.transparent_multi_log(log_text().replace("\n", "\r\n"), workload, True))
        software = benchmark.transparent_multi_log(log_text(LIT_SOFTWARE), workload, False)
        self.assertEqual(software[LIT_SHADOW_ROUTE], LIT_SOFTWARE)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.transparent_multi_log(log_text(LIT_SOFTWARE), workload, True)
        for altered in (log_text().replace(LIT_N_1280X900, LIT_N_960X720), log_text().replace(LIT_IN_FLIGHT_RANGES_32, LIT_IN_FLIGHT_RANGES_2), log_text().replace("render unfocused 1", "render unfocused 0"),
            log_text().replace(LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN, ""), log_text() + "\nAvboitTimingProbe: in-flight ranges 32", log_text() + "\n[ERROR]: rejected",
            log_text() + "\nFramebufferCapture: capture ready", log_text().replace("GraphicsRuntime: created device", "device")):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.transparent_multi_log(altered, workload, True)

    def test_material_signature_preserves_mixed_routes_independent_of_creation_order(self):
        indexed = "RendererSystem: material 'glass' selected VertexIndexed + PS from persistent object-space geometry"
        compute = LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED
        device = LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU
        mixed = benchmark.device_material_signature("\n".join((device, compute, indexed, compute)))
        reordered = benchmark.device_material_signature("\n".join((device, indexed, compute)))
        self.assertEqual(mixed, reordered)
        self.assertEqual(mixed["material_routes"]["glass"], [compute.split(LIT_SELECTED)[1], indexed.split(LIT_SELECTED)[1]])
        self.assertNotEqual(mixed, benchmark.device_material_signature("\n".join((device, indexed))))
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.device_material_signature(device)

    def test_current_hardware_route_requires_dispatch_and_rejects_retired_routes(self):
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        for altered in (log_text().replace(marker, ""), log_text() + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.transparent_multi_log(altered, workload, True)
        retired = log_text().replace(LIT_NATURAL_HARDWARE_SHADOW_ROUTE, LIT_NATURAL_HYBRID_SHADOW_ROUTE)
        retired_route = next(line for line in retired.splitlines() if LIT_NATURAL_HYBRID_SHADOW_ROUTE in line)
        for altered in (retired, log_text() + "\n" + retired_route):
            for require_hardware in (False, True):
                with self.subTest(hardware=require_hardware), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.transparent_multi_log(altered, workload, require_hardware)

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
        return replace(benchmark.workloads()[LIT_TRANSPARENT_MULTI], **values)

    def test_shadow_target_change_is_not_misclassified_as_control_drift(self):
        _, orders, trials = trial_matrix()
        workload = self.shadow_workload()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_MEAN_MS] = .2
                trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_TOTAL_MS] = .2 * trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_GPU_SAMPLES]
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result[LIT_STATUS], LIT_RESOLVED_GPU_TIME_REDUCTION)

    def test_composite_and_present_drift_remain_controls(self):
        for changed_scope in (LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT):
            _, orders, trials = trial_matrix()
            for trial in trials:
                if trial[LIT_ARM] == LIT_CANDIDATE:
                    trial[LIT_SCOPES][changed_scope][LIT_MEAN_MS] = .8
            with self.subTest(scope=changed_scope):
                result = benchmark.compare_trials(trials, orders, self.shadow_workload())
                self.assertEqual(result[LIT_STATUS], LIT_CONTROL_DRIFT)
                self.assertTrue(result[LIT_CONTROLS][changed_scope]["material_drift"])

    def test_uncertain_present_control_cannot_claim_reduction(self):
        _, orders, trials = trial_matrix()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][LIT_RENDER_DEFERRED_PRESENT][LIT_MEAN_MS] += .2 if trial[LIT_BLOCK] % 2 else -.2
        result = benchmark.compare_trials(trials, orders, self.shadow_workload())
        self.assertEqual(result[LIT_STATUS], LIT_CONTROL_UNCERTAIN)

    def test_empty_duplicate_or_mutable_controls_are_rejected(self):
        for controls in ((), (LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_OPAQUE_REGULAR), [LIT_RENDER_OPAQUE_REGULAR]):
            with self.subTest(controls=controls), self.assertRaisesRegex(benchmark.SmokeFailure, "nonempty unique tuple"):
                self.shadow_workload(control_scopes=controls)

    def test_unobserved_control_is_rejected(self):
        with self.assertRaisesRegex(benchmark.SmokeFailure, "not an observed scope"):
            self.shadow_workload(control_scopes=("render.not_measured",))

    def test_primary_and_secondary_targets_cannot_be_controls(self):
        for control in (benchmark.FRAME, LIT_RENDER_SHADOW_VISIBILITY):
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
            benchmark.compare_trials(trials[:-1], orders, self.shadow_workload())


class ShadowWorkloadPolicyTests(unittest.TestCase):
    def test_shadow_environment_clears_inherited_capture_and_extent_policy(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        env, overrides = benchmark.configure_environment({LIT_NWB_SOFT_SHADOW_TEST_ANGLE: "0.2", LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS: "1", LIT_NWB_SOFT_SHADOW_TEST_TIMING: "0",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "360", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP, LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_PRESERVED: LIT_YES}, workload, Path(LIT_TIMING_TXT))
        self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **dict(workload.environment_overrides), LIT_NWB_GPU_TIMING_FILE: LIT_TIMING_TXT})

    def test_shadow_logs_require_actual_policy_route_extent_and_lifecycle(self):
        for name in (LIT_SHADOW_ZERO_EXTENT, LIT_SHADOW_FINITE_EXTENT):
            workload = benchmark.workloads()[name]
            text = soft_shadow_log_text(workload)
            expected = benchmark.soft_shadow_log(text, workload, True)
            self.assertEqual(expected, benchmark.soft_shadow_log(text.replace("\n", "\r\n"), workload, True))
            for altered in (text.replace(LIT_IN_FLIGHT_RANGES_32, LIT_IN_FLIGHT_RANGES_2), text.replace("caustic emission 0", "caustic emission 1"), text.replace(LIT_N_1280X900, LIT_N_960X720),
                text.replace(LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN, ""), text + "\nShadowTimingProbe: render unfocused 0", text.replace("natural shadow route hardware", "natural shadow route software")):
                with self.subTest(name=name, text=altered), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.soft_shadow_log(altered, workload, True)

    def test_shadow_hardware_dispatch_evidence_is_required_and_retired_routes_are_rejected(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        text = soft_shadow_log_text(workload)
        for altered in (text.replace(marker, ""), text + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(altered, workload, True)
        retired = soft_shadow_log_text(workload, LIT_HYBRID)
        retired_route = f"ShadowTimingProbe: natural shadow route {LIT_HYBRID}"
        for altered in (retired, text + "\n" + retired_route):
            for require_hardware in (False, True):
                with self.subTest(text=altered, require_hardware=require_hardware), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.soft_shadow_log(altered, workload, require_hardware)
        software_text = soft_shadow_log_text(workload, LIT_SOFTWARE)
        software = benchmark.soft_shadow_log(software_text, workload, False)
        self.assertEqual(software[LIT_SHADOW_ROUTE], LIT_SOFTWARE)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.soft_shadow_log(software_text, workload, True)

    def test_shadow_logs_require_exact_material_indirect_response(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        text = soft_shadow_log_text(workload)
        marker = LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE
        self.assertEqual(benchmark.soft_shadow_log(text, workload, True)["indirect_response"], "hemi-ambient")
        for altered in (text.replace(marker, ""), text.replace(marker, "ShadowTimingProbe: indirect response surfel"), text + "\n" + marker, text + "\nShadowTimingProbe: indirect response surfel"):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(altered, workload, True)

    def test_shadow_zero_policy_does_not_accept_tiny_nonzero_or_nonfinite(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        text = soft_shadow_log_text(workload)
        for value in ("0.000000001", LIT_NAN, "inf", "-inf"):
            with self.subTest(value=value), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(text.replace("angular=0 radius=0", f"angular={value} radius=0"), workload, True)

    def test_shadow_logs_reject_validation_capture_and_fallback_emission(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        for marker in ("FramebufferCapture: ready", "Vulkan: enabled validation layer", LIT_VK_LAYER_KHRONOS_VALIDATION, "render submission suspended", "retaining all-lit visibility", "preserving opaque visibility"):
            with self.subTest(marker=marker), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(soft_shadow_log_text(workload) + "\n" + marker, workload, True)

    def test_shadow_coverage_requires_each_phase_and_rejects_caustic_work(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        values = scopes(workload)
        benchmark.validate_coverage(values, workload, 6, 100)
        for name in benchmark.SHADOW_PHASES:
            missing = copy.deepcopy(values)
            del missing[name]
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(missing, workload, 6, 100)
        for name in benchmark.SHADOW_INACTIVE:
            unexpected = copy.deepcopy(values)
            unexpected[name] = dict(values[benchmark.FRAME])
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_inactive_scopes(unexpected, workload)


class ReflectionWorkloadTests(unittest.TestCase):
    def test_reflection_environment_reuses_fixed_production_controls_after_clearing_inheritance(self):
        inherited = {LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES: "1", LIT_NWB_AVBOIT_SMOKE_TIMING: "1", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP,
            LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO: "5", LIT_PRESERVED: LIT_YES}
        for workload in benchmark.workloads().values():
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
        workload = benchmark.workloads()[LIT_REFLECTION_ROUGH_SPATIAL]
        for key in (LIT_VK_INSTANCE_LAYERS, LIT_VK_LOADER_LAYERS_ENABLE):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: "validation"}, workload, Path("timing"))

    def test_screen_depth_rejects_incomplete_or_excess_mip_coverage(self):
        workload = benchmark.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        for multiplier in (1, 9, 11):
            changed = scopes(workload)
            changed[benchmark.reflection.DEPTH][LIT_GPU_SAMPLES] = 200 * multiplier
            with self.subTest(multiplier=multiplier), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_mirror_rejects_missing_spatial_range(self):
        workload = benchmark.workloads()[LIT_REFLECTION_MIRROR_SPATIAL]
        changed = scopes(workload)
        del changed[benchmark.reflection.SPATIAL]
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_coverage(changed, workload, 6, 100)

    def test_filtered_retains_temporal_one_per_frame_and_both_controls(self):
        workload = benchmark.workloads()[LIT_REFLECTION_ROUGH_FILTERED]
        for missing in (benchmark.reflection.TEMPORAL, benchmark.reflection.SPATIAL, benchmark.CONTROLS[1]):
            changed = scopes(workload)
            del changed[missing]
            with self.subTest(scope=missing), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_inactive_ranges_are_rejected_even_outside_the_retained_window(self):
        for workload in benchmark.workloads().values():
            for inactive in workload.inactive_scopes:
                with self.subTest(workload=workload.name, scope=inactive), self.assertRaisesRegex(benchmark.SmokeFailure, LIT_INACTIVE_REFLECTION_SCOPES):
                    benchmark.validate_inactive_scopes({inactive: {LIT_GPU_SAMPLES: 1}}, workload)

    def test_inactive_hashed_scope_is_decoded_and_rejected(self):
        workload = benchmark.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        symbols = benchmark.load_name_symbols(None, workload.observed_scopes)
        token = next(token for token, name in symbols.items() if name == benchmark.reflection.HARDWARE)
        text = f"=== interval: 1 frames / 0.5s ===\n  {token}: window_avg_ms=1 window_min_ms=1 window_max_ms=1 published_windows=1 total_ms=1 gpu_samples=1 sample_avg_ms=1\n"
        parsed = benchmark.summarize_intervals(benchmark.parse_intervals(text, symbols, finalized=True))
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_INACTIVE_REFLECTION_SCOPES):
            benchmark.validate_inactive_scopes(parsed, workload)

    def test_reflection_runtime_signature_uses_production_log_validation(self):
        for workload in benchmark.workloads().values():
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
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_INSIDE]
        text = reflection_log_text(workload)
        for changed in (text.replace("optical query limit 16", "optical query limit 8"), text + "\nReflectionSmokeProject: optical query limit 16"):
            with self.assertRaisesRegex(benchmark.SmokeFailure, "optical query limit"):
                workload.validate_log(changed, workload, True)

    def test_declared_native_dispatch_count_never_changes_range_coverage(self):
        common = [LIT_BASELINE_EXECUTABLE, LIT_A, LIT_BASELINE_RUNTIME, LIT_AR, LIT_BASELINE_SOURCE_MANIFEST, LIT_AS_JSON, LIT_CANDIDATE_EXECUTABLE, LIT_B, LIT_CANDIDATE_RUNTIME, LIT_BR, LIT_CANDIDATE_SOURCE_MANIFEST, LIT_BS_JSON,
            LIT_LOGSERVER_EXECUTABLE, LIT_LOGGER, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        for count in (2, 169):
            args = benchmark.parse_args(common + [LIT_WORKLOAD, LIT_REFLECTION_OPTICAL_CSG_CAP, LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, str(count)])
            workload = benchmark.workloads()[args.workload]
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
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CSG_CAP]
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
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result[benchmark.LIT_MEANS_MS][LIT_CANDIDATE][benchmark.reflection.HARDWARE], 2351)
        self.assertEqual(result["secondary_per_frame_work"][benchmark.LIT_MEANS_MS][LIT_CANDIDATE], 2351)
        self.assertEqual(result["secondary"][LIT_MEAN_MS], 2301)
        self.assertEqual(result["frame"][LIT_MEAN_MS], 2300)

    def test_depth_secondary_rejects_an_incomplete_trial_matrix(self):
        workload = benchmark.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        _, orders, trials = trial_matrix()
        for trial in trials:
            frame_ms = 5 if trial[LIT_ARM] == LIT_BASELINE else 4.5
            trial[LIT_SCOPES] = scopes(workload, frame_ms=frame_ms)
            trial[LIT_WALL_FRAME] = {LIT_ELIGIBLE: True, LIT_MEAN_FRAME_MS: frame_ms, LIT_MEAN_FPS: 1000 / frame_ms}
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][benchmark.reflection.DEPTH][LIT_MEAN_MS] = .4
                trial[LIT_SCOPES][benchmark.reflection.DEPTH][LIT_TOTAL_MS] = .4 * 200 * 10
        benchmark.compare_trials(trials, orders, workload)
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            benchmark.compare_trials(trials[:-1], orders, workload)


if __name__ == LIT_MAIN:
    unittest.main()
