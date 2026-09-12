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
        self.assertEqual(set(benchmark.workloads()), {"transparent-multi", *expected})
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


if __name__ == "__main__":
    unittest.main()
