import ctypes
from dataclasses import dataclass
import struct

from window_capture_outcome import SmokeFailure


LIT_B_BM = b"BM"
LIT_LITTLE = "little"
LIT_BIG = "big"
LIT_N_2SIHHI = "<2sIHHI"


@dataclass(frozen=True)
class PixelDiff:
    width: int
    height: int
    max_abs: int
    mean_abs: float
    changed_pixels: int
    total_pixels: int

    @property
    def changed_fraction(self) -> float:
        return self.changed_pixels / self.total_pixels if self.total_pixels else 0.0


class Bmp24Codec:
    @staticmethod
    def write_bmp_24(path, width, height, rows_rgb):
        path.parent.mkdir(parents=True, exist_ok=True)

        row_stride = ((width * 3 + 3) // 4) * 4
        image_size = row_stride * height
        file_size = 14 + 40 + image_size
        padding = b"\0" * (row_stride - width * 3)

        with path.open("wb") as out:
            out.write(struct.pack(LIT_N_2SIHHI, LIT_B_BM, file_size, 0, 0, 14 + 40))
            out.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
            for row in reversed(rows_rgb):
                encoded = bytearray()
                for red, green, blue in row:
                    encoded.extend((blue, green, red))
                out.write(encoded)
                out.write(padding)

    @staticmethod
    def read_bmp_24_rows(path):
        try:
            data = path.read_bytes()
        except OSError as error:
            raise SmokeFailure(f"failed to read application capture BMP '{path}': {error}") from error

        if len(data) < 54:
            raise SmokeFailure(f"application capture BMP '{path}' is shorter than the required headers")

        signature, declared_file_size, _, _, pixel_offset = struct.unpack_from(LIT_N_2SIHHI, data, 0)
        if signature != LIT_B_BM:
            raise SmokeFailure(f"application capture BMP '{path}' has an invalid signature")
        if declared_file_size != len(data):
            raise SmokeFailure(f"application capture BMP '{path}' declares {declared_file_size} bytes but contains {len(data)}")

        dib_size = struct.unpack_from("<I", data, 14)[0]
        if dib_size < 40 or 14 + dib_size > len(data):
            raise SmokeFailure(f"application capture BMP '{path}' has an invalid DIB header size {dib_size}")

        width, signed_height, planes, bits_per_pixel, compression, image_size = struct.unpack_from("<iiHHII", data, 18)
        if width <= 0 or signed_height == 0:
            raise SmokeFailure(f"application capture BMP '{path}' has invalid dimensions {width}x{signed_height}")
        if planes != 1 or bits_per_pixel != 24 or compression != 0:
            raise SmokeFailure(f"application capture BMP '{path}' must be uncompressed 24-bit RGB " f"(planes={planes}, bits={bits_per_pixel}, compression={compression})")
        if pixel_offset < 14 + dib_size or pixel_offset > len(data):
            raise SmokeFailure(f"application capture BMP '{path}' has invalid pixel offset {pixel_offset}")

        height = abs(signed_height)
        row_stride = ((width * 3 + 3) // 4) * 4
        expected_image_size = row_stride * height
        expected_file_size = pixel_offset + expected_image_size
        if image_size not in (0, expected_image_size):
            raise SmokeFailure(f"application capture BMP '{path}' declares image size {image_size}, expected {expected_image_size}")
        if expected_file_size != len(data):
            raise SmokeFailure(f"application capture BMP '{path}' pixel payload ends at {expected_file_size}, file has {len(data)} bytes")

        stored_rows = []
        for stored_y in range(height):
            row_start = pixel_offset + stored_y * row_stride
            row = []
            for x in range(width):
                blue, green, red = data[row_start + x * 3:row_start + x * 3 + 3]
                row.append((red, green, blue))
            stored_rows.append(row)

        rows_rgb = stored_rows if signed_height < 0 else list(reversed(stored_rows))
        return width, height, rows_rgb

    @staticmethod
    def read_bmp_24_rgb(path, *, allow_extra_bytes=False):
        try:
            data = path.read_bytes()
        except OSError as error:
            raise SmokeFailure(f"could not read BMP '{path}': {error}") from error
        if len(data) < 54 or data[:2] != LIT_B_BM:
            raise SmokeFailure(f"capture is not a BMP: {path}")

        pixel_offset = struct.unpack_from("<I", data, 10)[0]
        dib_size = struct.unpack_from("<I", data, 14)[0]
        if dib_size < 40 or len(data) < 14 + dib_size:
            raise SmokeFailure(f"capture has an unsupported DIB header: {path}")
        if pixel_offset < 14 + dib_size:
            raise SmokeFailure(f"capture pixel data overlaps its headers: {path}")

        width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
        if width <= 0 or signed_height == 0 or planes != 1 or bits_per_pixel != 24 or compression != 0:
            raise SmokeFailure(f"capture must be an uncompressed 24-bit BMP: {path}")

        height = abs(signed_height)
        source_stride = ((width * 3 + 3) // 4) * 4
        required_bytes = pixel_offset + source_stride * height
        if required_bytes > len(data) or (not allow_extra_bytes and required_bytes != len(data)):
            raise SmokeFailure(f"capture pixel data is truncated: {path}")

        rows = []
        bottom_up = signed_height > 0
        for logical_row in range(height):
            source_row = height - 1 - logical_row if bottom_up else logical_row
            source_offset = pixel_offset + source_row * source_stride
            source = data[source_offset:source_offset + width * 3]
            rgb = bytearray(width * 3)
            rgb[0::3] = source[2::3]
            rgb[1::3] = source[1::3]
            rgb[2::3] = source[0::3]
            rows.append(bytes(rgb))
        return width, height, b"".join(rows)

    @staticmethod
    def write_bmp_24_rgb(path, width, height, rgb):
        if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
            raise ValueError("invalid RGB baseline image dimensions or byte count")

        path.parent.mkdir(parents=True, exist_ok=True)
        row_stride = ((width * 3 + 3) // 4) * 4
        image_size = row_stride * height
        padding = b"\0" * (row_stride - width * 3)
        with path.open("wb") as output:
            output.write(struct.pack(LIT_N_2SIHHI, LIT_B_BM, 14 + 40 + image_size, 0, 0, 54))
            output.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
            for row_index in range(height - 1, -1, -1):
                row = rgb[row_index * width * 3:(row_index + 1) * width * 3]
                bgr = bytearray(width * 3)
                bgr[0::3] = row[2::3]
                bgr[1::3] = row[1::3]
                bgr[2::3] = row[0::3]
                output.write(bgr)
                output.write(padding)

    @staticmethod
    def compare_bmp_24_rgb(first_width, first_height, first_rgb, second_width, second_height, second_rgb, *, difference_path=None,
            dimension_error="pixel-parity captures have different dimensions: {0}x{1} vs {2}x{3}"):
        if (first_width, first_height) != (second_width, second_height):
            raise SmokeFailure(dimension_error.format(first_width, first_height, second_width, second_height))

        total_abs = 0
        max_abs = 0
        changed_pixels = 0
        difference_rgb = bytearray(len(first_rgb)) if difference_path is not None else None
        for offset in range(0, len(first_rgb), 3):
            changed = False
            for channel in range(3):
                delta = abs(first_rgb[offset + channel] - second_rgb[offset + channel])
                total_abs += delta
                max_abs = max(max_abs, delta)
                if difference_rgb is not None:
                    difference_rgb[offset + channel] = min(255, delta * 8)
                changed = changed or delta != 0
            if changed:
                changed_pixels += 1

        if difference_path is not None:
            Bmp24Codec.write_bmp_24_rgb(difference_path, first_width, first_height, bytes(difference_rgb))

        total_pixels = first_width * first_height
        return PixelDiff(width=first_width, height=first_height, max_abs=max_abs, mean_abs=total_abs / len(first_rgb),
            changed_pixels=changed_pixels, total_pixels=total_pixels)


class NativePixelCodec:
    @staticmethod
    def channel_from_mask(pixel, mask):
        if mask == 0:
            return 0

        low_bit = mask & -mask
        shift = low_bit.bit_length() - 1
        bit_count = mask.bit_length() - shift
        value = (pixel & mask) >> shift
        maximum = (1 << bit_count) - 1
        return (value * 255 + maximum // 2) // maximum

    @staticmethod
    def ximage_rgb_rows(image, byte_order):
        bytes_per_pixel = (image.bits_per_pixel + 7) // 8
        raw_size = image.bytes_per_line * image.height
        raw = ctypes.string_at(image.data, raw_size)
        rows = []

        if (image.bits_per_pixel in (24, 32) and (image.red_mask, image.green_mask, image.blue_mask) == (0xFF0000, 0xFF00, 0xFF)
                and byte_order in (LIT_LITTLE, LIT_BIG) and image.width >= 0 and image.bytes_per_line >= image.width * bytes_per_pixel):
            red, green, blue = (2, 1, 0) if byte_order == LIT_LITTLE else (bytes_per_pixel - 3, bytes_per_pixel - 2, bytes_per_pixel - 1)
            for y in range(image.height):
                row_base = y * image.bytes_per_line
                row_end = row_base + image.width * bytes_per_pixel
                rows.append(list(zip(raw[row_base + red:row_end:bytes_per_pixel], raw[row_base + green:row_end:bytes_per_pixel], raw[row_base + blue:row_end:bytes_per_pixel])))
            return rows

        for y in range(image.height):
            row_base = y * image.bytes_per_line
            row = []
            for x in range(image.width):
                pixel_base = row_base + x * bytes_per_pixel
                pixel = int.from_bytes(raw[pixel_base:pixel_base + bytes_per_pixel], byte_order)
                red = NativePixelCodec.channel_from_mask(pixel, image.red_mask)
                green = NativePixelCodec.channel_from_mask(pixel, image.green_mask)
                blue = NativePixelCodec.channel_from_mask(pixel, image.blue_mask)
                row.append((red, green, blue))
            rows.append(row)

        return rows

    @staticmethod
    def bgra_rgb_rows(buffer, width, height):
        rows = []
        for y in range(height):
            row = []
            row_base = y * width * 4
            for x in range(width):
                pixel_base = row_base + x * 4
                blue = buffer[pixel_base]
                green = buffer[pixel_base + 1]
                red = buffer[pixel_base + 2]
                row.append((red, green, blue))
            rows.append(row)

        return rows
