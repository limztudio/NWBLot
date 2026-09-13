#!/usr/bin/env python3
"""Adversarial parser and image-oracle tests; no renderer or GPU work is performed."""

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from reflection_spatial_owner_smoke import (GPU_DEBUG_MARKERS, SELECTIONS, compare_images, records, validate_gpu_debug, validate_owner_evidence)
from window_capture_smoke import SmokeFailure


def evidence(selection="sequence1"):
    radii = SELECTIONS[selection]
    lines = [f"ReflectionSpatialOwner: selection={selection} initial_radius={radii[0]} final_radius={radii[-1]} phases={len(radii)}"]
    history, statistics = [], []
    for index, radius in enumerate(radii):
        first, last = 10 + index * 10, 13 + index * 10
        sequence, publish = 100 + index, 20 + index
        seed = 101 + index
        lines += [
            f"ReflectionSpatialOwnerPhase: index={index} radius={radius} seed={seed} graphics_frame={first}",
            f"ReflectionSpatialOwnerWork: publish={publish} first={first} last={last} samples=4",
            f"ReflectionSpatialOwnerWarm: index={index} sequence={sequence} generation=7 graphics_frame={last} epoch={index + 1} start_graphics_frame={first} sample_index=3 seed={seed} work_publish={publish} work_first={first} work_last={last} work_samples=4",
        ]
        history.append(dict(sequence=sequence, generation=7, graphics_frame=last, epoch=index + 1,
            start_graphics_frame=first, sample_index=3, seed=seed, count=1, eligible=1))
        statistics.append(dict(sequence=sequence, generation=7, hardware_ready=1, device_generation=2))
    source, last = 10 + len(radii) * 10, 12 + len(radii) * 10
    lines += [
        f"ReflectionSpatialOwnerCapture: radius={radii[-1]} graphics_frame={source} seed=0",
        f"FramebufferCapture: graphics source frame {source}",
        f"ReflectionSpatialOwnerWork: publish=90 first={source - 1} last={last} samples=4",
    ]
    history.append(dict(sequence=999, generation=7, graphics_frame=last, epoch=20,
        start_graphics_frame=source, sample_index=2, seed=0, count=1, eligible=1))
    statistics.append(dict(sequence=999, generation=7, hardware_ready=1, device_generation=2))
    return "\n".join(lines), history, statistics


def image_set():
    frames = {}
    for radius in (1, 2, 3):
        rows = [[(20 * radius, 30 * radius, 15 * radius) for _ in range(96)] for _ in range(72)]
        frame = (96, 72, rows)
        frames["fresh" + str(radius)] = frame
        frames["sequence" + str(radius)] = copy.deepcopy(frame)
    return frames


class SpatialOwnerValidationMarkerTests(unittest.TestCase):
    def test_requested_validation_requires_actual_loader_layer_and_messenger(self):
        text = "\n".join(GPU_DEBUG_MARKERS)
        self.assertTrue(validate_gpu_debug(text, ["--gpudbg"])["requested"])
        for marker in GPU_DEBUG_MARKERS:
            with self.subTest(marker=marker), self.assertRaises(SmokeFailure):
                validate_gpu_debug(text.replace(marker, ""), ["--gpudbg"])

    def test_repeated_markers_do_not_merge_multiple_process_logs(self):
        text = "\n".join(GPU_DEBUG_MARKERS) + "\n" + GPU_DEBUG_MARKERS[0]
        with self.assertRaises(SmokeFailure):
            validate_gpu_debug(text, ["--gpudbg"])

    def test_unrequested_run_makes_no_validation_claim(self):
        self.assertEqual(validate_gpu_debug("", []), {"requested": False, "markers": []})


class SpatialOwnerEvidenceTests(unittest.TestCase):
    def test_every_real_sequence_shape_accepts_completed_aggregated_windows(self):
        for selection in SELECTIONS:
            with self.subTest(selection=selection):
                text, history, statistics = evidence(selection)
                result = validate_owner_evidence(text, selection, history, statistics)
                self.assertEqual(result["radii"], list(SELECTIONS[selection]))

    def test_missing_or_reordered_phase_is_rejected(self):
        text, history, statistics = evidence()
        text = text.replace("index=1 radius=3", "index=2 radius=3")
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)

    def test_warm_claim_requires_matching_completed_queue_identity(self):
        text, history, statistics = evidence()
        statistics.pop(1)
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)

    def test_source_frame_count_does_not_replace_accepted_sample_index(self):
        text, history, statistics = evidence()
        history[0]["sample_index"] = 1
        text = text.replace("sample_index=3 seed=101", "sample_index=1 seed=101")
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)

    def test_old_radius_work_cannot_qualify_next_radius(self):
        text, history, statistics = evidence()
        text = text.replace("publish=21 first=20", "publish=21 first=19")
        text = text.replace("work_publish=21 work_first=20", "work_publish=21 work_first=19")
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)

    def test_sparse_final_window_cannot_prove_capture_source_membership(self):
        text, history, statistics = evidence()
        text = text.replace("publish=90 first=49 last=52 samples=4", "publish=90 first=49 last=52 samples=3")
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)

    def test_duplicate_publication_and_duplicate_field_are_rejected(self):
        text, history, statistics = evidence()
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text + "\nReflectionSpatialOwnerWork: publish=90 first=50 last=50 samples=1",
                "sequence1", history, statistics)
        with self.assertRaises(SmokeFailure):
            records("ReflectionSpatialOwnerWork: publish=1 first=0 last=0 samples=1 samples=1", "Work")

    def test_later_history_epoch_cannot_cover_the_first_reset_image(self):
        text, history, statistics = evidence()
        history[-1]["start_graphics_frame"] += 1
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)

    def test_resource_or_device_recreation_is_not_silently_merged(self):
        text, history, statistics = evidence()
        statistics[-1]["device_generation"] = 3
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)

    def test_capture_on_an_update_number_without_exact_readback_anchor_is_rejected(self):
        text, history, statistics = evidence()
        text = text.replace("FramebufferCapture: graphics source frame 50", "FramebufferCapture: graphics source frame 51")
        with self.assertRaises(SmokeFailure):
            validate_owner_evidence(text, "sequence1", history, statistics)


class SpatialOwnerImageTests(unittest.TestCase):
    def test_exact_pairs_and_three_discriminating_references(self):
        result = compare_images(image_set())
        self.assertEqual(len(result["exact_pairs"]), 3)
        self.assertEqual(len(result["radius_discrimination"]), 3)

    def test_wrong_reused_radius_fails_even_with_valid_shape(self):
        frames = image_set()
        frames["sequence1"] = frames["fresh2"]
        with self.assertRaises(SmokeFailure):
            compare_images(frames)

    def test_identity_or_blank_references_cannot_qualify_owner_selection(self):
        frames = image_set()
        for key in frames:
            frames[key] = frames["fresh1"]
        with self.assertRaises(SmokeFailure):
            compare_images(frames)

    def test_one_changed_pixel_cannot_fake_radius_discrimination(self):
        frames = image_set()
        for key in frames:
            frames[key] = copy.deepcopy(frames["fresh1"])
        for radius in (2, 3):
            for prefix in ("fresh", "sequence"):
                frames[prefix + str(radius)][2][35][47] = (80 * radius, 40, 40)
        with self.assertRaises(SmokeFailure):
            compare_images(frames)


if __name__ == "__main__":
    unittest.main()
