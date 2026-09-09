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


class RefractionGalleryTests(unittest.TestCase):
    def test_case_selection_rejects_unknown_duplicates_and_empty(self):
        self.assertEqual(parse_selection("nested, torus", CASES, "cases"), ("nested", "torus"))
        for value in ("", "unknown", "nested,nested"):
            with self.assertRaises(argparse.ArgumentTypeError):
                parse_selection(value, CASES, "cases")

    def test_capture_environment_overrides_case_and_removes_stale_capture_controls(self):
        inherited = {
            "NWB_REFRACTION_SMOKE_CASE": "nested", "NWB_REFRACTION_SMOKE_GEOMETRY": "1",
            "NWB_REFRACTION_SMOKE_ENABLED": "0", "NWB_REFRACTION_SMOKE_HARDWARE": "1",
            "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "1", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH": "old.bmp",
            "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT": "1", "PATH": "preserved",
        }
        env = capture_environment("prism", "screen", inherited)
        self.assertEqual(env["NWB_REFRACTION_SMOKE_CASE"], "prism")
        self.assertEqual(env["NWB_REFRACTION_SMOKE_GEOMETRY"], "0")
        self.assertEqual(env["NWB_REFRACTION_SMOKE_ENABLED"], "1")
        self.assertEqual(env["NWB_REFRACTION_SMOKE_HARDWARE"], "0")
        self.assertEqual(env["PATH"], "preserved")
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", env)
        self.assertNotIn("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", env)
        self.assertNotIn("NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", env)
        self.assertEqual(inherited["NWB_REFRACTION_SMOKE_CASE"], "nested")
        preview = capture_environment("nested", "geometry", inherited)
        self.assertEqual(preview["NWB_REFRACTION_SMOKE_ENABLED"], "0")
        self.assertEqual(preview["NWB_REFRACTION_SMOKE_GEOMETRY"], "1")

    @staticmethod
    def decode_png_pixels(data):
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise AssertionError("invalid PNG signature")
        offset = 8
        compressed = b""
        while offset < len(data):
            count = struct.unpack_from(">I", data, offset)[0]
            kind = data[offset + 4:offset + 8]
            payload = data[offset + 8:offset + 8 + count]
            crc = struct.unpack_from(">I", data, offset + 8 + count)[0]
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
            manifest = {"variants": list(VARIANTS), "cases": [{"captures": [{"id": "geometry", "file": path.name}]}]}
            write_gallery(root, manifest)
            html = (root / "gallery.html").read_text(encoding="utf-8")
            payload = html.split('<script id="capture-data" type="application/json">', 1)[1].split("</script>", 1)[0]
            embedded = json.loads(payload)
            image = embedded["cases"][0]["captures"][0]["image"]
            self.assertTrue(image.startswith("data:image/png;base64,"))
            self.assertEqual(self.decode_png_pixels(base64.b64decode(image.split(",", 1)[1])), frame)
            self.assertEqual(path.read_bytes(), original)
            self.assertNotIn("image", json.loads((root / "gallery_manifest.json").read_text())["cases"][0]["captures"][0])
            self.assertNotIn('src="https://', html)


if __name__ == "__main__":
    unittest.main()
