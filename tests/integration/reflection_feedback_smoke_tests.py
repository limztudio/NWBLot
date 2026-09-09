#!/usr/bin/env python3
"""Projection, motion and measured-cost guard tests for the pending SSR-feedback capture suite."""

import math
from dataclasses import replace
from contextlib import redirect_stderr
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import reflection_feedback_smoke as smoke
import smoke_volume_identity as volume_identity
from reflection_smoke import SmokeFailure, panel_projection


def paint_rectangle(rows, projection, color):
    center_x, center_y, half_width, half_height = projection
    for y in range(max(0, math.ceil(center_y - half_height)), min(len(rows), math.floor(center_y + half_height) + 1)):
        for x in range(max(0, math.ceil(center_x - half_width)), min(len(rows[0]), math.floor(center_x + half_width) + 1)):
            rows[y][x] = color


def long_frame(missing=None, shift=0):
    width, height = 400, 300
    rows = [[(60, 60, 60)] * width for _ in range(height)]
    for name, color in (("red", (220, 20, 20)), ("green", (20, 220, 20))):
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
    case = "onscreen" if final else "boundary"
    for name, color in (("red", (220, 20, 20)), ("green", (20, 220, 20))):
        for reflected in (False, True):
            paint_rectangle(rows, panel_projection(width, height, name, case, reflected), color)
    return width, height, rows


class CapturePlanTests(unittest.TestCase):
    def test_plan_is_bounded_and_has_full_pairs(self):
        self.assertEqual(len(smoke.CAPTURES), 24)
        self.assertEqual(len({spec.name for spec in smoke.CAPTURES}), len(smoke.CAPTURES))
        for prefix, _, _ in smoke.PAIR_CASES:
            baseline = next(spec for spec in smoke.CAPTURES if spec.name == prefix + "_baseline")
            feedback = next(spec for spec in smoke.CAPTURES if spec.name == prefix + "_feedback")
            self.assertFalse(baseline.enabled)
            self.assertTrue(feedback.enabled)
            for field in ("case", "mode", "hardware_budget", "accepted_frames", "screen_steps", "roughness", "extent"):
                self.assertEqual(getattr(baseline, field), getattr(feedback, field))

    def test_explicit_guards_do_not_fake_hardware_availability(self):
        self.assertTrue(any(spec.hardware_budget == 0 for spec in smoke.CAPTURES))
        self.assertTrue(any(spec.hardware_budget == 64 for spec in smoke.CAPTURES))
        self.assertTrue(any(spec.mode == "screen" for spec in smoke.CAPTURES))
        self.assertTrue(any(spec.roughness == 1 for spec in smoke.CAPTURES))
        spec = smoke.CAPTURES[1]
        with patch.dict("os.environ", {"NWB_REFLECTION_SMOKE_FEEDBACK": "0", "NWB_REFLECTION_SMOKE_TEMPORAL": "1"}):
            environment = smoke.spec_environment(spec)
        self.assertEqual(environment["NWB_REFLECTION_SMOKE_FEEDBACK"], "1")
        self.assertEqual(environment["NWB_REFLECTION_SMOKE_TEMPORAL"], "0")
        self.assertEqual(environment["NWB_REFLECTION_SMOKE_SPATIAL"], "0")
        self.assertNotIn("NWB_REFLECTION_SMOKE_HARDWARE_AVAILABLE", environment)

    def test_npot_pair_uses_a_fixed_partial_workgroup_extent(self):
        specs = [spec for spec in smoke.CAPTURES if spec.extent == "npot"]
        self.assertEqual(len(specs), 2)
        for spec in specs:
            self.assertEqual(spec.dimensions, (953, 713))
            self.assertNotEqual(spec.dimensions[0] % 8, 0)
            self.assertNotEqual(spec.dimensions[1] % 8, 0)
            self.assertEqual(smoke.spec_environment(spec)["NWB_REFLECTION_SMOKE_EXTENT"], "npot")

    def test_launcher_forwards_typed_feedback_steps_and_fixed_extent(self):
        location = Path(__file__).resolve().parents[1] / "smoke/launch.py"
        spec = importlib.util.spec_from_file_location("feedback_test_launcher", location)
        launcher = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = launcher
        spec.loader.exec_module(launcher)
        args = launcher.make_parser().parse_args(["reflection", "--reflection-feedback", "on",
            "--reflection-screen-steps", "16", "--reflection-extent", "npot"])
        environment = launcher.build_smoke_environment(args)
        self.assertEqual(environment["NWB_REFLECTION_SMOKE_FEEDBACK"], "1")
        self.assertEqual(environment["NWB_REFLECTION_SMOKE_SCREEN_STEPS"], "16")
        self.assertEqual(environment["NWB_REFLECTION_SMOKE_EXTENT"], "npot")
        self.assertEqual(launcher.make_parser().parse_args(["reflection", "--reflection-screen-steps", "8"]).reflection_screen_steps, 8)
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            launcher.make_parser().parse_args(["reflection", "--reflection-screen-steps", "7"])

    def test_zero_budget_capture_requires_actual_screen_route_and_accepted_settings(self):
        args = SimpleNamespace(output_directory=Path("unused"), executable=Path("app.exe"),
            working_directory=Path("runtime"), timeout=60, require_hardware=True,
            logserver_executable=None, application_arg=[])
        spec = smoke.FeedbackCapture("zero", "floor", True, hardware_budget=0, screen_steps=16)
        with patch.object(smoke.subprocess, "run", return_value=SimpleNamespace(returncode=77)) as run:
            self.assertIsNone(smoke.capture(args, spec))
        command = run.call_args.args[0]
        pairs = set(zip(command, command[1:]))
        self.assertIn(("--expect-log-message", "Reflection resolve: screen-space"), pairs)
        self.assertIn(("--expect-log-message", "ReflectionSmokeProject: screen feedback 1"), pairs)
        self.assertIn(("--expect-log-message", "ReflectionSmokeProject: screen steps 16"), pairs)
        self.assertNotIn(("--expect-log-message", "Reflection resolve: hardware"), pairs)


