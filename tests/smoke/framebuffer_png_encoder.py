"""Encode captured RGB framebuffers as lossless PNG images."""
import struct
import zlib

from window_capture_smoke import SmokeFailure

LIT_I = ">I"


class FramebufferPngEncoder:
    @staticmethod
    def encode(frame):
        """Losslessly encode the unchanged framebuffer RGB pixels without imaging dependencies."""
        width, height, rows = frame
        if width <= 0 or height <= 0 or len(rows) != height or any(len(row) != width for row in rows):
            raise SmokeFailure("cannot encode a malformed framebuffer as PNG")

        def chunk(kind, payload):
            return struct.pack(LIT_I, len(payload)) + kind + payload + struct.pack(LIT_I, zlib.crc32(kind + payload) & 0xffffffff)

        # PNG filter 0 stores raw RGB bytes before lossless compression.
        scanlines = bytearray()
        for row in rows:
            scanlines.append(0)
            scanlines.extend(channel for pixel in row for channel in pixel)
        header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
        return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b"")

