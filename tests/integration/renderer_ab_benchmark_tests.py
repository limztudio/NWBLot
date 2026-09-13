#!/usr/bin/env python3
"""Pure CPU tests for frozen-arm renderer A/B acquisition contracts."""

import collections
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import struct
from types import SimpleNamespace
from unittest.mock import patch
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark
from smoke_volume_identity import volume_segment_filename


def scopes(workload, frames=200, frame_ms=5.0):
    return {name: {"total_ms": (frame_ms if name == benchmark.FRAME else .5) * frames * multiplier,
        "gpu_samples": frames * multiplier, "reports": 6,
        "mean_ms": frame_ms if name == benchmark.FRAME else .5}
        for name, multiplier in workload.scope_multipliers}


def log_text(route="hybrid"):
    natural = next(message for message, name in benchmark.SHADOW_ROUTES.items() if name == route)
    return "\n".join((natural, "AvboitTimingProbe: in-flight ranges 32",
        "AvboitTimingProbe: render unfocused 1",
        "TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created",
        "RendererSystem: deferred rendering targets ready (1280x900, samples=1)",
        "Vulkan: created device 'Example GPU'",
        "RendererSystem: material 'glass' selected CS + PS through compute emulation",
        "TransparentMultiSmokeProject: shutdown"))


def source_manifest(directory, text="source", revision="revision"):
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / "source.cpp"
    source.write_text(text, encoding="utf-8")
    manifest = directory / "source.json"
    manifest.write_text(json.dumps({"revision": revision,
        "files": {source.name: hashlib.sha256(source.read_bytes()).hexdigest()}}), encoding="utf-8")
    return manifest


def make_arm(directory, name):
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / "renderer"
    executable.write_bytes(b"binary")
    runtime = directory / "runtime"
    (runtime / "res").mkdir(parents=True)
    (runtime / "res" / "authored.vol").write_bytes(b"authored asset")
    return benchmark.Arm(name, executable, runtime, source_manifest(directory / "source"))


def trial_matrix(frame_delta=-.5):
    workload = benchmark.workloads()["transparent-multi"]
    arms = (benchmark.Arm("baseline", Path("a"), Path("a_runtime"), Path("a.json")),
        benchmark.Arm("candidate", Path("b"), Path("b_runtime"), Path("b.json")))
    orders = [[arm.name for arm in row] for row in benchmark.balanced_orders(arms, 8, 0)]
    trials = []
    for block, row in enumerate(orders):
        for position, arm in enumerate(row):
            current = scopes(workload, frame_ms=5 + block * .01 + (frame_delta if arm == "candidate" else 0))
            trials.append({"arm": arm, "block": block, "position": position, "scopes": current})
    return workload, orders, trials


class CoverageTests(unittest.TestCase):
    def test_actual_gpu_counts_ignore_cpu_frame_and_displayed_average_fields(self):
        text = ("=== interval: 99999 frames / 0.5s ===\n"
            "  render.frame: avg=123 samples=1 total_ms=90 gpu_samples=10\n"
            "=== interval: 1 frames / 0.5s ===\n"
            "  render.frame: avg=456 samples=1 total_ms=100 gpu_samples=100\n")
        result = benchmark.summarize_intervals(benchmark.parse_intervals(text, finalized=True))
        self.assertEqual(result[benchmark.FRAME]["gpu_samples"], 110)
        self.assertAlmostEqual(result[benchmark.FRAME]["mean_ms"], 190 / 110)

    def test_partial_live_tail_is_not_counted(self):
        text = ("=== interval: 1 frames / 0.5s ===\n"
            "  render.frame: total_ms=4 gpu_samples=1\n"
            "=== interval: 1 frames / 0.5s ===\n  render.frame: total_ms=")
        self.assertEqual(len(benchmark.parse_intervals(text)), 1)

    def test_all_avboit_and_control_ranges_are_required(self):
        workload = benchmark.workloads()["transparent-multi"]
        complete = scopes(workload)
        self.assertEqual(len(workload.scopes), 12)
        benchmark.validate_coverage(complete, workload, 6, 100)
        for name in workload.scopes:
            with self.subTest(scope=name):
                missing = copy.deepcopy(complete)
                del missing[name]
                with self.assertRaisesRegex(benchmark.SmokeFailure, "missing completed GPU scopes"):
                    benchmark.validate_coverage(missing, workload, 6, 100)

    def test_ratio_boundary_and_sparse_reports_are_rejected(self):
        workload = benchmark.workloads()["transparent-multi"]
        for frames, tolerance in ((100, 2), (1000, 20)):
            for scope in (benchmark.OCCUPANCY, benchmark.CONTROLS[0]):
                for sign in (-1, 1):
                    value = scopes(workload, frames)
                    value[scope]["gpu_samples"] += sign * tolerance
                    benchmark.validate_coverage(value, workload, 6, 100)
                    value[scope]["gpu_samples"] += sign
                    with self.assertRaisesRegex(benchmark.SmokeFailure, "sample ratio"):
                        benchmark.validate_coverage(value, workload, 6, 100)
        value = scopes(workload)
        value[benchmark.OCCUPANCY]["reports"] = 3
        with self.assertRaisesRegex(benchmark.SmokeFailure, "publications"):
            benchmark.validate_coverage(value, workload, 6, 100)
        with self.assertRaisesRegex(benchmark.SmokeFailure, "insufficient"):
            benchmark.validate_coverage(scopes(workload, 99), workload, 6, 100)


