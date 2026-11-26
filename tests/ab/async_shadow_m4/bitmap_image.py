from __future__ import annotations

from pathlib import Path

from window_capture_image_codec import Bmp24Codec, PixelDiff


class BitmapImage:
    def __init__(self, path: Path):
        self.width, self.height, self.rgb = Bmp24Codec.read_bmp_24_rgb(path, allow_extra_bytes=True)

    def compare(self, other: BitmapImage) -> PixelDiff:
        return Bmp24Codec.compare_bmp_24_rgb(self.width, self.height, self.rgb, other.width, other.height, other.rgb)
