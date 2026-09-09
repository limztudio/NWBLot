#!/usr/bin/env python3
"""Independent physics sanity checks and rejection cases for completed-history image acceptance."""

import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from reflection_roughness_reference import (analyze_furnace, analyze_roughness, decode_radiance,
    integrated_radiance, mirror_cells, reference_grid, smith_lambda)
from reflection_roughness_smoke import (CaptureSpec, HISTORY_FIELDS, ROUGH_CAPTURES, TEMPORAL_CAPTURES,
    compare_convergence, compare_deformation, compare_exact_scene, compare_sampling_seeds, high_frequency_energy, parse_history,
    spec_environment, validate_history)
from reflection_smoke import SmokeFailure


def encode_radiance(value):
    mapped = value / (1.0 + value)
    srgb = mapped * 12.92 if mapped <= 0.0031308 else 1.055 * mapped ** (1.0 / 2.4) - 0.055
    return round(srgb * 255)


def reference_frame(roughness, scale=1.0, swap_colors=False, camera_x=0.0):
    width, height = 320, 240
    rows = [[(0, 0, 0)] * width for _ in range(height)]
    for cell, value in zip(mirror_cells(width, height, camera_x), reference_grid(width, height, roughness, camera_x)):
        values = value[::-1] if swap_colors else value
        pixel = tuple(encode_radiance(channel * scale) for channel in values) + (0,)
        for y in range(cell[1], cell[3]):
            for x in range(cell[0], cell[2]):
                rows[y][x] = pixel
    return width, height, rows


class ReflectionRoughnessReferenceTests(unittest.TestCase):
    def test_reinhard_srgb_roundtrip_matches_presentation_quantization(self):
        for value in (0.001, 0.01, 0.1, 0.5, 1.0):
            self.assertAlmostEqual(decode_radiance(encode_radiance(value)), value, delta=0.012)

    def test_exact_mirror_hits_virtual_emitter_and_rejects_wrong_position(self):
        # Point x=-1.6*6/14 reflects exactly to emitter x=-1.6 at z=-8.
        self.assertAlmostEqual(integrated_radiance((-1.6 * 6 / 14, 1.4, 0), 0, 0), 0.95, places=6)
        self.assertEqual(integrated_radiance((0, 1.4, 0), 0, 0), 0)
        self.assertEqual(integrated_radiance((-1.6 * 6 / 14, 1.4, 0), 0, 1), 0)

    def test_area_quadrature_converges_without_production_sampler(self):
        point = (-0.7, 1.4, 0)
        coarse = integrated_radiance(point, 0.4, 0, resolution=12)
        fine = integrated_radiance(point, 0.4, 0, resolution=48)
        self.assertGreater(fine, 0.01)
        self.assertAlmostEqual(coarse, fine, delta=fine * 0.005)

    def test_smith_lambda_normal_incidence_and_alpha_one_closed_form(self):
        self.assertEqual(smith_lambda(1, 0.8), 0)
        self.assertAlmostEqual(smith_lambda(0.4, 1), (1 / 0.4 - 1) / 2)

    def test_correct_integrated_shape_survives_capture_quantization(self):
        for roughness in (0, 0.2, 0.4, 0.6):
            with self.subTest(roughness=roughness):
                result = analyze_roughness(reference_frame(roughness), roughness)
                self.assertLess(result["normalized_reference_error"], 0.03)

    def test_smooth_image_cannot_pass_for_rough_lobe(self):
        with self.assertRaises(SmokeFailure):
            analyze_roughness(reference_frame(0), 0.4)

    def test_diffuse_constant_cannot_pass_as_correct_blurred_reflection(self):
        frame = reference_frame(0.4)
        for row in frame[2]:
            row[:] = [(32, 32, 0)] * frame[0]
        with self.assertRaises(SmokeFailure):
            analyze_roughness(frame, 0.4)

    def test_misplaced_emitter_colors_cannot_pass(self):
        with self.assertRaises(SmokeFailure):
            analyze_roughness(reference_frame(0.4, swap_colors=True), 0.4)

    def test_bright_and_dim_lobes_fail_even_when_shape_is_correct(self):
        for scale in (0.5, 1.5):
            with self.subTest(scale=scale), self.assertRaises(SmokeFailure):
                analyze_roughness(reference_frame(0.4, scale=scale), 0.4)

    def test_white_furnace_rejects_renormalized_below_horizon_samples(self):
        pixel = (encode_radiance(1),) * 3
        frame = 320, 240, [[pixel] * 320 for _ in range(240)]
        analyze_furnace(frame, 0)
        with self.assertRaises(SmokeFailure):
            analyze_furnace(frame, 1)

    def test_white_furnace_cannot_ignore_a_missing_color_channel(self):
        pixel = encode_radiance(1), encode_radiance(1), 0
        with self.assertRaises(SmokeFailure):
            analyze_furnace((320, 240, [[pixel] * 320 for _ in range(240)]), 0)

    def test_stale_color_is_rejected_by_final_scene_comparison(self):
        clean = reference_frame(0.4)
        compare_exact_scene(clean, clean)
        with self.assertRaises(SmokeFailure):
            compare_exact_scene(clean, reference_frame(0.4, scale=0.8))

    def test_spatial_metric_detects_local_sample_noise(self):
        frame = reference_frame(0.4)
        clean = high_frequency_energy(frame)
        for row in frame[2]:
            for x in range(0, frame[0], 2):
                row[x] = (80, 0, 0)
        self.assertGreater(high_frequency_energy(frame), clean * 2)

    def test_camera_settled_reference_uses_shifted_world_and_receiver_crop(self):
        analyze_roughness(reference_frame(0.4, camera_x=0.6), 0.4, camera_x=0.6)
        with self.assertRaises(SmokeFailure):
            analyze_roughness(reference_frame(0.4), 0.4, camera_x=0.6)

    def test_deformation_requires_visible_changed_model_and_unchanged_control(self):
        before = 320, 240, [[(0, 0, 0)] * 320 for _ in range(240)]
        after = 320, 240, [[(0, 0, 0)] * 320 for _ in range(240)]
        for frame, shift in ((before, 0), (after, 4)):
            for y in range(110, 125):
                for x in range(110 + shift, 120 + shift):
                    frame[2][y][x] = (180, 0, 0)
                for x in range(200, 215):
                    frame[2][y][x] = (0, 180, 0)
        self.assertGreater(compare_deformation(before, after)["changed_red_footprint_pixels"], 0)
        with self.assertRaises(SmokeFailure):
            compare_deformation(before, before)
        for row in after[2]:
            row[:] = [(0, 0, 0)] * 320
        with self.assertRaises(SmokeFailure):
            compare_deformation(before, after)

    def test_sampling_seed_comparison_rejects_identical_estimator_noise(self):
        first = reference_frame(0.4)
        with self.assertRaises(SmokeFailure):
            compare_sampling_seeds(first, first)
        second = reference_frame(0.4)
        for row in second[2]:
            for x in range(0, second[0], 2):
                row[x] = (40, 0, 0)
        self.assertGreater(compare_sampling_seeds(first, second)["changed_receiver_pixels"], 64)

    def test_more_samples_must_converge_without_hiding_late_energy_bias(self):
        self.assertEqual(compare_convergence(0.06, 0.02, 0.008)["accepted_caps"], [8, 64, 256])
        for errors in ((0.06, 0.02, 0.046), (0.06, 0.07, 0.008), (0.06, 0.058, 0.057)):
            with self.subTest(errors=errors), self.assertRaises(SmokeFailure):
                compare_convergence(*errors)


