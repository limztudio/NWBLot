#!/usr/bin/env python3
"""Stress reflection diagnostic and quality replay; never launch a renderer."""


from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_timing_smoke as smoke
from stress_timing_test_support import reflection_log, reflection_record, shadow_quality_record, valid_log, workload_record


LIT_ALL_REJECTED = "all_rejected"
LIT_QUERY_BUDGET_UNITS_PER_HARDWARE_RAY = "query_budget_units_per_hardware_ray"
LIT_MALFORMED = "malformed"
LIT_N_96 = "96"
LIT_RENDERER_EXE = "renderer.exe"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_SYS_STDERR = "sys.stderr"
LIT_VERIFIED = "verified"
LIT_N_1 = "-1"
LIT_BAD = "bad"
LIT_UTF_8 = "utf-8"
LIT_FIXTURE = "fixture"
LIT_OPTICAL_REFLECTION = "optical_reflection"
LIT_STATUS = "status"
LIT_SAMPLE_COUNT = "sample_count"
LIT_SUMS = "sums"
LIT_UNSUPPORTED_RATIO = "unsupported_ratio"
LIT_QUERIES_PER_HARDWARE_RAY = "physical_queries_per_hardware_ray"
LIT_EXTERIOR_ELIGIBLE_RATIO = "exterior_eligible_ratio"
LIT_RANGES_BY_GENERATION = "ranges_by_generation"
LIT_GENERATION = "generation"
LIT_UNSUPPORTED = "unsupported"
LIT_HARDWARE_READY_SAMPLES = "hardware_ready_samples"
LIT_UNSUPPORTED_PATHS_10 = " unsupported_paths=10"
LIT_EXTRA_0 = " extra=0"
LIT_QUERY_BUDGET_UNITS_0 = "query_budget_units=0"
LIT_REFLECTION_SCREEN_STEPS = "--reflection-screen-steps"
LIT_NWB_REFLECTION_SCREEN_STEPS = "NWB_REFLECTION_SCREEN_STEPS"
LIT_SCREEN_WORK_MEASURED = "screen_work_measured"
LIT_N_48 = "48"
LIT_SCREEN = "screen"
LIT_MAIN = "__main__"