class ProjectionTests(unittest.TestCase):
    def test_long_panel_reflection_uses_floor_virtual_image(self):
        direct = smoke.long_panel_projection(960, 720, "red", False)
        reflected = smoke.long_panel_projection(960, 720, "red", True)
        self.assertAlmostEqual(direct[0], 391.66540881398724)
        self.assertAlmostEqual(reflected[0], direct[0])
        self.assertAlmostEqual(reflected[1] - direct[1], 374.1229744348775)
        fraction = 1.4 / (1.4 + 3.6)
        receiver_z = -6 + 12 * fraction
        self.assertAlmostEqual(receiver_z, -2.64)
        self.assertGreater(receiver_z, -4)
        self.assertLess(receiver_z, 8)

    def test_long_direct_and_reflected_markers_pass(self):
        result = smoke.analyze_long_miss_panels(long_frame())
        self.assertGreater(result["red"]["reflection"]["pixels"], 100)
        self.assertGreater(result["green"]["projected_traversal_pixels"], 150)

    def test_missing_hardware_marker_fails(self):
        with self.assertRaises(SmokeFailure):
            smoke.analyze_long_miss_panels(long_frame(missing=("red", True)))

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
    def test_moved_marker_matches_fresh_and_preserves_control(self):
        result = smoke.compare_mutation_images(mutation_frame(False), mutation_frame(True), mutation_frame(True))
        self.assertEqual(result["changed_equals_fresh"]["mean_channel_byte_error"], 0)
        self.assertLess(result["measured_green_motion_pixels"], -30)

    def test_retained_old_marker_fails(self):
        changed = mutation_frame(True)
        paint_rectangle(changed[2], panel_projection(400, 300, "green", "boundary", True), (20, 220, 20))
        with self.assertRaises(SmokeFailure):
            smoke.compare_mutation_images(mutation_frame(False), changed, mutation_frame(True))

    def test_initial_image_is_not_a_valid_reset(self):
        with self.assertRaises(SmokeFailure):
            smoke.compare_mutation_images(mutation_frame(False), mutation_frame(False), mutation_frame(True))


