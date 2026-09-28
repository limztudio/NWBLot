#!/usr/bin/env python3
"""Check lossless gallery packaging and deterministic per-case capture controls."""

import argparse
import base64
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from refraction_gallery_smoke import (  # noqa: E402
    CASES, VARIANTS, capture_environment, frame_difference, parse_selection, png_rgb_bytes, write_gallery,
)
from window_capture_smoke import write_bmp_24  # noqa: E402

# Shared literals (no inline hardcodes below this block).
LIT_CASES = "cases"
LIT_NESTED = "nested"
LIT_NWB_REFRACTION_SMOKE_CASE = "NWB_REFRACTION_SMOKE_CASE"
LIT_NWB_REFRACTION_SMOKE_GEOMETRY = "NWB_REFRACTION_SMOKE_GEOMETRY"
LIT_NWB_REFRACTION_SMOKE_ENABLED = "NWB_REFRACTION_SMOKE_ENABLED"
LIT_NWB_REFRACTION_SMOKE_HARDWARE = "NWB_REFRACTION_SMOKE_HARDWARE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT"
LIT_PATH = "PATH"
LIT_PRESERVED = "preserved"
LIT_PRISM = "prism"
LIT_GEOMETRY = "geometry"
LIT_I = ">I"
LIT_CAPTURES = "captures"
LIT_IMAGE = "image"
LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"


class RefractionGalleryTests(unittest.TestCase):
    def test_case_selection_rejects_unknown_duplicates_and_empty(self):
        self.assertEqual(parse_selection("nested, torus", CASES, LIT_CASES), (LIT_NESTED, "torus"))
        for value in ("", "unknown", "nested,nested"):
            with self.assertRaises(argparse.ArgumentTypeError):
                parse_selection(value, CASES, LIT_CASES)

    def test_capture_environment_overrides_case_and_removes_stale_capture_controls(self):
        inherited = {
            LIT_NWB_REFRACTION_SMOKE_CASE: LIT_NESTED, LIT_NWB_REFRACTION_SMOKE_GEOMETRY: "1",
            LIT_NWB_REFRACTION_SMOKE_ENABLED: "0", LIT_NWB_REFRACTION_SMOKE_HARDWARE: "1",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "1", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: "old.bmp",
            LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO: "1", LIT_PATH: LIT_PRESERVED,
        }
        env = capture_environment(LIT_PRISM, "screen", inherited)
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_CASE], LIT_PRISM)
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_GEOMETRY], "0")
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_ENABLED], "1")
        self.assertEqual(env[LIT_NWB_REFRACTION_SMOKE_HARDWARE], "0")
        self.assertEqual(env[LIT_PATH], LIT_PRESERVED)
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, env)
        self.assertNotIn(LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH, env)
        self.assertNotIn(LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO, env)
        self.assertEqual(inherited[LIT_NWB_REFRACTION_SMOKE_CASE], LIT_NESTED)
        preview = capture_environment(LIT_NESTED, LIT_GEOMETRY, inherited)
        self.assertEqual(preview[LIT_NWB_REFRACTION_SMOKE_ENABLED], "0")
        self.assertEqual(preview[LIT_NWB_REFRACTION_SMOKE_GEOMETRY], "1")

    @staticmethod
    def decode_png_pixels(data):
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise AssertionError("invalid PNG signature")
        offset = 8
        compressed = b""
        while offset < len(data):
            count = struct.unpack_from(LIT_I, data, offset)[0]
            kind = data[offset + 4:offset + 8]
            payload = data[offset + 8:offset + 8 + count]
            crc = struct.unpack_from(LIT_I, data, offset + 8 + count)[0]
            if crc != zlib.crc32(kind + payload) & 0xffffffff:
                raise AssertionError("invalid PNG CRC")
            if kind == b"IHDR":
                width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
                if (depth, color, compression, filtering, interlace) != (8, 2, 0, 0, 0):
                    raise AssertionError("unexpected PNG format")
            if kind == b"IDAT":
                compressed += payload
            offset += count + 12
        raw = zlib.decompress(compressed)
        rows = []
        for y in range(height):
            row = raw[y * (width * 3 + 1):(y + 1) * (width * 3 + 1)]
            if row[0] != 0:
                raise AssertionError("unexpected PNG filter")
            rows.append([tuple(row[1 + x * 3:4 + x * 3]) for x in range(width)])
        return width, height, rows

    def test_png_round_trip_preserves_rgb_and_row_order_exactly(self):
        frame = (3, 2, [[(0, 1, 2), (253, 254, 255), (100, 40, 9)], [(33, 44, 55), (0, 0, 0), (255, 255, 255)]])
        self.assertEqual(self.decode_png_pixels(png_rgb_bytes(frame)), frame)

    def test_descriptive_difference_keeps_threshold_and_channel_units(self):
        reference = (2, 1, [[(0, 0, 0), (10, 10, 10)]])
        output = (2, 1, [[(9, 0, 0), (18, 10, 10)]])
        result = frame_difference(reference, output)
        self.assertEqual(result["changed_pixels"], 1)
        self.assertEqual(result["maximum_channel_difference"], 9)
        self.assertEqual(result["changed_fraction"], 0.5)
        self.assertAlmostEqual(result["mean_absolute_rgb_difference"], 17 / 6)

    def test_gallery_embeds_exact_capture_pixels_and_retains_bmp(self):
        frame = (2, 1, [[(1, 200, 3), (244, 5, 255)]])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "single_geometry.bmp"
            write_bmp_24(path, *frame)
            original = path.read_bytes()
            manifest = {"variants": list(VARIANTS), LIT_CASES: [{LIT_CAPTURES: [{"id": LIT_GEOMETRY, "file": path.name}]}]}
            write_gallery(root, manifest)
            html = (root / "gallery.html").read_text(encoding=LIT_UTF_8)
            payload = html.split('<script id="capture-data" type="application/json">', 1)[1].split("</script>", 1)[0]
            embedded = json.loads(payload)
            image = embedded[LIT_CASES][0][LIT_CAPTURES][0][LIT_IMAGE]
            self.assertTrue(image.startswith("data:image/png;base64,"))
            self.assertEqual(self.decode_png_pixels(base64.b64decode(image.split(",", 1)[1])), frame)
            self.assertEqual(path.read_bytes(), original)
            self.assertNotIn(LIT_IMAGE, json.loads((root / "gallery_manifest.json").read_text())[LIT_CASES][0][LIT_CAPTURES][0])
            self.assertNotIn('src="https://', html)


if __name__ == LIT_MAIN:
    unittest.main()