class WorkloadPolicyTests(unittest.TestCase):
    def test_inherited_smoke_capture_diagnostics_and_pose_are_cleared(self):
        workload = benchmark.workloads()["transparent-multi"]
        inherited = {"NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old.bmp", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1",
            "NWB_CAUSTIC_SMOKE_ENABLED": "1", "NWB_TRANSPARENT_CSG_DISABLE_CUTTER": "1",
            "NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "2", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "5",
            "NWB_GPU_TIMING_FILE": "old.txt", "PRESERVED": "yes"}
        env, overrides = benchmark.configure_environment(inherited, workload, Path("timing.txt"))
        self.assertEqual(env, {"PRESERVED": "yes", **dict(workload.environment_overrides),
            "NWB_GPU_TIMING_FILE": "timing.txt"})
        self.assertEqual(overrides["NWB_AVBOIT_SMOKE_TIMING"], "1")
        self.assertEqual(overrides["NWB_TRANSPARENT_MULTI_SPIN_ANGLE"], "0")
        self.assertEqual(inherited["NWB_GPU_TIMING_FILE"], "old.txt")

    def test_explicit_validation_is_rejected_not_silently_disabled(self):
        workload = benchmark.workloads()["transparent-multi"]
        for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: "VK_LAYER_KHRONOS_validation"}, workload, Path("t"))

    def test_logs_require_actual_route_extent_policy_device_and_shutdown(self):
        self.assertEqual(benchmark.device_material_signature(log_text()),
            benchmark.device_material_signature(log_text().replace("\n", "\r\n")))
        workload = benchmark.workloads()["transparent-multi"]
        expected = benchmark.transparent_multi_log(log_text(), workload, True)
        self.assertEqual(expected["shadow_route"], "hybrid")
        self.assertEqual(expected, benchmark.transparent_multi_log(log_text().replace("\n", "\r\n"), workload, True))
        software = benchmark.transparent_multi_log(log_text("software"), workload, False)
        self.assertEqual(software["shadow_route"], "software")
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.transparent_multi_log(log_text("software"), workload, True)
        for altered in (log_text().replace("1280x900", "960x720"),
            log_text().replace("in-flight ranges 32", "in-flight ranges 2"),
            log_text().replace("render unfocused 1", "render unfocused 0"),
            log_text().replace("TransparentMultiSmokeProject: shutdown", ""),
            log_text() + "\nAvboitTimingProbe: in-flight ranges 32", log_text() + "\n[ERROR]: rejected",
            log_text() + "\nFramebufferCapture: capture ready", log_text().replace("Vulkan: created device", "device")):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.transparent_multi_log(altered, workload, True)

    def test_cli_defaults_and_lower_coverage_or_unbalanced_plans(self):
        common = ["--baseline-executable", "a", "--baseline-runtime", "ar", "--baseline-source-manifest", "as.json",
            "--candidate-executable", "b", "--candidate-runtime", "br", "--candidate-source-manifest", "bs.json",
            "--logserver-executable", "logger", "--output-directory", "output"]
        args = benchmark.parse_args(common)
        self.assertEqual((args.blocks, args.warmup_intervals, args.sample_intervals, args.minimum_frame_samples, args.timeout),
            (8, 2, 6, 100, 90))
        for extra in (("--blocks", "7"), ("--blocks", "6"), ("--warmup-intervals", "1"),
            ("--sample-intervals", "5"), ("--minimum-frame-samples", "99"), ("--timeout", "nan"),
            ("--application-arg=--gpudbg",)):
            with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + list(extra))


class WorkloadControlSelectionTests(unittest.TestCase):
    SHADOW_CONTROLS = ("render.opaque_regular", "render.deferred_lighting",
        "render.deferred_composite", "render.deferred_present")

    def shadow_workload(self, **changes):
        from dataclasses import replace
        values = {"name": "test-shadow-controls", "secondary_scope": "render.shadow_visibility",
            "control_scopes": self.SHADOW_CONTROLS}
        values.update(changes)
        return replace(benchmark.workloads()["transparent-multi"], **values)

    def test_existing_workloads_keep_original_control_policy(self):
        for workload in benchmark.workloads().values():
            with self.subTest(workload=workload.name):
                expected = benchmark.SHADOW_CONTROLS if workload.validate_log is benchmark.soft_shadow_log else benchmark.CONTROLS
                if workload.validate_log is benchmark.caustic_log:
                    expected = (*benchmark.CONTROLS, *benchmark.OBSERVATIONS, benchmark.caustic.PHOTONS)
                self.assertEqual(workload.control_scopes, expected)

    def test_shadow_target_change_is_not_misclassified_as_control_drift(self):
        _, orders, trials = trial_matrix()
        workload = self.shadow_workload()
        for trial in trials:
            if trial["arm"] == "candidate":
                trial["scopes"]["render.shadow_visibility"]["mean_ms"] = .2
                trial["scopes"]["render.shadow_visibility"]["total_ms"] = .2 * trial["scopes"]["render.shadow_visibility"]["gpu_samples"]
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result["status"], "resolved_gpu_time_reduction")
        self.assertEqual(tuple(result["controls"]), self.SHADOW_CONTROLS)
        self.assertAlmostEqual(result["scope_deltas"]["render.shadow_visibility"]["mean_ms"], -.3)

    def test_composite_and_present_drift_remain_controls(self):
        for changed_scope in ("render.deferred_composite", "render.deferred_present"):
            _, orders, trials = trial_matrix()
            for trial in trials:
                if trial["arm"] == "candidate":
                    trial["scopes"][changed_scope]["mean_ms"] = .8
            with self.subTest(scope=changed_scope):
                result = benchmark.compare_trials(trials, orders, self.shadow_workload())
                self.assertEqual(result["status"], "control_drift")
                self.assertTrue(result["controls"][changed_scope]["material_drift"])

    def test_uncertain_present_control_cannot_claim_reduction(self):
        _, orders, trials = trial_matrix()
        for trial in trials:
            if trial["arm"] == "candidate":
                trial["scopes"]["render.deferred_present"]["mean_ms"] += .2 if trial["block"] % 2 else -.2
        result = benchmark.compare_trials(trials, orders, self.shadow_workload())
        self.assertEqual(result["status"], "control_uncertain")

    def test_empty_duplicate_or_mutable_controls_are_rejected(self):
        for controls in ((), ("render.opaque_regular", "render.opaque_regular"), ["render.opaque_regular"]):
            with self.subTest(controls=controls), self.assertRaisesRegex(benchmark.SmokeFailure, "nonempty unique tuple"):
                self.shadow_workload(control_scopes=controls)

    def test_unobserved_control_is_rejected(self):
        with self.assertRaisesRegex(benchmark.SmokeFailure, "not an observed scope"):
            self.shadow_workload(control_scopes=("render.not_measured",))

    def test_primary_and_secondary_targets_cannot_be_controls(self):
        for control in (benchmark.FRAME, "render.shadow_visibility"):
            with self.subTest(control=control), self.assertRaisesRegex(benchmark.SmokeFailure, "target cannot also be a control"):
                self.shadow_workload(control_scopes=(control,))

    def test_shadow_controls_still_require_complete_scope_coverage(self):
        workload = self.shadow_workload()
        for control in self.SHADOW_CONTROLS:
            values = scopes(workload)
            del values[control]
            with self.subTest(control=control), self.assertRaisesRegex(benchmark.SmokeFailure, "missing completed GPU scopes"):
                benchmark.validate_coverage(values, workload, 6, 100)

    def test_per_workload_controls_do_not_relax_complete_trial_requirement(self):
        _, orders, trials = trial_matrix()
        with self.assertRaisesRegex(benchmark.SmokeFailure, "every planned trial"):
            benchmark.compare_trials(trials[:-1], orders, self.shadow_workload())


