#!/usr/bin/env python3
"""Reject incomplete, stale and misleading CPU/GPU diagnostic evidence without launching a renderer."""

from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import reflection_cpu_timing_smoke as smoke
import smoke_cpu_gpu_timing as timing
from window_capture_smoke import SmokeFailure

CASE = "optical_csg_cap"


def native_log():
    lines = ["GraphicsRuntime: created device 'fixture device'",
        "RendererSystem: material 'fixture' selected typed mesh",
        "RendererSystem: deferred rendering targets ready (960x720, fixture)",
        f"ReflectionSmokeProject: case {CASE} created", "ReflectionSmokeProject: reflection mode hardware",
        "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: hardware ray budget 1382400",
        "ReflectionSmokeProject: screen feedback 0", "ReflectionSmokeProject: screen steps 96",
        "ReflectionSmokeProject: optical query limit 16", "ReflectionSmokeProject: timing render unfocused 1",
        "ReflectionSmokeProject: timing in-flight ranges 32", "ReflectionSmokeProject: timing depth mip count 10",
        "Reflection resolve: hardware",
        "ReflectionCpuTiming: configured case=optical_csg_cap preparation_presentations=64 warmup_seconds=5 "
        "minimum_seconds=30 minimum_presentations=100 minimum_positive_intervals=11 diagnostic_only=1",
        "SmokeCpuGpuTimingProbe: enabled owner=reflection cpu=1 gpu=1 memory=0 diagnostic_only=1",
        "ReflectionCpuTiming: preparation complete presentations=64 source_frame=60",
        "ReflectionCpuTiming: measurement begin presentations=80 source_frame=100 warmup_seconds=5.1"]
    for index in range(11):
        lines.append(f"ReflectionCpuTiming: interval presentations=10 seconds=3 first={80+10*index} "
            f"last={90+10*index} first_source={100+10*index} end_source={110+10*index}")
    lines += ["SmokeCpuGpuTimingProbe: complete owner=reflection records=77 first=80 last=190 first_source=100 end_source=210",
        "ReflectionCpuTiming: measurement complete presentations=110 seconds=33 first=80 last=190 "
        "positive_intervals=11 end_source=210 diagnostic_only=1", "ReflectionSmokeProject: shutdown"]
    return "\n".join(lines) + "\n"


def raw_publications():
    names = [(0, index, name) for index, name in enumerate(sorted(timing.REQUIRED_CPU))]
    names += [(1, index, name) for index, name in enumerate(smoke.REQUIRED_GPU)]
    lines = ["NWB_SMOKE_CPU_GPU_DIAGNOSTIC 1", "capture cpu=1 gpu=1 memory=0 diagnostic_only=1", "window 80 190 100 210 33"]
    lines += [f"scope {domain} {index} 1 {name}" for domain, index, name in names]
    for interval in range(11):
        observation = 110 + 10 * interval
        for domain, index, name in names:
            seconds = 0.002 if domain == 0 else 0.05
            lines.append(f"sample {domain} {index} 1 {observation} {90+10*interval} {observation} "
                f"{observation-10} {observation-1} 10 {seconds*10} {seconds} {seconds} {seconds}")
    return "\n".join(lines + ["complete 77 7"]) + "\n"


def replay(log=None, raw=None):
    log = native_log() if log is None else log
    measurement = smoke.parse_runtime_log(log, CASE)
    with TemporaryDirectory() as directory:
        path = Path(directory) / smoke.RAW_NAME
        path.write_text(raw_publications() if raw is None else raw, encoding="utf-8")
        return smoke.validate_publications(log, path, measurement)


