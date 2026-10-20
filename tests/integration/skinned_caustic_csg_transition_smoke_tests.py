#!/usr/bin/env python3
"""Exercise source-frame, completed GPU and visual rejection gates for skinned transitions."""

from array import array
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
from contextlib import redirect_stderr
from io import StringIO
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import skinned_caustic_csg_transition_smoke as smoke
from window_capture_smoke import SmokeFailure

LIT_CURRENT = "current"
LIT_NO_PHOTONS = "no_photons"
LIT_LAGGED = 'lagged'
LIT_RECEIVER = "receiver"
LIT_GROUND = "ground"
LIT_RUNS = "runs"
LIT_PROOF_TXT = "proof.txt"
LIT_RETAINED_EVIDENCE = "retained evidence"
LIT_UTF_8 = "utf-8"
LIT_EXECUTABLE = "--executable"
LIT_APP_EXE = "app.exe"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_RUNTIME = "runtime"
LIT_OUTPUT_ROOT = "--output-root"
LIT_OUTPUT_DIRECTORY = "--output-directory"


def runtime_log(variant=LIT_CURRENT):
    photons = variant != LIT_NO_PHOTONS
    first = 700
    lines = [f"SkinnedCausticTransition: setup lagged={int(variant == LIT_LAGGED)} caustics={int(photons)} minimum_frame=360 warmup_seconds=30 phase_frames=64 samples=8 model=closed_prism", "SkinnedCausticTransition: warmup started graphics frame 360",
        f"SkinnedCausticTransition: warmup complete elapsed_seconds=30.001234 first_frame={first}"]
    for phase in range(4):
        lines.append(f"SkinnedCausticTransition: phase {phase} csg={phase % 2} graphics frame {first + phase * 64}")
    for sample in range(8):
        frame = first + (sample + 1) * 32 - 1
        lines += [f"SkinnedCausticTransition: sample {sample} graphics frame {frame} phase {sample // 2} pose_frame {(frame - first) % 64}", "FramebufferCapture: capture ready"]
    for phase in range(4):
        lines.append(f"SkinnedCausticTransition: completed GPU phase {phase} photon_samples={60 if photons else 0} skinning_samples=60")
    if photons:
        for phase in range(4):
            queue = int(variant == LIT_LAGGED)
            lines.append(f"SkinnedCausticTransition: accepted photon route phase {phase} source_frame={first + phase * 64 + 8} queue={queue} graphics_queue=0 generation=1 off_graphics={queue}")
        lines.append("RendererSystem: dispatched hardware caustic producer (16384 photons/frame)")
    if variant == LIT_LAGGED:
        lines += ["RendererSystem: frame-lagged async lighting bootstrap accepted", "RendererSystem: frame-lagged async lighting active history accepted"]
    return "\n".join(lines + ["SkinnedCausticTransition: complete samples=8", "SkinnedCausticSmokeProject: shutdown"])


def region(color):
    return array("B", color * 512)


def image_series():
    series = {}
    for variant in smoke.VARIANTS:
        frames = []
        for sample in range(8):
            value = 10 + (sample % 4) * 10
            frames.append({LIT_RECEIVER: region((value, value + 15, value + 30)), LIT_GROUND: region((40, 40, 40) if variant != LIT_NO_PHOTONS else (30, 30, 30))})
        series[variant] = frames
    return series


