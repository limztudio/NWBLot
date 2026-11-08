#!/usr/bin/env python3
"""Adversarial parser and image-oracle tests; no renderer or GPU work is performed."""

import copy
import json
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import reflection_spatial_owner_smoke as spatial_owner
from reflection_spatial_owner_smoke import GPU_DEBUG_MARKERS, validate_gpu_debug
from reflection_spatial_owner_evidence import SELECTIONS, ReflectionSpatialOwnerEvidence
from reflection_spatial_owner_image_evidence import ReflectionSpatialOwnerImageEvidence
from window_capture_smoke import SmokeFailure

LIT_SEQUENCE1 = "sequence1"
LIT_N = "\n"
LIT_FRESH = "fresh"
LIT_SEQUENCE = "sequence"
LIT_GPUDBG = "--gpudbg"
LIT_REQUESTED = "requested"
LIT_FRESH1 = "fresh1"
LIT_MAIN = "__main__"


def evidence(selection=LIT_SEQUENCE1):
    radii = SELECTIONS[selection]
    lines = [f"ReflectionSpatialOwner: selection={selection} initial_radius={radii[0]} final_radius={radii[-1]} phases={len(radii)}"]
    history, statistics = [], []
    for index, radius in enumerate(radii):
        first, last = 10 + index * 10, 13 + index * 10
        sequence, publish = 100 + index, 20 + index
        seed = 101 + index
        lines += [f"ReflectionSpatialOwnerPhase: index={index} radius={radius} seed={seed} graphics_frame={first}", f"ReflectionSpatialOwnerWork: publish={publish} first={first} last={last} samples=4",
            f"ReflectionSpatialOwnerWarm: index={index} sequence={sequence} generation=7 graphics_frame={last} epoch={index + 1} start_graphics_frame={first} sample_index=3 seed={seed} work_publish={publish} work_first={first} work_last={last} work_samples=4",
        ]
        history.append(dict(sequence=sequence, generation=7, graphics_frame=last, epoch=index + 1, start_graphics_frame=first, sample_index=3, seed=seed, count=1, eligible=1))
        statistics.append(dict(sequence=sequence, generation=7, hardware_ready=1, device_generation=2))
    source, last = 10 + len(radii) * 10, 12 + len(radii) * 10
    lines += [f"ReflectionSpatialOwnerCapture: radius={radii[-1]} graphics_frame={source} seed=0", f"FramebufferCapture: graphics source frame {source}", f"ReflectionSpatialOwnerWork: publish=90 first={source - 1} last={last} samples=4",]
    history.append(dict(sequence=999, generation=7, graphics_frame=last, epoch=20, start_graphics_frame=source, sample_index=2, seed=0, count=1, eligible=1))
    statistics.append(dict(sequence=999, generation=7, hardware_ready=1, device_generation=2))
    return LIT_N.join(lines), history, statistics


def image_set():
    frames = {}
    for radius in (1, 2, 3):
        rows = [[(20 * radius, 30 * radius, 15 * radius) for _ in range(96)] for _ in range(72)]
        frame = (96, 72, rows)
        frames[LIT_FRESH + str(radius)] = frame
        frames[LIT_SEQUENCE + str(radius)] = copy.deepcopy(frame)
    return frames


class SpatialOwnerValidationMarkerTests(unittest.TestCase):
    def test_requested_validation_requires_actual_loader_activation(self):
        text = LIT_N.join(GPU_DEBUG_MARKERS)
        self.assertTrue(validate_gpu_debug(text, [LIT_GPUDBG])[LIT_REQUESTED])
        for marker in GPU_DEBUG_MARKERS:
            with self.subTest(marker=marker), self.assertRaises(SmokeFailure):
                validate_gpu_debug(text.replace(marker, ""), [LIT_GPUDBG])

    def test_repeated_markers_do_not_merge_multiple_process_logs(self):
        text = LIT_N.join(GPU_DEBUG_MARKERS) + LIT_N + GPU_DEBUG_MARKERS[0]
        with self.assertRaises(SmokeFailure):
            validate_gpu_debug(text, [LIT_GPUDBG])


