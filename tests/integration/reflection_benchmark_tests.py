#!/usr/bin/env python3
"""CPU-only tests of reflection benchmark launch and qualification contracts."""
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
from reflection_workload_qualification import ReflectionWorkloadQualification
from reflection_gpu_coverage import ReflectionGpuCoverage
from smoke_volume_identity import volume_segment_filename

from reflection_benchmark_test_support import report

LIT_RAW = "raw"
LIT_VK_LAYER_KHRONOS_VALIDATION = "VK_LAYER_KHRONOS_validation"
LIT_SUMMARY_JSON = "summary.json"
LIT_TIMING_UNFOCUSED = "ReflectionSmokeProject: timing render unfocused 1\n"
LIT_TIMING_RANGES = "ReflectionSmokeProject: timing in-flight ranges 32\n"
LIT_FLOOR = "floor"
LIT_HYBRID = "hybrid"
LIT_HARDWARE = "hardware"
LIT_NAME = "name"
LIT_ROUGH = "rough"
LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS = "NWB_REFLECTION_SMOKE_DIAGNOSTICS"
LIT_NWB_REFLECTION_SMOKE_FINAL_STATE = "NWB_REFLECTION_SMOKE_FINAL_STATE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_N_10 = "10"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_OLD_TXT = "old.txt"
LIT_NWB_REFLECTION_SMOKE_FEEDBACK = "NWB_REFLECTION_SMOKE_FEEDBACK"
LIT_PRESERVED = "PRESERVED"
LIT_YES = "yes"
LIT_NEW_TXT = "new.txt"
LIT_EXECUTABLE = "--executable"
LIT_TEST_EXE = "test.exe"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_OUTPUT = "output"
LIT_FAMILY = "--family"
LIT_RAY_BUDGET = "--ray-budget"
LIT_FEEDBACK_LONG_MISS = "feedback_long_miss"
LIT_OPTICAL_CLEAR = "optical_clear"
LIT_INCLUDE_FEEDBACK = "--include-feedback"
LIT_MINIMUM_FRAME_SAMPLES = "--minimum-frame-samples"
LIT_SCREEN_STEPS = "--screen-steps"
LIT_N_257 = "257"
LIT_SCREEN_STEPS_96 = "screen steps 96"
LIT_RES = "res"
LIT_AUTHORED_VOL = "authored.vol"
LIT_APP_EXE = "app.exe"
LIT_TEMPORAL = "temporal"
LIT_OPTICAL_QUERIES = "optical_queries"
LIT_BUILD_IDENTITY = "build_identity"
LIT_EXECUTABLE_SHA256 = "executable_sha256"
LIT_SHA256 = "sha256"
LIT_CAPTURES = "captures"
LIT_HARDWARE_BUDGET = "hardware_budget"
LIT_SCREEN_STEPS_2 = "screen_steps"
LIT_SEED = "seed"
LIT_EXTENT = "extent"
LIT_METRICS = "metrics"
LIT_LONG_MISS_GEOMETRY = "long_miss_geometry"
LIT_PASSED = "passed"
LIT_COSTLY_MISS_QUALIFICATION = "costly_miss_qualification"
LIT_COMPLETED_OBSERVATIONS = "completed_observations"
LIT_ACTUAL_ATTEMPTS = "actual_attempts"
LIT_ACTUAL_ITERATIONS = "actual_iterations"
LIT_MEAN_ITERATIONS_PER_ATTEMPT = "mean_iterations_per_attempt"
LIT_ACTUAL_STEP_LIMIT_MISSES = "actual_step_limit_misses"
LIT_UTF_8 = "utf-8"
LIT_RUNTIME_PIPELINE_CACHE = "runtime_pipeline_cache"
LIT_PLAN_JSON = "plan.json"
LIT_ORIGINAL = "original"
LIT_WRITE_STATUS = "write_status"
LIT_BACKEND_CLOSE_FAILED = "backend close failed"
LIT_BACKEND_DISCOVERY_FAILED = "backend discovery failed"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_RUNTIME_ASSET_IDENTITY = "runtime_asset_identity"
LIT_CREATE_CAPTURE_BACKEND = "create_capture_backend"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LOGS = "logs"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_LAUNCH_FAILED = "launch failed"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_ORIGINAL_LOG = "original log"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_BLOCK_00_00_HARDWARE = "block_00_00_hardware"
LIT_MAIN = "__main__"


