#!/usr/bin/env python3
"""Require current GPU timing rows and preserve publication-window measurements."""

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "smoke"))
sys.path.insert(0, str(ROOT / "tests" / "ab"))
from gpu_timing_parse import NAME_SYMBOLS_HEADER, load_name_symbols, parse_timing_file, SmokeFailure

INTERVAL = "=== interval: 20 frames / 0.5s ===\n"
CURRENT_ROW = "  frame_hash: window_avg_ms=4 window_min_ms=3 window_max_ms=5 published_windows=20 total_ms=80 gpu_samples=40 sample_avg_ms=2\n"
RETIRED_ROW = "  frame_hash: avg=4 min=3 max=5 samples=20 total_ms=80 gpu_samples=40 sample_avg_ms=2\n"
SYMBOLS = {"frame_hash": "render.frame"}


class GpuTimingParseTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.path = Path(directory.name) / "timing.txt"

    def parse(self, text, offset=0):
        self.path.write_bytes(text.encode("utf-8"))
        return parse_timing_file(self.path, SYMBOLS, offset)

    def test_unsupported_name_symbol_document_cannot_import_rows_then_current_input_recovers(self):
        row = "sidecar_hash\t0\tsidecar.scope\n"
        current = NAME_SYMBOLS_HEADER + "\tproducer\truntime\n" + row
        unsupported = (
            "", row, "\n" + current,
            "nwb_namesym_v0\n" + row,
            "nwb_namesym_v2\n" + row,
            "nwb_namesym_v10\n" + row,
            NAME_SYMBOLS_HEADER + "_suffix\n" + row,
            "unknown\n" + current,
        )
        for text in unsupported:
            with self.subTest(document=text):
                self.path.write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(SmokeFailure, "requires current nwb_namesym_v1 header"):
                    load_name_symbols(self.path, ())
                self.path.write_text(current, encoding="utf-8")
                self.assertEqual(load_name_symbols(self.path, ()), {"sidecar_hash": "sidecar.scope"})

        self.path.write_text(NAME_SYMBOLS_HEADER + "\n", encoding="utf-8")
        self.assertEqual(load_name_symbols(self.path, ()), {})

    def test_retired_or_partial_rows_fail_alone_and_among_current_intervals_then_recover(self):
        partial = CURRENT_ROW.split(" total_ms=")[0] + "\n"
        retired_labels = tuple(CURRENT_ROW.replace(current, old) for current, old in (
            ("window_avg_ms=", "avg="), ("window_min_ms=", "min="),
            ("window_max_ms=", "max="), ("published_windows=", "samples="),
        ))
        for row in (RETIRED_ROW, partial, *retired_labels,
            CURRENT_ROW.replace(" gpu_samples=40", ""), CURRENT_ROW.replace(" sample_avg_ms=2", "")):
            for text in (INTERVAL + row, INTERVAL + CURRENT_ROW + INTERVAL + row):
                with self.subTest(text=text), self.assertRaises(SmokeFailure):
                    self.parse(text)
                self.assertEqual(self.parse(INTERVAL + CURRENT_ROW), [{"render.frame": 4.0}])

    def test_invalid_values_and_duplicate_scope_evidence_fail_then_recover(self):
        rows = [CURRENT_ROW.replace(old, new) for old, new in (
            ("window_avg_ms=4", "window_avg_ms=1e999"), ("window_avg_ms=4", "window_avg_ms=..."), ("window_avg_ms=4", "window_avg_ms=1e"),
            ("total_ms=80", "total_ms=-1"),
            ("sample_avg_ms=2", "sample_avg_ms=nan"), ("gpu_samples=40", "gpu_samples=0"),
            ("published_windows=20", "published_windows=0"), ("window_min_ms=3", "window_min_ms=6"), ("window_max_ms=5", "window_max_ms=2"),
        )]
        rows.append(CURRENT_ROW + CURRENT_ROW)
        for row in rows:
            with self.subTest(row=row), self.assertRaises(SmokeFailure):
                self.parse(INTERVAL + row)
            self.assertEqual(self.parse(INTERVAL + CURRENT_ROW), [{"render.frame": 4.0}])

    def test_unrecognized_indentation_or_rows_cannot_hide_retired_evidence(self):
        retired = RETIRED_ROW
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
        for old, new in (("published_windows=20", "published_windows=4294967296"), ("gpu_samples=40", "gpu_samples=18446744073709551616")):
            with self.subTest(counter=new), self.assertRaises(SmokeFailure):
                self.parse(INTERVAL + CURRENT_ROW.replace(old, new))
        zero = CURRENT_ROW.replace("window_avg_ms=4 window_min_ms=3 window_max_ms=5", "window_avg_ms=0 window_min_ms=0 window_max_ms=0").replace("total_ms=80", "total_ms=0").replace("sample_avg_ms=2", "sample_avg_ms=0")
        self.assertEqual(self.parse(INTERVAL + zero), [{"render.frame": 0.0}])
        self.assertEqual(self.parse(INTERVAL + CURRENT_ROW), [{"render.frame": 4.0}])

    def test_measurement_offset_excludes_retired_prefix_and_preserves_window_average(self):
        prefix = INTERVAL + RETIRED_ROW
        text = prefix + INTERVAL + CURRENT_ROW
        with self.assertRaises(SmokeFailure):
            self.parse(text)
        self.assertEqual(self.parse(text, len(prefix.encode("utf-8"))), [{"render.frame": 4.0}])
        for offset in (-1, len(text.encode("utf-8")) + 1):
            with self.subTest(offset=offset), self.assertRaises(SmokeFailure):
                parse_timing_file(self.path, SYMBOLS, offset)


if __name__ == "__main__":
    unittest.main()
