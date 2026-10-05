#!/usr/bin/env python3
"""Projection, motion and measured-cost guard tests for the pending SSR-feedback capture suite."""

import math
from dataclasses import replace
from contextlib import redirect_stderr
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import reflection_feedback_smoke as smoke
import smoke_volume_identity as volume_identity
from reflection_smoke import SmokeFailure, panel_projection

LIT_RED = "red"
LIT_GREEN = "green"
LIT_BOUNDARY = "boundary"
LIT_NWB_REFLECTION_SMOKE_FEEDBACK = "NWB_REFLECTION_SMOKE_FEEDBACK"
LIT_NWB_REFLECTION_SMOKE_TEMPORAL = "NWB_REFLECTION_SMOKE_TEMPORAL"
LIT_NWB_REFLECTION_SMOKE_SPATIAL = "NWB_REFLECTION_SMOKE_SPATIAL"
LIT_REFLECTION = "reflection"
LIT_REFLECTION_SCREEN_STEPS = "--reflection-screen-steps"
LIT_FLOOR = "floor"
LIT_GENERATION = "generation"
LIT_SCREEN_HITS = "screen_hits"
LIT_EFFECTIVE_BUDGET = "effective_budget"
LIT_N = "\n"
LIT_CAPTURED_GRAPHICS_FRAME = "captured_graphics_frame"
LIT_EXACT_FIRST_FRAME_COUNTERS_AVAILABLE = "exact_first_frame_counters_available"
LIT_STABLE_FEEDBACK = "stable_feedback"
LIT_OFFSCREEN = "offscreen"
LIT_HARDWARE = "hardware"
LIT_SCREEN_RETURNS = "screen_returns"
LIT_START_GRAPHICS_FRAME = "start_graphics_frame"
LIT_MUTATION_CHANGED = "mutation_changed"
LIT_FEEDBACK_MUTATION = "feedback_mutation"
LIT_RUNTIME_PIPELINE_CACHE = "runtime_pipeline_cache"
LIT_RES = "res"
LIT_CACHE = b"cache"
LIT_RES_1F98ED5C238BF1C3_VOL = "res/1f98ed5c238bf1c3.vol"
LIT_RES_2 = "res/"
LIT_RES_AUTHORED_VOL = "res/authored.vol"
LIT_REFLECTIONSMOKEFEEDBACK = "ReflectionSmokeFeedback:"
LIT_MAIN = "__main__"


def paint_rectangle(rows, projection, color):
    center_x, center_y, half_width, half_height = projection
    for y in range(max(0, math.ceil(center_y - half_height)), min(len(rows), math.floor(center_y + half_height) + 1)):
        for x in range(max(0, math.ceil(center_x - half_width)), min(len(rows[0]), math.floor(center_x + half_width) + 1)):
            rows[y][x] = color


def long_frame(missing=None, shift=0):
    width, height = 400, 300
    rows = [[(60, 60, 60)] * width for _ in range(height)]
    for name, color in ((LIT_RED, (220, 20, 20)), (LIT_GREEN, (20, 220, 20))):
        for reflected in (False, True):
            if missing == (name, reflected):
                continue
            projection = smoke.long_panel_projection(width, height, name, reflected)
            if reflected:
                projection = (projection[0] + shift, *projection[1:])
            paint_rectangle(rows, projection, color)
    return width, height, rows


def mutation_frame(final):
    width, height = 400, 300
    rows = [[(60, 60, 60)] * width for _ in range(height)]
    case = "onscreen" if final else LIT_BOUNDARY
    for name, color in ((LIT_RED, (220, 20, 20)), (LIT_GREEN, (20, 220, 20))):
        for reflected in (False, True):
            paint_rectangle(rows, panel_projection(width, height, name, case, reflected), color)
    return width, height, rows


class CapturePlanTests(unittest.TestCase):
    def test_explicit_guards_do_not_fake_hardware_availability(self):
        spec = smoke.CAPTURES[1]
        with patch.dict("os.environ", {LIT_NWB_REFLECTION_SMOKE_FEEDBACK: "0", LIT_NWB_REFLECTION_SMOKE_TEMPORAL: "1"}):
            environment = smoke.spec_environment(spec)
        self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_FEEDBACK], "1")
        self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_TEMPORAL], "0")
        self.assertEqual(environment[LIT_NWB_REFLECTION_SMOKE_SPATIAL], "0")
        self.assertNotIn("NWB_REFLECTION_SMOKE_HARDWARE_AVAILABLE", environment)

    def test_launcher_rejects_feedback_steps_below_minimum(self):
        location = Path(__file__).resolve().parents[1] / "smoke/launch.py"
        spec = importlib.util.spec_from_file_location("feedback_test_launcher", location)
        launcher = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = launcher
        spec.loader.exec_module(launcher)
        self.assertEqual(launcher.make_parser().parse_args([LIT_REFLECTION, LIT_REFLECTION_SCREEN_STEPS, "8"]).reflection_screen_steps, 8)
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            launcher.make_parser().parse_args([LIT_REFLECTION, LIT_REFLECTION_SCREEN_STEPS, "7"])