def soft_shadow_log_text(workload, route="hybrid"):
    values = dict(workload.environment_overrides)
    return "\n".join(("ShadowTimingProbe: in-flight ranges 32", "ShadowTimingProbe: render unfocused 1",
        "ShadowTimingProbe: caustic emission 0", "ShadowTimingProbe: indirect response hemi-ambient",
        f"ShadowTimingProbe: natural shadow route {route}",
        f"ShadowTimingProbe: source extents angular={values['NWB_SOFT_SHADOW_TEST_ANGLE']} radius={values['NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS']}",
        "SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius=0 rad",
        "RendererSystem: deferred rendering targets ready (1280x900, samples=1)",
        "Vulkan: created device 'Example GPU'", "RendererSystem: material 'glass' selected CS + PS through compute emulation",
        "SoftShadowTestSmokeProject: shutdown"))


class ShadowWorkloadPolicyTests(unittest.TestCase):
    def test_four_extent_workloads_have_exact_scope_and_control_policy(self):
        expected = {"shadow-zero-extent": ("0", "0"), "shadow-finite-extent": ("0.03", "0.15"),
            "shadow-zero-directional": ("0", "0.15"), "shadow-zero-punctual": ("0.03", "0")}
        for name, extents in expected.items():
            workload = benchmark.workloads()[name]
            values = dict(workload.environment_overrides)
            self.assertEqual((values["NWB_SOFT_SHADOW_TEST_ANGLE"], values["NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS"]), extents)
            self.assertEqual(workload.control_scopes, benchmark.SHADOW_CONTROLS)
            self.assertEqual(workload.inactive_scopes, benchmark.SHADOW_INACTIVE)
            self.assertEqual(len(workload.scopes), 13)
            self.assertEqual(set(dict(workload.scope_multipliers).values()), {1})
            self.assertEqual(workload.secondary_scope, "render.shadow_visibility")

    def test_shadow_environment_clears_inherited_capture_and_extent_policy(self):
        workload = benchmark.workloads()["shadow-zero-extent"]
        env, overrides = benchmark.configure_environment({"NWB_SOFT_SHADOW_TEST_ANGLE": "0.2",
            "NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS": "1", "NWB_SOFT_SHADOW_TEST_TIMING": "0",
            "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "360", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old.bmp",
            "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1", "PRESERVED": "yes"}, workload, Path("timing.txt"))
        self.assertEqual(env, {"PRESERVED": "yes", **dict(workload.environment_overrides), "NWB_GPU_TIMING_FILE": "timing.txt"})
        self.assertEqual(overrides["NWB_SOFT_SHADOW_TEST_TIMING"], "1")

    def test_shadow_logs_require_actual_policy_route_extent_and_lifecycle(self):
        for name in ("shadow-zero-extent", "shadow-finite-extent"):
            workload = benchmark.workloads()[name]
            text = soft_shadow_log_text(workload)
            expected = benchmark.soft_shadow_log(text, workload, True)
            self.assertEqual(expected, benchmark.soft_shadow_log(text.replace("\n", "\r\n"), workload, True))
            for altered in (text.replace("in-flight ranges 32", "in-flight ranges 2"),
                text.replace("caustic emission 0", "caustic emission 1"), text.replace("1280x900", "960x720"),
                text.replace("SoftShadowTestSmokeProject: shutdown", ""), text + "\nShadowTimingProbe: render unfocused 0",
                text.replace("natural shadow route hybrid", "natural shadow route software")):
                with self.subTest(name=name, text=altered), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.soft_shadow_log(altered, workload, True)

    def test_shadow_logs_require_exact_material_indirect_response(self):
        workload = benchmark.workloads()["shadow-zero-extent"]
        text = soft_shadow_log_text(workload)
        marker = "ShadowTimingProbe: indirect response hemi-ambient"
        self.assertEqual(benchmark.soft_shadow_log(text, workload, True)["indirect_response"], "hemi-ambient")
        for altered in (text.replace(marker, ""), text.replace(marker, "ShadowTimingProbe: indirect response surfel"),
            text + "\n" + marker, text + "\nShadowTimingProbe: indirect response surfel"):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(altered, workload, True)

    def test_shadow_zero_policy_does_not_accept_tiny_nonzero_or_nonfinite(self):
        workload = benchmark.workloads()["shadow-zero-extent"]
        text = soft_shadow_log_text(workload)
        for value in ("0.000000001", "nan", "inf", "-inf"):
            with self.subTest(value=value), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(text.replace("angular=0 radius=0", f"angular={value} radius=0"), workload, True)

    def test_shadow_logs_reject_validation_capture_and_fallback_emission(self):
        workload = benchmark.workloads()["shadow-zero-extent"]
        for marker in ("FramebufferCapture: ready", "Vulkan: enabled validation layer", "VK_LAYER_KHRONOS_validation",
            "render submission suspended", "retaining all-lit visibility", "preserving opaque visibility"):
            with self.subTest(marker=marker), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(soft_shadow_log_text(workload) + "\n" + marker, workload, True)

    def test_shadow_coverage_requires_each_phase_and_rejects_caustic_work(self):
        workload = benchmark.workloads()["shadow-zero-extent"]
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


