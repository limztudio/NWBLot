"""Exercise the .nwb/.font authoring pair through both metadata cook importers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

LIT_SFNT_UINT16_BIG_ENDIAN = ">H"
LIT_METADATA_SUFFIX = ".nwb"
LIT_FONT_SUFFIX = ".font"
LIT_FLOAT32_LITTLE_ENDIAN = "<f"
LIT_METADATA_TEMP_SUFFIX = ".nwb.tmp"
LIT_FONT_TEMP_SUFFIX = ".font.tmp"
LIT_METADATA_BACKUP_SUFFIX = ".nwb.old"
LIT_FONT_BACKUP_SUFFIX = ".font.old"

LIT_FONT_BUILDER_TIMEOUT_SECONDS = 180

LIT_PACKAGE_PROBE_TIMEOUT_SECONDS = 120

LIT_SFNT = "sfnt"
LIT_GROUPS = "groups"
LIT_FONT_HASH = "font_hash"
LIT_FACE_INDEX = "face_index"
LIT_ID = "id"
LIT_GROUP = "group"
LIT_CHANNEL = "channel"
LIT_X = "x"
LIT_Y = "y"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_PLANE_LEFT = "plane_left"
LIT_PLANE_TOP = "plane_top"
LIT_PLANE_RIGHT = "plane_right"
LIT_PLANE_BOTTOM = "plane_bottom"
LIT_ADVANCE_UNITS = "advance_units"
LIT_DRAWABLE = "drawable"
LIT_UNITS_PER_EM = "units_per_em"
LIT_PPEM = "ppem"
LIT_GUARD = "guard"
LIT_GLYPHS = "glyphs"
LIT_TABLES = "tables"
LIT_BODY_NWB = "body.nwb"
LIT_ATLAS_BIN = "atlas.bin"
LIT_UTF_8 = "utf-8"
LIT_SOURCE_SFNT = "source.sfnt"
LIT_MAXP = "maxp"
LIT_PPEM_2 = "--ppem"
LIT_EXTENT = "--extent"
LIT_OVERWRITE = "--overwrite"
LIT_B_OWNED_BY_ANOTHER_OPERATION = b"owned by another operation"


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
    return {LIT_SFNT: sfnt, LIT_GROUPS: groups, LIT_FONT_HASH: digest, LIT_FACE_INDEX: face_index}


def source_bytes(path):
    return read_prepared_font(path)[LIT_SFNT]


def sfnt_tables(data):
    count = struct.unpack_from(LIT_SFNT_UINT16_BIG_ENDIAN, data, 4)[0]
    tables = {}
    for index in range(count):
        tag, _checksum, offset, length = struct.unpack_from(">4sIII", data, 12 + index * 16)
        tables[tag.decode("ascii")] = data[offset:offset + length]
    return tables


def read_atlas(data):
    if len(data) < ATLAS_HEADER.size:
        raise AssertionError("truncated FTA1 header")
    (magic, version, encoding, reserved, identity, font_hash, face_index, units_per_em, source_glyph_count, ppem, spread, guard, raster, ascender, descender, line_gap, glyph_count, group_count, table_count) = ATLAS_HEADER.unpack_from(data)
    if (magic, version, encoding, reserved) != (0x31415446, 2, 1, 0):
        raise AssertionError("invalid FTA1 header")
    if not (glyph_count == source_glyph_count and 0 < group_count <= 8 and table_count <= 3):
        raise AssertionError("invalid FTA1 counts")
    cursor = ATLAS_HEADER.size
    glyphs = []
    for _ in range(glyph_count):
        values = GLYPH_RECORD.unpack_from(data, cursor)
        glyphs.append({LIT_ID: values[0], LIT_GROUP: values[1], LIT_CHANNEL: values[2], LIT_X: values[3], LIT_Y: values[4], LIT_WIDTH: values[5], LIT_HEIGHT: values[6], LIT_PLANE_LEFT: values[7], LIT_PLANE_TOP: values[8], LIT_PLANE_RIGHT: values[9], LIT_PLANE_BOTTOM: values[10],
                       LIT_ADVANCE_UNITS: values[11], LIT_DRAWABLE: values[12]})
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
    return {LIT_FONT_HASH: font_hash, "identity": identity, LIT_FACE_INDEX: face_index, LIT_UNITS_PER_EM: units_per_em, LIT_PPEM: ppem, "spread": spread, LIT_GUARD: guard, "raster": raster, "ascender": ascender, "descender": descender, "line_gap": line_gap,
            LIT_GLYPHS: glyphs, LIT_GROUPS: groups, LIT_TABLES: tables}


class FontBuilderCli(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="nwb_font_builder_")
        cls.root = Path(cls.work.name)
        cls.latin = CONFIG.fonts / "latin.font"
        cls.korean = CONFIG.fonts / "korean.font"
        cls.output = cls.root / "latin" / LIT_BODY_NWB
        cls.invoke(cls.latin, cls.output)
        cls.payloads = cls.decode_package(cls.output)
        cls.atlas = read_atlas(cls.payloads[LIT_ATLAS_BIN])

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    @classmethod
    def invoke(cls, font, output, *extra, success=True):
        arguments = [str(CONFIG.executable), "--font", str(font), "--output", str(output), *extra]
        result = subprocess.run(arguments, cwd=cls.root, capture_output=True, text=True, timeout=LIT_FONT_BUILDER_TIMEOUT_SECONDS)
        if (result.returncode == 0) != success:
            raise AssertionError(f"CLI exit {result.returncode}: {result.stdout}\n{result.stderr}")
        return result

    @classmethod
    def decode_package(cls, metadata, success=True):
        with tempfile.TemporaryDirectory(prefix="decoded_", dir=cls.root) as temporary:
            destination = Path(temporary)
            result = subprocess.run([str(CONFIG.package_probe), str(metadata), str(destination)], cwd=cls.root, capture_output=True, text=True, timeout=LIT_PACKAGE_PROBE_TIMEOUT_SECONDS)
            if (result.returncode == 0) != success:
                raise AssertionError(f"Package probe exit {result.returncode}: {result.stdout}\n{result.stderr}")
            return {path.name: path.read_bytes() for path in destination.iterdir()}

    def assert_pair(self, output, payloads=None):
        self.assertEqual({path.name for path in output.parent.iterdir()}, {output.with_suffix(extension).name for extension in (LIT_METADATA_SUFFIX, LIT_FONT_SUFFIX)})
        text = output.read_text(encoding=LIT_UTF_8)
        self.assertIn("font face;", text)
        self.assertIn("font_atlas atlas;", text)
        self.assertIn("atlas.font = face;", text)
        self.assertIn("asset_bunch bunch = [face, atlas];", text)
        for obsolete in ("schema_version", "sha256", "source", ".atlas", LIT_FACE_INDEX, LIT_UNITS_PER_EM, "glyph_count", "guard_texels", "atlas.groups", LIT_PLANE_RIGHT, LIT_PLANE_BOTTOM, LIT_DRAWABLE):
            self.assertNotIn(obsolete, text)
        payloads = self.decode_package(output) if payloads is None else payloads
        atlas = read_atlas(payloads[LIT_ATLAS_BIN])
        prepared = read_prepared_font(output.with_suffix(LIT_FONT_SUFFIX))
        self.assertEqual(prepared[LIT_GROUPS], atlas[LIT_GROUPS])
        self.assertEqual(prepared[LIT_FONT_HASH], atlas[LIT_FONT_HASH])
        self.assertEqual(prepared[LIT_SFNT], payloads[LIT_SOURCE_SFNT])
        self.assertEqual(atlas[LIT_FACE_INDEX], prepared[LIT_FACE_INDEX])
        source_tables = sfnt_tables(prepared[LIT_SFNT])
        self.assertEqual(atlas[LIT_UNITS_PER_EM], struct.unpack_from(LIT_SFNT_UINT16_BIG_ENDIAN, source_tables["head"], 18)[0])
        self.assertEqual(len(atlas[LIT_GLYPHS]), struct.unpack_from(LIT_SFNT_UINT16_BIG_ENDIAN, source_tables[LIT_MAXP], 4)[0])
        metadata_glyphs = self.metadata_list(text, LIT_GLYPHS)
        self.assertEqual(len(metadata_glyphs), len(atlas[LIT_GLYPHS]))
        float_fields = (LIT_PLANE_LEFT, LIT_PLANE_TOP, LIT_ADVANCE_UNITS)
        units_per_pixel = struct.unpack(LIT_FLOAT32_LITTLE_ENDIAN, struct.pack(LIT_FLOAT32_LITTLE_ENDIAN, atlas[LIT_UNITS_PER_EM] / atlas[LIT_PPEM]))[0]
        for index, (declared, decoded) in enumerate(zip(metadata_glyphs, atlas[LIT_GLYPHS])):
            self.assertEqual(decoded[LIT_ID], index)
            self.assertEqual(set(declared), (set(decoded) - {LIT_ID, LIT_DRAWABLE, LIT_PLANE_RIGHT, LIT_PLANE_BOTTOM}) if decoded[LIT_DRAWABLE] else {LIT_ADVANCE_UNITS})
            for origin, extent, end in ((LIT_PLANE_LEFT, LIT_WIDTH, LIT_PLANE_RIGHT), (LIT_PLANE_TOP, LIT_HEIGHT, LIT_PLANE_BOTTOM)):
                size = struct.unpack(LIT_FLOAT32_LITTLE_ENDIAN, struct.pack(LIT_FLOAT32_LITTLE_ENDIAN, decoded[extent] * units_per_pixel))[0]
                # ARM can fuse the multiply/add; either rounding retains the same bitmap plane.
                expected = (struct.pack(LIT_FLOAT32_LITTLE_ENDIAN, decoded[origin] + size), struct.pack(LIT_FLOAT32_LITTLE_ENDIAN, decoded[origin] + decoded[extent] * units_per_pixel))
                self.assertIn(struct.pack(LIT_FLOAT32_LITTLE_ENDIAN, decoded[end]), expected)
            for key in declared:
                if key in float_fields:
                    self.assertEqual(struct.pack(LIT_FLOAT32_LITTLE_ENDIAN, declared[key]), struct.pack(LIT_FLOAT32_LITTLE_ENDIAN, decoded[key]))
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
        self.assertEqual(atlas[LIT_GUARD], 1)
        for index, (width, height, channels, pixels) in enumerate(atlas[LIT_GROUPS]):
            glyphs = [glyph for glyph in atlas[LIT_GLYPHS] if glyph[LIT_DRAWABLE] and glyph[LIT_GROUP] == index]
            self.assertTrue(glyphs)
            self.assertEqual(channels, max(glyph[LIT_CHANNEL] for glyph in glyphs) + 1)
            self.assertEqual(width, max(glyph[LIT_X] + glyph[LIT_WIDTH] + 1 for glyph in glyphs))
            self.assertEqual(height, max(glyph[LIT_Y] + glyph[LIT_HEIGHT] + 1 for glyph in glyphs))
            for glyph in glyphs:
                x, y, glyph_width, glyph_height = (glyph[key] for key in (LIT_X, LIT_Y, LIT_WIDTH, LIT_HEIGHT))
                channel = glyph[LIT_CHANNEL]
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
        return {extension: output.with_suffix(extension).read_bytes() for extension in (LIT_METADATA_SUFFIX, LIT_FONT_SUFFIX)}

    def test_non_drawable_glyphs_retain_positive_advance(self):
        self.assertTrue(any(glyph[LIT_DRAWABLE] == 0 and glyph[LIT_ADVANCE_UNITS] > 0 for glyph in self.atlas[LIT_GLYPHS]))

    def test_compact_channels_preserve_guards_and_exact_positioning_tables(self):
        atlas = self.atlas
        self.assert_compact_groups(atlas)
        expected = {f"group_{index}.pixels" for index in range(len(atlas[LIT_GROUPS]))}
        expected.update(f"table_{tag}.bin" for tag in atlas[LIT_TABLES])
        expected.update((LIT_ATLAS_BIN, LIT_SOURCE_SFNT))
        self.assertEqual(set(self.payloads), expected)
        used = {(glyph[LIT_GROUP], glyph[LIT_CHANNEL]) for glyph in atlas[LIT_GLYPHS] if glyph[LIT_DRAWABLE]}
        self.assertGreater(len(used), 4)
        self.assertGreater(len(atlas[LIT_GROUPS]), 1)
        for index, (width, height, channels, pixels) in enumerate(atlas[LIT_GROUPS]):
            self.assertEqual(self.payloads[f"group_{index}.pixels"], pixels)
            self.assertEqual(len(pixels), width * height * channels)
            for channel in range(channels):
                self.assertGreater(max(pixels[channel::channels]), 128)
                self.assertLess(min(pixels[channel::channels]), 128)
        source = sfnt_tables(source_bytes(self.latin))
        self.assertIn("GPOS", atlas[LIT_TABLES])
        for tag, raw in atlas[LIT_TABLES].items():
            self.assertEqual(raw, source[tag])
            self.assertEqual(self.payloads[f"table_{tag}.bin"], raw)

    def test_non_power_of_two_capacity_keeps_payload_within_exact_extent(self):
        output = self.root / "non_power_of_two" / LIT_BODY_NWB
        options = (LIT_PPEM_2, "33", LIT_EXTENT, "1023")
        self.invoke(self.latin, output, *options)
        payloads = self.decode_package(output)
        atlas = self.assert_pair(output, payloads)
        self.assert_compact_groups(atlas)
        self.assertTrue(any(dimension & (dimension - 1) for group in atlas[LIT_GROUPS] for dimension in group[:2]))
        self.assertTrue(all(width <= 1023 and height <= 1023 for width, height, _, _ in atlas[LIT_GROUPS]))
        self.assertLess(sum(len(group[3]) for group in atlas[LIT_GROUPS]), len(atlas[LIT_GROUPS]) * 1023 * 1023 * 4)
        self.assertEqual(payloads, { LIT_ATLAS_BIN: payloads[LIT_ATLAS_BIN], LIT_SOURCE_SFNT: source_bytes(self.latin), **{f"group_{index}.pixels": group[3] for index, group in enumerate(atlas[LIT_GROUPS])},
            **{f"table_{tag}.bin": raw for tag, raw in atlas[LIT_TABLES].items()}})

    def test_overwrite_and_failed_capacity_preserve_complete_pair(self):
        before = self.pair_bytes(self.output)
        self.invoke(self.latin, self.output, success=False)
        self.assertEqual(self.pair_bytes(self.output), before)
        self.invoke(self.latin, self.output, LIT_OVERWRITE)
        self.assertEqual(self.pair_bytes(self.output), before)
        self.invoke(self.latin, self.output, LIT_OVERWRITE, LIT_EXTENT, "32", "--max-groups", "1", success=False)
        self.assertEqual(self.pair_bytes(self.output), before)
        self.assert_pair(self.output, self.payloads)

    def test_malformed_input_and_unknown_renderer_publish_nothing(self):
        invalid = self.root / "invalid.ttf"
        invalid.write_bytes(b"not a font")
        output = self.root / "invalid_output" / LIT_BODY_NWB
        self.invoke(invalid, output, success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))
        self.invoke(self.latin, output, "--renderer", "invented", success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))

    def test_old_font_envelope_and_corrupt_source_hash_are_rejected(self):
        sfnt = source_bytes(self.latin)
        old = self.root / "old.font"
        old.write_bytes(struct.pack("<IIIIQ", 0x464F4E31, 1, 0, 0, len(sfnt)) + sfnt)
        output = self.root / "rejected_source" / LIT_BODY_NWB
        self.invoke(old, output, success=False)
        corrupt = self.root / "corrupt.font"
        data = bytearray(self.latin.read_bytes())
        data[24] ^= 1
        corrupt.write_bytes(data)
        self.invoke(corrupt, output, success=False)
        self.assertFalse(output.parent.exists() and list(output.parent.iterdir()))

    def test_partial_pair_and_occupied_work_paths_preserve_existing_bytes(self):
        for extension in (LIT_METADATA_SUFFIX, LIT_FONT_SUFFIX):
            with self.subTest(extension=extension):
                output = self.root / f"partial_{extension[1:]}" / LIT_BODY_NWB
                output.parent.mkdir()
                original = self.output.with_suffix(extension).read_bytes()
                output.with_suffix(extension).write_bytes(original)
                self.invoke(self.latin, output, LIT_OVERWRITE, success=False)
                self.assertEqual({path.name for path in output.parent.iterdir()}, {output.with_suffix(extension).name})
                self.assertEqual(output.with_suffix(extension).read_bytes(), original)
        for extension in (LIT_METADATA_TEMP_SUFFIX, LIT_FONT_TEMP_SUFFIX, LIT_METADATA_BACKUP_SUFFIX, LIT_FONT_BACKUP_SUFFIX):
            with self.subTest(work_path=extension):
                output = self.root / f"occupied_{extension[1:]}" / LIT_BODY_NWB
                output.parent.mkdir()
                sentinel = output.with_suffix(extension)
                sentinel.write_bytes(LIT_B_OWNED_BY_ANOTHER_OPERATION)
                self.invoke(self.latin, output, success=False)
                self.assertEqual({path.name for path in output.parent.iterdir()}, {sentinel.name})
                self.assertEqual(sentinel.read_bytes(), LIT_B_OWNED_BY_ANOTHER_OPERATION)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux dangling-symlink output admission")
    def test_dangling_output_and_work_symlinks_are_preserved_without_publication(self):
        for extension in (LIT_METADATA_SUFFIX, LIT_FONT_SUFFIX, LIT_METADATA_TEMP_SUFFIX, LIT_FONT_TEMP_SUFFIX, LIT_METADATA_BACKUP_SUFFIX, LIT_FONT_BACKUP_SUFFIX):
            for overwrite in (False, True):
                with self.subTest(extension=extension, overwrite=overwrite):
                    output = self.root / f"dangling_{extension[1:]}_{int(overwrite)}" / LIT_BODY_NWB
                    output.parent.mkdir()
                    sentinel = output.with_suffix(extension)
                    missing_target = Path("missing_owner_output")
                    sentinel.symlink_to(missing_target)
                    self.invoke(self.latin, output, *((LIT_OVERWRITE,) if overwrite else ()), success=False)
                    self.assertEqual({path.name for path in output.parent.iterdir()}, {sentinel.name})
                    self.assertTrue(sentinel.is_symlink())
                    self.assertEqual(sentinel.readlink(), missing_target)
                    self.assertFalse((output.parent / missing_target).exists())

    def test_cook_rejects_corrupt_pixels_and_invalid_readable_mappings(self):
        output = self.root / "corrupt_pair" / LIT_BODY_NWB
        output.parent.mkdir()
        original_text = self.output.read_text(encoding=LIT_UTF_8)
        original_font = self.output.with_suffix(LIT_FONT_SUFFIX).read_bytes()
        output.write_text(original_text, encoding=LIT_UTF_8)
        corrupt = bytearray(original_font)
        corrupt[-1] ^= 1
        output.with_suffix(LIT_FONT_SUFFIX).write_bytes(corrupt)
        self.assertFalse(self.decode_package(output, success=False))
        output.with_suffix(LIT_FONT_SUFFIX).write_bytes(original_font)
        for text in (original_text + "\nface.face_index = 0;\n", original_text + "\natlas.units_per_em = 1000;\n", original_text + "\natlas.guard_texels = 1;\n", original_text + '\natlas.groups = [{ "width": 1, "height": 1, "channels": 4 }];\n',
                     original_text.replace('"group":', '"id": 0, "group":', 1), original_text.replace('"group":', '"drawable": 1, "group":', 1), original_text.replace('"group":', '"plane_right": 0, "group":', 1),
                     original_text.replace('"group":', '"plane_bottom": 0, "group":', 1), re.sub(r'"height": \d+', '"height": 0', original_text, count=1), original_text.replace('{ "advance_units":', '{ "x": 0, "advance_units":', 1)):
            output.write_text(text, encoding=LIT_UTF_8)
            self.assertFalse(self.decode_package(output, success=False))

    def test_korean_full_font_preserves_single_channel_tail_group(self):
        output = self.root / "korean" / "hangul.nwb"
        self.invoke(self.korean, output, LIT_PPEM_2, "32", LIT_EXTENT, "2048")
        atlas = self.assert_pair(output)
        self.assert_compact_groups(atlas)
        self.assertEqual(atlas[LIT_GROUPS][-1][2], 1)
        original = source_bytes(self.korean)
        glyph_count = struct.unpack_from(LIT_SFNT_UINT16_BIG_ENDIAN, sfnt_tables(original)[LIT_MAXP], 4)[0]
        self.assertEqual(len(atlas[LIT_GLYPHS]), glyph_count)
        self.assertEqual(atlas[LIT_FONT_HASH], hashlib.sha256(original).digest())
        self.assertGreater(len(atlas[LIT_GROUPS]), 1)
        self.assertLessEqual(len(atlas[LIT_GROUPS]), 8)
        source = sfnt_tables(original)
        for tag, raw in atlas[LIT_TABLES].items():
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
