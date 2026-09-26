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

MEASUREMENT = dict(first=80, last=560, seconds=30.0)
NAMES = sorted(diagnostic.REQUIRED_CPU)


def publication_text(extra_rows=()):
    lines = ["NWB_STRESS_CPU_GPU_DIAGNOSTIC 1", "capture cpu=1 gpu=1 memory=0 diagnostic_only=1", "window 80 560 100 580 30"]
    lines.extend(f"scope 0 {index} {name}" for index, name in enumerate(NAMES))
    lines.append("scope 1 0 renderer.frame")
    rows = [f"sample 0 {index} 101 81 100 100 100 1 0.002 0.002 0.002 0.002" for index in range(len(NAMES))]
    rows.append("sample 1 0 101 81 100 97 99 3 0.006 0.002 0.002 0.002")
    rows.extend(f"sample 0 {index} 580 560 579 579 579 1 0.004 0.004 0.004 0.004" for index in range(len(NAMES)))
    rows.append("sample 1 0 580 560 579 570 572 3 0.009 0.003 0.003 0.003")
    rows.extend(extra_rows)
    return "\n".join(lines + rows + [f"complete {len(rows)} {len(NAMES)+1}"]) + "\n"


def complete_log(records=12):
    return diagnostic.ENABLED + "\n" + diagnostic.COMPLETE + f"records={records} first=80 last=560 first_source=100 end_source=580\n"