class ReflectionCpuTimingTests(unittest.TestCase):
    def test_missing_or_too_short_actual_warmup_is_rejected(self):
        for replacement in ("4.9", "nan", "garbage"):
            with self.subTest(replacement=replacement), self.assertRaises(SmokeFailure):
                replay(native_log().replace("warmup_seconds=5.1", f"warmup_seconds={replacement}"))
        with self.assertRaises(SmokeFailure):
            replay(native_log().replace(" warmup_seconds=5.1", ""))

    def test_missing_measurement_or_changed_window_fails(self):
        for replacement in ("", "ReflectionCpuTiming: measurement complete presentations=110 seconds=33 first=81 last=190 "
                "positive_intervals=11 end_source=210 diagnostic_only=1"):
            lines = native_log().splitlines()
            lines = [replacement if line.startswith("ReflectionCpuTiming: measurement complete") else line for line in lines]
            with self.subTest(replacement=replacement), self.assertRaises(SmokeFailure):
                replay("\n".join(lines))

    def test_discontinuous_source_or_presentation_interval_is_rejected(self):
        target = "first=90 last=100 first_source=110 end_source=120"
        for change in ("first=91 last=100 first_source=110 end_source=120", "first=90 last=100 first_source=111 end_source=120"):
            with self.subTest(change=change), self.assertRaisesRegex(SmokeFailure, "endpoints"):
                replay(native_log().replace(target, change))

    def test_publication_coverage_cannot_be_inferred_from_presentations(self):
        raw = raw_publications()
        # All 110 completed samples remain, but every target publication is observed in only the final interval.
        lines = []
        for line in raw.splitlines():
            if line.startswith("sample "):
                parts = line.split()
                parts[4:6] = ["210", "190"]
                line = " ".join(parts)
            lines.append(line)
        with self.assertRaisesRegex(SmokeFailure, "11 positive completed"):
            replay(raw="\n".join(lines))
        result = replay()
        self.assertTrue(all(row["positive_publication_intervals"] == 11 for row in result["required_publication_coverage"].values()))
        self.assertFalse(result["performance_qualification"])

    def test_missing_target_scope_and_too_few_completed_samples_fail(self):
        with self.assertRaisesRegex(SmokeFailure, "100 completed"):
            replay(raw=raw_publications().replace("render.reflection_hardware", "unrelated.scope"))
        raw = raw_publications().replace("10 0.5 0.05 0.05 0.05", "9 0.45 0.05 0.05 0.05")
        with self.assertRaisesRegex(SmokeFailure, "100 completed"):
            replay(raw=raw)

    def test_wrong_owner_or_stale_generation_fail(self):
        for log, raw in ((native_log().replace("owner=reflection", "owner=stress"), raw_publications()),
                (native_log(), raw_publications().replace("sample 1 0 1 ", "sample 1 0 2 "))):
            with self.subTest(log=log[-150:]), self.assertRaises(SmokeFailure):
                replay(log, raw)

    def test_repeated_publication_or_overlapping_source_window_fails(self):
        for before, after in (("1 120 100 120 110 119", "1 120 100 110 110 119"),
                ("1 120 100 120 110 119", "1 120 100 120 109 118")):
            with self.subTest(before=before), self.assertRaises(SmokeFailure):
                replay(raw=raw_publications().replace(before, after))

    def test_inherited_capture_and_diagnostic_controls_are_removed(self):
        env = smoke.launch_environment({"NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old.bmp",
            "NWB_GPU_TIMING_FILE": "old.txt", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1",
            "NWB_REFLECTION_CPU_GPU_TIMING_FILE": "old.txt"}, CASE, Path("new.txt"))
        self.assertNotIn("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", env)
        self.assertNotIn("NWB_GPU_TIMING_FILE", env)
        self.assertEqual(env["NWB_REFLECTION_SMOKE_DIAGNOSTICS"], "0")
        self.assertEqual(env["NWB_REFLECTION_CPU_GPU_TIMING_FILE"], "new.txt")

    def test_contradictory_scene_hardware_extent_and_warning_fail(self):
        for extra in ("ReflectionSmokeProject: hardware unavailable", "ReflectionSmokeProject: case optical_clear created",
                "[WARNING] unsupported source", "ReflectionSmokeStatistics: unexpected diagnostics",
                "RendererSystem: deferred rendering targets ready (1280x900, fixture)"):
            with self.subTest(extra=extra), self.assertRaises(SmokeFailure):
                replay(native_log() + extra + "\n")


if __name__ == "__main__":
    unittest.main()
