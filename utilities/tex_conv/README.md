# tex_conv

`tex_conv` converts LDR and HDR images into a pair of NWB texture files. Its directory
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
linear data; HDR sources are always linear. `--force` is required to replace either existing output file.

Use `--alpha` to choose an alpha source independently of the RGB image:

    python launcher.py tex-conv -- rgb.png --alpha opacity.png
    python launcher.py tex-conv -- rgb.exr --alpha black
    python launcher.py tex-conv -- rgb.hdr --alpha white

`--alpha path` takes the mask image's red channel, normalized to `[0, 1]`.
The mask must match the source width and height; for cubemaps and volumes the
same mask is applied to every face or slice. `--alpha white` and `--alpha black`
select constant 1.0 and 0.0 alpha. Without `--alpha`, the converter keeps the
input's alpha if it has one, otherwise it uses opaque alpha.

All texture modes accept PNG, JPEG/JFIF, TGA, and QOI LDR images plus OpenEXR
(`.exr`) and Radiance (`.hdr`) HDR images. A single conversion must use only
LDR or only HDR source images. The input decoder is the vendored Basis Universal
encoder; unsupported formats fail rather than silently producing a different
texture type.

## File contract

The `.tex` file has no container header. Its payload is selected by the metadata
format. LDR input is encoded as a contiguous sequence of standard UASTC LDR
4x4 blocks. Every block is 16 bytes and follows the pinned UASTC texture
specification's LSB-first bit layout (revision
`b624c07ad3c659e7b0f0badcb36e9a6b8820a99d`). Edge blocks are the normal
clamped 4x4 UASTC blocks produced by the encoder.

HDR input is encoded as a contiguous sequence of linear UASTC HDR 4x4 RGB
blocks. UASTC HDR 4x4 is directly valid ASTC HDR 4x4 data; it also transcodes
to BC6H or `RGBA16_FLOAT` when needed. RGB values must be finite and in
`[0, 65216]`, preserving radiance above 1.0. Because this Basis HDR path is
RGB-only, variable alpha is stored as a second, trailing same-layout UASTC LDR
mask stream. Each mask texel is encoded as `(a, a, a, 255)` and decoded from
its red channel. Opaque HDR has no mask stream; non-white constant alpha is
stored in metadata without a mask stream.

LDR texture metadata uses version 1 and `uastc_ldr_4x4`; HDR metadata uses
version 2 and `uastc_hdr_4x4`. Both use the `mip_major_slice_major_blocks`
order: each mip's planes are contiguous before the next mip. A 2D mip has one
plane, cube planes retain `+X, -X, +Y, -Y, +Z, -Z` order, and volume planes
retain ascending Z order. Volume mips reduce all three dimensions, including
depth, until they reach 1×1×1.

The sibling .nwb file is readable metascript metadata. It records:

- the payload format, color space, base resolution, dimension, and depth;
- the format's 4x4 / 16-byte UASTC block layout;
- the .tex basename and mip-major payload layout; and
- for each mip, its dimensions, block grid, byte offset, byte size, and plane
  count.

HDR metadata additionally records `alpha_mode`: `opaque`, `constant_unorm8`,
or `uastc_ldr_4x4`. The latter includes the trailing alpha stream's byte offset,
byte count, and pinned LDR UASTC revision.

For example, a 7x5 source has three mips and its payload records 64 bytes at
offset 0 for 7x5, then 16 bytes each for 3x2 and 1x1. The metadata's
offset_bytes and size_bytes are authoritative when reading the raw payload.
