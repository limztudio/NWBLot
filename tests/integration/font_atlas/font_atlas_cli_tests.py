"""Exercise real CLI generation, lossless channels, reproducibility and failure publication."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest


def parse_metadata(path):
    text = path.read_text(encoding="utf-8")
    fields = {}
    for name, value in re.findall(r"asset\.(\w+)\s*=\s*(.*?);", text, re.DOTALL):
        fields[name] = json.loads(value)
    return fields


def sfnt_tables(path):
    data = path.read_bytes()
    count = struct.unpack_from(">H", data, 4)[0]
    tables = {}
    for index in range(count):
        tag, _checksum, offset, length = struct.unpack_from(">4sIII", data, 12 + index * 16)
        tables[tag.decode("ascii")] = data[offset:offset + length]
    return tables


class FontAtlasCli(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="nwb_font_atlas_")
        cls.root = Path(cls.work.name)
        cls.latin = CONFIG.fonts / "NotoSans-Regular.ttf"
        cls.korean = CONFIG.fonts / "NotoSansKR-Regular.otf"
        cls.output = cls.root / "latin" / "atlas.nwb"
        cls.invoke(cls.latin, cls.output)
        cls.fields = parse_metadata(cls.output)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    @classmethod
    def invoke(cls, font, output, *extra, success=True):
        result = subprocess.run([str(CONFIG.executable), "--font", str(font), "--font-asset", "project/ui/fonts/body", "--output", str(output), *extra], capture_output=True, text=True, timeout=180)
        if (result.returncode == 0) != success:
            raise AssertionError(f"CLI exit {result.returncode}: {result.stdout}\n{result.stderr}")
        return result

    def test_all_glyphs_and_font_hash(self):
        self.assertEqual(self.fields["font_sha256"], hashlib.sha256(self.latin.read_bytes()).hexdigest())
        tables = sfnt_tables(self.latin)
        count = struct.unpack_from(">H", tables["maxp"], 4)[0]
        self.assertEqual(self.fields["source_glyph_count"], count)
        self.assertEqual([g["glyph_id"] for g in self.fields["glyphs"]], list(range(count)))
        self.assertTrue(any(g["drawable"] == 0 and g["advance_units"] > 0 for g in self.fields["glyphs"]))

    def test_lossless_payload_hashes_and_four_channels(self):
        used = set()
        for glyph in self.fields["glyphs"]:
            if glyph["drawable"]:
                used.add((glyph["group"], glyph["channel"]))
        self.assertGreater(len(used), 4)
        self.assertGreater(len(self.fields["groups"]), 1)
        for group in self.fields["groups"]:
            raw = (self.output.parent / group["data"]).read_bytes()
            self.assertEqual(len(raw), group["extent"][0] * group["extent"][1] * 4)
            self.assertEqual(len(raw), group["byte_count"])
            self.assertEqual(hashlib.sha256(raw).hexdigest(), group["sha256"])
        raw = (self.output.parent / self.fields["groups"][0]["data"]).read_bytes()
        for channel in range(4):
            self.assertGreater(max(raw[channel::4]), 128)
            self.assertLess(min(raw[channel::4]), 128)

    def test_positioning_is_exact_original_table_bytes(self):
        source = sfnt_tables(self.latin)
        tags = set()
        for record in self.fields["positioning_tables"]:
            tags.add(record["tag"])
            raw = (self.output.parent / record["data"]).read_bytes()
            self.assertEqual(raw, source[record["tag"]])
            self.assertEqual(hashlib.sha256(raw).hexdigest(), record["sha256"])
        self.assertIn("GPOS", tags)
        self.assertEqual(self.fields["kerning_mode"], "opentype_tables")

    def test_repeated_bake_is_byte_identical(self):
        second = self.root / "repeat" / "atlas.nwb"
        self.invoke(self.latin, second)
        self.assertEqual(second.read_bytes(), self.output.read_bytes())
        for record in self.fields["groups"] + self.fields["positioning_tables"]:
            self.assertEqual((second.parent / record["data"]).read_bytes(), (self.output.parent / record["data"]).read_bytes())

    def test_explicit_overwrite_keeps_package_and_reuses_content(self):
        before = {p.name: p.read_bytes() for p in self.output.parent.iterdir()}
        self.invoke(self.latin, self.output, success=False)
        self.invoke(self.latin, self.output, "--overwrite")
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.output.parent.iterdir()})

    def test_capacity_failure_preserves_previous_metadata(self):
        before = {p.name: p.read_bytes() for p in self.output.parent.iterdir()}
        self.invoke(self.latin, self.output, "--overwrite", "--extent", "32", "--max-groups", "1", success=False)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.output.parent.iterdir()})

    def test_malformed_source_and_unknown_mode_publish_nothing(self):
        invalid = self.root / "invalid.ttf"
        invalid.write_bytes(b"not a font")
        destination = self.root / "invalid_output" / "atlas.nwb"
        self.invoke(invalid, destination, success=False)
        self.assertFalse(destination.exists())
        self.invoke(self.latin, destination, "--renderer", "invented", success=False)
        self.assertFalse(destination.exists())

    def test_same_path_font_named_metadata_is_rejected(self):
        source = self.root / "font.nwb"
        original = self.latin.read_bytes()
        source.write_bytes(original)
        self.invoke(source, source, "--overwrite", success=False)
        self.assertEqual(source.read_bytes(), original)

    def test_occupied_temporary_path_preserves_previous_package(self):
        previous = self.output.read_bytes()
        temporary = self.output.with_name(self.output.name + ".tmp")
        temporary.write_bytes(b"unrelated existing file")
        self.invoke(self.latin, self.output, "--overwrite", success=False)
        self.assertEqual(self.output.read_bytes(), previous)
        self.assertEqual(temporary.read_bytes(), b"unrelated existing file")
        temporary.unlink()

    def test_korean_fullfont_explicit_larger_pages(self):
        output = self.root / "korean" / "atlas.nwb"
        self.invoke(self.korean, output, "--ppem", "32", "--extent", "2048")
        fields = parse_metadata(output)
        count = struct.unpack_from(">H", sfnt_tables(self.korean)["maxp"], 4)[0]
        self.assertEqual(len(fields["glyphs"]), count)
        self.assertLessEqual(len(fields["groups"]), 8)
        self.assertGreater(len(fields["groups"]), 1)
        for group in fields["groups"]:
            raw = (output.parent / group["data"]).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), group["sha256"])
        repeated = self.root / "korean_repeat" / "atlas.nwb"
        self.invoke(self.korean, repeated, "--ppem", "32", "--extent", "2048")
        self.assertEqual(output.read_bytes(), repeated.read_bytes())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--fonts", type=Path, required=True)
    CONFIG, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])
