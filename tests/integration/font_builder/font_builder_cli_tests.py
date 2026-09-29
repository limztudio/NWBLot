"""Exercise paired font-builder output through the runtime atlas decoder."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


FONT_HEADER = struct.Struct("<IIIIQ")
ATLAS_HEADER = struct.Struct("<IIII64s32sIIIIIIIfffIII")
GLYPH_RECORD = struct.Struct("<IIIIIIIfffffI")
GROUP_HEADER = struct.Struct("<III32s")
TABLE_HEADER = struct.Struct("<II32s")
TABLE_TAGS = {0x6B65726E: "kern", 0x47504F53: "GPOS", 0x47444546: "GDEF"}


def source_bytes(path):
    data = path.read_bytes()
    if len(data) < FONT_HEADER.size:
        raise AssertionError("truncated FON1 header")
    magic, version, face_index, reserved, byte_count = FONT_HEADER.unpack_from(data)
    if (magic, version, face_index, reserved) != (0x464F4E31, 1, 0, 0):
        raise AssertionError("invalid FON1 header")
    if byte_count == 0 or byte_count != len(data) - FONT_HEADER.size:
        raise AssertionError("invalid FON1 length")
    return data[FONT_HEADER.size:]


def sfnt_tables(data):
    count = struct.unpack_from(">H", data, 4)[0]
    tables = {}
    for index in range(count):
        tag, _checksum, offset, length = struct.unpack_from(">4sIII", data, 12 + index * 16)
        tables[tag.decode("ascii")] = data[offset:offset + length]
    return tables


def read_atlas(path):
    data = path.read_bytes()
    if len(data) < ATLAS_HEADER.size:
        raise AssertionError("truncated FTA1 header")
    (magic, version, encoding, reserved, identity, font_hash, face_index,
     units_per_em, source_glyph_count, ppem, spread, guard, raster,
     ascender, descender, line_gap, glyph_count, group_count, table_count) = ATLAS_HEADER.unpack_from(data)
    if (magic, version, encoding, reserved) != (0x31415446, 1, 1, 0):
        raise AssertionError("invalid FTA1 header")
    if not (glyph_count == source_glyph_count and 0 < group_count <= 8 and table_count <= 3):
        raise AssertionError("invalid FTA1 counts")
    cursor = ATLAS_HEADER.size
    glyphs = []
    for _ in range(glyph_count):
        values = GLYPH_RECORD.unpack_from(data, cursor)
        glyphs.append({"id": values[0], "group": values[1], "channel": values[2],
                       "advance": values[11], "drawable": values[12]})
        cursor += GLYPH_RECORD.size
    groups = []
    for _ in range(group_count):
        width, height, byte_count, digest = GROUP_HEADER.unpack_from(data, cursor)
        cursor += GROUP_HEADER.size
        pixels = data[cursor:cursor + byte_count]
        if len(pixels) != byte_count or byte_count != width * height * 4:
            raise AssertionError("invalid RGBA group length")
        if hashlib.sha256(pixels).digest() != digest:
            raise AssertionError("invalid RGBA group hash")
        groups.append((width, height, pixels))
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
        cls.atlas = read_atlas(cls.output.with_suffix(".atlas"))
        cls.payloads = cls.decode_package(cls.output.with_suffix(".atlas"))

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
    def decode_package(cls, atlas):
        with tempfile.TemporaryDirectory(prefix="decoded_", dir=cls.root) as temporary:
            destination = Path(temporary)
            result = subprocess.run([str(CONFIG.package_probe), str(atlas), str(destination)],
                                    cwd=cls.root, capture_output=True, text=True, timeout=120)
            if result.returncode:
                raise AssertionError(f"Package probe exit {result.returncode}: {result.stdout}\n{result.stderr}")
            return {path.name: path.read_bytes() for path in destination.iterdir()}

    def assert_triplet(self, output):
        self.assertEqual({path.name for path in output.parent.iterdir()},
                         {output.with_suffix(extension).name for extension in (".nwb", ".font", ".atlas")})
        text = output.read_text(encoding="utf-8")
        self.assertIn("font_bundle asset;", text)
        self.assertIn("asset.schema_version = 1;", text)
        self.assertNotIn("asset.source", text)
        self.assertNotIn("asset.font", text)

    def triplet_bytes(self, output):
        return {extension: output.with_suffix(extension).read_bytes()
                for extension in (".nwb", ".font", ".atlas")}

    def test_pairing_every_glyph_and_source_hash(self):
        self.assert_triplet(self.output)
        original = source_bytes(self.latin)
        self.assertEqual(source_bytes(self.output.with_suffix(".font")), original)
        self.assertEqual(self.atlas["font_hash"], hashlib.sha256(original).digest())
        glyph_count = struct.unpack_from(">H", sfnt_tables(original)["maxp"], 4)[0]
        self.assertEqual([glyph["id"] for glyph in self.atlas["glyphs"]], list(range(glyph_count)))
        self.assertTrue(any(glyph["drawable"] == 0 and glyph["advance"] > 0
                            for glyph in self.atlas["glyphs"]))

    def test_lossless_rgba_channels_and_exact_positioning_tables(self):
        atlas = self.atlas
        expected = {f"group_{index}.rgba" for index in range(len(atlas["groups"]))}
        expected.update(f"table_{tag}.bin" for tag in atlas["tables"])
        self.assertEqual(set(self.payloads), expected)
        used = {(glyph["group"], glyph["channel"]) for glyph in atlas["glyphs"] if glyph["drawable"]}
        self.assertGreater(len(used), 4)
        self.assertGreater(len(atlas["groups"]), 1)
        for index, (width, height, pixels) in enumerate(atlas["groups"]):
            self.assertEqual(self.payloads[f"group_{index}.rgba"], pixels)
            self.assertEqual(len(pixels), width * height * 4)
        pixels = atlas["groups"][0][2]
        for channel in range(4):
            self.assertGreater(max(pixels[channel::4]), 128)
            self.assertLess(min(pixels[channel::4]), 128)
        source = sfnt_tables(source_bytes(self.latin))
        self.assertIn("GPOS", atlas["tables"])
        for tag, raw in atlas["tables"].items():
            self.assertEqual(raw, source[tag])
            self.assertEqual(self.payloads[f"table_{tag}.bin"], raw)

    def test_repeated_bake_and_raw_sfnt_input_are_byte_identical(self):
        repeated = self.root / "repeat" / "body.nwb"
        self.invoke(self.latin, repeated)
        self.assert_triplet(repeated)
        self.assertEqual(self.triplet_bytes(repeated), self.triplet_bytes(self.output))
        raw = self.root / "input.ttf"
        raw.write_bytes(source_bytes(self.latin))
        external = self.root / "external" / "body.nwb"
        self.invoke(raw, external)
        self.assertEqual(self.triplet_bytes(external), self.triplet_bytes(self.output))

    def test_overwrite_and_failed_capacity_preserve_complete_triplet(self):
        before = self.triplet_bytes(self.output)
        self.invoke(self.latin, self.output, success=False)
        self.assertEqual(self.triplet_bytes(self.output), before)
        self.invoke(self.latin, self.output, "--overwrite")
        self.assertEqual(self.triplet_bytes(self.output), before)
        self.invoke(self.latin, self.output, "--overwrite", "--extent", "32", "--max-groups", "1", success=False)
        self.assertEqual(self.triplet_bytes(self.output), before)
        self.assert_triplet(self.output)

    def test_malformed_input_and_unknown_renderer_publish_nothing(self):
        invalid = self.root / "invalid.ttf"
        invalid.write_bytes(b"not a font")
        output = self.root / "invalid_output" / "body.nwb"
        self.invoke(invalid, output, success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))
        self.invoke(self.latin, output, "--renderer", "invented", success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))

    def test_relocation_keeps_same_stem_triplet(self):
        destination = self.root / "relocated"
        destination.mkdir()
        for extension in (".nwb", ".font", ".atlas"):
            shutil.copyfile(self.output.with_suffix(extension), destination / f"body{extension}")
        relocated = destination / "body.nwb"
        self.assert_triplet(relocated)
        self.assertEqual(self.decode_package(relocated.with_suffix(".atlas")), self.payloads)

    def test_korean_full_font_uses_multiple_rgba_groups(self):
        output = self.root / "korean" / "hangul.nwb"
        self.invoke(self.korean, output, "--ppem", "32", "--extent", "2048")
        self.assert_triplet(output)
        atlas = read_atlas(output.with_suffix(".atlas"))
        original = source_bytes(self.korean)
        glyph_count = struct.unpack_from(">H", sfnt_tables(original)["maxp"], 4)[0]
        self.assertEqual(len(atlas["glyphs"]), glyph_count)
        self.assertEqual(atlas["font_hash"], hashlib.sha256(original).digest())
        self.assertGreater(len(atlas["groups"]), 1)
        self.assertLessEqual(len(atlas["groups"]), 8)
        source = sfnt_tables(original)
        for tag, raw in atlas["tables"].items():
            self.assertEqual(raw, source[tag])
        repeated = self.root / "korean_repeat" / "hangul.nwb"
        self.invoke(self.korean, repeated, "--ppem", "32", "--extent", "2048")
        self.assertEqual(self.triplet_bytes(output), self.triplet_bytes(repeated))


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