class StressCpuTimingTests(unittest.TestCase):
    def test_optimized_scope_hashes_decode_with_exact_engine_algorithm(self):
        from name_symbols import debug_name_hash_token
        text = publication_text()
        for name in NAMES:
            text = text.replace(name, debug_name_hash_token(name))
        text = text.replace("renderer.frame", debug_name_hash_token("render.frame"))
        result = diagnostic.parse_publications(text, MEASUREMENT)
        self.assertTrue(all(row["decoded_from_hash"] for row in result["scopes"]))
        self.assertEqual({row["name"] for row in result["scopes"]}, set(NAMES) | {"render.frame"})
        for row in result["scopes"]:
            self.assertEqual(row["raw_name"], debug_name_hash_token(row["name"]))

    def test_unrecognized_or_tampered_hash_does_not_satisfy_required_phase(self):
        from name_symbols import debug_name_hash_token
        token = debug_name_hash_token("graphics.present")
        corrupted = ("0" if token[0] != "0" else "1") + token[1:]
        with self.assertRaisesRegex(SmokeFailure, "phases are missing"):
            diagnostic.parse_publications(publication_text().replace("graphics.present", corrupted), MEASUREMENT)
        text = publication_text().replace("renderer.frame", debug_name_hash_token("unknown.gpu.scope"))
        result = diagnostic.parse_publications(text, MEASUREMENT)
        gpu = next(row for row in result["scopes"] if row["domain"] == "gpu")
        self.assertEqual(gpu["name"], gpu["raw_name"])
        self.assertFalse(gpu["decoded_from_hash"])

    def test_source_window_excludes_warmup_and_reports_dispatch_denominator(self):
        result = diagnostic.parse_publications(publication_text(), MEASUREMENT)
        self.assertTrue(result["diagnostic_only"])
        self.assertFalse(result["performance_qualification"])
        cpu = next(row for row in result["scopes"] if row["name"] == "graphics.present")
        self.assertEqual(cpu["samples"], 2)
        self.assertEqual(cpu["single_source_frame_count"], 2)
        self.assertAlmostEqual(cpu["sample_mean_ms"], 3)
        gpu = next(row for row in result["scopes"] if row["domain"] == "gpu")
        self.assertEqual(gpu["samples"], 3)
        self.assertEqual(gpu["excluded_samples"], 3)
        self.assertAlmostEqual(gpu["sample_mean_ms"], 3)
        self.assertEqual(gpu["last_source_frame"], 572)
        self.assertLess(gpu["last_source_frame"], result["window"]["end_source_frame_exclusive"] - 1)

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
        gpu = next(row for row in result["scopes"] if row["domain"] == "gpu")
        self.assertEqual(gpu["samples"], 3)
        self.assertEqual(gpu["excluded_samples"], 5)

    def test_wrong_window_options_or_incomplete_file_fail(self):
        mutations = (("window 80 560 100 580 30", "window 81 560 100 580 30"),
            ("100 580 30", "100 580 29"), ("memory=0", "memory=1"), ("complete 12 6", ""))
        for before, after in mutations:
            with self.subTest(before=before), self.assertRaises(SmokeFailure):
                diagnostic.parse_publications(publication_text().replace(before, after), MEASUREMENT)

    def test_invalid_duration_and_source_provenance_fail(self):
        target = "sample 1 0 580 560 579 570 572 3 0.009 0.003 0.003 0.003"
        mutations = ("sample 1 0 580 560 579 570 581 3 0.009 0.003 0.003 0.003",
            target.replace("0.009", "nan"), target.replace("0.009", "0.9"),
            target.replace("570 572 3", "570 572 0"), target.replace("580 560", "99 80"))
        for replacement in mutations:
            with self.subTest(replacement=replacement), self.assertRaises(SmokeFailure):
                diagnostic.parse_publications(publication_text().replace(target, replacement), MEASUREMENT)

    def test_missing_required_cpu_phase_and_gpu_data_fail(self):
        for name in ("graphics.present", "graphics.begin_frame"):
            with self.subTest(name=name), self.assertRaisesRegex(SmokeFailure, "phases are missing"):
                diagnostic.parse_publications(publication_text().replace(name, "unrelated." + name), MEASUREMENT)
        with self.assertRaisesRegex(SmokeFailure, "GPU publications"):
            diagnostic.parse_publications(publication_text().replace("579 570 572", "579 97 99"), MEASUREMENT)

    def test_scope_identity_and_footer_count_fail(self):
        text = publication_text()
        for candidate in (text.replace("scope 1 0 renderer.frame", "scope 0 0 renderer.frame"),
                text.replace("complete 12 6", "complete 11 6")):
            with self.assertRaises(SmokeFailure):
                diagnostic.parse_publications(candidate, MEASUREMENT)

    def test_explicit_request_and_matching_application_markers_required(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "cpu_gpu_timing.txt"
            self.assertFalse(diagnostic.verify_capture("", path, MEASUREMENT, False)["requested"])
            with self.assertRaises(SmokeFailure):
                diagnostic.verify_capture(complete_log(), path, MEASUREMENT, True)
            path.write_text(publication_text(), encoding="utf-8")
            result = diagnostic.verify_capture(complete_log(), path, MEASUREMENT, True)
            self.assertTrue(result["requested"])
            for log, requested in (("", True), (complete_log(13), True), (complete_log(), False),
                    (complete_log() + diagnostic.ENABLED, True)):
                with self.subTest(log=log, requested=requested), self.assertRaises(SmokeFailure):
                    diagnostic.verify_capture(log, path, MEASUREMENT, requested)

    def test_cli_defaults_off_and_environment_cannot_inject_capture(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            renderer = root / "renderer.exe"
            renderer.write_bytes(b"synthetic")
            argv = ["--executable", str(renderer), "--working-directory", str(root), "--no-logserver"]
            args = smoke.parse_args(argv)
            self.assertFalse(args.cpu_diagnostics)
            env = smoke.launch_environment({"NWB_STRESS_CPU_DIAGNOSTICS": "1", "NWB_STRESS_CPU_TIMING_FILE": "old"}, args, root)
            self.assertNotIn("NWB_STRESS_CPU_DIAGNOSTICS", env)
            self.assertNotIn("NWB_STRESS_CPU_TIMING_FILE", env)
            args = smoke.parse_args(argv + ["--cpu-diagnostics"])
            env = smoke.launch_environment({}, args, root)
            self.assertEqual(env["NWB_STRESS_CPU_DIAGNOSTICS"], "1")
            self.assertEqual(env["NWB_STRESS_CPU_TIMING_FILE"], str(root / "cpu_gpu_timing.txt"))


if __name__ == "__main__":
    unittest.main()
