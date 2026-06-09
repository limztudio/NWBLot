#!/usr/bin/env python3
import argparse
from pathlib import Path
import struct
import sys


def read_bmp_24(path):
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"{path} is not a BMP file")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    if header_size != 40:
        raise RuntimeError(f"{path} uses an unsupported BMP header")

    width, signed_height, planes, bpp, compression = struct.unpack_from("<iiHHI", data, 18)
    if planes != 1 or bpp != 24 or compression != 0:
        raise RuntimeError(f"{path} must be an uncompressed 24-bit BMP")

    height = abs(signed_height)
    row_stride = ((width * 3 + 3) // 4) * 4
    rows = []
    top_down = signed_height < 0
    for row_index in range(height):
        source_y = row_index if top_down else height - 1 - row_index
        row_base = pixel_offset + source_y * row_stride
        row = []
        for x in range(width):
            blue, green, red = data[row_base + x * 3:row_base + x * 3 + 3]
            row.append((red, green, blue))
        rows.append(row)

    return width, height, rows


def write_bmp_24(path, width, height, rows):
    row_stride = ((width * 3 + 3) // 4) * 4
    padding = b"\0" * (row_stride - width * 3)
    image_size = row_stride * height
    file_size = 14 + 40 + image_size

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as out:
        out.write(struct.pack("<2sIHHI", b"BM", file_size, 0, 0, 54))
        out.write(struct.pack("<IiiHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
        for row in reversed(rows):
            encoded = bytearray()
            for red, green, blue in row:
                encoded.extend((blue, green, red))
            out.write(encoded)
            out.write(padding)


def build_diff_rows(reference_rows, candidate_rows, threshold):
    diff_rows = []
    different_pixels = 0
    total_abs_delta = 0
    max_channel_delta = 0

    for reference_row, candidate_row in zip(reference_rows, candidate_rows):
        diff_row = []
        for reference, candidate in zip(reference_row, candidate_row):
            deltas = tuple(abs(a - b) for a, b in zip(reference, candidate))
            channel_max = max(deltas)
            total_abs_delta += sum(deltas)
            max_channel_delta = max(max_channel_delta, channel_max)
            if channel_max > threshold:
                different_pixels += 1
                diff_row.append((255, min(255, channel_max * 8), 0))
            else:
                luma = (54 * reference[0] + 183 * reference[1] + 19 * reference[2]) >> 8
                diff_row.append((luma, luma, luma))
        diff_rows.append(diff_row)

    return diff_rows, different_pixels, total_abs_delta, max_channel_delta


def main():
    parser = argparse.ArgumentParser(description="Compare two smoke-capture BMP files.")
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--diff-output", type=Path)
    parser.add_argument("--pixel-threshold", type=int, default=4)
    parser.add_argument("--max-different-pixels", type=int, default=1500)
    parser.add_argument("--max-mean-abs-delta", type=float, default=0.25)
    args = parser.parse_args()

    reference_width, reference_height, reference_rows = read_bmp_24(args.reference)
    candidate_width, candidate_height, candidate_rows = read_bmp_24(args.candidate)
    if (reference_width, reference_height) != (candidate_width, candidate_height):
        raise RuntimeError(
            f"image sizes differ: reference {reference_width}x{reference_height}, "
            f"candidate {candidate_width}x{candidate_height}"
        )

    diff_rows, different_pixels, total_abs_delta, max_channel_delta = build_diff_rows(
        reference_rows,
        candidate_rows,
        args.pixel_threshold,
    )
    if args.diff_output:
        write_bmp_24(args.diff_output, reference_width, reference_height, diff_rows)

    pixel_count = reference_width * reference_height
    mean_abs_delta = total_abs_delta / max(pixel_count * 3, 1)
    print(
        "BMP compare: "
        f"different_pixels={different_pixels}/{pixel_count}, "
        f"mean_abs_delta={mean_abs_delta:.4f}, "
        f"max_channel_delta={max_channel_delta}"
    )

    if different_pixels > args.max_different_pixels or mean_abs_delta > args.max_mean_abs_delta:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