class FrozenIdentityTests(unittest.TestCase):
    def test_source_content_and_manifest_are_both_frozen(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = source_manifest(root)
            frozen = benchmark.source_identity(manifest)
            self.assertEqual(frozen["revision"], "revision")
            (root / "source.cpp").write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(benchmark.SmokeFailure, "source bytes"):
                benchmark.source_identity(manifest)
            for document in ([], {"revision": "r", "files": {}}, {"revision": "r", "files": {"x": "invalid"}}):
                manifest.write_text(json.dumps(document), encoding="utf-8")
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.source_identity(manifest)

    def test_only_exact_contiguous_pipeline_cache_segments_can_mutate(self):
        with tempfile.TemporaryDirectory() as temporary:
            arm = make_arm(Path(temporary), "baseline")
            before = benchmark.freeze_arm(arm)
            cache = arm.runtime / "res" / volume_segment_filename("runtime_pipeline_cache", 0)
            cache.write_bytes(b"runtime cache")
            self.assertEqual(before, benchmark.freeze_arm(arm))
            cache.write_bytes(b"updated runtime cache")
            self.assertEqual(before, benchmark.freeze_arm(arm))
            unrelated = arm.runtime / "res" / volume_segment_filename("runtime_pipeline_cache", 2)
            unrelated.write_bytes(b"gap means authored/unknown")
            self.assertNotEqual(before, benchmark.freeze_arm(arm))

    def test_binary_authored_source_and_shared_tool_changes_abort(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            arm = make_arm(root / "arm", "baseline")
            other = make_arm(root / "other", "candidate")
            tool = root / "tool.py"
            tool.write_bytes(b"tool")
            identities = {item.name: benchmark.freeze_arm(item) for item in (arm, other)}
            shared = {str(tool): benchmark.file_identity(tool)}
            benchmark.verify_frozen((arm, other), identities, shared)
            for path in (arm.executable, arm.runtime / "res" / "authored.vol", tool):
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
            first = make_arm(root / "a", "baseline")
            second = make_arm(root / "b", "candidate")
            with self.assertRaisesRegex(benchmark.SmokeFailure, "distinct"):
                benchmark.validate_arm_separation((first, first))
            benchmark.validate_arm_separation((first, second))
            filename = volume_segment_filename("runtime_pipeline_cache", 0)
            first_cache = first.runtime / "res" / filename
            second_cache = second.runtime / "res" / filename
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
    def test_eight_blocks_balance_position_and_retain_every_sample(self):
        workload, orders, trials = trial_matrix()
        positions = collections.Counter((position, arm) for row in orders for position, arm in enumerate(row))
        self.assertEqual(set(positions.values()), {4})
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result["completed_trials"], 16)
        self.assertEqual(result["completed_blocks"], 8)
        self.assertEqual(result["completed_gpu_frames"], 3200)
        self.assertEqual(result["status"], "resolved_gpu_time_reduction")
        self.assertAlmostEqual(result["frame"]["mean_ms"], -.5)
        self.assertEqual(result["secondary_scope"], benchmark.OCCUPANCY)
        self.assertAlmostEqual(result["secondary"]["mean_ms"], 0)

    def test_missing_duplicate_and_wrong_order_trials_are_not_silently_dropped(self):
        workload, orders, trials = trial_matrix()
        changed = copy.deepcopy(trials)
        changed[0]["position"] = 1
        for invalid in (trials[:-1], trials + [trials[0]], changed):
            with self.assertRaisesRegex(benchmark.SmokeFailure, "every planned trial"):
                benchmark.compare_trials(invalid, orders, workload)

    def test_small_effect_and_uncertain_controls_cannot_claim_speedup(self):
        workload, orders, trials = trial_matrix(-.01)
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)["status"], "unresolved")
        workload, orders, trials = trial_matrix(-.5)
        for trial in trials:
            if trial["arm"] == "candidate":
                trial["scopes"][benchmark.CONTROLS[0]]["mean_ms"] += .2 if trial["block"] % 2 else -.2
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result["status"], "control_uncertain")
        for trial in trials:
            if trial["arm"] == "candidate":
                trial["scopes"][benchmark.CONTROLS[0]]["mean_ms"] = .3
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)["status"], "control_drift")


def reflection_log_text(workload):
    policy = workload.reflection_policy
    route = "screen-space" if policy.variant.mode == "screen" else "hardware"
    lines = [f"ReflectionSmokeProject: case {policy.family} created",
        f"ReflectionSmokeProject: reflection mode {policy.variant.mode}",
        f"ReflectionSmokeProject: hardware ray budget {policy.ray_budget}",
        "ReflectionSmokeProject: screen feedback 0",
        f"ReflectionSmokeProject: screen steps {policy.screen_steps}",
        "ReflectionSmokeProject: timing render unfocused 1",
        "ReflectionSmokeProject: timing in-flight ranges 32",
        "ReflectionSmokeProject: timing depth mip count 10",
        "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: shutdown",
        f"Reflection resolve: {route}", "Vulkan: created device 'Example GPU'",
        "RendererSystem: material 'receiver' selected CS + PS through compute emulation",
        "RendererSystem: deferred rendering targets ready (960x720, samples=1)"]
    if policy.family.startswith("optical_"):
        lines.append(f"ReflectionSmokeProject: optical query limit {policy.optical_queries}")
    return "\n".join(lines)


