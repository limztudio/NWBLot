#!/usr/bin/env python3
"""Pure CPU tests for frozen-arm renderer A/B acquisition contracts."""


import contextlib
import copy
import io
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark
from renderer_trial_comparison import RendererTrialComparison
from renderer_workload_registry import CONTROLS
from renderer_benchmark_identity import RendererBenchmarkIdentity
from smoke_volume_identity import volume_segment_filename
from renderer_ab_benchmark_test_support import trial_matrix


LIT_A = "a"
LIT_B = "b"
LIT_R = "r"
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
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_ARM = "arm"
LIT_BLOCK = "block"
LIT_POSITION = "position"
LIT_SCOPES = "scopes"
LIT_STATUS = "status"
LIT_CONTROL_DRIFT = "control_drift"
LIT_CONTROL_UNCERTAIN = "control_uncertain"
LIT_EVERY_PLANNED_TRIAL = "every planned trial"
LIT_RUNTIME_PIPELINE_CACHE = "runtime_pipeline_cache"
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


class CleanupFailureTests(unittest.TestCase):
    def test_unwritable_cleanup_report_keeps_primary_and_cleanup_errors(self):
        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(output_directory=Path(root), application_arg=[], timeout=1)
            arm = SimpleNamespace(name="baseline", executable=Path("app"), runtime=Path("runtime"))
            backend = mock.Mock()
            backend.close.side_effect = OSError("backend cleanup failed")

            def write(path, _value):
                if path.name == "cleanup_error.json":
                    raise OSError("report denied")

            captured = io.StringIO()
            with mock.patch.object(benchmark, "build_launch_environment", return_value={}), \
                    mock.patch.object(benchmark, "configure_environment", return_value=({}, {})), \
                    mock.patch.object(RendererBenchmarkIdentity, "cache_identity", return_value={}), \
                    mock.patch.object(benchmark, "write_json", side_effect=write), \
                    mock.patch.object(benchmark, "create_capture_backend", return_value=backend), \
                    mock.patch.object(benchmark, "launch_logserver", side_effect=benchmark.SmokeFailure("startup failed")), \
                    mock.patch.object(benchmark.CaptureProcessTermination, "terminate_process"), contextlib.redirect_stderr(captured):
                with self.assertRaisesRegex(benchmark.SmokeFailure, "startup failed"):
                    benchmark.acquire_trial(args, arm, None, 0, 0, {})
            self.assertIn("backend cleanup failed", captured.getvalue())
            self.assertIn("report denied", captured.getvalue())


class CaptureSourceIdentityTests(unittest.TestCase):
    def test_changed_log_or_validation_helper_invalidates_frozen_run(self):
        paths = benchmark.window_capture_source_paths()
        names = {path.name for path in paths}
        self.assertTrue({"window_capture_logs.py", "window_capture_validation.py", "window_capture_windows_pixels.py", "window_capture_image_codec.py", "window_capture_image_regions.py", "window_capture_scene_evidence.py", "window_capture_x11_pixels.py", "window_capture_x11_input.py", "smoke_failure_reporting.py"}.issubset(names))
        frozen = {str(path): {"digest": "original"} for path in paths}
        for changed in ("window_capture_logs.py", "window_capture_validation.py", "window_capture_windows_pixels.py", "window_capture_image_codec.py", "window_capture_image_regions.py", "window_capture_scene_evidence.py", "window_capture_x11_pixels.py", "window_capture_x11_input.py"):
            with self.subTest(changed=changed), mock.patch.object(RendererBenchmarkIdentity, "validate_arm_separation"), \
                    mock.patch("renderer_benchmark_identity.file_identity", side_effect=lambda path: {"digest": "changed" if path.name == changed else "original"}):
                with self.assertRaisesRegex(benchmark.SmokeFailure, "shared runner.*changed"):
                    RendererBenchmarkIdentity.verify_frozen((), {}, frozen)


