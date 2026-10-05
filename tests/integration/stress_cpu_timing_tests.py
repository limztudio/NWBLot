#!/usr/bin/env python3
"""Diagnostic publication provenance and fail-closed parser tests; no renderer or GPU is launched."""

from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_cpu_timing as diagnostic
import stress_timing_smoke as smoke
from window_capture_smoke import SmokeFailure

LIT_WINDOW_80_560_100_580_30 = "window 80 560 100 580 30"
LIT_SCOPE_1_0_RENDERER_FRAME = "scope 1 0 renderer.frame"
LIT_SAMPLE_1_0_580_560_579_570_572_3_0_009 = "sample 1 0 580 560 579 570 572 3 0.009 0.003 0.003 0.003"
LIT_N = "\n"
LIT_RENDERER_FRAME = "renderer.frame"
LIT_DECODED_FROM_HASH = "decoded_from_hash"
LIT_SCOPES = "scopes"
LIT_NAME = "name"
LIT_RAW_NAME = "raw_name"
LIT_GRAPHICS_PRESENT = "graphics.present"
LIT_PHASES_ARE_MISSING = "phases are missing"
LIT_DOMAIN = "domain"
LIT_GPU = "gpu"
LIT_SAMPLES = "samples"
LIT_SAMPLE_MEAN_MS = "sample_mean_ms"
LIT_EXCLUDED_SAMPLES = "excluded_samples"
LIT_LAST_SOURCE_FRAME = "last_source_frame"
LIT_COMPLETE_12_6 = "complete 12 6"
LIT_N_0_009 = "0.009"
LIT_CPU_GPU_TIMING_TXT = "cpu_gpu_timing.txt"
LIT_REQUESTED = "requested"
LIT_NWB_STRESS_CPU_DIAGNOSTICS = "NWB_STRESS_CPU_DIAGNOSTICS"
LIT_NWB_STRESS_CPU_TIMING_FILE = "NWB_STRESS_CPU_TIMING_FILE"
LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"

MEASUREMENT = dict(first=80, last=560, seconds=30.0)
NAMES = sorted(diagnostic.REQUIRED_CPU)


def publication_text(extra_rows=()):
    lines = ["NWB_STRESS_CPU_GPU_DIAGNOSTIC 1", "capture cpu=1 gpu=1 memory=0 diagnostic_only=1", LIT_WINDOW_80_560_100_580_30]
    lines.extend(f"scope 0 {index} {name}" for index, name in enumerate(NAMES))
    lines.append(LIT_SCOPE_1_0_RENDERER_FRAME)
    rows = [f"sample 0 {index} 101 81 100 100 100 1 0.002 0.002 0.002 0.002" for index in range(len(NAMES))]
    rows.append("sample 1 0 101 81 100 97 99 3 0.006 0.002 0.002 0.002")
    rows.extend(f"sample 0 {index} 580 560 579 579 579 1 0.004 0.004 0.004 0.004" for index in range(len(NAMES)))
    rows.append(LIT_SAMPLE_1_0_580_560_579_570_572_3_0_009)
    rows.extend(extra_rows)
    return LIT_N.join(lines + rows + [f"complete {len(rows)} {len(NAMES)+1}"]) + LIT_N


def complete_log(records=12):
    return diagnostic.ENABLED + LIT_N + diagnostic.COMPLETE + f"records={records} first=80 last=560 first_source=100 end_source=580\n"


