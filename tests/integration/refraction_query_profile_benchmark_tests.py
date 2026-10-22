#!/usr/bin/env python3
"""Reject biased profile orders, incomplete campaigns, and unusable host telemetry."""

from collections import Counter
import contextlib
import io
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import gpu_load_telemetry as telemetry  # noqa: E402
import refraction_query_profile_benchmark as profile  # noqa: E402


class RefractionQueryProfileTests(unittest.TestCase):
    def test_williams_cycle_balances_position_and_first_order_predecessor(self):
        sequence = profile.order_plan(profile.orders(8, 0))
        positions = Counter((row["cell"], row["position"]) for row in sequence)
        predecessors = Counter((row["predecessor_within_block"], row["cell"]) for row in sequence if row["predecessor_within_block"] is not None)
        self.assertEqual(len(positions), 16)
        self.assertEqual(set(positions.values()), {2})
        self.assertEqual(len(predecessors), 12)
        self.assertEqual(set(predecessors.values()), {2})
        self.assertEqual(sequence[4]["predecessor"], sequence[3]["cell"])
        self.assertIsNone(sequence[4]["predecessor_within_block"])

    def test_campaign_refuses_missing_or_duplicate_planned_trial(self):
        plan = {"sequence": [{"block": 0, "position": 0, "cell": "one"}, {"block": 0, "position": 1, "cell": "two"}]}
        one = {"block": 0, "position": 0, "cell": "one"}
        for trials in ([one], [one, one]):
            with self.subTest(trials=trials), self.assertRaisesRegex(profile.density.SmokeFailure, "every planned trial"):
                profile.summarize(trials, plan)

    def test_inherited_diagnostic_and_capture_settings_cannot_contaminate_timing(self):
        cell = profile.CELLS[0]
        arm = type("Arm", (), {"name": "candidate", "scheduling": "tiled"})()
        environment, overrides = profile.density.ab.configure_environment({"NWB_REFRACTION_SMOKE_DIAGNOSTICS": "1", "NWB_REFRACTION_SMOKE_DIAGNOSTICS_PATH": "old-counter-output",
            "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old-image-output", "NWB_GPU_TIMING_FILE": "old-timing"}, profile.density.workload_for(arm, cell), Path("new-timing"))
        self.assertNotIn("NWB_REFRACTION_SMOKE_DIAGNOSTICS", environment)
        self.assertNotIn("NWB_REFRACTION_SMOKE_DIAGNOSTICS_PATH", environment)
        self.assertNotIn("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", environment)
        self.assertEqual(environment["NWB_GPU_TIMING_FILE"], str(Path("new-timing")))

    def test_protocol_cannot_reduce_warmup_or_use_incomplete_williams_cycles(self):
        arguments = [value for name in ("executable", "runtime", "logserver", "source-manifest", "output-directory") for value in ("--" + name, "unused-" + name)]
        for extra in (("--blocks", "6"), ("--warmup-minimum-seconds", "29.99"), ("--warmup-minimum-seconds", "nan"),
            ("--sample-intervals", "11"), ("--warmup-minimum-frames", "99"), ("--timeout", "inf")):
            with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                profile.parse_args(arguments + list(extra))

    def test_engine_load_is_partitioned_by_pid_and_engine_without_inventing_one_gpu_percentage(self):
        name = lambda pid, engine: f"pid_{pid}_luid_0x00000000_0x000139DE_phys_0_eng_{engine}_engtype_3D"
        item = lambda pid, engine, value, status=0: {"instance": name(pid, engine), "status": status, "value": value}
        result = telemetry.reduce_instances([item(42, 0, 70), item(42, 1, 60), item(43, 0, 10), item(44, 0, 20), item(42, 2, 99, 0x800007D5)], 42)
        self.assertEqual(result["fixture_busiest_engine_percent"], 70)
        self.assertEqual(result["other_processes_busiest_engine_percent"], 30)
        self.assertEqual(len(result["engines"]), 2)
        self.assertEqual(len(result["invalid_instances"]), 1)
        self.assertTrue(result["fixture_instances_present"])

    def test_missing_fixture_or_invalid_samples_cannot_qualify_telemetry(self):
        sample = lambda stamp, valid: {"monotonic_seconds": stamp, "query_status": 0, "fixture_instances_present": valid,
            "fixture_busiest_engine_percent": 0, "other_processes_busiest_engine_percent": 70}
        for samples in ([sample(1, False), sample(2, False)], [sample(1, True)], [sample(10, True), sample(11, True)]):
            with self.subTest(samples=samples):
                self.assertFalse(telemetry.summarize_samples(samples, (0, 3))["eligible"])
        good = telemetry.summarize_samples([sample(1, True), sample(2, True)], (0, 3))
        self.assertTrue(good["eligible"])
        self.assertEqual(good["fixture_busiest_engine_percent"]["mean"], 0)

    def test_nonfinite_negative_and_unknown_counter_instances_are_retained_as_unusable(self):
        name = "pid_42_luid_0x0_0x1_phys_0_eng_0_engtype_3D"
        items = [{"instance": name, "status": 0, "value": value} for value in (math.nan, -1, None)]
        items.append({"instance": "unrecognized", "status": 0, "value": 20})
        result = telemetry.reduce_instances(items, 42)
        self.assertEqual(len(result["invalid_instances"]), 3)
        self.assertEqual(len(result["unmatched_instances"]), 1)
        self.assertFalse(result["fixture_instances_present"])

    def test_publication_alignment_uses_the_reported_interval_and_refuses_missing_boundaries(self):
        bounds = telemetry.retained_publication_bounds([1, 1.5, 2., 2.5], 1, 3, [.5] * 4)
        self.assertEqual(bounds, (1., 2.))
        for start, stop, durations in ((1, 5, [.5] * 5), (2, 2, [.5] * 4), (1, 3, [.5, math.inf, .5, .5])):
            with self.subTest(start=start, stop=stop), self.assertRaises(telemetry.SmokeFailure):
                telemetry.retained_publication_bounds([1, 1.5, 2., 2.5], start, stop, durations)


if __name__ == "__main__":
    unittest.main()
