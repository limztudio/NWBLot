# Offline font maker

`font_builder` readies one still SFNT face for the UI asset cooker. It takes
an outside `.ttf` or `.otf`, or a ready `.font`, and writes two files with
the same stem:

| File | Use |
| --- | --- |
| `<stem>.nwb` | Free-standing `font face` and `font_atlas atlas` notes kept in an `asset_bunch`. Bake settings, editable up/down/line-gap numbers, and each glyph map stay clear text. Face facts and image plans come from the ready source. |
| `<stem>.font` | FON2 version 1 ready source: exact first SFNT bytes and small R8/RG8/RGB8/RGBA8 signed-gap images, with in-file lengths and check hashes. |

The plain asset bunch puts out `<data path>/face` and `<data path>/atlas`.
It links `atlas.font = face` as a typed need. Both cookers find the paired
`.font` path from the first `.nwb` base name. The font cooker reads the exact
SFNT with no image bytes made. The atlas cooker takes the clear glyph maps
and settings with the source face and image folder. It checks image hashes
and copies first place tables from the SFNT. Cooking remakes plain `Font` and
`FontAtlas` run assets with no re-draw. The input TTF/OTF can stay outside the
repo. `.font` still works for later atlas remakes.

The `font face;` note has no fields. The atlas notes its Font link,
`bake_ppem`, `spread_pixels`, `raster_mode`, editable
`ascender_units`/`descender_units`/`line_gap_units`, and `glyphs`. Face index,
units per em, glyph count, group size/lanes, and the fixed one-texel edge come
from cooking. `.nwb` refuses those copied facts, `groups`, version fields,
and hashes.

Each source glyph has one row in source order. Blank glyphs note only needed
`advance_units`. Drawn glyphs note `group`, `channel`, `x`, `y`, good `width`
and `height`, `plane_left`, `plane_top`, and `advance_units`. Cooking finds
glyph ID from the list spot, draw state from drawn-field use, and right/low
plane edges from start, drawn size, units per em, and bake ppem. Data refuses
made `id`, `drawable`, `plane_right`, `plane_bottom`, and fixed-zero shapes
on blank glyphs. Floats use nine key decimal digits, so their f32 bits live
through cooking.

FON2 holds a 56-byte head, 48-byte image folder rows, the exact SFNT, then
tight small image bytes. In-file source/pixel hashes stay in that binary. Old
ready-source packs must be remade from the first font.

## Build and use

The folder launcher shows as `font-builder` and builds the `nwb_font_builder`
target. Flags after `--` go to the tool.

```text
python -m launcher font-builder --build-only --config opt
python -m launcher font-builder --config opt -- --font "/absolute/path/source.ttf" --output "/absolute/path/assets/latin.nwb" --ppem 32 --extent 1024
python -m launcher font-builder --skip-build --config opt -- --font "/absolute/path/assets/latin.font" --output "/absolute/path/another/latin.nwb" --ppem 32 --extent 1024
```

The first form readies the tool with no font bake. The launcher sets up and
builds as needed. See [the launcher guide](../../launcher/README.md) for
build flags.

Use `--overwrite` to swap a full old pair. A half pair, busy `.tmp` or `.old`
work path, or bad source fails before publish. The maker stages, flushes, and
byte-checks both files before swap. It backs up old files, puts out `.font`
first and the `.nwb` note last, and tries to bring back the copies if publish
fails. A failed bring-back is reported and may leave copy files for rescue.

Defaults are 64 pixels per em, spread 8, a max square pack size of 1024
pixels, and up to 8 four-plane groups. `--ppem` takes 16..256, `--spread`
2..32, `--extent` 32..2048, and `--max-groups` 1..8. Odd pack sizes work.
After place, each group is auto cut to its glyph bounds with the one-texel
edge, and idle tail lanes drop. End groups can be wide or tall with one to
four lanes. Full stage size is at most 128 MiB. Size fail never drops glyphs
or changes the asked bake size.

`--renderer bitmap` is the default. FreeType draws a plain grey map. Then its
fixed map-SDF draw makes the field. `--renderer outline` asks for direct edge
SDF and fails if the face has a bad edge. Both FreeType 2.14.3 SDF draws come
from the kept copy noted in `3rd_parties/freetype/nwb_update.txt`.

## Small images and draw

All source glyph IDs, with blank space and shaped joins, have atlas rows.
Pack uses four free signed-gap planes in R, G, B, A. Alpha is a fourth gap
field. Each end group holds its lanes through its last used plane as exact
linear R8, RG8, RGB8, or RGBA8 bytes with no color change or mip make. The
cut keeps the first SDF values and each edge texel. Pack is fixed: tall then
wide glyphs first, then low ID first, on edged shelves in R/G/B/A order. Then
it does exact box/lane shrink.

The in-tool FreeType byte SDF code uses byte 128 for zero gap and good gap
inside the glyph. The shader reads
`(sample * 255 - 128) * spread / 128` and uses slopes for smooth edge. Authors
pick `raster_mode = "bitmap"` or `"outline"`. The code form is fixed by the
maker and cooker. Current UI SDF draw is proven only from 0.75 to 1.5 times
the atlas bake ppem. Past that span the renderer uses plain grey cover from
the same shape face. Small-scale baked SDF may lose the low smooth row of
small tails.

The cooked atlas keeps the first `kern`, `GPOS`, and `GDEF` table bytes with
lengths and in-file hashes. This keeps OpenType `kern` pairs, GPOS kind
grids, script and tongue pick, devices, and look-up order with no wide pair
list growth. Run-time HarfBuzz shape uses the same SFNT. Copied table bytes
must not hit shaped spots twice. Run codecs stay FON1 version 1 and FTA1
version 3, apart from the FON2 source pack.

Made outs hold no time marks and no host/source paths. A repeat bake on one
box with same settings and source bytes gives byte-same pairs. Cross-box byte
match still needs a run on both boxes. CLI checks cover make, data cook,
exact glyph float bits, small image bytes and edges, place tables, bad-data
refuse, fixed repeat, half-pair/work-path refuse, swap keep, and move of both
files.

## Code result rules

`Bake` gives `Expected<Impl::FontAtlasPayload>`. `Rasterize` gives its glyph
set. `BuildPreparedFont` gives `Expected<AssetBytes>`. Check good before use.
See [made values and set faults](../../docs/expected_results.md).
