#!/usr/bin/env python3
"""Strict presentation measurement replay and acquisition failure retention; never launch a renderer."""

from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_timing_smoke as smoke


def valid_log():
    lines = list(smoke.REQUIRED[:-1])
    lines.remove(smoke.SHUTDOWN)
    lines.append(smoke.REQUIRED[-1])
    lines.append("RendererSystem: deferred rendering targets ready (1280x900, format test)")
    # Synthetic fixed simulation delta is 1/60, while genuine wall/count evidence yields 16 FPS.
    lines.append("Fixture: fixed simulation delta 0.016666667")
    for index in range(60):
        lines.append(smoke.INTERVAL + f"avg=16 presentations=8 seconds=0.5 first={80+8*index} last={88+8*index}")
    lines.append(smoke.DONE + "fps=16 presentations=480 seconds=30 first=80 last=560")
    lines.append(smoke.SHUTDOWN)
    return "\n\n".join(lines) + "\n"


class StressMeasurementTests(unittest.TestCase):
    def test_complete_rate_uses_presentations_and_wall_not_fixed_delta_or_queries(self):
        result = smoke.parse_measurement(valid_log())
        self.assertEqual(result["fps"], 16.)
        self.assertEqual(result["frame_ms"], 62.5)
        self.assertEqual(result["presentations"], 480)
        self.assertEqual(result["last"] - result["first"], 480)
        self.assertEqual(len(result["intervals"]), 60)

    def test_optional_pacing_summary_parses_and_rejects_disorder(self):
        paced = valid_log() + "StressTestSmokeProject: presentation pacing samples=480 p50ms=62.5 p95ms=70.0 maxms=120.0 stalls50ms=3\n"
        parsed = smoke.parse_runtime_log(paced, 0)
        self.assertEqual(parsed["pacing"]["samples"], 480)
        self.assertEqual(parsed["pacing"]["stalls50ms"], 3)
        self.assertLessEqual(parsed["pacing"]["p50ms"], parsed["pacing"]["p95ms"])
        self.assertLessEqual(parsed["pacing"]["p95ms"], parsed["pacing"]["maxms"])
        self.assertIsNone(smoke.parse_runtime_log(valid_log(), 0)["pacing"])
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(paced.replace("p50ms=62.5 p95ms=70.0", "p50ms=80.0 p95ms=70.0"))
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(paced + "StressTestSmokeProject: presentation pacing samples=1 p50ms=1 p95ms=1 maxms=1 stalls50ms=0")

    def test_bad_exit_rejected_even_with_complete_log(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_runtime_log(valid_log(), 1)

    def test_rejects_timing_sample_rate_in_place_of_presentation_rate(self):
        with self.assertRaisesRegex(smoke.SmokeFailure, "count divided"):
            smoke.parse_measurement(valid_log().replace("complete fps=16", "complete fps=10.7"))

    def test_counts_positive_exact_bounded_and_rates_finite(self):
        for old, new in (("presentations=480", "presentations=479"),
            ("fps=16 presentations=480 seconds=30 first=80 last=560", "fps=0 presentations=0 seconds=30 first=80 last=80"),
            ("complete fps=16", "complete fps=nan"),
            ("seconds=30", "seconds=inf"),
            ("last=560", "last=18446744073709551616")):
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log().replace(old, new))

    def test_requires_full_wall_window(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(valid_log().replace("fps=16 presentations=480 seconds=30", "fps=16.551724137931034 presentations=480 seconds=29"))

    def test_duplicate_missing_or_out_of_order_markers_rejected(self):
        for text in (valid_log() + smoke.START, valid_log().replace(smoke.START, ""),
            valid_log() + smoke.DONE + "fps=16 presentations=480 seconds=30 first=80 last=560",
            valid_log().replace(smoke.SHUTDOWN, ""), smoke.SHUTDOWN + "\n" + valid_log().replace(smoke.SHUTDOWN, "")):
            with self.subTest(text=text[-100:]), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(text)

    def test_interval_count_chain_and_wall_sum_are_load_bearing(self):
        for old, new in (("first=88 last=96", "first=89 last=97"),
            ("avg=16 presentations=8 seconds=0.5 first=80", "avg=8 presentations=8 seconds=1 first=80")):
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log().replace(old, new))

    def test_actual_gpu_debug_markers_required_only_when_requested(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_runtime_log(valid_log(), 0, ["--gpudbg"])
        text = "\n".join("    " + marker + "   " for marker in smoke.GPU_DEBUG) + "\n" + valid_log()
        self.assertEqual(smoke.parse_runtime_log(text, 0, ["--gpudbg"])["measurement"]["fps"], 16.)

    def test_errors_and_incomplete_or_suspended_measurements_rejected(self):
        for marker in ("[ERROR] GPU broke", "VUID-123", "presentation measurement incomplete", "render submission suspended"):
            with self.subTest(marker=marker), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log() + marker)

    def test_environment_replaces_inherited_capture_controls(self):
        args = SimpleNamespace(spin_angle=.6, fixed_delta_seconds=.016666667)
        env = smoke.launch_environment({"NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "96",
            "NWB_STRESS_TEST_SPIN_ANGLE": "2", "NWB_OTHER": "bad", "PATH": "kept"}, args, Path("trial"))
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", env)
        self.assertNotIn("NWB_OTHER", env)
        self.assertEqual(env["NWB_STRESS_TEST_SPIN_ANGLE"], "0.6")
        self.assertEqual(env["NWB_STRESS_SMOKE_TIMING"], "1")
        self.assertEqual(env["PATH"], "kept")

    def test_output_guard_preserves_prior_evidence_and_rejects_input_overlap(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / "old"
            old.mkdir()
            evidence = old / "failure.json"
            evidence.write_text("keep me", encoding="utf-8")
            with self.assertRaises(smoke.SmokeFailure):
                smoke.reserve_output(old, [])
            self.assertEqual(evidence.read_text(encoding="utf-8"), "keep me")
            protected = root / "runtime"
            protected.mkdir()
            with self.assertRaises(smoke.SmokeFailure):
                smoke.reserve_output(protected / "new", [protected])

    def test_timeout_preserves_raw_failure_log_and_original_failure(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            args = SimpleNamespace(executable=output / "app.exe", working_directory=output,
                no_logserver=True, logserver_executable=None, application_arg=[], timeout=90,
                spin_angle=.6, fixed_delta_seconds=.016666667)
            process = Mock()
            process.wait.side_effect = subprocess.TimeoutExpired("app", 90)
            with patch.object(smoke, "identities", return_value={}), \
                patch.object(smoke, "build_launch_environment", return_value={}), \
                patch.object(smoke, "launch_logserver", return_value=(None, None, output, {}, "*.log")), \
                patch.object(smoke, "launch_testbed", return_value=process), \
                patch.object(smoke, "terminate_process", return_value=(1, "failed process output")), \
                patch.object(smoke, "shutdown_logserver_and_collect", return_value="raw timeout runtime log"):
                with self.assertRaisesRegex(smoke.SmokeFailure, "self-exit"):
                    smoke.acquire(args, output)
            self.assertEqual((output / "runtime.log").read_text(encoding="utf-8"), "raw timeout runtime log")
            self.assertEqual((output / "process_tail.txt").read_text(encoding="utf-8"), "failed process output")


if __name__ == "__main__":
    unittest.main()
