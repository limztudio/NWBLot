# tex_conv

`tex_conv` converts LDR and HDR images into prepared linear UASTC texture assets.
It owns source color conversion, float mip filtering, alpha transport selection,
and UASTC encoding. The asset cooker validates the descriptive metadata and
packages the same-stem raw block payload.

The directory launcher is discovered automatically by the repository root:

    python -m launcher tex-conv --build-only --config dbg
    python -m launcher tex-conv --working-directory . -- --help
    python -m launcher tex-conv --working-directory . -- assets/textures/foobar.png

The build-only command prepares the converter without processing an image.
The launcher configures and builds as needed; see [the launcher guide](../../launcher/README.md).

A single positional image produces a 2D texture. Cubemaps and volumes use
explicit ordered image lists:

    python -m launcher tex-conv --working-directory . -- --cube posx.png negx.png posy.png negy.png posz.png negz.png --output sky
    python -m launcher tex-conv --working-directory . -- --volume z0.png z1.png z2.png z3.png --output fog

`--cube` takes six square faces in `+X, -X, +Y, -Y, +Z, -Z` order.
`--volume` takes one or more same-sized slices in ascending Z order.
The converter writes `foobar.nwb` and `foobar.tex` beside the first input unless
`--output` selects a different base name. `--force` is required to replace either
existing output file.

## Source interpretation and filtering

Ordinary LDR input is interpreted as sRGB. `--linear` declares LDR input to be
linear data instead. HDR input is always linear. The image decoder expands input
to float RGBA; the converter applies the sRGB transfer function to RGB before
filtering or encoding. Alpha always represents linear coverage.

Accepted inputs are PNG, JPEG/JFIF, TGA, QOI, OpenEXR (`.exr`), and Radiance (`.hdr`).
One texture cannot mix LDR and HDR RGB inputs. LDR RGB must be finite and in
`[0, 1]`; HDR RGB must be finite and in `[0, 65216]`. Original alpha must be
finite and in `[0, 1]`. Unsupported images and invalid values fail before
publication.

The converter generates a complete mip chain in linear float space. Cube faces
filter independently. Volume mips reduce all three dimensions, weighting source
Z slabs by their overlap with each destination interval so odd depths preserve
coverage. Alpha filtering uses straight linear coverage. LDR quantization to
UNORM8 happens after color conversion and mip filtering. HDR encoding retains
floating-point RGB above 1.0. Basis encoding uses one worker for reproducible
output.

LDR UASTC is an 8-bit normalized target, so linearized dark colors have less
precision than an sRGB-encoded 8-bit representation. UI artwork follows the
same conversion and storage rules as other color textures.

## Alpha sources

Use `--alpha` to choose alpha independently of RGB:

    python -m launcher tex-conv --working-directory . -- rgb.png --alpha opacity.png
    python -m launcher tex-conv --working-directory . -- rgb.exr --alpha black
    python -m launcher tex-conv --working-directory . -- rgb.hdr --alpha white

`--alpha path` takes the mask image's red channel, clamped to `[0, 1]`.
The mask must match the source width and height; the same mask applies to every
cube face or volume slice. `--alpha white` and `--alpha black` select constant
1.0 and 0.0. Without `--alpha`, source alpha is retained or defaults to opaque
when the image has no alpha channel.

Alpha transport is derived from the quantized mip chain. LDR UASTC stores
opaque or embedded alpha. HDR UASTC is RGB-only, so opaque alpha needs no extra
stream, constant alpha below 255 is stored in metadata, and variable alpha
uses a trailing UASTC LDR stream. Every alpha mask texel stores `(a, a, a, 255)`
and is decoded from red. The alpha stream mirrors the primary mip and slice
layout.

## Prepared file contract

Each texture is an implicit same-basename pair: `foobar.nwb` describes the
texture, and `foobar.tex` holds its raw linear UASTC blocks. The first UASTC block
starts at byte zero. The payload contains no container header, mip table, or
footer. Shared block dimensions and checked mip layout arithmetic live in
[global/texture_payload.h](../../global/texture_payload.h).

Every UASTC block covers 4x4 texels and occupies 16 bytes. Payload order is
mip-major, then slice-major; cube faces retain the input face order and volume
slices retain ascending Z order. A variable HDR alpha stream immediately follows
the complete primary stream and mirrors its mip/slice layout.

For example, a 7x5 LDR 2D texture with alpha has this metadata:

```cpp
texture asset;

asset.format = "uastc_ldr_4x4";
asset.dimension = "2d";
asset.resolution = [7, 5];
asset.alpha_mode = "embedded_ldr";
```

The current metadata fields are:

- `format`: `uastc_ldr_4x4` or `uastc_hdr_4x4`;
- `dimension`: `2d`, `cube`, or `volume`;
- `resolution`: `[width, height]` for 2D/cube or
  `[width, height, depth]` for a volume;
- `alpha_mode`: `opaque`, `embedded_ldr`, `constant_unorm8`, or
  `separate_uastc_ldr_4x4`;
- `alpha_constant_unorm8`: an integer in `[0, 254]`, present only for
  `constant_unorm8`.

LDR allows opaque or embedded alpha. HDR allows opaque, constant, or separate
alpha. All dimensions are positive, cube faces are square, and all prepared RGB
values are linear. Metadata contains no source color-space flag, data filename,
separate width/height/depth fields, version, mip count or table, or byte offsets.
The cooker derives the complete mip chain length from the resolution. Unknown
fields are rejected.

The converter's mip generation filter is a linear-space box filter. Runtime
nearest/linear interpolation belongs to the separate Sampler asset's
`mip_filter` field; textures do not carry an ignored interpolation setting.

Cooking derives the complete mip chain through 1x1, and depth 1 for volumes,
from the semantic dimensions. It derives block grids, slice counts, and byte
ranges and checks the complete raw byte count. It packages the blocks
without source decoding, color conversion, filtering, or re-encoding. The cooked
runtime asset carries its internal header and mip records; the authored `.tex`
file remains the raw block stream. Runtime sampling always uses linear formats.

## Publication and result contracts

Publication stages both files before moving existing regular outputs to sibling
`.old` backups. It removes the backups after both replacements succeed and
restores previous outputs on failure. Existing `.tmp` or `.old` work paths
cause refusal. If the filesystem also refuses rollback, the converter reports
the failure and retains previous `.old` files for recovery.
The transaction handles one conversion's reported failures and exception unwind;
it does not atomically swap the pair against process crashes or concurrent writers.

`ResolveOutputPaths` returns `Expected<OutputPaths>`, and `PrepareTexture` returns
`Expected<PreparedTexture>`. Check success before moving a result or beginning
paired publication. Transaction ownership flags remain live cleanup state,
including a staged path created before a later write fails.
See [produced values and expected failures](../../docs/expected_results.md).

The converter integration tests exercise LDR/HDR encoding, linear color
conversion, complete cube and volume mip chains, alpha overrides, prepared
binary admission, and failed-publication recovery.