class TraversalQualificationTests(unittest.TestCase):
    def test_actual_long_work_qualifies_without_claiming_speed(self):
        sample = smoke.TraversalEvidence(attempts=1000, iterations=14000, step_limit_misses=750, maximum_steps=16)
        result = smoke.qualify_costly_misses([sample] * 8)
        self.assertEqual(result["mean_iterations_per_attempt"], 14)
        self.assertEqual(result["step_limit_miss_fraction"], 0.75)
        self.assertIn("timing comparison remains separate", result["qualification"])

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
            stats.append({"sequence": index + 1, "generation": 2, "frame": frame, "opaque_pixels": 600,
                "glass_pixels": 0, "screen_hits": 50, "screen_attempts": 100, "effective_budget": smoke.DEFAULT_RAY_BUDGET})
            feedback.append(dict(zip(smoke.FEEDBACK_FIELDS, (index + 1, 2, frame + 100, frame + 40, 1, 100,
                frame if enabled else 0, int(enabled), int(enabled), int(enabled), 0, 0, 1, 600, 60,
                100 if enabled else 0, 1 if enabled else 0, 800, 2))))
        return stats, feedback

    def log(self, samples, source=160, mutation=None):
        result = "\n".join("ReflectionSmokeFeedback: " + " ".join(f"{key}={sample[key]}" for key in smoke.FEEDBACK_FIELDS)
            for sample in samples)
        result += "\nFramebufferCapture: graphics source frame " + str(source)
        if mutation is not None:
            result += "\nReflectionSmokeFeedbackMutation: graphics_frame=" + str(source) + " fresh_final=" + str(int(mutation))
        return result

    def validate(self, samples=None, spec=None, stats=None, source=160, mutation=None):
        baseline_stats, baseline_feedback = self.observations()
        return smoke.validate_feedback(self.log(samples if samples is not None else baseline_feedback, source, mutation),
            stats if stats is not None else baseline_stats,
            spec or smoke.FeedbackCapture("floor_feedback", "floor", True))

    def test_completed_feedback_has_source_and_token_join(self):
        result = self.validate()
        self.assertEqual(result["captured_graphics_frame"], 160)
        self.assertFalse(result["exact_first_frame_counters_available"])
        self.assertEqual(len(result["stable_feedback"]), 8)

    def test_metadata_cannot_detach_from_statistics(self):
        _, samples = self.observations()
        samples[-1]["generation"] = 3
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
        smoke.validate_statistics([sample], "offscreen", "hardware", extent=(953, 713))
        with self.assertRaisesRegex(SmokeFailure, "dimensions"):
            smoke.validate_statistics([sample], "offscreen", "hardware")
        sample["queue_capacity"] = smoke.DEFAULT_RAY_BUDGET
        sample["effective_budget"] = smoke.DEFAULT_RAY_BUDGET
        with self.assertRaisesRegex(SmokeFailure, "capacity"):
            smoke.validate_statistics([sample], "offscreen", "hardware", extent=(953, 713))

    def test_low_budget_blocks_bypass_even_with_previous_valid_header(self):
        stats, samples = self.observations()
        for item in stats:
            item["effective_budget"] = 64
        with self.assertRaisesRegex(SmokeFailure, "upper-bound"):
            self.validate(samples, replace(smoke.CAPTURES[1], hardware_budget=64), stats)

    def test_rough_receivers_never_bypass(self):
        with self.assertRaisesRegex(SmokeFailure, "smooth"):
            self.validate(spec=smoke.FeedbackCapture("rough_feedback", "rough_furnace", True, roughness=1))

    def test_any_return_counter_retains_provisional_hits(self):
        stats, samples = self.observations()
        for item in stats:
            item["screen_hits"] = 0
        result = self.validate(samples, stats=stats)
        self.assertEqual(result["stable_feedback"][-1]["screen_returns"], 60)
        samples[-1]["screen_returns"] = 101
        with self.assertRaisesRegex(SmokeFailure, "bounded actual"):
            self.validate(samples, stats=stats)

    def test_disabled_observations_advance_sequence_without_probe_index(self):
        stats, samples = self.observations(False)
        result = self.validate(samples, smoke.FeedbackCapture("floor_baseline", "floor", False), stats)
        self.assertTrue(all(item["probe_index"] == 0 for item in result["feedback"]))

    def test_epoch_start_cannot_move_without_an_epoch_change(self):
        _, samples = self.observations()
        samples[-1]["start_graphics_frame"] = 101
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
        spec = smoke.FeedbackCapture("mutation_changed", "feedback_mutation", True, mutation=True)
        result = self.validate(samples, spec, stats, source=167, mutation=False)
        self.assertTrue(result["exact_first_frame_counters_available"])
        last["bypassed_pixels"] = 1
        with self.assertRaises(SmokeFailure):
            self.validate(samples, spec, stats, source=167, mutation=False)

    def test_skipped_exact_changed_sample_uses_epoch_anchor_without_claiming_counter_proof(self):
        stats, samples = self.observations()
        samples[-1].update(epoch=2, start_graphics_frame=166, probe_index=1, reset=0, reused=1, reason=0)
        spec = smoke.FeedbackCapture("mutation_changed", "feedback_mutation", True, mutation=True)
        result = self.validate(samples, spec, stats, source=166, mutation=False)
        self.assertFalse(result["exact_first_frame_counters_available"])
        samples[-1]["start_graphics_frame"] = 100
        with self.assertRaisesRegex(SmokeFailure, "previous epoch"):
            self.validate(samples, spec, stats, source=166, mutation=False)


