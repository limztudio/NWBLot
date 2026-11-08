"""Image fixtures for prepared texture integration."""

from __future__ import annotations

import math
import pathlib
import struct

from png_fixture import write_png_rows

LIT_A = "A"
LIT_B = "B"
LIT_G = "G"
LIT_R = "R"
LIT_Y = "Y"
LIT_ASCII = "ascii"
LIT_X00 = b"\x00"
LIT_COMPRESSION = "compression"
LIT_BOX2I = "box2i"
LIT_N_4I = "<4i"
LIT_LINEORDER = "lineOrder"
LIT_FLOAT = "float"
LIT_F = "<f"
EXR_UINT = 0
EXR_FLOAT = 2


class TextureImageFixtures:
    @staticmethod
    def write_png_pixels(path: pathlib.Path, width: int, height: int, color_type: int, pixels: tuple[tuple[int, ...], ...]) -> None:
        channel_count = 4 if color_type == 6 else 3 if color_type == 2 else 0
        if channel_count == 0:
            raise AssertionError(f"unsupported PNG fixture color type: {color_type}")
        if len(pixels) != width * height:
            raise AssertionError("PNG fixture dimensions do not match the supplied pixel count")

        rows = bytearray()
        for y in range(height):
            rows.append(0)
            for pixel in pixels[y * width : (y + 1) * width]:
                if len(pixel) != channel_count or any(component < 0 or component > 255 for component in pixel):
                    raise AssertionError("PNG fixture has an invalid pixel")
                rows.extend(pixel)

        write_png_rows(path, width, height, color_type, rows)

    @staticmethod
    def float_to_rgbe(red: float, green: float, blue: float) -> bytes:
        maximum = max(red, green, blue)
        if maximum < 1.0e-32:
            return b"\x00\x00\x00\x00"

        mantissa, exponent = math.frexp(maximum)
        scale = mantissa * 256.0 / maximum
        return bytes((max(0, min(255, int(red * scale))), max(0, min(255, int(green * scale))), max(0, min(255, int(blue * scale))), exponent + 128,))

    @staticmethod
    def write_radiance_hdr_pixels(path: pathlib.Path, width: int, height: int, pixels: tuple[tuple[float, float, float, float], ...]) -> None:
        if len(pixels) != width * height:
            raise AssertionError("Radiance fixture dimensions do not match the supplied pixel count")
        payload = bytearray(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode(LIT_ASCII))
        for red, green, blue, _alpha in pixels:
            payload.extend(TextureImageFixtures.float_to_rgbe(red, green, blue))
        path.write_bytes(payload)

    @staticmethod
    def exr_attribute(name: str, kind: str, value: bytes) -> bytes:
        return name.encode(LIT_ASCII) + LIT_X00 + kind.encode(LIT_ASCII) + LIT_X00 + struct.pack("<I", len(value)) + value

    @staticmethod
    def write_openexr(path: pathlib.Path, pixels: tuple[tuple[float, float, float, float], ...], channel_names: tuple[str, ...] = (LIT_A, LIT_B, LIT_G, LIT_R), channel_types: tuple[int, ...] = ()) -> None:
        # Minimal uncompressed scanline EXR writer so tests need no external image packages.
        width = 4
        height = 2
        if len(pixels) != width * height:
            raise AssertionError("EXR fixture dimensions do not match the supplied pixel count")
        if not channel_types:
            channel_types = (EXR_FLOAT,) * len(channel_names)
        if len(channel_types) != len(channel_names) or any(pixel_type not in (EXR_UINT, EXR_FLOAT) for pixel_type in channel_types):
            raise AssertionError("EXR fixture requires one supported pixel type per channel")
        channels = bytearray()
        for channel, pixel_type in zip(channel_names, channel_types):
            channels.extend(channel.encode(LIT_ASCII) + LIT_X00)
            channels.extend(struct.pack("<iB3xii", pixel_type, 0, 1, 1))
        channels.append(0)

        header = bytearray()
        header.extend(TextureImageFixtures.exr_attribute("channels", "chlist", bytes(channels)))
        header.extend(TextureImageFixtures.exr_attribute(LIT_COMPRESSION, LIT_COMPRESSION, LIT_X00))
        header.extend(TextureImageFixtures.exr_attribute("dataWindow", LIT_BOX2I, struct.pack(LIT_N_4I, 0, 0, width - 1, height - 1)))
        header.extend(TextureImageFixtures.exr_attribute("displayWindow", LIT_BOX2I, struct.pack(LIT_N_4I, 0, 0, width - 1, height - 1)))
        header.extend(TextureImageFixtures.exr_attribute(LIT_LINEORDER, LIT_LINEORDER, LIT_X00))
        header.extend(TextureImageFixtures.exr_attribute("pixelAspectRatio", LIT_FLOAT, struct.pack(LIT_F, 1.0)))
        header.extend(TextureImageFixtures.exr_attribute("screenWindowCenter", "v2f", struct.pack("<2f", 0.0, 0.0)))
        header.extend(TextureImageFixtures.exr_attribute("screenWindowWidth", LIT_FLOAT, struct.pack(LIT_F, 1.0)))
        header.append(0)

        chunk_data_size = width * len(channel_names) * 4
        first_chunk_offset = 8 + len(header) + height * 8
        output = bytearray(struct.pack("<II", 20000630, 2))
        output.extend(header)
        for row in range(height):
            output.extend(struct.pack("<Q", first_chunk_offset + row * (8 + chunk_data_size)))
        for row in range(height):
            output.extend(struct.pack("<ii", row, chunk_data_size))
            row_pixels = pixels[row * width : (row + 1) * width]
            for channel, pixel_type in zip(channel_names, channel_types):
                component = {LIT_R: 0, LIT_G: 1, LIT_B: 2, LIT_A: 3, LIT_Y: 0}[channel]
                packing = "I" if pixel_type == EXR_UINT else "f"
                output.extend(struct.pack(f"<{width}{packing}", *(pixel[component] for pixel in row_pixels)))
        path.write_bytes(output)