class ReflectionWorkloadTests(unittest.TestCase):
    def test_fixed_six_workloads_pin_settings_target_and_active_ranges(self):
        reflection = benchmark.reflection
        expected = {
            "reflection-rough-spatial": ("rough", "hardware", .4, False, True, reflection.SPATIAL),
            "reflection-mirror-spatial": ("rough", "hardware", 0.0, False, True, reflection.SPATIAL),
            "reflection-rough-filtered": ("rough", "hardware", .4, True, True, reflection.SPATIAL),
            "reflection-screen-depth": ("floor", "screen", 0.0, False, False, reflection.DEPTH),
            "reflection-optical-clear": ("optical_clear", "hardware", 0.0, False, False, reflection.HARDWARE),
            "reflection-optical-inside": ("optical_inside", "hardware", 0.0, False, False, reflection.HARDWARE),
        }
        self.assertEqual(set(benchmark.workloads()), {"transparent-multi", *expected,
            "shadow-zero-extent", "shadow-finite-extent", "shadow-zero-directional", "shadow-zero-punctual",
            "caustic-populated", "caustic-sparse"})
        for name, values in expected.items():
            with self.subTest(workload=name):
                workload = benchmark.workloads()[name]
                policy = workload.reflection_policy
                self.assertEqual((policy.family, policy.variant.mode, policy.roughness,
                    policy.variant.temporal, policy.variant.spatial, workload.secondary_scope), values)
                self.assertEqual((workload.width, workload.height, policy.ray_budget, policy.optical_queries,
                    policy.screen_steps, policy.history_samples, policy.sampling_seed), (960, 720, 1382400, 16, 96, 16, 0))
                self.assertFalse(policy.variant.feedback)
                self.assertEqual(set(workload.observed_scopes), set(reflection.KNOWN_SCOPES))
                benchmark.validate_coverage(scopes(workload), workload, 6, 100)

    def test_reflection_environment_reuses_fixed_production_controls_after_clearing_inheritance(self):
        inherited = {"NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1", "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": "1",
            "NWB_AVBOIT_SMOKE_TIMING": "1", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old.bmp",
            "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "5", "PRESERVED": "yes"}
        for workload in benchmark.workloads().values():
            if workload.reflection_policy is None:
                continue
            with self.subTest(workload=workload.name):
                env, overrides = benchmark.configure_environment(inherited, workload, Path("new_timing.txt"))
                self.assertEqual(env, {"PRESERVED": "yes", **overrides})
                self.assertEqual(overrides, {**dict(workload.environment_overrides), "NWB_GPU_TIMING_FILE": "new_timing.txt"})
                self.assertEqual(overrides["NWB_REFLECTION_SMOKE_DIAGNOSTICS"], "0")
                self.assertEqual(overrides["NWB_REFLECTION_SMOKE_TIMING"], "1")
                self.assertEqual(overrides["NWB_REFLECTION_SMOKE_HISTORY_SAMPLES"], "16")
                self.assertEqual(overrides["NWB_REFLECTION_SMOKE_FEEDBACK"], "0")
                self.assertEqual(overrides["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"], "0.016666667")
                self.assertEqual(overrides["NWB_REFLECTION_SMOKE_ROUGHNESS"], str(workload.reflection_policy.roughness))
        self.assertEqual(inherited["NWB_REFLECTION_SMOKE_DIAGNOSTICS"], "1")

    def test_reflection_cannot_override_explicit_vulkan_validation(self):
        workload = benchmark.workloads()["reflection-rough-spatial"]
        for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: "validation"}, workload, Path("timing"))

    def test_screen_depth_requires_all_ten_mips_and_no_hardware_ranges(self):
        workload = benchmark.workloads()["reflection-screen-depth"]
        self.assertEqual(dict(workload.scope_multipliers)[benchmark.reflection.DEPTH], 10)
        self.assertIn(benchmark.reflection.HARDWARE, workload.inactive_scopes)
        self.assertIn(benchmark.reflection.BUILD_ARGS, workload.inactive_scopes)
        for multiplier in (1, 9, 11):
            changed = scopes(workload)
            changed[benchmark.reflection.DEPTH]["gpu_samples"] = 200 * multiplier
            with self.subTest(multiplier=multiplier), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_mirror_still_requires_spatial_but_no_temporal_range(self):
        workload = benchmark.workloads()["reflection-mirror-spatial"]
        self.assertEqual(dict(workload.scope_multipliers)[benchmark.reflection.SPATIAL], 1)
        self.assertIn(benchmark.reflection.TEMPORAL, workload.inactive_scopes)
        changed = scopes(workload)
        del changed[benchmark.reflection.SPATIAL]
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_coverage(changed, workload, 6, 100)

    def test_filtered_retains_temporal_one_per_frame_and_both_controls(self):
        workload = benchmark.workloads()["reflection-rough-filtered"]
        self.assertEqual(dict(workload.scope_multipliers)[benchmark.reflection.TEMPORAL], 1)
        for missing in (benchmark.reflection.TEMPORAL, benchmark.reflection.SPATIAL, benchmark.CONTROLS[1]):
            changed = scopes(workload)
            del changed[missing]
            with self.subTest(scope=missing), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_inactive_ranges_are_rejected_even_outside_the_retained_window(self):
        for workload in benchmark.workloads().values():
            for inactive in workload.inactive_scopes:
                with self.subTest(workload=workload.name, scope=inactive), self.assertRaisesRegex(
                    benchmark.SmokeFailure, "inactive reflection scopes"):
                    benchmark.validate_inactive_scopes({inactive: {"gpu_samples": 1}}, workload)

    def test_inactive_hashed_scope_is_decoded_and_rejected(self):
        workload = benchmark.workloads()["reflection-screen-depth"]
        symbols = benchmark.load_name_symbols(None, workload.observed_scopes)
        token = next(token for token, name in symbols.items() if name == benchmark.reflection.HARDWARE)
        text = f"=== interval: 1 frames / 0.5s ===\n  {token}: total_ms=1 gpu_samples=1\n"
        parsed = benchmark.summarize_intervals(benchmark.parse_intervals(text, symbols, finalized=True))
        with self.assertRaisesRegex(benchmark.SmokeFailure, "inactive reflection scopes"):
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
                for altered in (text.replace("960x720", "1280x900"),
                    text.replace("timing in-flight ranges 32", "timing in-flight ranges 2"),
                    text + "\nReflectionSmokeProject: screen steps 16", text + "\nVUID-rejected",
                    text + "\nFramebufferCapture: capture ready", text.replace("hardware available", "hardware unavailable"),
                    text.replace("ReflectionSmokeProject: shutdown", "")):
                    with self.assertRaises(benchmark.SmokeFailure):
                        workload.validate_log(altered, workload, True)

    def test_inside_query_cap_is_strict_despite_shared_benchmark_only_checking_clear(self):
        workload = benchmark.workloads()["reflection-optical-inside"]
        text = reflection_log_text(workload)
        for changed in (text.replace("optical query limit 16", "optical query limit 8"),
            text + "\nReflectionSmokeProject: optical query limit 16"):
            with self.assertRaisesRegex(benchmark.SmokeFailure, "optical query limit"):
                workload.validate_log(changed, workload, True)

    def test_declared_native_dispatch_count_never_changes_range_coverage(self):
        common = ["--baseline-executable", "a", "--baseline-runtime", "ar", "--baseline-source-manifest", "as.json",
            "--candidate-executable", "b", "--candidate-runtime", "br", "--candidate-source-manifest", "bs.json",
            "--logserver-executable", "logger", "--output-directory", "output"]
        args = benchmark.parse_args(common + ["--workload", "reflection-optical-clear",
            "--candidate-hardware-dispatches-per-range", "2"])
        self.assertEqual((args.blocks, args.baseline_hardware_dispatches_per_range,
            args.candidate_hardware_dispatches_per_range), (8, 1, 2))
        self.assertTrue(args.require_hardware)
        workload = benchmark.workloads()[args.workload]
        self.assertEqual(dict(workload.scope_multipliers)[benchmark.reflection.HARDWARE], 1)
        value = scopes(workload)
        value[benchmark.reflection.HARDWARE]["gpu_samples"] *= 2
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_coverage(value, workload, 6, 100)
        for name in ("transparent-multi", "reflection-screen-depth", "reflection-rough-filtered"):
            with self.subTest(workload=name), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + ["--workload", name, "--candidate-hardware-dispatches-per-range", "2"])

    def test_depth_secondary_reports_per_mip_and_aggregate_work_without_frame_attribution(self):
        workload = benchmark.workloads()["reflection-screen-depth"]
        _, orders, trials = trial_matrix()
        for trial in trials:
            trial["scopes"] = scopes(workload, frame_ms=5 if trial["arm"] == "baseline" else 4.5)
            if trial["arm"] == "candidate":
                trial["scopes"][benchmark.reflection.DEPTH]["mean_ms"] = .4
                trial["scopes"][benchmark.reflection.DEPTH]["total_ms"] = .4 * 200 * 10
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result["secondary_scope"], benchmark.reflection.DEPTH)
        self.assertAlmostEqual(result["secondary"]["mean_ms"], -.1)
        self.assertAlmostEqual(result["secondary_per_frame_work"]["delta"]["mean_ms"], -1.0)
        self.assertEqual(result["completed_trials"], 16)
        self.assertEqual(result["completed_blocks"], 8)
        self.assertEqual(result["completed_gpu_frames"], 3200)
        with self.assertRaisesRegex(benchmark.SmokeFailure, "every planned trial"):
            benchmark.compare_trials(trials[:-1], orders, workload)