class StressReflectionDiagnosticTests(unittest.TestCase):
    def test_unrequested_reflection_evidence_is_rejected(self):
        with self.assertRaisesRegex(smoke.SmokeFailure, "unrequested reflection"):
            smoke.parse_runtime_log(reflection_log(reflection_record()), 0)

    def test_all_rejected_reflections_are_exposed_without_changing_presentation_result(self):
        text = reflection_log(reflection_record(sequence=3, frame=11, graphics_frame=14), reflection_record(sequence=7, frame=15, graphics_frame=18))
        result = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)
        optical = result[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_STATUS], LIT_ALL_REJECTED)
        self.assertEqual(optical[LIT_SAMPLE_COUNT], 2)
        self.assertEqual(optical[LIT_SUMS]["hardware_rays"], 20)
        self.assertEqual(optical[LIT_SUMS]["unsupported_paths"], 20)
        self.assertEqual(optical[LIT_UNSUPPORTED_RATIO], 1.)
        self.assertEqual(optical[LIT_QUERIES_PER_HARDWARE_RAY], 0.)
        self.assertEqual(optical[LIT_EXTERIOR_ELIGIBLE_RATIO], 0.)

    def test_partial_unsupported_ratios_are_weighted_by_admitted_rays(self):
        text = reflection_log(reflection_record(candidates=1, hardware_rays=1, unsupported_paths=1), reflection_record(sequence=2, frame=11, graphics_frame=11, candidates=9, hardware_rays=9,
                query_budget_units=18, physical_queries=18, bootstrap_events=23, transparent_paths=3, exterior_eligible_rays=6, unsupported_paths=0))
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_STATUS], LIT_UNSUPPORTED)
        self.assertAlmostEqual(optical[LIT_UNSUPPORTED_RATIO], .1)
        self.assertAlmostEqual(optical[LIT_EXTERIOR_ELIGIBLE_RATIO], .6)
        self.assertAlmostEqual(optical[LIT_QUERIES_PER_HARDWARE_RAY], 1.8)

    def test_queries_are_observations_not_a_complete_optical_support_claim(self):
        for unsupported, expected in ((0, "queries_observed"), (10, LIT_UNSUPPORTED)):
            with self.subTest(unsupported=unsupported):
                text = reflection_log(reflection_record(query_budget_units=20, physical_queries=20, bootstrap_events=4, transparent_paths=2, unsupported_paths=unsupported))
                optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
                self.assertEqual(optical[LIT_STATUS], expected)
                self.assertEqual(optical[LIT_QUERIES_PER_HARDWARE_RAY], 2.)

    def test_budget_reservation_cannot_be_reported_as_a_physical_query(self):
        text = reflection_log(reflection_record(query_budget_units=10, physical_queries=0))
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_STATUS], LIT_ALL_REJECTED)
        self.assertEqual(optical[LIT_QUERY_BUDGET_UNITS_PER_HARDWARE_RAY], 1.)
        self.assertEqual(optical[LIT_QUERIES_PER_HARDWARE_RAY], 0.)

    def test_zero_ray_samples_do_not_produce_false_support_or_zero_ratios(self):
        text = reflection_log(reflection_record(hardware_ready=0, transport_enabled=0, candidates=0, hardware_rays=0, unsupported_paths=0))
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_STATUS], "no_queries")
        self.assertIsNone(optical[LIT_UNSUPPORTED_RATIO])
        self.assertIsNone(optical[LIT_EXTERIOR_ELIGIBLE_RATIO])
        self.assertIsNone(optical[LIT_QUERIES_PER_HARDWARE_RAY])
        self.assertEqual(optical[LIT_HARDWARE_READY_SAMPLES], 0)

    def test_requested_diagnostics_require_enablement_and_samples_before_shutdown(self):
        valid = reflection_log(reflection_record())
        variants = (valid_log(), reflection_log(), valid.replace(smoke.REFLECTION_ENABLED, ""), valid + smoke.REFLECTION_ENABLED, valid.replace(reflection_record(), "") + reflection_record(), reflection_record() + "\n" + valid.replace(reflection_record(), ""))
        for text in variants:
            with self.subTest(text=text[-140:]), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)

    def test_duplicate_regressing_and_revisited_generations_are_rejected(self):
        first = reflection_record()
        variants = ((first, first), (reflection_record(sequence=4), reflection_record(sequence=3, frame=11, graphics_frame=11)), (first, reflection_record(sequence=2)),
            (first, reflection_record(generation=2), reflection_record(sequence=2, frame=11, graphics_frame=11)))
        for rows in variants:
            with self.subTest(rows=rows), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(*rows), 0, reflection_diagnostics=True)
        optical = smoke.parse_runtime_log(reflection_log(first, reflection_record(generation=2)), 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual([row[LIT_GENERATION] for row in optical[LIT_RANGES_BY_GENERATION]], [1, 2])

    def test_numeric_bounds_flags_and_impossible_counter_relationships_are_rejected(self):
        variants = (dict(sequence=0), dict(generation=0), dict(sequence=2 ** 64), dict(frame=2 ** 32), dict(graphics_frame=2 ** 64), dict(query_budget_units=2 ** 32), dict(physical_queries=2 ** 32), dict(physical_queries=1), dict(hardware_ready=2),
            dict(transport_enabled=2), dict(hardware_ready=0), dict(transport_enabled=0), dict(candidates=9), dict(exterior_eligible_rays=11), dict(transparent_paths=11, query_budget_units=20),
            dict(unsupported_paths=11), dict(bootstrap_events=1), dict(transparent_paths=1), dict(hardware_rays=0, unsupported_paths=0, query_budget_units=1))
        for change in variants:
            with self.subTest(change=change), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(reflection_record(**change)), 0, reflection_diagnostics=True)

    def test_missing_duplicate_extra_and_noninteger_fields_are_rejected(self):
        valid = reflection_record()
        variants = (valid.replace(LIT_UNSUPPORTED_PATHS_10, ""), valid + LIT_UNSUPPORTED_PATHS_10, valid + LIT_EXTRA_0, valid.replace(LIT_QUERY_BUDGET_UNITS_0, "query_budget_units=-1"), valid.replace(LIT_QUERY_BUDGET_UNITS_0, "query_budget_units=nan"))
        for record in variants:
            with self.subTest(record=record), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(record), 0, reflection_diagnostics=True)


