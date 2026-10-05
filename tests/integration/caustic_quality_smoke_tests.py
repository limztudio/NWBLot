#!/usr/bin/env python3
"""Validate explicit quality selection and actual producer evidence without launching a renderer."""

from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import caustic_quality_smoke as quality
import caustic_optical_smoke as optical
import stress_timing_smoke as stress

LIT_HARDWARE = "hardware"
LIT_N = "\n"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_CAUSTIC_PHOTON_GRID_DIVISOR = "--caustic-photon-grid-divisor"
LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR = "NWB_CAUSTIC_PHOTON_GRID_DIVISOR"
LIT_NWB_CAUSTIC_SMOKE_ENABLED = "NWB_CAUSTIC_SMOKE_ENABLED"
LIT_PRODUCERS = "producers"
LIT_MAIN = "__main__"


def record(divisor=1, backend=LIT_HARDWARE, phases=2, base=512):
    full = (base // divisor) ** 2
    return (quality.SETTING_MARKER + str(divisor) + LIT_N
        + f"RendererSystem: dispatched {backend} caustic producer ({full // phases} photons/frame, "
        + f"{phases} temporal phases, {full} full-grid budget, 2 caustic lights, 10 refractive instances)")


class CausticQualitySmokeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        executable = self.output / "renderer.exe"
        executable.write_bytes(b"fixture")
        self.stress_args = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(self.output), "--no-logserver"]
        self.optical_args = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(self.output),
            "--output-directory", str(self.output / "optical")]

    def test_cli_rejects_unsupported_photon_grid_divisors(self):
        for parse, required in ((stress.parse_args, self.stress_args), (optical.parse_args, self.optical_args)):
            for value in ("0", "3", "8", "-1", "2.0", "fast"):
                with self.subTest(value=value), patch("sys.stderr"), self.assertRaises(SystemExit):
                    parse(required + [LIT_CAUSTIC_PHOTON_GRID_DIVISOR, value])

    def test_stress_launch_strips_inherited_budget_and_uses_explicit_divisor(self):
        for divisor in (1, 2, 4):
            args = stress.parse_args(self.stress_args + [LIT_CAUSTIC_PHOTON_GRID_DIVISOR, str(divisor)])
            env = stress.launch_environment({LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR: "8", LIT_NWB_CAUSTIC_SMOKE_ENABLED: "0"}, args, self.output)
            self.assertEqual(env[LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR], str(divisor))
            self.assertNotIn(LIT_NWB_CAUSTIC_SMOKE_ENABLED, env)

    def test_optical_quality_overrides_inherited_divisor_across_variants(self):
        with patch.dict(optical.os.environ, {LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR: "8"}):
            for software in (False, True):
                for divisor in (1, 2, 4):
                    for variant in optical.VARIANTS:
                        env = optical.capture_environment(variant, software, divisor)
                        self.assertEqual(env[LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR], str(divisor))


    def test_missing_mismatched_malformed_and_incoherent_evidence_fails(self):
        text = record(2)
        self.assertTrue(quality.verify_settings(text, 2)["verified"])
        cases = ("", text.replace(quality.SETTING_MARKER + "2", quality.SETTING_MARKER + "1"),
            text + LIT_N + quality.SETTING_MARKER + "2", text.replace("65536 full-grid", "262144 full-grid"),
            text.replace("32768 photons/frame", "16384 photons/frame"), text.replace("2 temporal", "3 temporal"),
            text.replace("2 caustic lights", "0 caustic lights"), text.replace("producer (", "producer (bad "))
        for candidate in cases:
            with self.subTest(candidate=candidate), self.assertRaises(quality.SmokeFailure):
                quality.verify_settings(candidate, 2)
        with self.assertRaises(quality.SmokeFailure):
            quality.verify_settings(record(1, LIT_HARDWARE, base=128), 1)

    def test_disabled_caustics_still_proves_requested_quality_but_rejects_dispatch(self):
        text = quality.SETTING_MARKER + "4"
        self.assertEqual(quality.verify_settings(text, 4, producer_enabled=False)[LIT_PRODUCERS], [])
        with self.assertRaises(quality.SmokeFailure):
            quality.verify_settings(text, 4)
        with self.assertRaises(quality.SmokeFailure):
            quality.verify_settings(record(4), 4, producer_enabled=False)


if __name__ == LIT_MAIN:
    unittest.main()