class BenchmarkEnvironmentTests(unittest.TestCase):
    def test_diagnostics_and_capture_controls_are_sanitized_then_explicit(self):
        args = SimpleNamespace(family=LIT_ROUGH, ray_budget=100, history_samples=16, roughness=.4, sampling_seed=0, optical_queries=8, screen_steps=96)
        inherited = {LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_NWB_REFLECTION_SMOKE_FINAL_STATE: "1", "NWB_REFRACTION_SMOKE_GALLERY": "1", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old.bmp",
            "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT": "4", LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: LIT_N_10, LIT_NWB_GPU_TIMING_FILE: LIT_OLD_TXT, LIT_NWB_REFLECTION_SMOKE_FEEDBACK: "1", LIT_PRESERVED: LIT_YES}
        env, overrides = benchmark.timed_environment(inherited, args, benchmark.Variant(LIT_RAW, LIT_HARDWARE), Path(LIT_NEW_TXT))
        self.assertEqual(env[LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS], "0")
        self.assertEqual(env[LIT_NWB_REFLECTION_SMOKE_FEEDBACK], "0")
        self.assertEqual(env[LIT_NWB_REFLECTION_SMOKE_FINAL_STATE], "0")
        self.assertEqual(env[LIT_NWB_GPU_TIMING_FILE], LIT_NEW_TXT)
        self.assertEqual(env[LIT_PRESERVED], LIT_YES)
        self.assertNotIn(LIT_PRESERVED, overrides)
        self.assertFalse(any(key.startswith("NWB_SMOKE_FRAMEBUFFER_CAPTURE_") for key in env))
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, env)
        self.assertEqual(inherited[LIT_NWB_GPU_TIMING_FILE], LIT_OLD_TXT)

    def test_explicit_loader_layers_are_rejected_without_mutating_inherited_controls(self):
        args = SimpleNamespace(family=LIT_ROUGH, ray_budget=100, history_samples=16, roughness=.4, sampling_seed=0, optical_queries=8, screen_steps=96)
        for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
            inherited = {key: LIT_VK_LAYER_KHRONOS_VALIDATION, LIT_PRESERVED: LIT_YES}
            with self.subTest(key=key), self.assertRaisesRegex(benchmark.SmokeFailure, key):
                benchmark.timed_environment(inherited, args, benchmark.Variant(LIT_RAW, LIT_HARDWARE), Path(LIT_NEW_TXT))
            self.assertEqual(inherited, {key: LIT_VK_LAYER_KHRONOS_VALIDATION, LIT_PRESERVED: LIT_YES})

    def test_optical_family_rejects_missing_or_wrong_query_bound(self):
        args = benchmark.parse_args([LIT_EXECUTABLE, LIT_TEST_EXE, LIT_WORKING_DIRECTORY, ".", LIT_OUTPUT_DIRECTORY, LIT_OUTPUT, LIT_FAMILY, LIT_OPTICAL_CLEAR, "--require-hardware"])
        variant = benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE)
        text = ("ReflectionSmokeProject: case optical_clear created\nReflectionSmokeProject: shutdown\n" + LIT_TIMING_UNFOCUSED + LIT_TIMING_RANGES + "ReflectionSmokeProject: timing depth mip count 10\n" +
            "ReflectionSmokeProject: reflection mode hardware\nReflectionSmokeProject: hardware ray budget 1382400\n" + "ReflectionSmokeProject: screen feedback 0\nReflectionSmokeProject: screen steps 96\n" +
            "ReflectionSmokeProject: optical query limit 16\n" + "Reflection resolve: hardware\nReflectionSmokeProject: hardware available\n" + "RendererSystem: deferred rendering targets ready (960x720, frame)\n")
        benchmark.validate_trial_log(text, args, variant)
        for bad in (text.replace("optical query limit 16", "optical query limit 1"), text.replace("ReflectionSmokeProject: optical query limit 16\n", "")):
            with self.subTest(text=bad), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_trial_log(bad, args, variant)

    def test_cli_rejects_abbreviated_controls_and_invalid_screen_step_limits(self):
        common = [LIT_EXECUTABLE, LIT_TEST_EXE, LIT_WORKING_DIRECTORY, ".", LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        for options in (["--application-arg=--gpudbg"], [LIT_RAY_BUDGET, "0"], [LIT_INCLUDE_FEEDBACK, LIT_MINIMUM_FRAME_SAMPLES, "63"], ["--warmup", "2"], [LIT_SCREEN_STEPS, "7"], [LIT_SCREEN_STEPS, LIT_N_257]):
            with self.subTest(options=options), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + options)
        for steps in (8, 256):
            self.assertEqual(benchmark.parse_args(common + [LIT_SCREEN_STEPS, str(steps)]).screen_steps, steps)

    def test_route_dimensions_and_diagnostics_are_verified_from_runtime_logs(self):
        args = SimpleNamespace(family=LIT_FLOOR, ray_budget=100, require_hardware=True, width=960, height=720, screen_steps=96, mip_count=10)
        variant = benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE)
        text = ("ReflectionSmokeProject: case floor created\nReflectionSmokeProject: shutdown\n" + LIT_TIMING_UNFOCUSED + LIT_TIMING_RANGES + "ReflectionSmokeProject: timing depth mip count 10\n" +
            "ReflectionSmokeProject: reflection mode hardware\nReflectionSmokeProject: hardware ray budget 100\n" + "ReflectionSmokeProject: screen feedback 0\nReflectionSmokeProject: screen steps 96\n" +
            "Reflection resolve: hardware\nReflectionSmokeProject: hardware available\n" + "RendererSystem: deferred rendering targets ready (960x720, frame)\n")
        benchmark.validate_trial_log(text, args, variant)
        benchmark.validate_trial_log(text.replace("\n", "\r\n"), args, variant)
        for bad in (text.replace("960x720", "1280x720"), text.replace("Reflection resolve: hardware", "Reflection resolve: disabled"), text.replace("screen feedback 0", "screen feedback 1"), text.replace(LIT_SCREEN_STEPS_96, "screen steps 16"),
                text.replace("ray budget 100", "ray budget 1000"), text.replace(LIT_SCREEN_STEPS_96, "screen steps 960"), text.replace("reflection mode hardware", "reflection mode hardware_unknown"),
                text + "ReflectionSmokeProject: hardware ray budget 200\n", text + "Reflection resolve: disabled\n", text + "ReflectionSmokeProject: screen feedback 0\n", text.replace(LIT_TIMING_UNFOCUSED, ""),
                text.replace("timing render unfocused 1", "timing render unfocused 0"), text.replace(LIT_TIMING_RANGES, ""), text.replace("timing in-flight ranges 32", "timing in-flight ranges 2"),
                text.replace("timing depth mip count 10", "timing depth mip count 9"), text + "ReflectionSmokeStatistics: sequence=3\n", text + "ReflectionSmokeFeedback: sequence=3\n", text + "ReflectionSmokeProject: hardware unavailable\n", text + "Vulkan: enabled validation layer\n", text + "VK_LAYER_KHRONOS_validation\n"):
            with self.subTest(text=bad), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_trial_log(bad, args, variant)


class CostlyMissQualificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / LIT_RES).mkdir()
        (self.root / LIT_RES / LIT_AUTHORED_VOL).write_bytes(b"exact shader and mesh cook")
        executable = self.root / LIT_APP_EXE
        executable.write_bytes(b"exact optimized executable")
        self.args = benchmark.parse_args([LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(self.root), LIT_OUTPUT_DIRECTORY, str(self.root / LIT_OUTPUT), LIT_FAMILY, LIT_FEEDBACK_LONG_MISS, LIT_INCLUDE_FEEDBACK,
            "--qualification", str(self.root / "qualified.json")])
        self.identity = benchmark.file_identity(executable)
        self.evidence = {LIT_TEMPORAL: False, "spatial_filter": False, "fixed_delta_seconds": 1 / 60, LIT_OPTICAL_QUERIES: 16, LIT_BUILD_IDENTITY: {LIT_EXECUTABLE_SHA256: self.identity[LIT_SHA256], "asset_volumes": benchmark.authored_volume_hashes(self.root)},
            LIT_CAPTURES: [{LIT_NAME: "long_miss_feedback" if enabled else "long_miss_baseline", "enabled": enabled, "case": LIT_FEEDBACK_LONG_MISS, "mode": LIT_HYBRID, LIT_HARDWARE_BUDGET: self.args.ray_budget,
                LIT_SCREEN_STEPS_2: 16, LIT_SEED: 0, "roughness": 0.0, "final_state": False, "mutation": False, LIT_EXTENT: "native"} for enabled in (False, True)],
            LIT_METRICS: {LIT_LONG_MISS_GEOMETRY: {LIT_PASSED: True}, "long_miss": {"images": {LIT_PASSED: True}}, LIT_COSTLY_MISS_QUALIFICATION: {LIT_COMPLETED_OBSERVATIONS: 8, LIT_ACTUAL_ATTEMPTS: 100,
                    LIT_ACTUAL_ITERATIONS: 1300, LIT_MEAN_ITERATIONS_PER_ATTEMPT: 13.0, LIT_ACTUAL_STEP_LIMIT_MISSES: 30, "step_limit_miss_fraction": .3}}}

    def write(self, evidence):
        self.args.qualification.write_text(json.dumps(evidence), encoding=LIT_UTF_8)

    def test_accepts_matching_qualified_build_and_excludes_only_mutable_cache(self):
        cache = self.root / LIT_RES / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)
        cache.write_bytes(b"driver state")
        self.write(self.evidence)
        ReflectionWorkloadQualification.validate_long_miss_qualification(self.args, self.identity)
        cache.write_bytes(b"new mutable driver state")
        ReflectionWorkloadQualification.validate_long_miss_qualification(self.args, self.identity)

    def test_rejects_missing_unqualified_or_incompatible_evidence(self):
        variations = []
        for key, value in ((LIT_COMPLETED_OBSERVATIONS, 7), (LIT_ACTUAL_ITERATIONS, 100), (LIT_ACTUAL_STEP_LIMIT_MISSES, 1), (LIT_MEAN_ITERATIONS_PER_ATTEMPT, 99.0), (LIT_ACTUAL_ATTEMPTS, 0)):
            changed = copy.deepcopy(self.evidence)
            changed[LIT_METRICS][LIT_COSTLY_MISS_QUALIFICATION][key] = value
            variations.append(changed)
        for key, value in ((LIT_SEED, 1), (LIT_HARDWARE_BUDGET, 64), (LIT_SCREEN_STEPS_2, 96), (LIT_EXTENT, "npot")):
            changed = copy.deepcopy(self.evidence)
            changed[LIT_CAPTURES][0][key] = value
            variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        changed[LIT_BUILD_IDENTITY][LIT_EXECUTABLE_SHA256] = "different build"
        variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        changed[LIT_TEMPORAL] = True
        variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        changed[LIT_OPTICAL_QUERIES] = 8
        variations.append(changed)
        changed = copy.deepcopy(self.evidence)
        del changed[LIT_METRICS][LIT_LONG_MISS_GEOMETRY]
        variations.append(changed)
        variations.extend(([], {LIT_BUILD_IDENTITY: []}))
        for changed in variations:
            with self.subTest(evidence=changed):
                self.write(changed)
                with self.assertRaises(benchmark.SmokeFailure):
                    ReflectionWorkloadQualification.validate_long_miss_qualification(self.args, self.identity)
        self.args.qualification = None
        with self.assertRaises(benchmark.SmokeFailure):
            ReflectionWorkloadQualification.validate_long_miss_qualification(self.args, self.identity)

    def test_rejects_changed_authored_volume_even_when_same_filename_remains(self):
        self.write(self.evidence)
        (self.root / LIT_RES / LIT_AUTHORED_VOL).write_bytes(b"later shader cook")
        with self.assertRaises(benchmark.SmokeFailure):
            ReflectionWorkloadQualification.validate_long_miss_qualification(self.args, self.identity)