class StressCpuTimingTests(unittest.TestCase):

    def test_unrecognized_or_tampered_hash_does_not_satisfy_required_phase(self):
        from name_symbols import debug_name_hash_token
        token = debug_name_hash_token(LIT_GRAPHICS_PRESENT)
        corrupted = ("0" if token[0] != "0" else "1") + token[1:]
        with self.assertRaisesRegex(SmokeFailure, LIT_PHASES_ARE_MISSING):
            diagnostic.parse_publications(publication_text().replace(LIT_GRAPHICS_PRESENT, corrupted), MEASUREMENT)
        text = publication_text().replace(LIT_RENDERER_FRAME, debug_name_hash_token("unknown.gpu.scope"))
        result = diagnostic.parse_publications(text, MEASUREMENT)
        gpu = next(row for row in result[LIT_SCOPES] if row[LIT_DOMAIN] == LIT_GPU)
        self.assertEqual(gpu[LIT_NAME], gpu[LIT_RAW_NAME])
        self.assertFalse(gpu[LIT_DECODED_FROM_HASH])

    def test_source_window_excludes_warmup_and_reports_dispatch_denominator(self):
        result = diagnostic.parse_publications(publication_text(), MEASUREMENT)
        self.assertTrue(result["diagnostic_only"])
        self.assertFalse(result["performance_qualification"])
        cpu = next(row for row in result[LIT_SCOPES] if row[LIT_NAME] == LIT_GRAPHICS_PRESENT)
        self.assertEqual(cpu[LIT_SAMPLES], 2)
        self.assertEqual(cpu["single_source_frame_count"], 2)
        self.assertAlmostEqual(cpu[LIT_SAMPLE_MEAN_MS], 3)
        gpu = next(row for row in result[LIT_SCOPES] if row[LIT_DOMAIN] == LIT_GPU)
        self.assertEqual(gpu[LIT_SAMPLES], 3)
        self.assertEqual(gpu[LIT_EXCLUDED_SAMPLES], 3)
        self.assertAlmostEqual(gpu[LIT_SAMPLE_MEAN_MS], 3)
        self.assertEqual(gpu[LIT_LAST_SOURCE_FRAME], 572)
        self.assertLess(gpu[LIT_LAST_SOURCE_FRAME], result["window"]["end_source_frame_exclusive"] - 1)

    def test_straddling_publication_is_excluded_whole_without_prorating(self):
        text = publication_text().replace("100 97 99 3", "100 99 100 3")
        result = diagnostic.parse_publications(text, MEASUREMENT)
        self.assertFalse(result["publications"][5]["included"])

    def test_duplicate_publication_is_rejected_even_when_observed_later(self):
        text = publication_text(["sample 1 0 580 560 579 573 575 3 0.009 0.003 0.003 0.003"])
        with self.assertRaisesRegex(SmokeFailure, "publication repeated"):
            diagnostic.parse_publications(text, MEASUREMENT)

    def test_late_old_source_does_not_move_window_or_double_count(self):
        text = publication_text(["sample 1 0 580 560 580 98 99 2 0.004 0.002 0.002 0.002"])
        result = diagnostic.parse_publications(text, MEASUREMENT)
        gpu = next(row for row in result[LIT_SCOPES] if row[LIT_DOMAIN] == LIT_GPU)
        self.assertEqual(gpu[LIT_SAMPLES], 3)
        self.assertEqual(gpu[LIT_EXCLUDED_SAMPLES], 5)

    def test_wrong_window_options_or_incomplete_file_fail(self):
        mutations = ((LIT_WINDOW_80_560_100_580_30, "window 81 560 100 580 30"),
            ("100 580 30", "100 580 29"), ("memory=0", "memory=1"), (LIT_COMPLETE_12_6, ""))
        for before, after in mutations:
            with self.subTest(before=before), self.assertRaises(SmokeFailure):
                diagnostic.parse_publications(publication_text().replace(before, after), MEASUREMENT)

    def test_invalid_duration_and_source_provenance_fail(self):
        target = LIT_SAMPLE_1_0_580_560_579_570_572_3_0_009
        mutations = ("sample 1 0 580 560 579 570 581 3 0.009 0.003 0.003 0.003",
            target.replace(LIT_N_0_009, "nan"), target.replace(LIT_N_0_009, "0.9"),
            target.replace("570 572 3", "570 572 0"), target.replace("580 560", "99 80"))
        for replacement in mutations:
            with self.subTest(replacement=replacement), self.assertRaises(SmokeFailure):
                diagnostic.parse_publications(publication_text().replace(target, replacement), MEASUREMENT)

    def test_missing_required_cpu_phase_and_gpu_data_fail(self):
        for name in (LIT_GRAPHICS_PRESENT, "graphics.begin_frame"):
            with self.subTest(name=name), self.assertRaisesRegex(SmokeFailure, LIT_PHASES_ARE_MISSING):
                diagnostic.parse_publications(publication_text().replace(name, "unrelated." + name), MEASUREMENT)
        with self.assertRaisesRegex(SmokeFailure, "GPU publications"):
            diagnostic.parse_publications(publication_text().replace("579 570 572", "579 97 99"), MEASUREMENT)

    def test_scope_identity_and_footer_count_fail(self):
        text = publication_text()
        for candidate in (text.replace(LIT_SCOPE_1_0_RENDERER_FRAME, "scope 0 0 renderer.frame"),
                text.replace(LIT_COMPLETE_12_6, "complete 11 6")):
            with self.assertRaises(SmokeFailure):
                diagnostic.parse_publications(candidate, MEASUREMENT)

    def test_explicit_request_and_matching_application_markers_required(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / LIT_CPU_GPU_TIMING_TXT
            self.assertFalse(diagnostic.verify_capture("", path, MEASUREMENT, False)[LIT_REQUESTED])
            with self.assertRaises(SmokeFailure):
                diagnostic.verify_capture(complete_log(), path, MEASUREMENT, True)
            path.write_text(publication_text(), encoding=LIT_UTF_8)
            result = diagnostic.verify_capture(complete_log(), path, MEASUREMENT, True)
            self.assertTrue(result[LIT_REQUESTED])
            for log, requested in (("", True), (complete_log(13), True), (complete_log(), False),
                    (complete_log() + diagnostic.ENABLED, True)):
                with self.subTest(log=log, requested=requested), self.assertRaises(SmokeFailure):
                    diagnostic.verify_capture(log, path, MEASUREMENT, requested)

    def test_inherited_environment_cannot_inject_capture(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            renderer = root / "renderer.exe"
            renderer.write_bytes(b"synthetic")
            argv = ["--executable", str(renderer), "--working-directory", str(root), "--no-logserver"]
            args = smoke.parse_args(argv)
            env = smoke.launch_environment({LIT_NWB_STRESS_CPU_DIAGNOSTICS: "1", LIT_NWB_STRESS_CPU_TIMING_FILE: "old"}, args, root)
            self.assertNotIn(LIT_NWB_STRESS_CPU_DIAGNOSTICS, env)
            self.assertNotIn(LIT_NWB_STRESS_CPU_TIMING_FILE, env)


if __name__ == LIT_MAIN:
    unittest.main()
