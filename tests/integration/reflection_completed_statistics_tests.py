#!/usr/bin/env python3
"""Validate completed reflection statistics and accepted-token evidence."""

import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from reflection_statistics import ReflectionStatistics, STATISTICS_FIELDS  # noqa: E402
from reflection_smoke import BUDGET_CAPTURES, DEFAULT_RAY_BUDGET, SmokeFailure, capture_environment  # noqa: E402

LIT_CANDIDATES = 'candidates'
LIT_DEVICE_GENERATION = 'device_generation'
LIT_DISABLED = 'disabled'
LIT_EXCEEDED_BUDGET = 'exceeded.*budget'
LIT_FALLBACK_PIXELS = 'fallback_pixels'
LIT_FLOOR = 'floor'
LIT_HARDWARE = 'hardware'
LIT_HARDWARE_HITS = 'hardware_hits'
LIT_HARDWARE_RAYS = 'hardware_rays'
LIT_HYBRID = 'hybrid'
LIT_NWB_REFLECTION_SMOKE_RAY_BUDGET = 'NWB_REFLECTION_SMOKE_RAY_BUDGET'
LIT_OPAQUE_PIXELS = 'opaque_pixels'
LIT_OPTICAL_CSG_CAP = 'optical_csg_cap'
LIT_PARTITION = 'partition'
LIT_REFLECTION_RESOLVE_HARDWARE = 'Reflection resolve: hardware'
LIT_ROUGH = 'rough'
LIT_SCREEN = 'screen'
LIT_SCREEN_HITS = 'screen_hits'
LIT_SCREEN_HITS_600 = 'screen_hits=600'


