"""Exercise the .nwb/.font authoring pair through both metadata cook importers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest


FONT_HEADER = struct.Struct("<IIIIII32s")
ATLAS_HEADER = struct.Struct("<IIII64s32sIIIIIIIfffIII")
GLYPH_RECORD = struct.Struct("<IIIIIIIfffffI")
GROUP_HEADER = struct.Struct("<IIII32s")
TABLE_HEADER = struct.Struct("<II32s")
TABLE_TAGS = {0x6B65726E: "kern", 0x47504F53: "GPOS", 0x47444546: "GDEF"}


def read_prepared_font(path):
    data = path.read_bytes()
    if len(data) < FONT_HEADER.size:
        raise AssertionError("truncated FON2 header")
    magic, version, face_index, byte_count, group_count, reserved, digest = FONT_HEADER.unpack_from(data)
    if (magic, version, face_index, reserved) != (0x324E4F46, 1, 0, 0):
        raise AssertionError("invalid FON2 header")
    if not (0 < byte_count <= 32 * 1024 * 1024 and 0 < group_count <= 8):
        raise AssertionError("invalid FON2 counts")
    cursor = FONT_HEADER.size
    directory = []
    for _ in range(group_count):
        width, height, channels, pixel_bytes, pixel_hash = GROUP_HEADER.unpack_from(data, cursor)
        if not (0 < width <= 2048 and 0 < height <= 2048 and 1 <= channels <= 4):
            raise AssertionError("invalid FON2 image dimensions")
        if pixel_bytes != width * height * channels:
            raise AssertionError("invalid FON2 image byte count")
        directory.append((width, height, channels, pixel_bytes, pixel_hash))
        cursor += GROUP_HEADER.size
    sfnt = data[cursor:cursor + byte_count]
    if len(sfnt) != byte_count or hashlib.sha256(sfnt).digest() != digest:
        raise AssertionError("invalid FON2 font digest")
    cursor += byte_count
    groups = []
    for width, height, channels, pixel_bytes, pixel_hash in directory:
        pixels = data[cursor:cursor + pixel_bytes]
        if len(pixels) != pixel_bytes or hashlib.sha256(pixels).digest() != pixel_hash:
            raise AssertionError("invalid FON2 pixel digest")
        groups.append((width, height, channels, pixels))
        cursor += pixel_bytes
    if cursor != len(data):
        raise AssertionError("trailing FON2 bytes")
    return {"sfnt": sfnt, "groups": groups, "font_hash": digest, "face_index": face_index}


def source_bytes(path):
    return read_prepared_font(path)["sfnt"]


def sfnt_tables(data):
    count = struct.unpack_from(">H", data, 4)[0]
    tables = {}
    for index in range(count):
        tag, _checksum, offset, length = struct.unpack_from(">4sIII", data, 12 + index * 16)
        tables[tag.decode("ascii")] = data[offset:offset + length]
    return tables


def read_atlas(data):
    if len(data) < ATLAS_HEADER.size:
        raise AssertionError("truncated FTA1 header")
    (magic, version, encoding, reserved, identity, font_hash, face_index,
     units_per_em, source_glyph_count, ppem, spread, guard, raster,
     ascender, descender, line_gap, glyph_count, group_count, table_count) = ATLAS_HEADER.unpack_from(data)
    if (magic, version, encoding, reserved) != (0x31415446, 2, 1, 0):
        raise AssertionError("invalid FTA1 header")
    if not (glyph_count == source_glyph_count and 0 < group_count <= 8 and table_count <= 3):
        raise AssertionError("invalid FTA1 counts")
    cursor = ATLAS_HEADER.size
    glyphs = []
    for _ in range(glyph_count):
        values = GLYPH_RECORD.unpack_from(data, cursor)
        glyphs.append({"id": values[0], "group": values[1], "channel": values[2],
                       "x": values[3], "y": values[4], "width": values[5], "height": values[6],
                       "plane_left": values[7], "plane_top": values[8],
                       "plane_right": values[9], "plane_bottom": values[10],
                       "advance_units": values[11], "drawable": values[12]})
        cursor += GLYPH_RECORD.size
    groups = []
    for _ in range(group_count):
        width, height, channels, byte_count, digest = GROUP_HEADER.unpack_from(data, cursor)
        cursor += GROUP_HEADER.size
        pixels = data[cursor:cursor + byte_count]
        if not (0 < width <= 2048 and 0 < height <= 2048 and 1 <= channels <= 4):
            raise AssertionError("invalid compact group dimensions or channels")
        if len(pixels) != byte_count or byte_count != width * height * channels:
            raise AssertionError("invalid compact group length")
        if hashlib.sha256(pixels).digest() != digest:
            raise AssertionError("invalid compact group hash")
        groups.append((width, height, channels, pixels))
        cursor += byte_count
    tables = {}
    for _ in range(table_count):
        tag, byte_count, digest = TABLE_HEADER.unpack_from(data, cursor)
        cursor += TABLE_HEADER.size
        raw = data[cursor:cursor + byte_count]
        if tag not in TABLE_TAGS or len(raw) != byte_count or hashlib.sha256(raw).digest() != digest:
            raise AssertionError("invalid positioning table")
        tables[TABLE_TAGS[tag]] = raw
        cursor += byte_count
    if cursor != len(data):
        raise AssertionError("trailing FTA1 bytes")
    return {"font_hash": font_hash, "identity": identity, "face_index": face_index,
            "units_per_em": units_per_em, "ppem": ppem, "spread": spread,
            "guard": guard, "raster": raster, "ascender": ascender,
            "descender": descender, "line_gap": line_gap,
            "glyphs": glyphs, "groups": groups, "tables": tables}


class FontBuilderCli(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="nwb_font_builder_")
        cls.root = Path(cls.work.name)
        cls.latin = CONFIG.fonts / "latin.font"
        cls.korean = CONFIG.fonts / "korean.font"
        cls.output = cls.root / "latin" / "body.nwb"
        cls.invoke(cls.latin, cls.output)
        cls.payloads = cls.decode_package(cls.output)
        cls.atlas = read_atlas(cls.payloads["atlas.bin"])

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    @classmethod
    def invoke(cls, font, output, *extra, success=True):
        arguments = [str(CONFIG.executable), "--font", str(font), "--output", str(output), *extra]
        result = subprocess.run(arguments, cwd=cls.root, capture_output=True, text=True, timeout=180)
        if (result.returncode == 0) != success:
            raise AssertionError(f"CLI exit {result.returncode}: {result.stdout}\n{result.stderr}")
        return result

    @classmethod
    def decode_package(cls, metadata, success=True):
        with tempfile.TemporaryDirectory(prefix="decoded_", dir=cls.root) as temporary:
            destination = Path(temporary)
            result = subprocess.run([str(CONFIG.package_probe), str(metadata), str(destination)],
                                    cwd=cls.root, capture_output=True, text=True, timeout=120)
            if (result.returncode == 0) != success:
                raise AssertionError(f"Package probe exit {result.returncode}: {result.stdout}\n{result.stderr}")
            return {path.name: path.read_bytes() for path in destination.iterdir()}

    def assert_pair(self, output, payloads=None):
        self.assertEqual({path.name for path in output.parent.iterdir()},
                         {output.with_suffix(extension).name for extension in (".nwb", ".font")})
        text = output.read_text(encoding="utf-8")
        self.assertIn("font face;", text)
        self.assertIn("font_atlas atlas;", text)
        self.assertIn("atlas.font = face;", text)
        self.assertIn("asset_bunch bunch = [face, atlas];", text)
        for obsolete in ("schema_version", "sha256", "source", ".atlas", "face_index",
                         "units_per_em", "glyph_count", "guard_texels", "atlas.groups",
                         "plane_right", "plane_bottom", "drawable"):
            self.assertNotIn(obsolete, text)
        payloads = self.decode_package(output) if payloads is None else payloads
        atlas = read_atlas(payloads["atlas.bin"])
        prepared = read_prepared_font(output.with_suffix(".font"))
        self.assertEqual(prepared["groups"], atlas["groups"])
        self.assertEqual(prepared["font_hash"], atlas["font_hash"])
        self.assertEqual(prepared["sfnt"], payloads["source.sfnt"])
        self.assertEqual(atlas["face_index"], prepared["face_index"])
        source_tables = sfnt_tables(prepared["sfnt"])
        self.assertEqual(atlas["units_per_em"], struct.unpack_from(">H", source_tables["head"], 18)[0])
        self.assertEqual(len(atlas["glyphs"]), struct.unpack_from(">H", source_tables["maxp"], 4)[0])
        metadata_glyphs = self.metadata_list(text, "glyphs")
        self.assertEqual(len(metadata_glyphs), len(atlas["glyphs"]))
        float_fields = ("plane_left", "plane_top", "advance_units")
        units_per_pixel = struct.unpack("<f", struct.pack("<f", atlas["units_per_em"] / atlas["ppem"]))[0]
        for index, (declared, decoded) in enumerate(zip(metadata_glyphs, atlas["glyphs"])):
            self.assertEqual(decoded["id"], index)
            self.assertEqual(set(declared), (set(decoded) - {"id", "drawable", "plane_right", "plane_bottom"})
                             if decoded["drawable"] else {"advance_units"})
            for origin, extent, end in (("plane_left", "width", "plane_right"),
                                        ("plane_top", "height", "plane_bottom")):
                size = struct.unpack("<f", struct.pack("<f", decoded[extent] * units_per_pixel))[0]
                # ARM can fuse the multiply/add; either rounding retains the same bitmap plane.
                expected = (struct.pack("<f", decoded[origin] + size),
                            struct.pack("<f", decoded[origin] + decoded[extent] * units_per_pixel))
                self.assertIn(struct.pack("<f", decoded[end]), expected)
            for key in declared:
                if key in float_fields:
                    self.assertEqual(struct.pack("<f", declared[key]), struct.pack("<f", decoded[key]))
                else:
                    self.assertEqual(declared[key], decoded[key])
        return atlas

    @staticmethod
    def metadata_list(text, field):
        match = re.search(rf"atlas\.{field}\s*=\s*(\[.*?\]);", text, re.DOTALL)
        if match is None:
            raise AssertionError(f"missing atlas.{field}")
        return json.loads(re.sub(r",\s*\]", "]", match.group(1)))

    def assert_compact_groups(self, atlas):
        self.assertEqual(atlas["guard"], 1)
        for index, (width, height, channels, pixels) in enumerate(atlas["groups"]):
            glyphs = [glyph for glyph in atlas["glyphs"] if glyph["drawable"] and glyph["group"] == index]
            self.assertTrue(glyphs)
            self.assertEqual(channels, max(glyph["channel"] for glyph in glyphs) + 1)
            self.assertEqual(width, max(glyph["x"] + glyph["width"] + 1 for glyph in glyphs))
            self.assertEqual(height, max(glyph["y"] + glyph["height"] + 1 for glyph in glyphs))
            for glyph in glyphs:
                x, y, glyph_width, glyph_height = (glyph[key] for key in ("x", "y", "width", "height"))
                channel = glyph["channel"]
                self.assertGreaterEqual(x, 1)
                self.assertGreaterEqual(y, 1)
                self.assertLess(channel, channels)
                for row in (y - 1, y + glyph_height):
                    begin = (row * width + x - 1) * channels + channel
                    end = begin + (glyph_width + 2) * channels
                    self.assertFalse(any(pixels[begin:end:channels]))
                for column in (x - 1, x + glyph_width):
                    begin = ((y - 1) * width + column) * channels + channel
                    end = begin + (glyph_height + 2) * width * channels
                    self.assertFalse(any(pixels[begin:end:width * channels]))

    def pair_bytes(self, output):
        return {extension: output.with_suffix(extension).read_bytes()
                for extension in (".nwb", ".font")}

    def test_non_drawable_glyphs_retain_positive_advance(self):
        self.assertTrue(any(glyph["drawable"] == 0 and glyph["advance_units"] > 0
                            for glyph in self.atlas["glyphs"]))

    def test_compact_channels_preserve_guards_and_exact_positioning_tables(self):
        atlas = self.atlas
        self.assert_compact_groups(atlas)
        expected = {f"group_{index}.pixels" for index in range(len(atlas["groups"]))}
        expected.update(f"table_{tag}.bin" for tag in atlas["tables"])
        expected.update(("atlas.bin", "source.sfnt"))
        self.assertEqual(set(self.payloads), expected)
        used = {(glyph["group"], glyph["channel"]) for glyph in atlas["glyphs"] if glyph["drawable"]}
        self.assertGreater(len(used), 4)
        self.assertGreater(len(atlas["groups"]), 1)
        for index, (width, height, channels, pixels) in enumerate(atlas["groups"]):
            self.assertEqual(self.payloads[f"group_{index}.pixels"], pixels)
            self.assertEqual(len(pixels), width * height * channels)
            for channel in range(channels):
                self.assertGreater(max(pixels[channel::channels]), 128)
                self.assertLess(min(pixels[channel::channels]), 128)
        source = sfnt_tables(source_bytes(self.latin))
        self.assertIn("GPOS", atlas["tables"])
        for tag, raw in atlas["tables"].items():
            self.assertEqual(raw, source[tag])
            self.assertEqual(self.payloads[f"table_{tag}.bin"], raw)

    def test_non_power_of_two_capacity_keeps_payload_within_exact_extent(self):
        output = self.root / "non_power_of_two" / "body.nwb"
        options = ("--ppem", "33", "--extent", "1023")
        self.invoke(self.latin, output, *options)
        payloads = self.decode_package(output)
        atlas = self.assert_pair(output, payloads)
        self.assert_compact_groups(atlas)
        self.assertTrue(any(dimension & (dimension - 1) for group in atlas["groups"] for dimension in group[:2]))
        self.assertTrue(all(width <= 1023 and height <= 1023 for width, height, _, _ in atlas["groups"]))
        self.assertLess(sum(len(group[3]) for group in atlas["groups"]), len(atlas["groups"]) * 1023 * 1023 * 4)
        self.assertEqual(payloads, {
            "atlas.bin": payloads["atlas.bin"], "source.sfnt": source_bytes(self.latin),
            **{f"group_{index}.pixels": group[3] for index, group in enumerate(atlas["groups"])},
            **{f"table_{tag}.bin": raw for tag, raw in atlas["tables"].items()}})

    def test_overwrite_and_failed_capacity_preserve_complete_pair(self):
        before = self.pair_bytes(self.output)
        self.invoke(self.latin, self.output, success=False)
        self.assertEqual(self.pair_bytes(self.output), before)
        self.invoke(self.latin, self.output, "--overwrite")
        self.assertEqual(self.pair_bytes(self.output), before)
        self.invoke(self.latin, self.output, "--overwrite", "--extent", "32", "--max-groups", "1", success=False)
        self.assertEqual(self.pair_bytes(self.output), before)
        self.assert_pair(self.output, self.payloads)

    def test_malformed_input_and_unknown_renderer_publish_nothing(self):
        invalid = self.root / "invalid.ttf"
        invalid.write_bytes(b"not a font")
        output = self.root / "invalid_output" / "body.nwb"
        self.invoke(invalid, output, success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))
        self.invoke(self.latin, output, "--renderer", "invented", success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))

    def test_old_font_envelope_and_corrupt_source_hash_are_rejected(self):
        sfnt = source_bytes(self.latin)
        old = self.root / "old.font"
        old.write_bytes(struct.pack("<IIIIQ", 0x464F4E31, 1, 0, 0, len(sfnt)) + sfnt)
        output = self.root / "rejected_source" / "body.nwb"
        self.invoke(old, output, success=False)
        corrupt = self.root / "corrupt.font"
        data = bytearray(self.latin.read_bytes())
        data[24] ^= 1
        corrupt.write_bytes(data)
        self.invoke(corrupt, output, success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))

    def test_partial_pair_and_occupied_work_paths_preserve_existing_bytes(self):
        for extension in (".nwb", ".font"):
            with self.subTest(extension=extension):
                output = self.root / f"partial_{extension[1:]}" / "body.nwb"
                output.parent.mkdir()
                original = self.output.with_suffix(extension).read_bytes()
                output.with_suffix(extension).write_bytes(original)
                self.invoke(self.latin, output, "--overwrite", success=False)
                self.assertEqual({path.name for path in output.parent.iterdir()},
                                 {output.with_suffix(extension).name})
                self.assertEqual(output.with_suffix(extension).read_bytes(), original)
        for extension in (".nwb.tmp", ".font.tmp", ".nwb.old", ".font.old"):
            with self.subTest(work_path=extension):
                output = self.root / f"occupied_{extension[1:]}" / "body.nwb"
                output.parent.mkdir()
                sentinel = output.with_suffix(extension)
                sentinel.write_bytes(b"owned by another operation")
                self.invoke(self.latin, output, success=False)
                self.assertEqual({path.name for path in output.parent.iterdir()}, {sentinel.name})
                self.assertEqual(sentinel.read_bytes(), b"owned by another operation")

    def test_cook_rejects_corrupt_pixels_and_invalid_readable_mappings(self):
        output = self.root / "corrupt_pair" / "body.nwb"
        output.parent.mkdir()
        original_text = self.output.read_text(encoding="utf-8")
        original_font = self.output.with_suffix(".font").read_bytes()
        output.write_text(original_text, encoding="utf-8")
        corrupt = bytearray(original_font)
        corrupt[-1] ^= 1
        output.with_suffix(".font").write_bytes(corrupt)
        self.assertFalse(self.decode_package(output, success=False))
        output.with_suffix(".font").write_bytes(original_font)
        for text in (original_text + "\nface.face_index = 0;\n",
                     original_text + "\natlas.units_per_em = 1000;\n",
                     original_text + "\natlas.guard_texels = 1;\n",
                     original_text + '\natlas.groups = [{ "width": 1, "height": 1, "channels": 4 }];\n',
                     original_text.replace('"group":', '"id": 0, "group":', 1),
                     original_text.replace('"group":', '"drawable": 1, "group":', 1),
                     original_text.replace('"group":', '"plane_right": 0, "group":', 1),
                     original_text.replace('"group":', '"plane_bottom": 0, "group":', 1),
                     re.sub(r'"height": \d+', '"height": 0', original_text, count=1),
                     original_text.replace('{ "advance_units":', '{ "x": 0, "advance_units":', 1)):
            output.write_text(text, encoding="utf-8")
            self.assertFalse(self.decode_package(output, success=False))

    def test_korean_full_font_preserves_single_channel_tail_group(self):
        output = self.root / "korean" / "hangul.nwb"
        self.invoke(self.korean, output, "--ppem", "32", "--extent", "2048")
        atlas = self.assert_pair(output)
        self.assert_compact_groups(atlas)
        self.assertEqual(atlas["groups"][-1][2], 1)
        original = source_bytes(self.korean)
        glyph_count = struct.unpack_from(">H", sfnt_tables(original)["maxp"], 4)[0]
        self.assertEqual(len(atlas["glyphs"]), glyph_count)
        self.assertEqual(atlas["font_hash"], hashlib.sha256(original).digest())
        self.assertGreater(len(atlas["groups"]), 1)
        self.assertLessEqual(len(atlas["groups"]), 8)
        source = sfnt_tables(original)
        for tag, raw in atlas["tables"].items():
            self.assertEqual(raw, source[tag])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--package-probe", type=Path, required=True)
    parser.add_argument("--fonts", type=Path, required=True)
    CONFIG, remaining = parser.parse_known_args()
    CONFIG.executable = CONFIG.executable.resolve()
    CONFIG.package_probe = CONFIG.package_probe.resolve()
    CONFIG.fonts = CONFIG.fonts.resolve()
    unittest.main(argv=[__file__, *remaining])