class AuthoredVolumeIdentityTests(unittest.TestCase):
    def test_exact_canonical_cache_hash_and_contiguous_segments_only(self):
        self.assertEqual(volume_identity.volume_segment_filename("runtime_pipeline_cache", 0), "1f98ed5c238bf1c3.vol")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            resources = root / "res"
            resources.mkdir()
            for index in (0, 1, 3):
                (resources / volume_identity.volume_segment_filename("runtime_pipeline_cache", index)).write_bytes(b"cache")
            (resources / "other.vol").write_bytes(b"authored")
            (resources / "runtime_pipeline_cache_fake.vol").write_bytes(b"authored too")
            hashes = volume_identity.authored_volume_hashes(root)
            self.assertNotIn("res/1f98ed5c238bf1c3.vol", hashes)
            self.assertNotIn("res/" + volume_identity.volume_segment_filename("runtime_pipeline_cache", 1), hashes)
            self.assertIn("res/" + volume_identity.volume_segment_filename("runtime_pipeline_cache", 3), hashes)
            self.assertIn("res/other.vol", hashes)
            self.assertIn("res/runtime_pipeline_cache_fake.vol", hashes)

    def test_mutating_cache_does_not_change_authored_identity_but_assets_do(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "res").mkdir()
            cache = root / "res/1f98ed5c238bf1c3.vol"
            asset = root / "res/authored.vol"
            cache.write_bytes(b"first cache")
            asset.write_bytes(b"first asset")
            before = volume_identity.authored_volume_hashes(root)
            cache.write_bytes(b"mutated cache")
            self.assertEqual(before, volume_identity.authored_volume_hashes(root))
            asset.write_bytes(b"changed shader")
            self.assertNotEqual(before, volume_identity.authored_volume_hashes(root))

    def test_report_records_exact_optical_limit_and_authored_volume_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "res").mkdir()
            (root / "res/1f98ed5c238bf1c3.vol").write_bytes(b"cache")
            (root / "res/authored.vol").write_bytes(b"asset")
            executable = root / "app.exe"
            executable.write_bytes(b"fixture")
            args = SimpleNamespace(output_directory=root, working_directory=root, executable=executable)
            smoke.write_report(args, [], {})
            manifest = json.loads((root / "reflection_feedback_manifest.json").read_text())
            self.assertEqual(manifest["optical_queries"], 16)
            self.assertEqual(set(manifest["build_identity"]["asset_volumes"]), {"res/authored.vol"})
            self.assertEqual(smoke.spec_environment(smoke.CAPTURES[0])["NWB_REFLECTION_SMOKE_OPTICAL_QUERIES"], "16")


