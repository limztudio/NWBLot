#!/usr/bin/env python3
"""Strict presentation measurement replay and performance-target checks; never launch a renderer."""


import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_timing_smoke as smoke
from stress_timing_test_support import reflection_log, reflection_record, shadow_quality_record, shadow_record, stress_arguments, valid_log, workload_record


LIT_QUERY_BUDGET_UNITS_PER_HARDWARE_RAY = "query_budget_units_per_hardware_ray"
LIT_RESULT_JSON = "result.json"
LIT_OBSERVED_FPS = "observed_fps"
LIT_ERROR = "error"
LIT_N_96 = "96"
LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI = "fps=16 presentations=480 seconds=30 first=80 last=560"
LIT_RENDERER_EXE = "renderer.exe"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_SYS_STDERR = "sys.stderr"
LIT_VERIFIED = "verified"
LIT_INVALID = "invalid"
LIT_N_1 = "-1"
LIT_BAD = "bad"
LIT_PATH = "PATH"
LIT_KEPT = "kept"
LIT_FPS = "fps"
LIT_UTF_8 = "utf-8"
LIT_RUNTIME_LOG = "runtime.log"
LIT_IDENTITIES = "identities"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LOG = "*.log"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_DEVICE_MATERIAL_SIGNATURE = "device_material_signature"
LIT_OPTICAL_REFLECTION = "optical_reflection"
LIT_MEASUREMENT = "measurement"
LIT_QUERIES_PER_HARDWARE_RAY = "physical_queries_per_hardware_ray"
LIT_GENERATION = "generation"
LIT_NWB_STRESS_TEST_SPIN_ANGLE = "NWB_STRESS_TEST_SPIN_ANGLE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_NWB_STRESS_CHARACTERS_PER_CLASS = "NWB_STRESS_CHARACTERS_PER_CLASS"
LIT_N_10 = "10"
LIT_PACING = "pacing"
LIT_COMPLETE_FPS_16 = "complete fps=16"
LIT_GPUDBG = "--gpudbg"
LIT_VUID_123 = "VUID-123"
LIT_NWB_OTHER = "NWB_OTHER"
LIT_NWB_STRESS_REFLECTION_DIAGNOSTICS = "NWB_STRESS_REFLECTION_DIAGNOSTICS"
LIT_FAILURE_JSON = "failure.json"
LIT_KEEP_ME = "keep me"
LIT_RUNTIME = "runtime"
LIT_FAILED_PROCESS_OUTPUT = "failed process output"
LIT_RAW_TIMEOUT_RUNTIME_LOG = "raw timeout runtime log"
LIT_PROCESS_TAIL_TXT = "process_tail.txt"
LIT_CAPTURE = "capture"
LIT_PROCESS_PRESERVED = "process preserved"
LIT_N_60 = "60"
LIT_MINIMUM_FPS = "--minimum-fps"
LIT_PASSED = "passed"
LIT_CAPTURE_VALIDATED = "capture_validated"
LIT_PERFORMANCE_TARGET = "performance_target"
LIT_NAN = "nan"
LIT_CPU_DIAGNOSTICS = "--cpu-diagnostics"
LIT_REFLECTION_DIAGNOSTICS = "--reflection-diagnostics"
LIT_IDENTITY_BEFORE = "identity_before"
LIT_IDENTITY_AFTER = "identity_after"
LIT_MINIMUM_FPS_2 = "minimum_fps"
LIT_MAIN = "__main__"