def caustic_log_text(preset="populated", enabled=True, capture=False):
    distance = benchmark.caustic.PRESETS[preset]
    text = "\n".join((
        "Vulkan: created device 'Example GPU'",
        "RendererSystem: material 'glass' selected CS + PS through compute emulation",
        "TransparentMultiSmokeProject: natural hybrid shadow route selected on RayQuery-capable hardware",
        "AvboitTimingProbe: in-flight ranges 32", "AvboitTimingProbe: render unfocused 1",
        "AvboitTimingProbe: caustic in-flight ranges 32",
        "CausticSphereSmokeProject: reflection mode 0", "CausticSphereSmokeProject: camera refraction disabled",
        "CausticSphereSmokeProject: caustics " + ("enabled" if enabled else "disabled"),
        "CausticTimingProbe: reflection diagnostics false temporal false spatial false feedback false",
        "CausticTimingProbe: scene single-static-sphere-ground-v1",
        f"CausticTimingProbe: camera {preset} distance {distance} height 0.85",
        "CausticTimingProbe: fixed delta 0.016666667 yaw 0 sphere scale 0.7",
        "CausticTimingProbe: directional pitch 0.9 yaw 0.65 intensity 2",
        "CausticTimingProbe: vertical FOV radians 1.0471976",
        "CausticTimingProbe: photon phases bootstrap 2 converged 4 warmup 8",
        "RendererSystem: deferred rendering targets ready (1280x900, samples=1)",
        "TransparentMultiSmokeProject: shutdown"))
    if enabled:
        text += "\nRendererSystem: dispatched hardware caustic producer (131072 photons/frame, 2 temporal phases, 262144 full-grid budget, 1 caustic lights, 1 refractive instances)"
    if capture:
        text += "\nFramebufferCapture: capture ready\nFramebufferCapture: graphics source frame 359"
    return text