class DiagnosticsOffTests(unittest.TestCase):
    def log(self, source=64):
        return "ReflectionSmokeProject: screen feedback 1\nReflectionSmokeProject: screen steps 96\n" \
            + "FramebufferCapture: graphics source frame " + str(source)

    def test_plan_preserves_strict_diagnostics_matrix_and_adds_six_frozen_pairs(self):
        self.assertEqual(len(smoke.CAPTURES), 24)
        self.assertTrue(all(spec.diagnostics for spec in smoke.CAPTURES))
        self.assertEqual(len(smoke.DIAGNOSTICS_OFF_CAPTURES), 6)
        for spec in smoke.DIAGNOSTICS_OFF_CAPTURES:
            self.assertFalse(spec.diagnostics)
            environment = smoke.spec_environment(spec)
            self.assertEqual(environment["NWB_REFLECTION_SMOKE_DIAGNOSTICS"], "0")
            self.assertEqual(environment["NWB_REFLECTION_SMOKE_FEEDBACK_CAPTURE"], "0")
            self.assertEqual(environment["NWB_REFLECTION_SMOKE_TEMPORAL"], "0")
            self.assertEqual(environment["NWB_REFLECTION_SMOKE_SPATIAL"], "0")

    def test_completed_readback_proof_does_not_invent_feedback_statistics(self):
        spec = smoke.DIAGNOSTICS_OFF_CAPTURES[1]
        result = smoke.validate_diagnostics_off(self.log(), spec)
        self.assertEqual(result["captured_graphics_frame"], 64)
        self.assertEqual(result["requested_prepared_graphics_frames"], 65)
        self.assertIsNone(result["completed_feedback_observations"])
        self.assertIsNone(result["completed_feedback_counters"])

    def test_early_or_unanchored_capture_fails(self):
        for log in (self.log(63), self.log() + "\nFramebufferCapture: graphics source frame 65", ""):
            with self.assertRaises(SmokeFailure):
                smoke.validate_diagnostics_off(log, smoke.DIAGNOSTICS_OFF_CAPTURES[1])

    def test_unexpected_completed_diagnostics_fail(self):
        for prefix in ("ReflectionSmokeFeedback:", "ReflectionSmokeStatistics:", "ReflectionSmokeHistory:", "ReflectionSmokeOptics:"):
            with self.assertRaisesRegex(SmokeFailure, "unexpectedly published"):
                smoke.validate_diagnostics_off(self.log() + "\n" + prefix, smoke.DIAGNOSTICS_OFF_CAPTURES[1])

    def test_capture_uses_ordinary_frame_predicate_and_rejects_statistics_logs(self):
        args = SimpleNamespace(output_directory=Path("unused"), executable=Path("app.exe"),
            working_directory=Path("runtime"), timeout=60, require_hardware=True,
            logserver_executable=None, application_arg=[])
        with patch.object(smoke.subprocess, "run", return_value=SimpleNamespace(returncode=77)) as run:
            smoke.capture(args, smoke.DIAGNOSTICS_OFF_CAPTURES[1])
        command = run.call_args.args[0]
        pairs = set(zip(command, command[1:]))
        self.assertIn(("--application-capture-frame-count", "65"), pairs)
        self.assertIn(("--reject-log-message", "ReflectionSmokeFeedback:"), pairs)
        self.assertNotIn(("--expect-log-message", "ReflectionSmokeFeedback:"), pairs)


if __name__ == "__main__":
    unittest.main()
