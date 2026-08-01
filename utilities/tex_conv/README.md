# tex_conv

tex_conv converts one LDR image into a pair of NWB texture files:

    tex_conv foobar.png

This writes foobar.nwb and foobar.tex beside the input. --output path/name selects
a different base name, --linear marks and filters the image as linear data, and
--force is required to replace either existing output file.

Version 1 accepts PNG, JPEG/JFIF, TGA, and QOI images. Its input decoder is the
vendored Basis Universal encoder; unsupported formats fail rather than silently
producing a different texture type.

## File contract

The .tex file has no container header. It is a contiguous sequence of standard
UASTC LDR 4x4 blocks, ordered by mip level from level 0 to the 1x1 level. Every
block is 16 bytes and follows the pinned UASTC texture specification's LSB-first
bit layout (revision b624c07ad3c659e7b0f0badcb36e9a6b8820a99d). Edge blocks are
the normal clamped 4x4 UASTC blocks produced by the encoder.

The sibling .nwb file is readable metascript metadata. It records:

- the uastc_ldr_4x4 format, color space, and base resolution;
- the 4x4 / 16-byte block layout;
- the .tex basename and mip-major payload layout; and
- for each mip, its dimensions, block grid, byte offset, and byte size.

For example, a 7x5 source has three mips and its payload records 64 bytes at
offset 0 for 7x5, then 16 bytes each for 3x2 and 1x1. The metadata's
offset_bytes and size_bytes are authoritative when reading the raw payload.
