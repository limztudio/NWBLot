import struct
import tempfile
import unittest
from pathlib import Path

import window_capture_image
import window_capture_smoke


LIT_I = "<I"


class BmpReadbackTests(unittest.TestCase):
    def test_bmp_parser_rejects_declared_size_and_compression_mismatches(self):
        rows = [[(1, 2, 3), (4, 5, 6)]]
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "invalid.bmp"
            window_capture_image.write_bmp_24(path, 2, 1, rows)
            data = bytearray(path.read_bytes())

            struct.pack_into(LIT_I, data, 2, len(data) + 1)
            path.write_bytes(data)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "declares .* but contains"):
                window_capture_smoke.read_bmp_24(path)

            struct.pack_into(LIT_I, data, 2, len(data))
            struct.pack_into(LIT_I, data, 30, 1)
            path.write_bytes(data)
            with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "must be uncompressed 24-bit"):
                window_capture_smoke.read_bmp_24(path)