class ProjectionTests(unittest.TestCase):
    def test_missing_hardware_marker_fails(self):
        with self.assertRaises(SmokeFailure):
            smoke.analyze_long_miss_panels(long_frame(missing=(LIT_RED, True)))

    def test_wrong_reflected_position_fails(self):
        with self.assertRaises(SmokeFailure):
            smoke.analyze_long_miss_panels(long_frame(shift=20))

    def test_static_comparison_rejects_unrelated_background_change(self):
        first, second = long_frame(), long_frame()
        for y in range(20, 60):
            for x in range(20, 60):
                second[2][y][x] = (160, 160, 160)
        with self.assertRaises(SmokeFailure):
            smoke.compare_static_images(first, second, "feedback_long_miss")


class MutationTests(unittest.TestCase):
    def test_retained_old_marker_fails(self):
        changed = mutation_frame(True)
        paint_rectangle(changed[2], panel_projection(400, 300, LIT_GREEN, LIT_BOUNDARY, True), (20, 220, 20))
        with self.assertRaises(SmokeFailure):
            smoke.compare_mutation_images(mutation_frame(False), changed, mutation_frame(True))

    def test_initial_image_is_not_a_valid_reset(self):
        with self.assertRaises(SmokeFailure):
            smoke.compare_mutation_images(mutation_frame(False), mutation_frame(False), mutation_frame(True))


class TraversalQualificationTests(unittest.TestCase):
    def test_large_attempt_count_with_immediate_rejects_cannot_qualify(self):
        sample = smoke.TraversalEvidence(attempts=100000, iterations=100000, step_limit_misses=0, maximum_steps=16)
        with self.assertRaises(SmokeFailure):
            smoke.qualify_costly_misses([sample] * 8)

    def test_productive_cost_does_not_qualify_as_costly_misses(self):
        sample = smoke.TraversalEvidence(attempts=1000, iterations=15000, step_limit_misses=50, maximum_steps=16)
        with self.assertRaises(SmokeFailure):
            smoke.qualify_costly_misses([sample] * 8)

    def test_impossible_iteration_counts_fail(self):
        for iterations in (100, 17000):
            sample = smoke.TraversalEvidence(attempts=1000, iterations=iterations, step_limit_misses=750, maximum_steps=16)
            with self.assertRaises(SmokeFailure):
                smoke.qualify_costly_misses([sample] * 8)

    def test_bypassed_work_cannot_qualify_a_baseline(self):
        sample = smoke.TraversalEvidence(attempts=1000, iterations=14000, step_limit_misses=750, maximum_steps=16, bypassed_pixels=10)
        with self.assertRaises(SmokeFailure):
            smoke.qualify_costly_misses([sample] * 8)

    def test_one_favorable_observation_is_insufficient(self):
        sample = smoke.TraversalEvidence(attempts=1000, iterations=14000, step_limit_misses=750, maximum_steps=16)
        with self.assertRaises(SmokeFailure):
            smoke.qualify_costly_misses([sample])