class SpatialOwnerEvidenceTests(unittest.TestCase):
    def test_missing_or_reordered_phase_is_rejected(self):
        text, history, statistics = evidence()
        text = text.replace("index=1 radius=3", "index=2 radius=3")
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)

    def test_warm_claim_requires_matching_completed_queue_identity(self):
        text, history, statistics = evidence()
        statistics.pop(1)
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)

    def test_source_frame_count_does_not_replace_accepted_sample_index(self):
        text, history, statistics = evidence()
        history[0]["sample_index"] = 1
        text = text.replace("sample_index=3 seed=101", "sample_index=1 seed=101")
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)

    def test_old_radius_work_cannot_qualify_next_radius(self):
        text, history, statistics = evidence()
        text = text.replace("publish=21 first=20", "publish=21 first=19")
        text = text.replace("work_publish=21 work_first=20", "work_publish=21 work_first=19")
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)

    def test_sparse_final_window_cannot_prove_capture_source_membership(self):
        text, history, statistics = evidence()
        text = text.replace("publish=90 first=49 last=52 samples=4", "publish=90 first=49 last=52 samples=3")
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)

    def test_duplicate_publication_and_duplicate_field_are_rejected(self):
        text, history, statistics = evidence()
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text + "\nReflectionSpatialOwnerWork: publish=90 first=50 last=50 samples=1", LIT_SEQUENCE1, history, statistics)
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.records("ReflectionSpatialOwnerWork: publish=1 first=0 last=0 samples=1 samples=1", "Work")

    def test_later_history_epoch_cannot_cover_the_first_reset_image(self):
        text, history, statistics = evidence()
        history[-1]["start_graphics_frame"] += 1
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)

    def test_resource_or_device_recreation_is_not_silently_merged(self):
        text, history, statistics = evidence()
        statistics[-1]["device_generation"] = 3
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)

    def test_capture_on_an_update_number_without_exact_readback_anchor_is_rejected(self):
        text, history, statistics = evidence()
        text = text.replace("FramebufferCapture: graphics source frame 50", "FramebufferCapture: graphics source frame 51")
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, LIT_SEQUENCE1, history, statistics)


class SpatialOwnerFrozenInputsTests(unittest.TestCase):
    def test_changed_extracted_input_stops_before_next_capture(self):
        for changed_name in ("reflection_spatial_owner_image_evidence.py", "reflection_roughness_oracle.py", "renderer_wall_frame_evidence.py", "gpu_timing_evidence.py", "renderer_workload_log_policy.py", "renderer_workload_registry.py", "renderer_trial_comparison.py"):
            with self.subTest(changed_name=changed_name):
                with TemporaryDirectory() as directory:
                    root = Path(directory)
                    inputs = root / "inputs"
                    output = root / "output"
                    changed = False

                    def identity(path):
                        return "changed" if changed and Path(path).name == changed_name else "stable"

                    def capture(args, selection):
                        nonlocal changed
                        changed = True
                        return None, {"selection": selection}

                    args = ["--executable", str(inputs / "renderer"), "--working-directory", str(inputs / "runtime"), "--logserver-executable", str(inputs / "logger"), "--source-manifest", str(inputs / "manifest.json"), "--output-directory", str(output)]
                    with patch.object(spatial_owner.RendererBenchmarkIdentity, "freeze_arm", return_value={}), patch.object(spatial_owner.RendererBenchmarkIdentity, "binary_identity", return_value={}), patch.object(spatial_owner, "file_identity", side_effect=identity), patch.object(spatial_owner, "capture", side_effect=capture) as run_capture, redirect_stderr(StringIO()):
                        self.assertEqual(spatial_owner.main(args), 1)
                    report = json.loads((output / "owner_qualification.json").read_text(encoding="utf-8"))
                    self.assertEqual(run_capture.call_count, 1)
                    self.assertEqual(len(report["captures"]), 1)
                    self.assertIn("shared input changed", report["error"])
                    self.assertTrue(any(Path(path).name == "reflection_spatial_owner_evidence.py" for path in report["shared_inputs"]))
                    self.assertTrue(any(Path(path).name == changed_name for path in report["shared_inputs"]))
                    self.assertTrue(any(Path(path).name == "reflection_roughness_oracle.py" for path in report["shared_inputs"]))


class SpatialOwnerImageTests(unittest.TestCase):
    def test_wrong_reused_radius_fails_even_with_valid_shape(self):
        frames = image_set()
        frames[LIT_SEQUENCE1] = frames["fresh2"]
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerImageEvidence.compare_images(frames)

    def test_identity_or_blank_references_cannot_qualify_owner_selection(self):
        frames = image_set()
        for key in frames:
            frames[key] = frames[LIT_FRESH1]
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerImageEvidence.compare_images(frames)

    def test_one_changed_pixel_cannot_fake_radius_discrimination(self):
        frames = image_set()
        for key in frames:
            frames[key] = copy.deepcopy(frames[LIT_FRESH1])
        for radius in (2, 3):
            for prefix in (LIT_FRESH, LIT_SEQUENCE):
                frames[prefix + str(radius)][2][35][47] = (80 * radius, 40, 40)
        with self.assertRaises(SmokeFailure):
            ReflectionSpatialOwnerImageEvidence.compare_images(frames)


if __name__ == LIT_MAIN:
    unittest.main()