class BenchmarkLifecycleTests(unittest.TestCase):
    def test_only_canonical_runtime_pipeline_cache_segments_are_mutable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            resources = root / LIT_RES
            resources.mkdir()
            authored = resources / LIT_AUTHORED_VOL
            authored.write_bytes(b"fixed cooked assets")
            similar = resources / "runtime_pipeline_cache_fake.vol"
            similar.write_bytes(b"must remain authored")
            caches = [resources / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, index) for index in range(2)]
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
            resources = root / LIT_RES
            resources.mkdir()
            (resources / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)).write_bytes(b"driver cache")
            with self.assertRaisesRegex(benchmark.SmokeFailure, "no authored resources"):
                benchmark.runtime_asset_identity(root)

    def test_existing_artifacts_are_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / LIT_APP_EXE
            executable.write_bytes(b"exe")
            output = root / LIT_OUTPUT
            output.mkdir()
            plan = output / LIT_PLAN_JSON
            plan.write_text(LIT_ORIGINAL, encoding=LIT_UTF_8)
            args = SimpleNamespace(executable=executable, working_directory=root, output_directory=output)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "must be empty"):
                benchmark.run(args)
            self.assertEqual(plan.read_text(encoding=LIT_UTF_8), LIT_ORIGINAL)

    def test_failed_launch_cleans_logserver_and_preserves_original_failure(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            args = benchmark.parse_args([LIT_EXECUTABLE, str(root / LIT_APP_EXE), LIT_WORKING_DIRECTORY, str(root), LIT_OUTPUT_DIRECTORY, str(root / LIT_OUTPUT)])
            backend = Mock()
            backend.close.side_effect = OSError(LIT_BACKEND_CLOSE_FAILED)
            logserver = object()
            stack.enter_context(patch.object(benchmark, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}))
            stack.enter_context(patch.object(benchmark, LIT_RUNTIME_ASSET_IDENTITY, return_value=[]))
            stack.enter_context(patch.object(benchmark, LIT_CREATE_CAPTURE_BACKEND, return_value=backend))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 1, root, {}, LIT_LOGS)))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_TESTBED, side_effect=benchmark.SmokeFailure(LIT_LAUNCH_FAILED)))
            stack.enter_context(patch.object(benchmark, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_ORIGINAL_LOG))
            stop = stack.enter_context(patch.object(benchmark.CaptureProcessTermination, LIT_TERMINATE_PROCESS, return_value=(0, "")))
            stack.enter_context(patch.object(benchmark, LIT_WRITE_STATUS))
            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_LAUNCH_FAILED):
                benchmark.run_trial(args, benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE), 0, 0, {}, {})
            stop.assert_any_call(logserver, "logserver")
            backend.close.assert_called_once()
            directory = args.output_directory / LIT_BLOCK_00_00_HARDWARE
            self.assertEqual((directory / "runtime.log").read_text(encoding=LIT_UTF_8), LIT_ORIGINAL_LOG)
            self.assertIn(LIT_BACKEND_CLOSE_FAILED, (directory / "cleanup_error.txt").read_text(encoding=LIT_UTF_8))

    def test_failed_cleanup_is_reported_even_when_artifact_cannot_be_written(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            args = benchmark.parse_args([LIT_EXECUTABLE, str(root / LIT_APP_EXE), LIT_WORKING_DIRECTORY, str(root), LIT_OUTPUT_DIRECTORY, str(root / LIT_OUTPUT)])
            backend = Mock()
            backend.close.side_effect = OSError(LIT_BACKEND_CLOSE_FAILED)
            logserver = object()
            stack.enter_context(patch.object(benchmark, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}))
            stack.enter_context(patch.object(benchmark, LIT_RUNTIME_ASSET_IDENTITY, return_value=[]))
            stack.enter_context(patch.object(benchmark, LIT_CREATE_CAPTURE_BACKEND, return_value=backend))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 1, root, {}, LIT_LOGS)))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_TESTBED, side_effect=benchmark.SmokeFailure(LIT_LAUNCH_FAILED)))
            stack.enter_context(patch.object(benchmark, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_ORIGINAL_LOG))
            stack.enter_context(patch.object(benchmark.CaptureProcessTermination, LIT_TERMINATE_PROCESS, return_value=(0, "")))
            status = stack.enter_context(patch.object(benchmark, LIT_WRITE_STATUS))
            original_write = Path.write_text
            def write(path, *args, **kwargs):
                if path.name == "cleanup_error.txt":
                    raise OSError("report denied")
                return original_write(path, *args, **kwargs)
            stack.enter_context(patch.object(Path, "write_text", write))
            cleanup = benchmark.TrialCleanupStatus()
            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_LAUNCH_FAILED):
                benchmark.run_trial(args, benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE), 0, 0, {}, {}, cleanup_status=cleanup)
            self.assertFalse(cleanup.succeeded)
            self.assertIn(LIT_BACKEND_CLOSE_FAILED, str(status.call_args_list))
            self.assertIn("report denied", str(status.call_args_list))

    def test_cleanup_status_rejects_abnormal_exit_or_failure_output(self):
        cases = ((0, "", "", True), (7, "", "", False), (0, "Exception: renderer failed", "Exception: renderer failed", False),
                 (0, "", "[ERROR] early failure\n" + "x" * 70000, False))
        for exit_code, tail, output, expected in cases:
            with self.subTest(exit_code=exit_code, tail=tail, early_error=bool(output)), tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
                root = Path(temporary)
                args = benchmark.parse_args([LIT_EXECUTABLE, str(root / LIT_APP_EXE), LIT_WORKING_DIRECTORY, str(root), LIT_OUTPUT_DIRECTORY, str(root / LIT_OUTPUT)])
                runtime, logserver, backend = Mock(), Mock(), Mock()
                capture_path = root / "capture.log"
                capture_path.write_text(output, encoding=LIT_UTF_8)
                runtime._nwb_output_capture = SimpleNamespace(path=capture_path)
                backend.wait_for_window.side_effect = benchmark.SmokeFailure("GPU benchmark timed out: no hardware")
                stack.enter_context(patch.object(benchmark, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}))
                stack.enter_context(patch.object(benchmark, LIT_RUNTIME_ASSET_IDENTITY, return_value=[]))
                stack.enter_context(patch.object(benchmark, LIT_CREATE_CAPTURE_BACKEND, return_value=backend))
                stack.enter_context(patch.object(benchmark, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 1, root, {}, LIT_LOGS)))
                stack.enter_context(patch.object(benchmark, LIT_LAUNCH_TESTBED, return_value=runtime))
                stack.enter_context(patch.object(benchmark, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_ORIGINAL_LOG))
                stack.enter_context(patch.object(benchmark.CaptureProcessTermination, LIT_TERMINATE_PROCESS, side_effect=lambda process, *_args, **_kwargs: (exit_code, tail) if process is runtime else (0, "")))
                stack.enter_context(patch.object(benchmark, LIT_WRITE_STATUS))
                cleanup = benchmark.TrialCleanupStatus()
                with self.assertRaisesRegex(benchmark.SmokeFailure, "GPU benchmark timed out"):
                    benchmark.run_trial(args, benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE), 0, 0, {}, {}, cleanup_status=cleanup)
                self.assertEqual(cleanup.succeeded, expected)

    def test_normal_acquisition_checks_full_output_before_capability_skip(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            args = benchmark.parse_args([LIT_EXECUTABLE, str(root / LIT_APP_EXE), LIT_WORKING_DIRECTORY, str(root), LIT_OUTPUT_DIRECTORY, str(root / LIT_OUTPUT)])
            runtime, logserver, backend = Mock(), Mock(), Mock()
            capture_path = root / "capture.log"
            capture_path.write_text("[ERROR] early failure\n" + "x" * 70000, encoding=LIT_UTF_8)
            runtime._nwb_output_capture = SimpleNamespace(path=capture_path)
            backend.wait_for_window.return_value = 1
            def launch(*_args):
                timing = args.output_directory / LIT_BLOCK_00_00_HARDWARE / "gpu_timing.txt"
                timing.write_text("timing", encoding=LIT_UTF_8)
                return runtime
            stack.enter_context(patch.object(benchmark, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}))
            stack.enter_context(patch.object(benchmark, LIT_RUNTIME_ASSET_IDENTITY, return_value=[]))
            stack.enter_context(patch.object(benchmark, LIT_CREATE_CAPTURE_BACKEND, return_value=backend))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 1, root, {}, LIT_LOGS)))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_TESTBED, side_effect=launch))
            stack.enter_context(patch.object(benchmark, "ensure_process_running"))
            stack.enter_context(patch.object(benchmark.GpuTimingEvidence, "parse_intervals", return_value=[{}]))
            stack.enter_context(patch.object(benchmark.GpuTimingEvidence, "retain_after_warmup", return_value=(0, [{}] * args.sample_intervals)))
            stack.enter_context(patch.object(benchmark.GpuTimingEvidence, "summarize_intervals", return_value={}))
            stack.enter_context(patch.object(ReflectionGpuCoverage, "validate_coverage"))
            stack.enter_context(patch.object(benchmark.CaptureProcessTermination, LIT_TERMINATE_PROCESS, return_value=(0, "")))
            stack.enter_context(patch.object(benchmark, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_ORIGINAL_LOG))
            log_validation = stack.enter_context(patch.object(benchmark, "validate_trial_log", side_effect=benchmark.SmokeFailure("hardware was unavailable for a hardware benchmark route")))
            with self.assertRaisesRegex(benchmark.SmokeFailure, "rejected"):
                benchmark.run_trial(args, benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE), 0, 0, {}, {})
            log_validation.assert_not_called()
            retained = args.output_directory / LIT_BLOCK_00_00_HARDWARE / "process_output.log"
            self.assertEqual(retained.read_bytes(), capture_path.read_bytes())

    def test_unmapped_window_backend_type_error_propagates_without_old_signature_retry(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            args = benchmark.parse_args([LIT_EXECUTABLE, str(root / LIT_APP_EXE), LIT_WORKING_DIRECTORY, str(root), LIT_OUTPUT_DIRECTORY, str(root / LIT_OUTPUT)])
            runtime, logserver, backend = Mock(), Mock(), Mock()
            capture_path = root / "capture.log"
            capture_path.write_text("", encoding=LIT_UTF_8)
            runtime._nwb_output_capture = SimpleNamespace(path=capture_path)
            backend.wait_for_window.return_value = None
            backend.find_window_for_pid.side_effect = TypeError(LIT_BACKEND_DISCOVERY_FAILED)
            stack.enter_context(patch.object(benchmark, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}))
            stack.enter_context(patch.object(benchmark, LIT_RUNTIME_ASSET_IDENTITY, return_value=[]))
            stack.enter_context(patch.object(benchmark, LIT_CREATE_CAPTURE_BACKEND, return_value=backend))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 1, root, {}, LIT_LOGS)))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_TESTBED, return_value=runtime))
            stop = stack.enter_context(patch.object(benchmark.CaptureProcessTermination, LIT_TERMINATE_PROCESS, return_value=(0, "")))
            stack.enter_context(patch.object(benchmark, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_ORIGINAL_LOG))
            with self.assertRaisesRegex(TypeError, LIT_BACKEND_DISCOVERY_FAILED):
                benchmark.run_trial(args, benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE), 0, 0, {}, {})
            backend.find_window_for_pid.assert_called_once_with(runtime.pid, None, require_mapped=False)
            backend.focus_window.assert_not_called()
            backend.close.assert_called_once()
            stop.assert_any_call(runtime, benchmark.LIT_REFLECTION_BENCHMARK, None, preserve_capture=True)
            self.assertFalse((args.output_directory / LIT_BLOCK_00_00_HARDWARE / LIT_SUMMARY_JSON).exists())

    def test_empty_completed_timing_reports_explain_absent_gpu_samples(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            args = benchmark.parse_args([LIT_EXECUTABLE, str(root / LIT_APP_EXE), LIT_WORKING_DIRECTORY, str(root), LIT_OUTPUT_DIRECTORY, str(root / LIT_OUTPUT)])
            runtime, logserver, backend = Mock(), Mock(), Mock()
            capture_path = root / "capture.log"
            capture_path.write_text("", encoding=LIT_UTF_8)
            runtime._nwb_output_capture = SimpleNamespace(path=capture_path)
            backend.wait_for_window.return_value = 1
            def launch(*_):
                timing = args.output_directory / LIT_BLOCK_00_00_HARDWARE / "gpu_timing.txt"
                timing.write_text(report([], cpu_frames=50000) * 2, encoding=LIT_UTF_8)
                return runtime
            stack.enter_context(patch.object(benchmark, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}))
            stack.enter_context(patch.object(benchmark, LIT_RUNTIME_ASSET_IDENTITY, return_value=[]))
            stack.enter_context(patch.object(benchmark, LIT_CREATE_CAPTURE_BACKEND, return_value=backend))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 1, root, {}, LIT_LOGS)))
            stack.enter_context(patch.object(benchmark, LIT_LAUNCH_TESTBED, side_effect=launch))
            stack.enter_context(patch.object(benchmark, "ensure_process_running"))
            stack.enter_context(patch.object(benchmark.CaptureProcessTermination, LIT_TERMINATE_PROCESS, return_value=(0, "")))
            stack.enter_context(patch.object(benchmark, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value="empty GPU reports"))
            stack.enter_context(patch.object(benchmark.time, "monotonic", side_effect=(0.0, 0.0, 100.0)))
            stack.enter_context(patch.object(benchmark.time, "sleep"))
            with self.assertRaisesRegex(benchmark.SmokeFailure, "timing reports contain no completed GPU frame samples"):
                benchmark.run_trial(args, benchmark.Variant(LIT_HARDWARE, LIT_HARDWARE), 0, 0, {}, {})
            self.assertFalse((args.output_directory / LIT_BLOCK_00_00_HARDWARE / LIT_SUMMARY_JSON).exists())


if __name__ == LIT_MAIN:
    unittest.main()