class CausticMeasurementTests(unittest.TestCase):
    def test_camera_presets_keep_optical_quality_and_geometry_controls_identical(self):
        populated, sparse = (benchmark.workloads()["caustic-" + preset] for preset in benchmark.caustic.PRESETS)
        a, b = dict(populated.environment_overrides), dict(sparse.environment_overrides)
        self.assertEqual({key for key in a if a[key] != b[key]}, {"NWB_CAUSTIC_SMOKE_CAMERA_PRESET"})
        self.assertEqual(a["NWB_CAUSTIC_SMOKE_ENABLED"], "1")
        self.assertEqual(a["NWB_REFRACTION_SMOKE_ENABLED"], "0")
        self.assertEqual(a["NWB_REFLECTION_SMOKE_MODE"], "disabled")
        self.assertEqual(populated.scope_multipliers, sparse.scope_multipliers)
        self.assertEqual(populated.secondary_scope, benchmark.caustic.RESOLVE)
        self.assertIn(benchmark.caustic.PHOTONS, populated.control_scopes)
        self.assertNotIn(benchmark.caustic.RESOLVE, populated.control_scopes)
        self.assertEqual(len(populated.scopes), 14)

    def test_completed_resolve_and_photon_counts_cannot_be_divided_or_missing(self):
        workload = benchmark.workloads()["caustic-populated"]
        benchmark.validate_coverage(scopes(workload), workload, 6, 100)
        for name in (benchmark.caustic.RESOLVE, benchmark.caustic.PHOTONS):
            missing = scopes(workload)
            del missing[name]
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(missing, workload, 6, 100)
            wrong = scopes(workload)
            wrong[name]["gpu_samples"] *= 2
            with self.assertRaisesRegex(benchmark.SmokeFailure, "sample ratio"):
                benchmark.validate_coverage(wrong, workload, 6, 100)

    def test_warmup_is_actual_completed_gpu_work_not_publication_count(self):
        values = {name: {"gpu_samples": 12, "total_ms": 1, "reports": 2}
            for name in (benchmark.FRAME, benchmark.caustic.PHOTONS)}
        benchmark.caustic.validate_warmup(values)
        for name in values:
            changed = copy.deepcopy(values)
            changed[name]["gpu_samples"] = 11
            changed[name]["reports"] = 1000
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_warmup(changed)

    def test_actual_native_route_geometry_and_photon_budget_are_load_bearing(self):
        for preset in benchmark.caustic.PRESETS:
            workload = benchmark.workloads()["caustic-" + preset]
            text = caustic_log_text(preset)
            actual = workload.validate_log(text, workload, True)
            self.assertEqual(actual["camera_distance"], benchmark.caustic.PRESETS[preset])
            self.assertEqual(actual["initial_producer"], [131072, 2, 262144, 1, 1])
            for changed in (text.replace("131072 photons", "65536 photons"),
                text.replace("262144 full-grid", "131072 full-grid"), text.replace("1 refractive", "2 refractive"),
                text.replace("height 0.85", "height 0.7"), text.replace("sphere scale 0.7", "sphere scale 0.35"),
                text.replace("intensity 2", "intensity 4"), text.replace("radians 1.0471976", "radians 0.7"),
                text.replace("warmup 8", "warmup 16"), text.replace("yaw 0", "yaw 1"),
                text.replace("hardware caustic producer", "software caustic producer"),
                text + "\nCausticSphereSmokeProject: reflection mode 2",
                text + "\nAvboitTimingProbe: caustic in-flight ranges 2"):
                with self.subTest(preset=preset, changed=changed[-120:]), self.assertRaises(benchmark.SmokeFailure):
                    workload.validate_log(changed, workload, True)

    def test_capture_evidence_is_required_for_qualification_and_forbidden_for_timing(self):
        workload = benchmark.workloads()["caustic-populated"]
        text = caustic_log_text(capture=True)
        settings = dict(workload.environment_overrides)
        actual = benchmark.caustic.validate_log(text, settings, capture=True)
        self.assertEqual(actual["graphics_source_frame"], 359)
        for value in (text, caustic_log_text() + "\nVK_LAYER_KHRONOS_validation"):
            with self.assertRaises(benchmark.SmokeFailure):
                workload.validate_log(value, workload, True)
        for changed in (caustic_log_text(), text.replace("source frame 359", "source frame 7")):
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_log(changed, settings, capture=True)
        off = benchmark.caustic.environment("populated", False)
        benchmark.caustic.validate_log(caustic_log_text(enabled=False, capture=True), off, capture=True)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_log(text, off, capture=True)

    def test_receiver_mask_excludes_sphere_and_image_background(self):
        width, height = 160, 120
        points = list(benchmark.caustic.receiver_pixels(width, height, 2.2))
        self.assertGreater(len(points), 100)
        self.assertNotIn((width // 2, height // 2), points)
        self.assertTrue(all(y > height // 2 for _, y in points))
        off = (width, height, [[(50, 50, 50) for _ in range(width)] for _ in range(height)])
        rows = [list(row) for row in off[2]]
        for x, y in points[:20]:
            rows[y][x] = (70, 70, 70)
        for x in range(width):
            rows[0][x] = (255, 255, 255)
        result = benchmark.caustic.footprint((width, height, rows), off, 2.2)
        self.assertEqual(result["positive_pixels"], 20)
        self.assertEqual(result["positive_channel_gain"], 1200)
        self.assertEqual(benchmark.caustic.footprint(off, off, 2.2)["positive_pixels"], 0)

    def test_visible_pixel_and_tile_reduction_are_independent_qualification_gates(self):
        good = {"populated": {"positive_pixels": 1000, "positive_tiles": 100, "positive_channel_gain": 60000},
            "sparse": {"positive_pixels": 250, "positive_tiles": 30, "positive_channel_gain": 15000}}
        benchmark.caustic.validate_metrics(good)
        for key, value in (("positive_pixels", 99), ("positive_channel_gain", 1499), ("positive_tiles", 81)):
            changed = copy.deepcopy(good)
            changed["sparse"][key] = value
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_metrics(changed)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_metrics({"populated": good["populated"], "sparse": good["populated"]})

    def test_caustic_cli_requires_both_frozen_arm_qualification_reports(self):
        common = ["--baseline-executable", "a", "--baseline-runtime", "ar", "--baseline-source-manifest", "as.json",
            "--candidate-executable", "b", "--candidate-runtime", "br", "--candidate-source-manifest", "bs.json",
            "--logserver-executable", "logger", "--output-directory", "output"]
        proofs = ["--baseline-caustic-qualification", "aqual.json", "--candidate-caustic-qualification", "bqual.json"]
        for partial in ([], proofs[:2], proofs[2:]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + ["--workload", "caustic-populated"] + partial)
        args = benchmark.parse_args(common + ["--workload", "caustic-sparse"] + proofs)
        self.assertTrue(args.require_hardware)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark.parse_args(common + proofs)

    def test_qualification_replay_requires_exact_identity_policy_and_all_four_captures(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "qualification.json"
            identity = {"frozen": "actual-arm"}
            document = {"schema": benchmark.caustic.SCHEMA, "policy": benchmark.caustic.POLICY,
                "arm": identity, "captures": {}}
            for changed in ({**document, "arm": {"frozen": "other-arm"}},
                {**document, "policy": {}}, document):
                path.write_text(json.dumps(changed), encoding="utf-8")
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.caustic.validate_report(path, identity)

    def test_caustic_rejected_output_preserves_existing_evidence_without_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            output = directory / "old_evidence"
            output.mkdir()
            saved = output / "failure.json"
            saved.write_bytes(b"original failure evidence")
            argv = ["--executable", str(directory / "bin" / "fixture.exe"),
                "--runtime", str(directory / "runtime"), "--source-manifest", str(directory / "source" / "source.json"),
                "--logserver-executable", str(directory / "logger" / "logger.exe"), "--output-directory", str(output)]
            with patch.object(benchmark.caustic.subprocess, "run") as launch, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(benchmark.caustic.main(argv), 1)
            launch.assert_not_called()
            self.assertEqual(saved.read_bytes(), b"original failure evidence")
            self.assertEqual(sorted(path.name for path in output.iterdir()), ["failure.json"])

    def test_caustic_output_overlap_rejected_in_both_directions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            protected = root / "inputs"
            for output in (protected, protected / "output", root):
                with self.subTest(output=output), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.caustic.validate_output_path(output, (protected,))
            benchmark.caustic.validate_output_path(root / "evidence", (protected,))

    def test_caustic_requested_gpu_debug_needs_actual_all_markers(self):
        utility = benchmark.caustic
        text = "\n".join(utility.GPU_DEBUG_MARKERS)
        utility.validate_gpu_debug(text, ["--gpudbg"])
        utility.validate_gpu_debug("ordinary launch", [])
        for marker in utility.GPU_DEBUG_MARKERS:
            with self.subTest(marker=marker), self.assertRaises(benchmark.SmokeFailure):
                utility.validate_gpu_debug(text.replace(marker, "requested only"), ["--gpudbg"])
        args = SimpleNamespace(executable="fixture.exe", runtime="runtime", logserver_executable="logger.exe",
            timeout=90, application_arg=["--gpudbg"])
        command = utility.capture_command(args, Path("output.bmp"))
        for marker in utility.GPU_DEBUG_MARKERS:
            self.assertIn(marker, command)

    def test_synthetic_raw_evidence_replay_and_tampering_checks(self):
        # Small synthetic parser evidence only; this does not qualify a real framebuffer or GPU arm.
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            policy = {**benchmark.caustic.POLICY, "width": 160, "height": 120}
            with patch.multiple(benchmark.caustic, WIDTH=160, HEIGHT=120, POLICY=policy):
                utility = benchmark.caustic
                width, height = 160, 120
                identity = {"executable": str(directory / "fixture.exe"), "runtime": str(directory / "runtime")}
                logger = directory / "logger.exe"
                logger.write_bytes(b"synthetic logger identity")
                (directory / "crash_handler.exe").write_bytes(b"synthetic crash helper identity")
                logger_dependency = directory / "logger_dependency.dll"
                logger_dependency.write_bytes(b"synthetic logger dependency identity")
                args = SimpleNamespace(**identity, logserver_executable=logger, timeout=90.0, application_arg=[])
                document = {"schema": utility.SCHEMA, "policy": policy, "arm": identity, "captures": {},
                    "metrics": {}, "qualification_tool": benchmark.file_identity(Path(utility.__file__)),
                    "launcher": benchmark.file_identity(Path(utility.__file__).with_name("window_capture_smoke.py")),
                    "logserver": benchmark.file_identity(logger), "logserver_path": str(logger),
                    "logserver_binaries": benchmark.binary_identity(logger),
                    "timeout_seconds": 90.0, "application_args": []}
                for preset, distance in utility.PRESETS.items():
                    frames = {}
                    for enabled in (True, False):
                        key = preset + ("_on" if enabled else "_off")
                        image, log = directory / (key + ".bmp"), directory / (key + ".log")
                        selected = set(utility.receiver_pixels(width, height, distance)) if enabled else set()
                        pixels = bytearray()
                        for y in reversed(range(height)):
                            for x in range(width):
                                pixels.extend(bytes((70, 70, 70) if (x, y) in selected else (50, 50, 50)))
                        header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
                        header += struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
                        image.write_bytes(header + pixels)
                        log.write_text(caustic_log_text(preset, enabled, capture=True).replace("1280x900", "160x120"), encoding="utf-8")
                        settings = utility.environment(preset, enabled)
                        document["captures"][key] = {"settings": settings,
                            "runtime": utility.validate_log(log.read_text(encoding="utf-8"), settings, capture=True),
                            "command": utility.capture_command(args, image),
                            "image": {"path": image.name, "identity": benchmark.file_identity(image)},
                            "log": {"path": log.name, "identity": benchmark.file_identity(log)}}
                        frames[enabled] = utility.read_bmp_24_rows(image)
                    document["metrics"][preset] = utility.footprint(frames[True], frames[False], distance)
                report = directory / "qualification.json"
                report.write_text(json.dumps(document), encoding="utf-8")
                result, paths = utility.validate_report(report, identity)
                self.assertEqual(result["metrics"], document["metrics"])
                self.assertEqual(len(paths), 12)
                saved_dependency = logger_dependency.read_bytes()
                logger_dependency.write_bytes(saved_dependency + b"modified")
                with self.assertRaisesRegex(benchmark.SmokeFailure, "dependency inventory changed"):
                    utility.validate_report(report, identity)
                logger_dependency.write_bytes(saved_dependency)
                for kind in ("image", "log"):
                    raw = directory / document["captures"]["populated_on"][kind]["path"]
                    saved = raw.read_bytes()
                    raw.write_bytes(saved + b"modified")
                    with self.subTest(kind=kind), self.assertRaisesRegex(benchmark.SmokeFailure, "raw .* changed"):
                        utility.validate_report(report, identity)
                    raw.write_bytes(saved)
                for field in ("metrics", "qualification_tool", "command"):
                    changed = copy.deepcopy(document)
                    if field == "metrics":
                        changed[field]["populated"]["positive_pixels"] += 1
                    elif field == "qualification_tool":
                        changed[field]["sha256"] = "0" * 64
                    else:
                        command = changed["captures"]["populated_on"]["command"]
                        command[command.index("--application-capture-frame-count") + 1] = "16"
                    report.write_text(json.dumps(changed), encoding="utf-8")
                    with self.subTest(field=field), self.assertRaises(benchmark.SmokeFailure):
                        utility.validate_report(report, identity)


if __name__ == "__main__":
    unittest.main()
