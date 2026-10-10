#!/usr/bin/env python3
"""Exercise source-frame, completed GPU and visual rejection gates for skinned transitions."""

from array import array
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import skinned_caustic_csg_transition_smoke as smoke
from window_capture_smoke import SmokeFailure


def runtime_log(variant="current"):
    photons = variant != "no_photons"
    first = 700
    lines = [f"SkinnedCausticTransition: setup lagged={int(variant == 'lagged')} caustics={int(photons)} minimum_frame=360 warmup_seconds=30 phase_frames=64 samples=8 model=closed_prism",
        "SkinnedCausticTransition: warmup started graphics frame 360",
        f"SkinnedCausticTransition: warmup complete elapsed_seconds=30.001234 first_frame={first}"]
    for phase in range(4):
        lines.append(f"SkinnedCausticTransition: phase {phase} csg={phase % 2} graphics frame {first + phase * 64}")
    for sample in range(8):
        frame = first + (sample + 1) * 32 - 1
        lines += [f"SkinnedCausticTransition: sample {sample} graphics frame {frame} phase {sample // 2} pose_frame {(frame - first) % 64}",
            "FramebufferCapture: capture ready"]
    for phase in range(4):
        lines.append(f"SkinnedCausticTransition: completed GPU phase {phase} photon_samples={60 if photons else 0} skinning_samples=60")
    if photons:
        for phase in range(4):
            queue = int(variant == "lagged")
            lines.append(f"SkinnedCausticTransition: accepted photon route phase {phase} source_frame={first + phase * 64 + 8} queue={queue} graphics_queue=0 generation=1 off_graphics={queue}")
        lines.append("RendererSystem: dispatched hardware caustic producer (16384 photons/frame)")
    if variant == "lagged":
        lines += ["RendererSystem: frame-lagged async lighting bootstrap accepted",
            "RendererSystem: frame-lagged async lighting active history accepted"]
    return "\n".join(lines + ["SkinnedCausticTransition: complete samples=8", "SkinnedCausticSmokeProject: shutdown"])


def region(color):
    return array("B", color * 512)


def image_series():
    series = {}
    for variant in smoke.VARIANTS:
        frames = []
        for sample in range(8):
            value = 10 + (sample % 4) * 10
            frames.append({"receiver": region((value, value + 15, value + 30)),
                "ground": region((40, 40, 40) if variant != "no_photons" else (30, 30, 30))})
        series[variant] = frames
    return series


class SkinnedCausticTransitionTests(unittest.TestCase):
    def test_complete_matched_phases_require_real_hardware_samples(self):
        for variant in smoke.VARIANTS:
            result = smoke.validate_series_log(runtime_log(variant), variant)
            self.assertEqual(result["source_frames"], [731, 763, 795, 827, 859, 891, 923, 955])
        self.assertEqual(len(smoke.compare_series(image_series())["current_vs_lagged"]), 16)

    def test_one_frame_pose_mismatch_is_rejected(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log().replace("sample 4 graphics frame 859", "sample 4 graphics frame 860"), "current")

    def test_missing_photons_in_csg_phase_are_rejected(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log().replace("GPU phase 1 photon_samples=60", "GPU phase 1 photon_samples=0"), "current")

    def test_uncompleted_skinning_is_rejected(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log().replace("GPU phase 2 photon_samples=60 skinning_samples=60",
                "GPU phase 2 photon_samples=60 skinning_samples=0"), "current")

    def test_graphics_fallback_cannot_qualify_compute_reader_hazard(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log("lagged") + "\nRendererSystem: frame-lagged async lighting Graphics queue route accepted", "lagged")

    def test_photon_queue_assigned_graphics_cannot_qualify_async_hazard(self):
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(runtime_log("lagged").replace("queue=1 graphics_queue=0 generation=1 off_graphics=1",
                "queue=0 graphics_queue=0 generation=1 off_graphics=0"), "lagged")

    def test_declared_compute_route_without_accepted_packet_is_rejected(self):
        log = runtime_log("lagged")
        log = "\n".join(line for line in log.splitlines() if "accepted photon route phase 2" not in line)
        with self.assertRaises(SmokeFailure):
            smoke.validate_series_log(log, "lagged")

    def test_stale_pose_after_csg_removal_is_rejected(self):
        series = image_series()
        series["lagged"][4]["receiver"] = region((150, 150, 150))
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)

    def test_photon_control_without_visible_ground_signal_is_rejected(self):
        series = image_series()
        for sample in range(8):
            series["no_photons"][sample]["ground"] = deepcopy(series["current"][sample]["ground"])
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)

    def test_cutter_without_visible_receiver_signal_is_rejected(self):
        series = image_series()
        for variant in ("current", "lagged"):
            for sample in (2, 3, 6, 7):
                series[variant][sample]["receiver"] = deepcopy(series[variant][sample % 2]["receiver"])
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)

    def test_static_bind_pose_cannot_satisfy_live_animation(self):
        series = image_series()
        for variant in ("current", "lagged"):
            for sample in (1, 3, 5, 7):
                series[variant][sample]["receiver"] = deepcopy(series[variant][sample - 1]["receiver"])
        with self.assertRaises(SmokeFailure):
            smoke.compare_series(series)


if __name__ == "__main__":
    unittest.main()
