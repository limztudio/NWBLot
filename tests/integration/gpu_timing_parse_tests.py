#!/usr/bin/env python3
"""Require current GPU timing rows and preserve publication-window measurements."""

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "smoke"))
sys.path.insert(0, str(ROOT / "tests" / "ab"))
from gpu_timing_parse import parse_timing_file, SmokeFailure

INTERVAL = "=== interval: 20 frames / 0.5s ===\n"
CURRENT_ROW = "  frame_hash: avg=4 min=3 max=5 samples=20 total_ms=80 gpu_samples=40 sample_avg_ms=2\n"
SYMBOLS = {"frame_hash": "render.frame"}


class GpuTimingParseTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.path = Path(directory.name) / "timing.txt"

    def parse(self, text, offset=0):
        self.path.write_bytes(text.encode("utf-8"))
        return parse_timing_file(self.path, SYMBOLS, offset)

    def test_retired_or_partial_rows_fail_alone_and_among_current_intervals_then_recover(self):
        retired = CURRENT_ROW.split(" total_ms=")[0] + "\n"
        for row in (retired, CURRENT_ROW.replace(" gpu_samples=40", ""), CURRENT_ROW.replace(" sample_avg_ms=2", "")):
            for text in (INTERVAL + row, INTERVAL + CURRENT_ROW + INTERVAL + row):
                with self.subTest(text=text), self.assertRaises(SmokeFailure):
                    self.parse(text)
                self.assertEqual(self.parse(INTERVAL + CURRENT_ROW), [{"render.frame": 4.0}])

    def test_invalid_values_and_duplicate_scope_evidence_fail_then_recover(self):
        rows = [CURRENT_ROW.replace(old, new) for old, new in (
            ("avg=4", "avg=1e999"), ("avg=4", "avg=..."), ("avg=4", "avg=1e"),
            ("total_ms=80", "total_ms=-1"),
            ("sample_avg_ms=2", "sample_avg_ms=nan"), ("gpu_samples=40", "gpu_samples=0"),
            ("samples=20", "samples=0"), ("min=3", "min=6"), ("max=5", "max=2"),
        )]
        rows.append(CURRENT_ROW + CURRENT_ROW)
        for row in rows:
            with self.subTest(row=row), self.assertRaises(SmokeFailure):
                self.parse(INTERVAL + row)
            self.assertEqual(self.parse(INTERVAL + CURRENT_ROW), [{"render.frame": 4.0}])

    def test_unrecognized_indentation_or_rows_cannot_hide_retired_evidence(self):
        retired = CURRENT_ROW.split(" total_ms=")[0] + "\n"
        for row in (retired[1:], "\t" + retired.lstrip(), CURRENT_ROW.lstrip(), "unexpected telemetry\n"):
            with self.subTest(row=row), self.assertRaises(SmokeFailure):
                self.parse(INTERVAL + CURRENT_ROW + row)
            self.assertEqual(self.parse(INTERVAL + CURRENT_ROW), [{"render.frame": 4.0}])
        with self.assertRaises(SmokeFailure):
            self.parse(CURRENT_ROW)

    def test_malformed_intervals_and_native_counter_overflows_fail_then_recover(self):
        for header in (INTERVAL.replace("0.5s", "...s"), INTERVAL.replace("0.5s", "1e999s"),
            INTERVAL.replace("0.5s", "0s"), INTERVAL.replace("0.5s", "-1s"),
            INTERVAL.replace("20 frames", "0 frames"), INTERVAL.replace("20 frames", "4294967296 frames")):
            with self.subTest(header=header), self.assertRaises(SmokeFailure):
                self.parse(INTERVAL + CURRENT_ROW + header + CURRENT_ROW)
        for old, new in (("samples=20", "samples=4294967296"), ("gpu_samples=40", "gpu_samples=18446744073709551616")):
            with self.subTest(counter=new), self.assertRaises(SmokeFailure):
                self.parse(INTERVAL + CURRENT_ROW.replace(old, new))
        zero = CURRENT_ROW.replace("avg=4 min=3 max=5", "avg=0 min=0 max=0").replace("total_ms=80", "total_ms=0").replace("sample_avg_ms=2", "sample_avg_ms=0")
        self.assertEqual(self.parse(INTERVAL + zero), [{"render.frame": 0.0}])
        self.assertEqual(self.parse(INTERVAL + CURRENT_ROW), [{"render.frame": 4.0}])

    def test_measurement_offset_excludes_retired_prefix_and_preserves_window_average(self):
        prefix = INTERVAL + CURRENT_ROW.split(" total_ms=")[0] + "\n"
        text = prefix + INTERVAL + CURRENT_ROW
        with self.assertRaises(SmokeFailure):
            self.parse(text)
        self.assertEqual(self.parse(text, len(prefix.encode("utf-8"))), [{"render.frame": 4.0}])
        for offset in (-1, len(text.encode("utf-8")) + 1):
            with self.subTest(offset=offset), self.assertRaises(SmokeFailure):
                parse_timing_file(self.path, SYMBOLS, offset)


if __name__ == "__main__":
    unittest.main()
