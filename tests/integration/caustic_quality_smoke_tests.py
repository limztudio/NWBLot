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


def record(divisor=1, backend="hardware", phases=2, base=512):
    full = (base // divisor) ** 2
    return (quality.SETTING_MARKER + str(divisor) + "\n"
        + f"RendererSystem: dispatched {backend} caustic producer ({full // phases} photons/frame, "
        + f"{phases} temporal phases, {full} full-grid budget, 2 caustic lights, 10 refractive instances)")


class CausticQualitySmokeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        executable = self.output / "renderer.exe"
        executable.write_bytes(b"fixture")
        self.stress_args = ["--executable", str(executable), "--working-directory", str(self.output), "--no-logserver"]
        self.optical_args = ["--executable", str(executable), "--working-directory", str(self.output),
            "--output-directory", str(self.output / "optical")]

    def test_cli_defaults_and_only_explicit_supported_divisors(self):
        for parse, required in ((stress.parse_args, self.stress_args), (optical.parse_args, self.optical_args)):
            self.assertEqual(parse(required).caustic_photon_grid_divisor, 1)
            for divisor in (1, 2, 4):
                self.assertEqual(parse(required + ["--caustic-photon-grid-divisor", str(divisor)]).caustic_photon_grid_divisor, divisor)
            for value in ("0", "3", "8", "-1", "2.0", "fast"):
                with self.subTest(value=value), patch("sys.stderr"), self.assertRaises(SystemExit):
                    parse(required + ["--caustic-photon-grid-divisor", value])

    def test_stress_launch_strips_inherited_budget_and_uses_explicit_divisor(self):
        for divisor in (1, 2, 4):
            args = stress.parse_args(self.stress_args + ["--caustic-photon-grid-divisor", str(divisor)])
            env = stress.launch_environment({"NWB_CAUSTIC_PHOTON_GRID_DIVISOR": "8", "NWB_CAUSTIC_SMOKE_ENABLED": "0"}, args, self.output)
            self.assertEqual(env["NWB_CAUSTIC_PHOTON_GRID_DIVISOR"], str(divisor))
            self.assertNotIn("NWB_CAUSTIC_SMOKE_ENABLED", env)

    def test_all_optical_variants_share_quality_without_changing_effect_toggles(self):
        with patch.dict(optical.os.environ, {"NWB_CAUSTIC_PHOTON_GRID_DIVISOR": "8"}):
            for software in (False, True):
                for divisor in (1, 2, 4):
                    for variant in optical.VARIANTS:
                        env = optical.capture_environment(variant, software, divisor)
                        self.assertEqual(env["NWB_CAUSTIC_PHOTON_GRID_DIVISOR"], str(divisor))
                        self.assertEqual(env["NWB_CAUSTIC_SMOKE_ENABLED"], "0" if variant == "caustics_disabled" else "1")
                        self.assertEqual(env["NWB_REFRACTION_SMOKE_ENABLED"], "0" if variant == "refraction_disabled" else "1")
            self.assertEqual(optical.capture_environment("combined")["NWB_CAUSTIC_PHOTON_GRID_DIVISOR"], "1")

    def test_actual_producer_counts_match_all_phases_and_both_backends(self):
        for divisor in (1, 2, 4):
            for phases in (1, 2, 4):
                for backend, base in (("hardware", 512), ("software", 512), ("software", 128)):
                    result = quality.verify_settings(record(divisor, backend, phases, base), divisor)
                    self.assertTrue(result["verified"])
                    self.assertEqual(result["producers"][0]["photons_per_frame"] * phases, (base // divisor) ** 2)

    def test_missing_mismatched_malformed_and_incoherent_evidence_fails(self):
        text = record(2)
        cases = ("", text.replace(quality.SETTING_MARKER + "2", quality.SETTING_MARKER + "1"),
            text + "\n" + quality.SETTING_MARKER + "2", text.replace("65536 full-grid", "262144 full-grid"),
            text.replace("32768 photons/frame", "16384 photons/frame"), text.replace("2 temporal", "3 temporal"),
            text.replace("2 caustic lights", "0 caustic lights"), text.replace("producer (", "producer (bad "))
        for candidate in cases:
            with self.subTest(candidate=candidate), self.assertRaises(quality.SmokeFailure):
                quality.verify_settings(candidate, 2)
        with self.assertRaises(quality.SmokeFailure):
            quality.verify_settings(record(1, "hardware", base=128), 1)

    def test_disabled_caustics_still_proves_requested_quality_but_rejects_dispatch(self):
        text = quality.SETTING_MARKER + "4"
        self.assertEqual(quality.verify_settings(text, 4, producer_enabled=False)["producers"], [])
        with self.assertRaises(quality.SmokeFailure):
            quality.verify_settings(text, 4)
        with self.assertRaises(quality.SmokeFailure):
            quality.verify_settings(record(4), 4, producer_enabled=False)


if __name__ == "__main__":
    unittest.main()