class ReflectionCompletedHistoryTests(unittest.TestCase):
    def sample(self, sequence=1, frame=40, count=64, epoch=2, start=2, **changes):
        result = dict(zip(HISTORY_FIELDS, (sequence, 1, frame, epoch, start, count, frame - start, 0, 1, 1, 0, 0)))
        result.update(changes)
        return result

    def evidence(self, samples, source, mutation=None, fresh=False):
        lines = ["ReflectionSmokeHistory: " + " ".join(f"{key}={value}" for key, value in item.items()) for item in samples]
        lines.append(f"FramebufferCapture: graphics source frame {source}")
        if mutation is not None:
            lines.append(f"ReflectionSmokeMutation: graphics_frame={mutation} fresh_final={int(fresh)} seed=0")
        statistics = [{"sequence": item["sequence"], "generation": item["generation"]} for item in samples]
        return "\n".join(lines), statistics

    def test_converged_image_requires_completed_cap_before_capture(self):
        spec = CaptureSpec("static")
        text, stats = self.evidence([self.sample(), self.sample(2, 42)], 41)
        self.assertEqual(validate_history(text, stats, spec)["captured_graphics_frame"], 41)
        text, stats = self.evidence([self.sample(count=8), self.sample(2, 42, count=8)], 41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, spec)

    def test_requested_256_does_not_substitute_for_accepted_samples(self):
        spec = CaptureSpec("reference", samples=256)
        text, stats = self.evidence([self.sample(), self.sample(2, 42)], 41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, spec)

    def test_latest_only_publication_can_cover_first_reset_source(self):
        samples = [self.sample(count=32), self.sample(2, 44, count=4, epoch=3, start=41)]
        text, stats = self.evidence(samples, 41, mutation=41)
        result = validate_history(text, stats, CaptureSpec("reset", case="temporal_camera"))
        self.assertEqual(result["covering_completed_history"]["graphics_frame"], 44)

    def test_capture_one_frame_late_cannot_claim_first_reset(self):
        text, stats = self.evidence([self.sample(count=32), self.sample(2, 44, count=4, epoch=3, start=41)], 42, mutation=41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, CaptureSpec("reset", case="temporal_camera"))

    def test_changed_epoch_without_matching_start_does_not_prove_reset(self):
        text, stats = self.evidence([self.sample(count=32), self.sample(2, 44, count=4, epoch=3, start=42)], 41, mutation=41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, CaptureSpec("reset", case="temporal_material"))

    def test_no_completed_sample_after_capture_is_rejected(self):
        text, stats = self.evidence([self.sample()], 41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, CaptureSpec("static"))

    def test_detached_history_token_is_rejected(self):
        text, stats = self.evidence([self.sample(), self.sample(2, 42)], 41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats[:1], CaptureSpec("static"))

    def test_reused_deformation_history_is_rejected(self):
        text, stats = self.evidence([self.sample(count=32), self.sample(2, 44, count=4, epoch=3, start=41)], 41, mutation=41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, CaptureSpec("deform", case="temporal_deform", roughness=0))

    def test_raw_and_held_deformation_advance_without_history(self):
        samples = [self.sample(count=0, start=0, eligible=0, reused=0),
            self.sample(2, 44, count=0, epoch=3, start=0, eligible=0, reused=0)]
        text, stats = self.evidence(samples, 41)
        validate_history(text, stats, CaptureSpec("raw", temporal=False))
        text, stats = self.evidence(samples, 41, mutation=41)
        validate_history(text, stats, CaptureSpec("deform", case="temporal_deform", roughness=0))

    def test_history_integer_overflow_is_rejected(self):
        text, stats = self.evidence([self.sample(), self.sample(2, 42, sample_index=2**32)], 41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, CaptureSpec("static"))

    def test_duplicate_history_field_and_wrong_boolean_are_rejected(self):
        text, stats = self.evidence([self.sample(), self.sample(2, 42, eligible=2)], 41)
        with self.assertRaises(SmokeFailure):
            validate_history(text, stats, CaptureSpec("static"))
        with self.assertRaises(SmokeFailure):
            parse_history("ReflectionSmokeHistory: sequence=1 sequence=2")

    def test_fresh_scene_preserves_same_mutation_boundary_with_seed_reset(self):
        samples = [self.sample(count=32, seed=1), self.sample(2, 44, count=4, epoch=3, start=41)]
        text, stats = self.evidence(samples, 41, mutation=41, fresh=True)
        validate_history(text, stats, CaptureSpec("fresh", case="temporal_transform", final_state=True))

    def test_requested_independent_seed_must_match_completed_metadata(self):
        spec = CaptureSpec("seed1", seed=1)
        text, stats = self.evidence([self.sample(), self.sample(2, 42)], 41)
        with self.assertRaisesRegex(SmokeFailure, "sampling seed"):
            validate_history(text, stats, spec)
        text, stats = self.evidence([self.sample(seed=1), self.sample(2, 42, seed=1)], 41)
        validate_history(text, stats, spec)
        self.assertEqual(spec_environment(spec)["NWB_REFLECTION_SMOKE_SEED"], "1")

    def test_environment_removes_inherited_controls(self):
        with patch.dict(os.environ, {"NWB_REFLECTION_SMOKE_MODE": "disabled", "NWB_REFLECTION_SMOKE_DEBUG": "source",
            "NWB_REFLECTION_SMOKE_SEED": "900", "NWB_REFRACTION_SMOKE_ENABLED": "0"}):
            env = spec_environment(CaptureSpec("raw", temporal=False, samples=8))
        self.assertEqual(env["NWB_REFLECTION_SMOKE_TEMPORAL"], "0")
        self.assertEqual(env["NWB_REFLECTION_SMOKE_HISTORY_SAMPLES"], "8")
        self.assertEqual(env["NWB_REFLECTION_SMOKE_SEED"], "0")
        self.assertNotIn("NWB_REFLECTION_SMOKE_DEBUG", env)
        self.assertNotIn("NWB_REFRACTION_SMOKE_ENABLED", env)

    def test_capture_matrix_has_independent_cap_and_motion_coverage(self):
        self.assertEqual(len(ROUGH_CAPTURES) + len(TEMPORAL_CAPTURES), 26)
        self.assertEqual({spec.samples for spec in ROUGH_CAPTURES}, {8, 64, 256})
        self.assertEqual(len({spec.name for spec in ROUGH_CAPTURES + TEMPORAL_CAPTURES}), 26)
        self.assertTrue(any(spec.case == "rough_furnace" and not spec.temporal and spec.roughness == 1 for spec in ROUGH_CAPTURES))
        self.assertTrue(any(spec.case == "temporal_deform" and not spec.final_state for spec in TEMPORAL_CAPTURES))


if __name__ == "__main__":
    unittest.main()
