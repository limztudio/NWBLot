# tex_conv

`tex_conv` converts LDR and HDR images into a pair of NWB texture files. Its directory
launcher is discovered automatically by the repository root, so it can be
built and run with:

    python -m launcher tex-conv --build-only --config dbg
    python -m launcher tex-conv --working-directory . -- --help
    python -m launcher tex-conv --working-directory . -- assets/textures/foobar.png

The build-only command prepares the converter without processing an image. The launcher
configures and builds as needed; see [the launcher guide](../../launcher/README.md) for build options.

A single positional image produces a 2D texture. Cubemaps and volume textures
use explicit ordered image lists:

    python -m launcher tex-conv --working-directory . -- --cube posx.png negx.png posy.png negy.png posz.png negz.png --output sky
    python -m launcher tex-conv --working-directory . -- --volume z0.png z1.png z2.png z3.png --output fog

`--cube` always takes exactly six square faces in `+X, -X, +Y, -Y, +Z, -Z`
order. `--volume` takes one or more same-sized slices in ascending Z order. This
writes `foobar.nwb` and `foobar.tex` beside the first input unless `--output`
selects a different base name. `--linear` marks and filters the image data as
linear data; HDR sources are always linear. `--force` is required to replace either existing output file.

Publication stages both files before moving existing regular outputs to sibling
`.old` backups. It removes the backups after both replacements succeed and
restores the previous outputs on failure. Existing `.tmp` or `.old` work paths
cause refusal. If the filesystem also refuses rollback, the converter reports the
failure and retains the previous `.old` files for recovery.
The transaction handles one conversion's reported failures and exception unwind;
it does not atomically swap the pair against process crashes or concurrent writers.

Use `--alpha` to choose an alpha source independently of the RGB image:

    python -m launcher tex-conv --working-directory . -- rgb.png --alpha opacity.png
    python -m launcher tex-conv --working-directory . -- rgb.exr --alpha black
    python -m launcher tex-conv --working-directory . -- rgb.hdr --alpha white

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

The converter selects `asset.format = "uastc_ldr_4x4";` for LDR and
`asset.format = "uastc_hdr_4x4";` for HDR. The sibling `.nwb` is readable
metascript metadata containing only author-controlled choices:

- `format`, `dimension` (`2d`, `cube`, or `volume`), positive `width` and
  `height`, and `data`, the same-directory `.tex` filename without path components;
- positive `depth` for volumes only; 2D and cube textures infer depth 1;
- for LDR, `color_space` (`linear` or `srgb`) and numeric `has_alpha` (0 or 1);
- for HDR, `alpha_mode` (`opaque`, `constant_unorm8`, or `uastc_ldr_4x4`),
  with `alpha_constant_unorm8` required only for `constant_unorm8` (integer
  0..254; use `opaque` for 255).

HDR is always linear, and its alpha presence follows `alpha_mode`. The cooker
derives those values, fixed block dimensions and byte size, the complete mip chain,
block grids, slice counts, byte offsets and sizes, and any trailing HDR alpha range.
Do not author block/layout/address fields, `mip_count`, `mips`, HDR `color_space`
or `has_alpha`, or alpha payload offsets/counts. Metadata also has no version or
specification-revision fields. Unknown fields fail cooking.

The raw sidecar uses a fixed mip-major, then slice-major byte order: each mip's
planes are contiguous before the next mip. A 2D mip has one plane, cube planes
retain `+X, -X, +Y, -Y, +Z, -Z` order, and volume planes retain ascending Z order.
Volume mips reduce all three dimensions, including depth, until they reach 1x1x1.
A separate HDR alpha stream starts immediately after RGB and mirrors the whole
RGB mip/slice layout. None of this byte layout is configurable in `.nwb`.

For example, a 7x5 2D source has a derived three-level chain: 7x5 uses 64 bytes
at offset 0, 3x2 uses 16 bytes at offset 64, and 1x1 uses 16 bytes at offset 80.
The primary stream therefore contains exactly 96 bytes. A variable-alpha HDR
source appends another 96-byte stream. The cooker computes these ranges from
the semantic fields and rejects a sidecar whose total byte count differs.

## Library result contracts

`ResolveOutputPaths` returns `Expected<OutputPaths>`, and `EncodeTexture` returns `Expected<TexturePayload>`. Decoders and mip producers return their owned candidates through the same contract. Check success before moving payloads or starting paired publication; append/alpha helpers retain existing payload/image storage and capacity. Transaction ownership flags remain live cleanup state, including a staged path created before a later write fails. See [produced values and expected failures](../../docs/expected_results.md).
