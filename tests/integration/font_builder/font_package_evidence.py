"""Decode font builder packages independently for integration checks."""

import hashlib
import struct

LIT_ADVANCE_UNITS = "advance_units"
LIT_CHANNEL = "channel"
LIT_DRAWABLE = "drawable"
LIT_FACE_INDEX = "face_index"
LIT_FONT_HASH = "font_hash"
LIT_GLYPHS = "glyphs"
LIT_GROUP = "group"
LIT_GROUPS = "groups"
LIT_GUARD = "guard"
LIT_HEIGHT = "height"
LIT_ID = "id"
LIT_PLANE_BOTTOM = "plane_bottom"
LIT_PLANE_LEFT = "plane_left"
LIT_PLANE_RIGHT = "plane_right"
LIT_PLANE_TOP = "plane_top"
LIT_PPEM = "ppem"
LIT_SFNT = "sfnt"
LIT_SFNT_UINT16_BIG_ENDIAN = ">H"
LIT_TABLES = "tables"
LIT_UNITS_PER_EM = "units_per_em"
LIT_WIDTH = "width"
LIT_X = "x"
LIT_Y = "y"


FONT_HEADER = struct.Struct("<IIIIII32s")
ATLAS_HEADER = struct.Struct("<IIII64s32sIIIIIIIfffIII")
GLYPH_RECORD = struct.Struct("<IIIIIIIfffffI")
GROUP_HEADER = struct.Struct("<IIII32s")
TABLE_HEADER = struct.Struct("<II32s")
TABLE_TAGS = {0x6B65726E: "kern", 0x47504F53: "GPOS", 0x47444546: "GDEF"}


class FontPackageEvidence:
    @staticmethod
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


    @staticmethod
    def source_bytes(path):
        return FontPackageEvidence.read_prepared_font(path)[LIT_SFNT]


    @staticmethod
    def sfnt_tables(data):
        count = struct.unpack_from(LIT_SFNT_UINT16_BIG_ENDIAN, data, 4)[0]
        tables = {}
        for index in range(count):
            tag, _checksum, offset, length = struct.unpack_from(">4sIII", data, 12 + index * 16)
            tables[tag.decode("ascii")] = data[offset:offset + length]
        return tables


    @staticmethod
    def read_atlas(data):
        if len(data) < ATLAS_HEADER.size:
            raise AssertionError("truncated FTA1 header")
        (magic, version, encoding, reserved, identity, font_hash, face_index, units_per_em, source_glyph_count, ppem, spread, guard, raster, ascender, descender, line_gap, glyph_count, group_count, table_count) = ATLAS_HEADER.unpack_from(data)
        if (magic, version, encoding, reserved) != (0x31415446, 3, 1, 0):
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

