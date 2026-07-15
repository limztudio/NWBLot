#!/usr/bin/env python3
# Generates a 4x-amplified abs-diff BMP between the baseline (full grid) and
# treatment (2x temporal) SW caustic captures, and reports the max abs diff.
import struct, sys

def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"BM", f"{path}: not a BMP"
    pix_off = struct.unpack_from("<I", data, 10)[0]
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    h = abs(h)
    bpp = struct.unpack_from("<H", data, 28)[0]
    assert bpp == 24, f"{path}: expected 24bpp, got {bpp}"
    row = ((w * 3 + 3) // 4) * 4  # 4-byte aligned
    return w, h, row, pix_off, data

def pixel(data, row, pix_off, w, h, x, y):
    # rows are bottom-up; mirror vertically for indexing
    off = pix_off + (h - 1 - y) * row + x * 3
    b, g, r = data[off], data[off + 1], data[off + 2]
    return r, g, b

a = sys.argv[1]; b = sys.argv[2]; out = sys.argv[3]
w, h, row, pix_off, da = read_bmp(a)
w2, h2, row2, pix_off2, db = read_bmp(b)
assert (w, h) == (w2, h2), f"size mismatch {w}x{h} vs {w2}x{h2}"

max_abs = 0
buf = bytearray(row * h)
for y in range(h):
    for x in range(w):
        ra, ga, ba = pixel(da, row, pix_off, w, h, x, y)
        rb, gb, bb = pixel(db, row2, pix_off2, w2, h2, x, y)
        dr = abs(ra - rb); dg = abs(ga - gb); dblue = abs(ba - bb)
        if dr > max_abs: max_abs = dr
        if dg > max_abs: max_abs = dg
        if dblue > max_abs: max_abs = dblue
        # amplify 4x, clamp
        ar = min(255, dr * 4); ag = min(255, dg * 4); ab = min(255, dblue * 4)
        off = (h - 1 - y) * row + x * 3
        buf[off] = ab; buf[off + 1] = ag; buf[off + 2] = ar

# write BMP header (clone the baseline header)
header = bytearray(da[:pix_off])
with open(out, "wb") as f:
    f.write(header)
    f.write(buf)

print(f"max abs pixel diff = {max_abs}")
print(f"wrote {out} ({w}x{h}, 4x amplified)")
