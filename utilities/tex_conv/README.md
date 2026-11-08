# tex_conv

`tex_conv` turns LDR and HDR images into ready linear UASTC texture files. It
does source color change, float mip shrink, alpha choice, and UASTC pack. The
asset cooker checks the text data and packs the raw block bytes.

The folder launcher is found on its own by the repo root:

 python -m launcher tex-conv --build-only --config dbg
 python -m launcher tex-conv --working-directory . -- --help
 python -m launcher tex-conv --working-directory . -- assets/textures/foobar.png

The build-only form readies the tool with no image work. The launcher sets up
and builds as needed. See [the launcher guide](../../launcher/README.md).

One image in gives a 2D texture. Cube maps and volumes use clear face lists:

 python -m launcher tex-conv --working-directory . -- --cube posx.png negx.png posy.png negy.png posz.png negz.png --output sky
 python -m launcher tex-conv --working-directory . -- --volume z0.png z1.png z2.png z3.png --output fog

`--cube` takes six square faces in `+X, -X, +Y, -Y, +Z, -Z` order. `--volume`
takes one or more same-size slices in low-to-high Z order. The tool writes
`foobar.nwb` and `foobar.tex` next to the first input unless `--output`
picks another base name. `--force` is needed to swap an old out file.

## Source read and shrink

Plain LDR input reads as sRGB. `--linear` marks LDR input as linear data.
HDR input is always linear. The image reader grows input to float RGBA. The
tool applies the sRGB curve to RGB before shrink or pack. Alpha always means
linear cover.

Good inputs are PNG, JPEG/JFIF, TGA, QOI, OpenEXR (`.exr`), and Radiance
(`.hdr`). One texture cannot mix LDR and HDR RGB inputs. LDR RGB must be real
and in `[0, 1]`. HDR RGB must be real and in `[0, 65216]`. Source alpha must
be real and in `[0, 1]`. Bad images and bad values fail before publish.

The tool makes a full mip chain in linear float space. Cube faces shrink on
their own. Volume mips shrink all three sides. Source Z slabs weigh by cover
with each out range, so odd depths keep cover. Alpha shrink uses plain linear
cover. LDR pack to UNORM8 runs after color change and mip shrink. HDR pack
keeps float RGB above 1.0. Basis pack uses one worker for same outs each run.

LDR UASTC is an 8-bit plain target. So dark linear colors hold less fine steps
than an sRGB-coded 8-bit form. UI art uses the same change and store rules as
other color textures.

## Alpha sources

Use `--alpha` to pick alpha apart from RGB:

 python -m launcher tex-conv --working-directory . -- rgb.png --alpha opacity.png
 python -m launcher tex-conv --working-directory . -- rgb.exr --alpha black
 python -m launcher tex-conv --working-directory . -- rgb.hdr --alpha white

`--alpha path` takes the mask red lane, cut to `[0, 1]`. The mask must match
the source width and height. The same mask fits each cube face or volume
slice. `--alpha white` and `--alpha black` pick fixed 1.0 and 0.0. With no
`--alpha`, source alpha stays, or opaque when the image has no alpha lane.

Alpha move form comes from the packed mip chain. LDR UASTC holds solid or
built-in alpha. HDR UASTC is RGB-only. So solid alpha needs no extra stream,
fixed alpha below 255 lives in data, and mixed alpha uses a tail UASTC LDR
stream. Each alpha mask texel holds `(a, a, a, 255)` and reads back from red.
The alpha stream mirrors the main mip and slice plan.

## Ready file rules

Each texture is a same-name pair: `foobar.nwb` states the texture, and
`foobar.tex` holds its raw linear UASTC blocks. The first UASTC block starts
at byte zero. The bytes hold no box head, mip table, or tail. Shared block
size and checked mip math live in
[global/texture_payload.h](../../global/texture_payload.h).

Each UASTC block covers 4x4 texels in 16 bytes. Byte order is mip-first, then
slice-first. Cube faces keep the input face order, and volume slices keep
low-to-high Z order. A mixed HDR alpha stream runs right after the full main
stream and mirrors its mip/slice plan.

For example, a 7x5 LDR 2D texture with alpha has this data:

```cpp
texture asset;

asset.format = "uastc_ldr_4x4";
asset.dimension = "2d";
asset.resolution = [7, 5];
asset.alpha_mode = "embedded_ldr";
```

The current data fields are:

- `format`: `uastc_ldr_4x4` or `uastc_hdr_4x4`;
- `dimension`: `2d`, `cube`, or `volume`;
- `resolution`: `[width, height]` for 2D/cube or `[width, height, depth]`
  for a volume;
- `alpha_mode`: `opaque`, `embedded_ldr`, `constant_unorm8`, or
  `separate_uastc_ldr_4x4`;
- `alpha_constant_unorm8`: a whole number in `[0, 254]`, only for
  `constant_unorm8`.

LDR allows solid or built-in alpha. HDR allows solid, fixed, or split alpha.
All sizes are above zero, cube faces are square, and all ready RGB values are
linear. Data holds no source color flag, data file name, split width/height
fields, version, mip count or table, or byte spots. The cooker finds the full
mip chain length from the size. Unknown fields fail.

The tool shrink filter is a linear-space box filter. Run-time near/linear
pick lives in the separate Sampler `mip_filter` field. Textures hold no idle
pick setting.

Cooking grows the full mip chain down to 1x1, and depth 1 for volumes, from
the stated sizes. It finds block grids, slice counts, and byte spans and
checks the full raw byte count. It packs the blocks with no source read,
color change, shrink, or re-pack. The cooked run asset holds its own head and
mip notes. The made `.tex` file stays the raw block stream. Run-time use
always reads linear forms.

## Publish and result rules

Publish stages both files before it moves old plain outs to next-door `.old`
copies. It drops the copies after both swaps win and brings back old outs on
fail. Old `.tmp` or `.old` work paths cause refuse. If the disk also refuses
roll-back, the tool reports the fail and keeps old `.old` files for rescue.
The deal covers one change run faults and unwind. It does not safely swap the
pair through kill or other writers.

`ResolveOutputPaths` gives `Expected<OutputPaths>`, and `PrepareTexture`
gives `Expected<PreparedTexture>`. Check good before you move a result or
start paired publish. Deal owner flags stay live clean-up state. This holds a
staged path made before a later write fails. See [made values and set
faults](../../docs/expected_results.md).

The tool checks cover LDR/HDR pack, linear color change, full cube and volume
mip chains, alpha swaps, ready binary take-in, and failed-publish rescue.