class StressReflectionQualityTests(unittest.TestCase):
    def test_explicit_steps_validate_and_replace_inherited_environment(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            executable = output / LIT_RENDERER_EXE
            executable.write_bytes(LIT_FIXTURE.encode(LIT_UTF_8))
            required = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(output), LIT_NO_LOGSERVER]
            for steps in (8, 256):
                args = smoke.parse_args(required + [LIT_REFLECTION_SCREEN_STEPS, str(steps)])
                env = smoke.launch_environment({LIT_NWB_REFLECTION_SCREEN_STEPS: "1"}, args, output)
                self.assertEqual(env[LIT_NWB_REFLECTION_SCREEN_STEPS], str(steps))
                result = smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + str(steps), args, {})
                self.assertTrue(result[LIT_VERIFIED])
            for invalid in ("0", "7", "257", LIT_N_1, "48.5", LIT_BAD):
                with patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                    smoke.parse_args(required + [LIT_REFLECTION_SCREEN_STEPS, invalid])

    def test_applied_budget_missing_duplicate_malformed_and_mismatch_fail(self):
        args = SimpleNamespace(reflection_screen_steps=48, reflection_diagnostics=False)
        good = smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_48
        for bad in ("", good + "\n" + good, good + LIT_EXTRA_0, smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_96):
            with self.subTest(bad=bad), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_reflection_quality_settings(bad, args, {})

    def test_actual_software_screen_work_is_independent_of_hardware_counters(self):
        row = reflection_record(hardware_ready=0, transport_enabled=0, candidates=0, hardware_rays=0, unsupported_paths=0, screen_attempts=20, screen_hits=2, screen_returns=4, screen_iterations=640, screen_limit_misses=10)
        text = reflection_log(row)
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_HARDWARE_READY_SAMPLES], 0)
        self.assertEqual(optical[LIT_SCREEN]["iterations_per_attempt"], 32)
        self.assertEqual(optical[LIT_SCREEN]["limit_miss_ratio"], .5)
        self.assertEqual(optical[LIT_SCREEN]["return_ratio"], .2)
        args = SimpleNamespace(reflection_screen_steps=48, reflection_diagnostics=True)
        self.assertTrue(smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_48, args, optical)[LIT_SCREEN_WORK_MEASURED])
        args.reflection_screen_steps = 8
        with self.assertRaisesRegex(smoke.SmokeFailure, "exceeds"):
            smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + "8", args, optical)

    def test_missing_screen_counters_reject_and_complete_current_records_recover(self):
        current = reflection_record(screen_attempts=1, screen_returns=1, screen_iterations=96)
        fields = current.split()
        for field in smoke.REFLECTION_SCREEN_FIELDS:
            missing = " ".join(value for value in fields if not value.startswith(field + "="))
            with self.subTest(field=field), self.assertRaisesRegex(smoke.SmokeFailure, LIT_MALFORMED):
                smoke.parse_runtime_log(reflection_log(missing), 0, reflection_diagnostics=True)
        without_screen = " ".join(value for value in fields if not any(value.startswith(field + "=") for field in smoke.REFLECTION_SCREEN_FIELDS))
        with self.assertRaisesRegex(smoke.SmokeFailure, LIT_MALFORMED):
            smoke.parse_runtime_log(reflection_log(without_screen), 0, reflection_diagnostics=True)
        optical = smoke.parse_runtime_log(reflection_log(current), 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_SCREEN][LIT_SAMPLE_COUNT], 1)
        args = SimpleNamespace(reflection_screen_steps=96, reflection_diagnostics=True)
        self.assertTrue(smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_96, args, optical)[LIT_SCREEN_WORK_MEASURED])

    def test_malformed_partial_overflow_or_impossible_screen_counters_fail(self):
        prefix = reflection_record().split(" " + smoke.REFLECTION_SCREEN_FIELDS[0] + "=")[0]
        tails = (" screen_attempts=1", " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=0", " screen_attempts=1 screen_hits=2 screen_returns=1 screen_iterations=48 screen_limit_misses=0",
            " screen_attempts=1 screen_hits=0 screen_returns=2 screen_iterations=48 screen_limit_misses=0", " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=48 screen_limit_misses=2",
            " screen_attempts=0 screen_hits=0 screen_returns=0 screen_iterations=1 screen_limit_misses=0", " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=257 screen_limit_misses=0",
            " screen_attempts=4294967296 screen_hits=0 screen_returns=0 screen_iterations=0 screen_limit_misses=0", " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=18446744073709551616 screen_limit_misses=0")
        for tail in tails:
            with self.subTest(tail=tail), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(prefix + tail), 0, reflection_diagnostics=True)


if __name__ == LIT_MAIN:
    unittest.main()