class CompletedFeedbackTests(unittest.TestCase):
    def observations(self, enabled=True):
        stats, feedback = [], []
        for index in range(8):
            frame = 32 + 5 * index
            stats.append({"sequence": index + 1, LIT_GENERATION: 2, "frame": frame, "opaque_pixels": 600,
                "glass_pixels": 0, LIT_SCREEN_HITS: 50, "screen_attempts": 100, LIT_EFFECTIVE_BUDGET: smoke.DEFAULT_RAY_BUDGET})
            feedback.append(dict(zip(smoke.FEEDBACK_FIELDS, (index + 1, 2, frame + 100, frame + 40, 1, 100,
                frame if enabled else 0, int(enabled), int(enabled), int(enabled), 0, 0, 1, 600, 60,
                100 if enabled else 0, 1 if enabled else 0, 800, 2))))
        return stats, feedback

    def log(self, samples, source=160, mutation=None):
        result = LIT_N.join("ReflectionSmokeFeedback: " + " ".join(f"{key}={sample[key]}" for key in smoke.FEEDBACK_FIELDS)
            for sample in samples)
        result += "\nFramebufferCapture: graphics source frame " + str(source)
        if mutation is not None:
            result += "\nReflectionSmokeFeedbackMutation: graphics_frame=" + str(source) + " fresh_final=" + str(int(mutation))
        return result

    def validate(self, samples=None, spec=None, stats=None, source=160, mutation=None):
        baseline_stats, baseline_feedback = self.observations()
        return smoke.validate_feedback(self.log(samples if samples is not None else baseline_feedback, source, mutation),
            stats if stats is not None else baseline_stats,
            spec or smoke.FeedbackCapture("floor_feedback", LIT_FLOOR, True))

    def test_metadata_cannot_detach_from_statistics(self):
        _, samples = self.observations()
        samples[-1][LIT_GENERATION] = 3
        with self.assertRaisesRegex(SmokeFailure, "detached"):
            self.validate(samples)

    def test_npot_statistics_use_actual_extent_and_queue_capacity(self):
        from reflection_smoke import STATISTICS_FIELDS
        sample = dict.fromkeys(STATISTICS_FIELDS, 0)
        capacity = 2 * 953 * 713
        sample.update(sequence=1, generation=1, frame=3, mode=2, width=953, height=713,
            requested_budget=smoke.DEFAULT_RAY_BUDGET, effective_budget=capacity, queue_capacity=capacity,
            hardware_requested=1, hardware_available=1, hardware_ready=1, token_value=1, device_generation=1,
            candidates=100, hardware_rays=100, hardware_hits=100, opaque_pixels=100)
        smoke.validate_statistics([sample], LIT_OFFSCREEN, LIT_HARDWARE, extent=(953, 713))
        with self.assertRaisesRegex(SmokeFailure, "dimensions"):
            smoke.validate_statistics([sample], LIT_OFFSCREEN, LIT_HARDWARE)
        sample["queue_capacity"] = smoke.DEFAULT_RAY_BUDGET
        sample[LIT_EFFECTIVE_BUDGET] = smoke.DEFAULT_RAY_BUDGET
        with self.assertRaisesRegex(SmokeFailure, "capacity"):
            smoke.validate_statistics([sample], LIT_OFFSCREEN, LIT_HARDWARE, extent=(953, 713))

    def test_low_budget_blocks_bypass_even_with_previous_valid_header(self):
        stats, samples = self.observations()
        for item in stats:
            item[LIT_EFFECTIVE_BUDGET] = 64
        with self.assertRaisesRegex(SmokeFailure, "upper-bound"):
            self.validate(samples, replace(smoke.CAPTURES[1], hardware_budget=64), stats)

    def test_rough_receivers_never_bypass(self):
        with self.assertRaisesRegex(SmokeFailure, "smooth"):
            self.validate(spec=smoke.FeedbackCapture("rough_feedback", "rough_furnace", True, roughness=1))

    def test_any_return_counter_retains_provisional_hits(self):
        stats, samples = self.observations()
        for item in stats:
            item[LIT_SCREEN_HITS] = 0
        result = self.validate(samples, stats=stats)
        self.assertEqual(result[LIT_STABLE_FEEDBACK][-1][LIT_SCREEN_RETURNS], 60)
        samples[-1][LIT_SCREEN_RETURNS] = 101
        with self.assertRaisesRegex(SmokeFailure, "bounded actual"):
            self.validate(samples, stats=stats)

    def test_disabled_observations_advance_sequence_without_probe_index(self):
        stats, samples = self.observations(False)
        result = self.validate(samples, smoke.FeedbackCapture("floor_baseline", LIT_FLOOR, False), stats)
        self.assertTrue(all(item["probe_index"] == 0 for item in result["feedback"]))

    def test_epoch_start_cannot_move_without_an_epoch_change(self):
        _, samples = self.observations()
        samples[-1][LIT_START_GRAPHICS_FRAME] = 101
        with self.assertRaisesRegex(SmokeFailure, "stable start"):
            self.validate(samples)

    def test_missing_fields_and_duplicate_keys_fail(self):
        _, samples = self.observations()
        for log in ("", self.log(samples).replace("screen_returns=60", "other=60"),
            self.log(samples).replace("sequence=1 ", "sequence=1 sequence=1 ", 1)):
            with self.assertRaises(SmokeFailure):
                smoke.parse_feedback(log)

    def test_first_changed_frame_has_new_anchor_and_no_stale_reuse(self):
        stats, samples = self.observations()
        last = samples[-1]
        last.update(epoch=2, start_graphics_frame=167, probe_index=0, reset=1, reused=0, bypassed_pixels=0, reason=4)
        spec = smoke.FeedbackCapture(LIT_MUTATION_CHANGED, LIT_FEEDBACK_MUTATION, True, mutation=True)
        result = self.validate(samples, spec, stats, source=167, mutation=False)
        self.assertTrue(result[LIT_EXACT_FIRST_FRAME_COUNTERS_AVAILABLE])
        last["bypassed_pixels"] = 1
        with self.assertRaises(SmokeFailure):
            self.validate(samples, spec, stats, source=167, mutation=False)

    def test_skipped_exact_changed_sample_uses_epoch_anchor_without_claiming_counter_proof(self):
        stats, samples = self.observations()
        samples[-1].update(epoch=2, start_graphics_frame=166, probe_index=1, reset=0, reused=1, reason=0)
        spec = smoke.FeedbackCapture(LIT_MUTATION_CHANGED, LIT_FEEDBACK_MUTATION, True, mutation=True)
        result = self.validate(samples, spec, stats, source=166, mutation=False)
        self.assertFalse(result[LIT_EXACT_FIRST_FRAME_COUNTERS_AVAILABLE])
        samples[-1][LIT_START_GRAPHICS_FRAME] = 100
        with self.assertRaisesRegex(SmokeFailure, "previous epoch"):
            self.validate(samples, spec, stats, source=166, mutation=False)