class StressMeasurementTests(unittest.TestCase):
    def test_current_pacing_summary_requires_exactly_one_ordered_record(self):
        paced = valid_log()
        record = next(line for line in paced.splitlines() if line.startswith(smoke.LIT_STRESSTESTSMOKEPROJECT_PRESENTATION_PA))
        self.assertEqual(smoke.parse_runtime_log(paced, 0)[LIT_PACING]["samples"], 480)
        missing = paced.replace(record, "")
        for invalid in (missing, paced + record, missing + record, missing.replace(smoke.START, record + "\n" + smoke.START), paced.replace("p50ms=62.5 p95ms=70.0", "p50ms=80.0 p95ms=70.0")):
            with self.subTest(log=invalid), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(invalid)

    def test_bad_exit_rejected_even_with_complete_log(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_runtime_log(valid_log(), 1)

    def test_rejects_timing_sample_rate_in_place_of_presentation_rate(self):
        with self.assertRaisesRegex(smoke.SmokeFailure, "count divided"):
            smoke.parse_measurement(valid_log().replace(LIT_COMPLETE_FPS_16, "complete fps=10.7"))

    def test_counts_positive_exact_bounded_and_rates_finite(self):
        for old, new in (("presentations=480", "presentations=479"), (LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI, "fps=0 presentations=0 seconds=30 first=80 last=80"), (LIT_COMPLETE_FPS_16, "complete fps=nan"), ("seconds=30", "seconds=inf"),
            ("last=560", "last=18446744073709551616")):
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log().replace(old, new))

    def test_requires_full_wall_window(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(valid_log().replace("fps=16 presentations=480 seconds=30", "fps=16.551724137931034 presentations=480 seconds=29"))

    def test_duplicate_missing_or_out_of_order_markers_rejected(self):
        for text in (valid_log() + smoke.START, valid_log().replace(smoke.START, ""), valid_log() + smoke.DONE + LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI,
            valid_log().replace(smoke.SHUTDOWN, ""), smoke.SHUTDOWN + "\n" + valid_log().replace(smoke.SHUTDOWN, "")):
            with self.subTest(text=text[-100:]), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(text)

    def test_interval_count_chain_and_wall_sum_are_load_bearing(self):
        for old, new in (("first=88 last=96", "first=89 last=97"), ("avg=16 presentations=8 seconds=0.5 first=80", "avg=8 presentations=8 seconds=1 first=80")):
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log().replace(old, new))

    def test_actual_gpu_debug_markers_required_only_when_requested(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_runtime_log(valid_log(), 0, [LIT_GPUDBG])
        text = "\n".join("    " + marker + "   " for marker in smoke.GPU_DEBUG) + "\n" + valid_log()
        smoke.parse_runtime_log(text, 0, [LIT_GPUDBG])

    def test_errors_and_incomplete_or_suspended_measurements_rejected(self):
        for marker in ("[ERROR] GPU broke", LIT_VUID_123, "presentation measurement incomplete", "render submission suspended"):
            with self.subTest(marker=marker), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log() + marker)

    def test_environment_replaces_inherited_capture_controls(self):
        runtime = Path(self.enterContext(TemporaryDirectory()))
        args = stress_arguments(runtime)
        env = smoke.launch_environment({LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: LIT_N_96, LIT_NWB_STRESS_TEST_SPIN_ANGLE: "2", LIT_NWB_OTHER: LIT_BAD,
            LIT_NWB_STRESS_REFLECTION_DIAGNOSTICS: "1", LIT_NWB_STRESS_CHARACTERS_PER_CLASS: "5", LIT_PATH: LIT_KEPT}, args, Path("trial"))
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, env)
        self.assertNotIn(LIT_NWB_OTHER, env)
        self.assertEqual(env[LIT_NWB_STRESS_TEST_SPIN_ANGLE], "0.6")
        self.assertEqual(env[LIT_NWB_STRESS_CHARACTERS_PER_CLASS], LIT_N_10)
        self.assertEqual(env[LIT_PATH], LIT_KEPT)
        self.assertNotIn(LIT_NWB_STRESS_REFLECTION_DIAGNOSTICS, env)

    def test_output_guard_preserves_prior_evidence_and_rejects_input_overlap(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / "old"
            old.mkdir()
            evidence = old / LIT_FAILURE_JSON
            evidence.write_text(LIT_KEEP_ME, encoding=LIT_UTF_8)
            with self.assertRaises(smoke.SmokeFailure):
                smoke.reserve_output(old, [])
            self.assertEqual(evidence.read_text(encoding=LIT_UTF_8), LIT_KEEP_ME)
            protected = root / LIT_RUNTIME
            protected.mkdir()
            with self.assertRaises(smoke.SmokeFailure):
                smoke.reserve_output(protected / "new", [protected])

    def test_timeout_preserves_raw_failure_log_and_original_failure(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            args = stress_arguments(output, ["--timeout", "90"])
            process = Mock()
            process.wait.side_effect = subprocess.TimeoutExpired("app", 90)
            with patch.object(smoke, LIT_IDENTITIES, return_value={}), \
                patch.object(smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                patch.object(smoke, LIT_LAUNCH_LOGSERVER, return_value=(None, None, output, {}, LIT_LOG)), \
                patch.object(smoke, LIT_LAUNCH_TESTBED, return_value=process), \
                patch.object(smoke, LIT_TERMINATE_PROCESS, return_value=(1, LIT_FAILED_PROCESS_OUTPUT)), \
                patch.object(smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_RAW_TIMEOUT_RUNTIME_LOG):
                with self.assertRaisesRegex(smoke.SmokeFailure, "self-exit"):
                    smoke.acquire(args, output)
            self.assertEqual((output / LIT_RUNTIME_LOG).read_text(encoding=LIT_UTF_8), LIT_RAW_TIMEOUT_RUNTIME_LOG)
            self.assertEqual((output / LIT_PROCESS_TAIL_TXT).read_text(encoding=LIT_UTF_8), LIT_FAILED_PROCESS_OUTPUT)


class StressPerformanceTargetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / LIT_RUNTIME
        self.runtime.mkdir()
        executable = self.runtime / LIT_RENDERER_EXE
        executable.write_bytes(b"synthetic")
        self.argv = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(self.runtime), LIT_NO_LOGSERVER]

    def rate_log(self, fps):
        count = int(fps / 2)
        lines = []
        interval = 0
        for line in valid_log().splitlines():
            if line.startswith(smoke.INTERVAL):
                first = 80 + count * interval
                line = smoke.INTERVAL + f"avg={fps} presentations={count} seconds=0.5 first={first} last={first+count}"
                interval += 1
            elif line.startswith(smoke.DONE):
                line = smoke.DONE + f"fps={fps} presentations={count*60} seconds=30 first=80 last={80+count*60}"
            lines.append(line)
        return shadow_record() + "\n" + "\n".join(lines) + "\n"

    def acquire_or_main(self, extra, text, use_main=False, identities=None):
        output = self.root / LIT_CAPTURE
        argv = self.argv + ["--output-directory", str(output)] + extra
        args = smoke.parse_args(argv)
        if not use_main:
            output.mkdir(exist_ok=True)
        with patch.object(smoke, LIT_IDENTITIES, side_effect=identities, return_value={LIT_VERIFIED: True}), \
            patch.object(smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
            patch.object(smoke, LIT_LAUNCH_LOGSERVER, return_value=(None, None, output, {}, LIT_LOG)), \
            patch.object(smoke, LIT_LAUNCH_TESTBED, return_value=Mock()), \
            patch.object(smoke, LIT_TERMINATE_PROCESS, return_value=(0, LIT_PROCESS_PRESERVED)), \
            patch.object(smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=text), \
            patch.object(smoke.ab, LIT_DEVICE_MATERIAL_SIGNATURE, return_value={}), patch.object(smoke, "write_status"):
            return smoke.main(argv) if use_main else smoke.acquire(args, output)

    def test_main_reports_physical_reflection_queries_and_keeps_validated_result(self):
        row = reflection_record(query_budget_units=20, physical_queries=10, unsupported_paths=0)
        text = shadow_record() + "\n" + reflection_log(row)
        self.assertEqual(self.acquire_or_main([LIT_REFLECTION_DIAGNOSTICS], text, use_main=True), 0)
        output = self.root / LIT_CAPTURE
        result = json.loads((output / LIT_RESULT_JSON).read_text(encoding=LIT_UTF_8))
        self.assertTrue(result[LIT_CAPTURE_VALIDATED])
        self.assertEqual(result[LIT_OPTICAL_REFLECTION][LIT_QUERY_BUDGET_UNITS_PER_HARDWARE_RAY], 2.)
        self.assertEqual(result[LIT_OPTICAL_REFLECTION][LIT_QUERIES_PER_HARDWARE_RAY], 1.)
        self.assertFalse((output / LIT_FAILURE_JSON).exists())

    def test_positive_finite_threshold_extremes_are_accepted(self):
        for threshold in ("5e-324", "1e308"):
            self.assertEqual(smoke.parse_args(self.argv + [LIT_MINIMUM_FPS, threshold]).minimum_fps, float(threshold))

    def test_invalid_or_diagnostic_threshold_requests_are_rejected(self):
        for value in ("0", LIT_N_1, LIT_NAN, "inf", "-inf", "1e309", "1e-999", LIT_INVALID):
            with self.subTest(value=value), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + ["--minimum-fps=" + value])
        for flags in ([LIT_CPU_DIAGNOSTICS], [LIT_REFLECTION_DIAGNOSTICS], [LIT_CPU_DIAGNOSTICS, LIT_REFLECTION_DIAGNOSTICS]):
            with self.subTest(flags=flags), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + [LIT_MINIMUM_FPS, LIT_N_60] + flags)
            self.assertIsNone(smoke.parse_args(self.argv + flags).minimum_fps)

    def test_acquisition_strict_boundary_and_validated_provenance(self):
        for fps, expected in ((58, False), (60, False), (62, True)):
            with self.subTest(fps=fps):
                result = self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], self.rate_log(fps))
                self.assertEqual(result[LIT_PASSED], expected)
                self.assertTrue(result[LIT_CAPTURE_VALIDATED])

    def test_rounded_logged_rate_cannot_turn_equal_count_rate_into_pass(self):
        text = self.rate_log(60).replace("complete fps=60 ", "complete fps=60.00000003 ")
        result = self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], text)
        self.assertGreater(result[LIT_MEASUREMENT][LIT_FPS], 60)
        self.assertEqual(result[LIT_PERFORMANCE_TARGET][LIT_OBSERVED_FPS], 60)
        self.assertFalse(result[LIT_PASSED])

    def test_ten_positive_intervals_make_a_fast_target_inconclusive(self):
        rows = iter(smoke.INTERVAL + f"avg={400 if index < 10 else 0} presentations={200 if index < 10 else 0} seconds=0.5 first={80 + 200 * min(index, 10)} last={80 + 200 * min(index + 1, 10)}" for index in range(60))
        done = smoke.DONE + f"fps={2000 / 30!r} presentations=2000 seconds=30 first=80 last=2080"
        text = "\n".join(next(rows) if line.startswith(smoke.INTERVAL) else done if line.startswith(smoke.DONE) else line for line in self.rate_log(62).splitlines()) + "\n"
        self.assertEqual(self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], text, use_main=True), 1)
        output = self.root / LIT_CAPTURE
        result = json.loads((output / LIT_RESULT_JSON).read_text())
        self.assertGreater(result[LIT_PERFORMANCE_TARGET][LIT_OBSERVED_FPS], 60)
        self.assertTrue(result[LIT_PERFORMANCE_TARGET]["inconclusive"])
        self.assertIsNone(result[LIT_PERFORMANCE_TARGET][LIT_PASSED])
        self.assertFalse(result[LIT_PASSED])
        self.assertIn("INCONCLUSIVE: 10 positive", json.loads((output / LIT_FAILURE_JSON).read_text())[LIT_ERROR])

    def test_main_retains_fully_validated_result_and_separate_target_failure(self):
        text = self.rate_log(60)
        self.assertEqual(self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], text, use_main=True), 1)
        output = self.root / LIT_CAPTURE
        result = json.loads((output / LIT_RESULT_JSON).read_text())
        failure = json.loads((output / LIT_FAILURE_JSON).read_text())
        self.assertTrue(result[LIT_CAPTURE_VALIDATED])
        self.assertFalse(result[LIT_PASSED])
        self.assertFalse(result[LIT_PERFORMANCE_TARGET][LIT_PASSED])
        self.assertIn("must exceed 60", failure[LIT_ERROR])
        self.assertEqual((output / LIT_RUNTIME_LOG).read_text(), text)
        self.assertEqual((output / LIT_PROCESS_TAIL_TXT).read_text(), LIT_PROCESS_PRESERVED)
        self.assertEqual(result[LIT_IDENTITY_BEFORE], {LIT_VERIFIED: True})
        self.assertEqual(result[LIT_IDENTITY_BEFORE], result[LIT_IDENTITY_AFTER])


    def test_target_does_not_bypass_validation_or_identity_checks(self):
        for text in (self.rate_log(62) + LIT_VUID_123, self.rate_log(62).replace("blocker_search=0", "blocker_search=1")):
            with self.subTest(text=text[-50:]), self.assertRaises(smoke.SmokeFailure):
                self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], text)
        with self.assertRaisesRegex(smoke.SmokeFailure, "identity changed"):
            self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], self.rate_log(62), identities=[{LIT_GENERATION: 1}, {LIT_GENERATION: 2}])

    def test_direct_acquisition_rejects_diagnostic_or_invalid_threshold_before_launch(self):
        for field, value in (("cpu_diagnostics", True), ("reflection_diagnostics", True), (LIT_MINIMUM_FPS_2, float(LIT_NAN))):
            args = smoke.parse_args(self.argv + [LIT_MINIMUM_FPS, LIT_N_60])
            setattr(args, field, value)
            with patch.object(smoke, LIT_LAUNCH_TESTBED) as launch, self.assertRaises(smoke.SmokeFailure):
                smoke.acquire(args, self.root / "unused")
            launch.assert_not_called()


if __name__ == LIT_MAIN:
    unittest.main()