class FrozenIdentityTests(unittest.TestCase):
    def test_source_content_and_manifest_are_both_frozen(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = source_manifest(root)
            RendererBenchmarkIdentity.source_identity(manifest)
            (root / LIT_SOURCE_CPP).write_text("changed", encoding=LIT_UTF_8)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "source bytes"):
                RendererBenchmarkIdentity.source_identity(manifest)
            for document in ([], {LIT_REVISION: LIT_R, LIT_FILES: {}}, {LIT_REVISION: LIT_R, LIT_FILES: {"x": "invalid"}}):
                manifest.write_text(json.dumps(document), encoding=LIT_UTF_8)
                with self.assertRaises(benchmark.SmokeFailure):
                    RendererBenchmarkIdentity.source_identity(manifest)

    def test_only_exact_contiguous_pipeline_cache_segments_can_mutate(self):
        with tempfile.TemporaryDirectory() as temporary:
            arm = make_arm(Path(temporary), LIT_BASELINE)
            before = RendererBenchmarkIdentity.freeze_arm(arm)
            cache = arm.runtime / LIT_RES / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)
            cache.write_bytes(b"runtime cache")
            self.assertEqual(before, RendererBenchmarkIdentity.freeze_arm(arm))
            cache.write_bytes(b"updated runtime cache")
            self.assertEqual(before, RendererBenchmarkIdentity.freeze_arm(arm))
            unrelated = arm.runtime / LIT_RES / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 2)
            unrelated.write_bytes(b"gap means authored/unknown")
            self.assertNotEqual(before, RendererBenchmarkIdentity.freeze_arm(arm))

    def test_binary_authored_source_and_shared_tool_changes_abort(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            arm = make_arm(root / LIT_ARM, LIT_BASELINE)
            other = make_arm(root / "other", LIT_CANDIDATE)
            tool = root / "tool.py"
            tool.write_bytes(b"tool")
            identities = {item.name: RendererBenchmarkIdentity.freeze_arm(item) for item in (arm, other)}
            shared = {str(tool): benchmark.file_identity(tool)}
            RendererBenchmarkIdentity.verify_frozen((arm, other), identities, shared)
            for path in (arm.executable, arm.runtime / LIT_RES / LIT_AUTHORED_VOL, tool):
                original = path.read_bytes()
                path.write_bytes(b"changed")
                with self.subTest(path=path), self.assertRaises(benchmark.SmokeFailure):
                    RendererBenchmarkIdentity.verify_frozen((arm, other), identities, shared)
                path.write_bytes(original)
            dependency = arm.executable.parent / "runtime.dll"
            dependency.write_bytes(b"new loader dependency")
            with self.assertRaises(benchmark.SmokeFailure):
                RendererBenchmarkIdentity.verify_frozen((arm, other), identities, shared)

    def test_shared_or_hard_linked_caches_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = make_arm(root / LIT_A, LIT_BASELINE)
            second = make_arm(root / LIT_B, LIT_CANDIDATE)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "distinct"):
                RendererBenchmarkIdentity.validate_arm_separation((first, first))
            RendererBenchmarkIdentity.validate_arm_separation((first, second))
            filename = volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)
            first_cache = first.runtime / LIT_RES / filename
            second_cache = second.runtime / LIT_RES / filename
            first_cache.write_bytes(b"shared cache")
            second_cache.hardlink_to(first_cache)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "alias"):
                RendererBenchmarkIdentity.validate_arm_separation((first, second))

    def test_missing_windows_crash_helper_fails_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "smoke.exe"
            executable.write_bytes(b"exe")
            with self.assertRaisesRegex(benchmark.SmokeFailure, "crash_handler"):
                RendererBenchmarkIdentity.binary_identity(executable)


class PairedInferenceTests(unittest.TestCase):

    def test_missing_duplicate_and_wrong_order_trials_are_not_silently_dropped(self):
        workload, orders, trials = trial_matrix()
        changed = copy.deepcopy(trials)
        changed[0][LIT_POSITION] = 1
        for invalid in (trials[:-1], trials + [trials[0]], changed):
            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
                RendererTrialComparison.compare_trials(invalid, orders, workload)

    def test_small_effect_and_uncertain_controls_cannot_claim_speedup(self):
        workload, orders, trials = trial_matrix(-.01)
        self.assertEqual(RendererTrialComparison.compare_trials(trials, orders, workload)[LIT_STATUS], "unresolved")
        workload, orders, trials = trial_matrix(-.5)
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][CONTROLS[0]][LIT_MEAN_MS] += .2 if trial[LIT_BLOCK] % 2 else -.2
        result = RendererTrialComparison.compare_trials(trials, orders, workload)
        self.assertEqual(result[LIT_STATUS], LIT_CONTROL_UNCERTAIN)
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][CONTROLS[0]][LIT_MEAN_MS] = .3
        self.assertEqual(RendererTrialComparison.compare_trials(trials, orders, workload)[LIT_STATUS], LIT_CONTROL_DRIFT)


if __name__ == LIT_MAIN:
    unittest.main()