class AuthoredVolumeIdentityTests(unittest.TestCase):
    def test_exact_canonical_cache_hash_and_contiguous_segments_only(self):
        self.assertEqual(volume_identity.volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0), "1f98ed5c238bf1c3.vol")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            resources = root / LIT_RES
            resources.mkdir()
            for index in (0, 1, 3):
                (resources / volume_identity.volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, index)).write_bytes(LIT_CACHE)
            (resources / "other.vol").write_bytes(b"authored")
            (resources / "runtime_pipeline_cache_fake.vol").write_bytes(b"authored too")
            hashes = volume_identity.authored_volume_hashes(root)
            self.assertNotIn(LIT_RES_1F98ED5C238BF1C3_VOL, hashes)
            self.assertNotIn(LIT_RES_2 + volume_identity.volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 1), hashes)
            self.assertIn(LIT_RES_2 + volume_identity.volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 3), hashes)
            self.assertIn("res/other.vol", hashes)
            self.assertIn("res/runtime_pipeline_cache_fake.vol", hashes)

    def test_mutating_cache_does_not_change_authored_identity_but_assets_do(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / LIT_RES).mkdir()
            cache = root / LIT_RES_1F98ED5C238BF1C3_VOL
            asset = root / LIT_RES_AUTHORED_VOL
            cache.write_bytes(b"first cache")
            asset.write_bytes(b"first asset")
            before = volume_identity.authored_volume_hashes(root)
            cache.write_bytes(b"mutated cache")
            self.assertEqual(before, volume_identity.authored_volume_hashes(root))
            asset.write_bytes(b"changed shader")
            self.assertNotEqual(before, volume_identity.authored_volume_hashes(root))

class DiagnosticsOffTests(unittest.TestCase):
    def log(self, source=64):
        return "ReflectionSmokeProject: screen feedback 1\nReflectionSmokeProject: screen steps 96\n" \
            + "FramebufferCapture: graphics source frame " + str(source)

    def test_completed_readback_proof_does_not_invent_feedback_statistics(self):
        spec = smoke.DIAGNOSTICS_OFF_CAPTURES[1]
        result = smoke.validate_diagnostics_off(self.log(), spec)
        self.assertEqual(result[LIT_CAPTURED_GRAPHICS_FRAME], 64)
        self.assertEqual(result["requested_prepared_graphics_frames"], 65)
        self.assertIsNone(result["completed_feedback_observations"])
        self.assertIsNone(result["completed_feedback_counters"])

    def test_early_or_unanchored_capture_fails(self):
        for log in (self.log(63), self.log() + "\nFramebufferCapture: graphics source frame 65", ""):
            with self.assertRaises(SmokeFailure):
                smoke.validate_diagnostics_off(log, smoke.DIAGNOSTICS_OFF_CAPTURES[1])

    def test_unexpected_completed_diagnostics_fail(self):
        for prefix in (LIT_REFLECTIONSMOKEFEEDBACK, "ReflectionSmokeStatistics:", "ReflectionSmokeHistory:", "ReflectionSmokeOptics:"):
            with self.assertRaisesRegex(SmokeFailure, "unexpectedly published"):
                smoke.validate_diagnostics_off(self.log() + LIT_N + prefix, smoke.DIAGNOSTICS_OFF_CAPTURES[1])

if __name__ == LIT_MAIN:
    unittest.main()