class SkinnedCausticTransitionTests(unittest.TestCase):
    def test_one_frame_pose_mismatch_is_rejected(self):
        smoke.validate_series_log(runtime_log(), LIT_CURRENT)
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log().replace("sample 4 graphics frame 859", "sample 4 graphics frame 860"), LIT_CURRENT)

    def test_missing_photons_in_csg_phase_are_rejected(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log().replace("GPU phase 1 photon_samples=60", "GPU phase 1 photon_samples=0"), LIT_CURRENT)

    def test_uncompleted_skinning_is_rejected(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log().replace("GPU phase 2 photon_samples=60 skinning_samples=60", "GPU phase 2 photon_samples=60 skinning_samples=0"), LIT_CURRENT)

    def test_graphics_fallback_cannot_qualify_compute_reader_hazard(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log(LIT_LAGGED) + "\nRendererSystem: frame-lagged async lighting Graphics queue route accepted", LIT_LAGGED)

    def test_photon_queue_assigned_graphics_cannot_qualify_async_hazard(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log(LIT_LAGGED).replace("queue=1 graphics_queue=0 generation=1 off_graphics=1", "queue=0 graphics_queue=0 generation=1 off_graphics=0"), LIT_LAGGED)

    def test_declared_compute_route_without_accepted_packet_is_rejected(self):
        log = runtime_log(LIT_LAGGED)
        log = "\n".join(line for line in log.splitlines() if "accepted photon route phase 2" not in line)
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(log, LIT_LAGGED)

    def test_stale_pose_after_csg_removal_is_rejected(self):
        series = image_series()
        smoke.compare_series(series)
        series[LIT_LAGGED][4][LIT_RECEIVER] = region((150, 150, 150))
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)

    def test_photon_control_without_visible_ground_signal_is_rejected(self):
        series = image_series()
        for sample in range(8):
            series[LIT_NO_PHOTONS][sample][LIT_GROUND] = deepcopy(series[LIT_CURRENT][sample][LIT_GROUND])
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)

    def test_cutter_without_visible_receiver_signal_is_rejected(self):
        series = image_series()
        for variant in (LIT_CURRENT, LIT_LAGGED):
            for sample in (2, 3, 6, 7):
                series[variant][sample][LIT_RECEIVER] = deepcopy(series[variant][sample % 2][LIT_RECEIVER])
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)

    def test_static_bind_pose_cannot_satisfy_live_animation(self):
        series = image_series()
        for variant in (LIT_CURRENT, LIT_LAGGED):
            for sample in (1, 3, 5, 7):
                series[variant][sample][LIT_RECEIVER] = deepcopy(series[variant][sample - 1][LIT_RECEIVER])
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)


class SkinnedTransitionOutputTests(unittest.TestCase):
    def test_repeated_output_root_runs_preserve_completed_variant_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / LIT_RUNS
            retained = root / "completed" / LIT_PROOF_TXT
            retained.parent.mkdir(parents=True)
            retained.write_text(LIT_RETAINED_EVIDENCE, encoding=LIT_UTF_8)
            destinations = []

            def capture(args, variant):
                if variant == smoke.VARIANTS[0]:
                    self.assertFalse(any(args.output_directory.iterdir()))
                    destinations.append(args.output_directory)
                output = args.output_directory / variant
                output.mkdir()
                (output / LIT_PROOF_TXT).write_text(f"capture {len(destinations)}", encoding=LIT_UTF_8)
                return 0

            required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_ROOT, str(root)]
            with patch.object(smoke, "capture", side_effect=capture), patch.object(smoke, "analyze", return_value={"status": "passed"}):
                self.assertEqual(smoke.main(required), 0)
                self.assertEqual(smoke.main(required), 0)
            self.assertEqual(len(set(destinations)), 2)
            self.assertTrue(all(destination.parent == root for destination in destinations))
            self.assertEqual(retained.read_text(encoding=LIT_UTF_8), LIT_RETAINED_EVIDENCE)
            for index, destination in enumerate(destinations, 1):
                self.assertEqual((destination / smoke.VARIANTS[0] / LIT_PROOF_TXT).read_text(encoding=LIT_UTF_8), f"capture {index}")

    def test_nonempty_exact_variant_directory_keeps_freshness_rejection_and_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            retained = output / smoke.VARIANTS[0] / LIT_PROOF_TXT
            retained.parent.mkdir()
            retained.write_text(LIT_RETAINED_EVIDENCE, encoding=LIT_UTF_8)
            required = [LIT_EXECUTABLE, LIT_APP_EXE, LIT_WORKING_DIRECTORY, LIT_RUNTIME, LIT_OUTPUT_DIRECTORY, str(output)]
            with patch.object(smoke.subprocess, "run") as launch, redirect_stderr(StringIO()):
                self.assertEqual(smoke.main(required), 1)
            launch.assert_not_called()
            self.assertEqual(retained.read_text(encoding=LIT_UTF_8), LIT_RETAINED_EVIDENCE)

    def test_output_root_rejects_analysis_only_and_ambiguous_destinations(self):
        for extra in (["--analysis-only"], [LIT_OUTPUT_DIRECTORY, "output"]):
            required = [LIT_OUTPUT_ROOT, LIT_RUNS, *extra]
            with self.subTest(extra=extra), redirect_stderr(StringIO()), self.assertRaises(SystemExit) as rejection:
                smoke.main(required)
            self.assertEqual(rejection.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
