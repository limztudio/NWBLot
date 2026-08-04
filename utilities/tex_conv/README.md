# tex_conv

`tex_conv` converts LDR images into a pair of NWB texture files. Its directory
launcher is discovered automatically by the repository root, so it can be
built and run with:

    python launcher.py tex-conv -- --help
    python launcher.py tex-conv -- assets/textures/foobar.png

A single positional image produces a 2D texture. Cubemaps and volume textures
use explicit ordered image lists:

    python launcher.py tex-conv -- --cube posx.png negx.png posy.png negy.png posz.png negz.png --output sky
    python launcher.py tex-conv -- --volume z0.png z1.png z2.png z3.png --output fog

`--cube` always takes exactly six square faces in `+X, -X, +Y, -Y, +Z, -Z`
order. `--volume` takes one or more same-sized slices in ascending Z order. This
writes `foobar.nwb` and `foobar.tex` beside the first input unless `--output`
selects a different base name. `--linear` marks and filters the image data as
linear data, and `--force` is required to replace either existing output file.

All texture modes accept PNG, JPEG/JFIF, TGA, and QOI images. The input decoder
is the vendored Basis Universal encoder; unsupported formats fail rather than
silently producing a different texture type.

## File contract

The .tex file has no container header. It is a contiguous sequence of standard
UASTC LDR 4x4 blocks. Every block is 16 bytes and follows the pinned UASTC
texture specification's LSB-first bit layout (revision
b624c07ad3c659e7b0f0badcb36e9a6b8820a99d). Edge blocks are the normal clamped
4x4 UASTC blocks produced by the encoder.

All texture assets use version 1 and the
`mip_major_slice_major_blocks` order: each mip's planes are contiguous before
the next mip. A 2D mip has one plane, cube planes retain `+X, -X, +Y, -Y, +Z,
-Z` order, and volume planes retain ascending Z order. Volume mips reduce all
three dimensions, including depth, until they reach 1×1×1.

The sibling .nwb file is readable metascript metadata. It records:

- the uastc_ldr_4x4 format, color space, base resolution, dimension, and depth;
- the 4x4 / 16-byte block layout;
- the .tex basename and mip-major payload layout; and
- for each mip, its dimensions, block grid, byte offset, byte size, and plane
  count.

For example, a 7x5 source has three mips and its payload records 64 bytes at
offset 0 for 7x5, then 16 bytes each for 3x2 and 1x1. The metadata's
offset_bytes and size_bytes are authoritative when reading the raw payload.