class ReflectionCompletedStatisticsTests(unittest.TestCase):
    @staticmethod
    def sample(mode=LIT_HYBRID, budget=DEFAULT_RAY_BUDGET, sequence=1, frame=3):
        requested = mode in (LIT_HARDWARE, LIT_HYBRID)
        value = dict(sequence=sequence, generation=1, frame=frame, mode=(LIT_DISABLED, LIT_SCREEN, LIT_HARDWARE, LIT_HYBRID).index(mode), width=960, height=720, requested_budget=budget, effective_budget=budget, queue_capacity=DEFAULT_RAY_BUDGET,
            hardware_requested=int(requested), hardware_available=1, hardware_ready=int(requested), token_queue=0, token_value=10 + sequence, physical_queue=0, device_generation=1,
            candidates=0, hardware_rays=0, hardware_hits=0, opaque_pixels=0, glass_pixels=0, fallback_pixels=0, screen_attempts=0, screen_hits=0)
        if mode != LIT_DISABLED:
            value[LIT_OPAQUE_PIXELS] = 1000
            value["screen_attempts"] = 1000 if mode in (LIT_SCREEN, LIT_HYBRID) else 0
            value[LIT_SCREEN_HITS] = 600 if mode in (LIT_SCREEN, LIT_HYBRID) else 0
            value[LIT_CANDIDATES] = 1000 - value[LIT_SCREEN_HITS] if requested else 0
            value[LIT_HARDWARE_RAYS] = min(value[LIT_CANDIDATES], budget)
            value[LIT_HARDWARE_HITS] = value[LIT_HARDWARE_RAYS] // 2
            value[LIT_FALLBACK_PIXELS] = 1000 - value[LIT_SCREEN_HITS] - value[LIT_HARDWARE_HITS]
        return value

    @staticmethod
    def log_line(sample):
        return "ReflectionSmokeStatistics: " + " ".join(f"{name}={sample[name]}" for name in STATISTICS_FIELDS)


    def test_parser_rejects_missing_duplicate_and_noninteger_fields(self):
        line = self.log_line(self.sample())
        for malformed in (line.rsplit(" ", 1)[0], line + " sequence=1", line.replace(LIT_SCREEN_HITS_600, "screen_hits=nan"), line.replace(LIT_SCREEN_HITS_600, "unknown=600")):
            with self.subTest(malformed=malformed), self.assertRaises(SmokeFailure):
                ReflectionStatistics.parse_statistics(malformed)
        with self.assertRaisesRegex(SmokeFailure, "no completed"):
            ReflectionStatistics.parse_statistics(LIT_REFLECTION_RESOLVE_HARDWARE)

    def test_zero_and_exhausted_ray_budgets_preserve_completed_fallback_partition(self):
        for case, mode, budget in BUDGET_CAPTURES:
            sample = self.sample(mode, budget)
            self.assertGreater(sample[LIT_FALLBACK_PIXELS], 0)
            self.assertEqual(ReflectionStatistics.validate_statistics([sample], case, mode, budget), [sample])

    def test_zero_budget_omits_prepared_scene_without_relaxing_positive_budget_admission(self):
        # Actual zero-budget CSG capture: hardware was advertised at startup, while no hardware scene was prepared.
        log = ("ReflectionSmokeStatistics: sequence=3 generation=2 frame=3 mode=2 width=960 height=720 " "requested_budget=0 effective_budget=0 queue_capacity=1 hardware_requested=1 hardware_available=0 "
            "hardware_ready=0 token_queue=0 token_value=81 physical_queue=0 device_generation=1 candidates=0 " "hardware_rays=0 hardware_hits=0 opaque_pixels=217668 glass_pixels=0 fallback_pixels=217668 " "screen_attempts=0 screen_hits=0")
        sample = ReflectionStatistics.parse_statistics(log)[0]
        ReflectionStatistics.validate_statistics([sample], LIT_OPTICAL_CSG_CAP, LIT_HARDWARE, budget=0)
        sample[LIT_HARDWARE_RAYS] = 1
        with self.assertRaisesRegex(SmokeFailure, LIT_EXCEEDED_BUDGET):
            ReflectionStatistics.validate_statistics([sample], LIT_OPTICAL_CSG_CAP, LIT_HARDWARE, budget=0)
        for available, ready in ((0, 0), (0, 1), (1, 0)):
            sample = self.sample(LIT_HARDWARE, budget=1)
            sample.update(hardware_available=available, hardware_ready=ready, hardware_rays=0, hardware_hits=0, fallback_pixels=1000)
            with self.subTest(available=available, ready=ready), self.assertRaisesRegex(SmokeFailure, "ready hardware route"):
                ReflectionStatistics.validate_statistics([sample], LIT_OPTICAL_CSG_CAP, LIT_HARDWARE, budget=1)

    def test_static_capture_rejects_wrong_mode_dimensions_budget_and_generation(self):
        for field, value in (("mode", 1), ("width", 1920), ("requested_budget", 64), ("effective_budget", 64), ("queue_capacity", 0), ("generation", 2), (LIT_DEVICE_GENERATION, 2)):
            second = self.sample(sequence=2, frame=4)
            second[field] = value
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                ReflectionStatistics.validate_statistics([self.sample(), second], LIT_FLOOR, LIT_HYBRID)

    def test_rejected_token_stale_sample_and_uncompleted_warmup_fail(self):
        for field, value in (("token_value", 0), (LIT_DEVICE_GENERATION, 0), ("physical_queue", 65535)):
            sample = self.sample()
            sample[field] = value
            with self.subTest(field=field), self.assertRaisesRegex(SmokeFailure, "accepted token"):
                ReflectionStatistics.validate_statistics([sample], LIT_FLOOR, LIT_HYBRID)
        for second in (self.sample(), self.sample(sequence=2, frame=3)):
            with self.assertRaisesRegex(SmokeFailure, "stale or out of order"):
                ReflectionStatistics.validate_statistics([self.sample(), second], LIT_FLOOR, LIT_HYBRID)
        with self.assertRaisesRegex(SmokeFailure, "stable frame 3"):
            ReflectionStatistics.validate_statistics([self.sample(frame=2)], LIT_FLOOR, LIT_HYBRID)

    def test_every_sample_must_obey_budget_including_warmup(self):
        bad = self.sample(budget=64, frame=1)
        bad[LIT_HARDWARE_RAYS] = 65
        with self.assertRaisesRegex(SmokeFailure, LIT_EXCEEDED_BUDGET):
            ReflectionStatistics.validate_statistics([bad, self.sample(budget=64, sequence=2)], LIT_FLOOR, LIT_HYBRID, 64)
        bad = self.sample(budget=0)
        bad[LIT_HARDWARE_RAYS] = 1
        with self.assertRaisesRegex(SmokeFailure, LIT_EXCEEDED_BUDGET):
            ReflectionStatistics.validate_statistics([bad], LIT_FLOOR, LIT_HYBRID, 0)

    def test_missing_outcome_duplicate_count_and_impossible_population_fail(self):
        for field, value in ((LIT_FALLBACK_PIXELS, 199), (LIT_HARDWARE_HITS, 201), (LIT_SCREEN_HITS, 1001), (LIT_OPAQUE_PIXELS, 960 * 720 + 1)):
            sample = self.sample()
            sample[field] = value
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                ReflectionStatistics.validate_statistics([sample], LIT_FLOOR, LIT_HYBRID)

    def test_rough_opaque_zero_samples_preserve_ray_budget_and_cannot_mask_excess_outcomes(self):
        sample = self.sample(LIT_HARDWARE)
        sample.update(candidates=600, hardware_rays=600, hardware_hits=300, fallback_pixels=300)
        ReflectionStatistics.validate_statistics([sample], LIT_ROUGH, LIT_HARDWARE, allow_zero_samples=True)
        with self.assertRaisesRegex(SmokeFailure, LIT_PARTITION):
            ReflectionStatistics.validate_statistics([sample], LIT_ROUGH, LIT_HARDWARE)
        sample.update(hardware_rays=0, hardware_hits=0)
        with self.assertRaisesRegex(SmokeFailure, "bounded candidate queue"):
            ReflectionStatistics.validate_statistics([sample], LIT_ROUGH, LIT_HARDWARE, allow_zero_samples=True)
        sample.update(hardware_rays=600, hardware_hits=300, fallback_pixels=701)
        with self.assertRaisesRegex(SmokeFailure, LIT_PARTITION):
            ReflectionStatistics.validate_statistics([sample], LIT_ROUGH, LIT_HARDWARE, allow_zero_samples=True)

    def test_screen_route_cannot_issue_hardware_work(self):
        sample = self.sample(LIT_SCREEN)
        sample.update(candidates=1, hardware_rays=1, hardware_ready=1)
        with self.assertRaisesRegex(SmokeFailure, "non-hardware"):
            ReflectionStatistics.validate_statistics([sample], LIT_FLOOR, LIT_SCREEN)

    def test_disabled_nonzero_counter_and_missing_glass_are_rejected(self):
        disabled = self.sample(LIT_DISABLED)
        disabled.update(opaque_pixels=1, fallback_pixels=1)
        with self.assertRaisesRegex(SmokeFailure, "disabled reflection"):
            ReflectionStatistics.validate_statistics([disabled], LIT_FLOOR, LIT_DISABLED)
        with self.assertRaisesRegex(SmokeFailure, "missing primary glass"):
            ReflectionStatistics.validate_statistics([self.sample(LIT_HARDWARE)], "opaque_glass", LIT_HARDWARE)

    def test_provisional_screen_result_cannot_pass_as_an_accepted_floor_hit(self):
        sample = self.sample(LIT_SCREEN)
        sample.update(screen_hits=0, fallback_pixels=1000)
        with self.assertRaisesRegex(SmokeFailure, "accepted screen hits"):
            ReflectionStatistics.validate_statistics([sample], LIT_FLOOR, LIT_SCREEN)

    def test_floor_ray_savings_require_accepted_hits_and_separated_completed_ranges(self):
        hardware = [self.sample(LIT_HARDWARE)]
        hybrid = [self.sample()]
        result = ReflectionStatistics.compare_hybrid_statistics(hardware, hybrid)
        self.assertEqual(result["hardware_ray_range"], [1000, 1000])
        self.assertEqual(result["hybrid_hardware_ray_range"], [400, 400])
        equal = self.sample()
        equal[LIT_HARDWARE_RAYS] = 1000
        with self.assertRaisesRegex(SmokeFailure, "reduce completed hardware rays"):
            ReflectionStatistics.compare_hybrid_statistics(hardware, [equal])
        provisional = self.sample()
        provisional[LIT_SCREEN_HITS] = 0
        with self.assertRaisesRegex(SmokeFailure, "accepting screen hits"):
            ReflectionStatistics.compare_hybrid_statistics(hardware, [provisional])

    def test_budget_environment_retains_explicit_zero(self):
        with patch.dict(os.environ, {LIT_NWB_REFLECTION_SMOKE_RAY_BUDGET: "999"}):
            result = capture_environment(LIT_FLOOR, LIT_HYBRID, 0)
        self.assertEqual(result[LIT_NWB_REFLECTION_SMOKE_RAY_BUDGET], "0")

    def test_reduced_population_cannot_pass_as_hybrid_ray_savings(self):
        hybrid = self.sample()
        hybrid[LIT_OPAQUE_PIXELS] = 500
        with self.assertRaisesRegex(SmokeFailure, "matched eligible populations"):
            ReflectionStatistics.compare_hybrid_statistics([self.sample(LIT_HARDWARE)], [hybrid])


if __name__ == "__main__":
    unittest.main()
