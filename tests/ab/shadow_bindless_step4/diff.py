#!/usr/bin/env python3
"""Pixel-parity diff for the SW-shadow bindless step-4b A/B.

Loads two captures (BMP or PNG), crops to the shared size, and reports per-channel max/mean
absolute difference. The migration is a pure descriptor-indirection swap (heap slot -> same
underlying buffer as the bounded array), so a correct 4b is EXACTLY zero difference; any nonzero
max flags a real divergence (a non-uniform-indexing miss reads a different mesh's geometry along a
wave-tile boundary -> a bright band). Writes an amplified diff PNG next to the inputs.

    python diff.py <before.(bmp|png)> <after.(bmp|png)> <diff-out.png>
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    if len(sys.argv) != 4:
        print("usage: diff.py <before> <after> <diff-out.png>", file=sys.stderr)
        return 2
    before = load_rgb(sys.argv[1])
    after = load_rgb(sys.argv[2])

    h = min(before.shape[0], after.shape[0])
    w = min(before.shape[1], after.shape[1])
    if before.shape != after.shape:
        print(f"WARN: shape mismatch before={before.shape} after={after.shape}; cropping to ({h},{w})")
    before = before[:h, :w]
    after = after[:h, :w]

    delta = np.abs(after - before)
    max_abs = int(delta.max())
    mean_abs = float(delta.mean())
    changed = int((delta.max(axis=2) > 0).sum())
    total = h * w
    print(f"resolution      : {w}x{h}")
    print(f"max abs diff    : {max_abs}   (per channel, 0-255)")
    print(f"mean abs diff   : {mean_abs:.6f}")
    print(f"changed pixels  : {changed}/{total} ({100.0*changed/total:.4f}%)")
    for c, name in enumerate("RGB"):
        print(f"  {name} max/mean : {int(delta[...,c].max())} / {delta[...,c].mean():.6f}")

    amp = np.clip(delta.astype(np.int32) * 8, 0, 255).astype(np.uint8)
    Image.fromarray(amp, "RGB").save(sys.argv[3])
    print(f"amplified(x8) diff -> {sys.argv[3]}")

    verdict = "PARITY (bit-identical)" if max_abs == 0 else f"DIVERGENCE (max={max_abs})"
    print(f"VERDICT: {verdict}")
    return 0 if max_abs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
