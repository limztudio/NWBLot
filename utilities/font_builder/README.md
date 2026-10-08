# Offline font builder

`font_builder` prepares one static SFNT face for the UI asset cooker. It takes an external `.ttf` or `.otf`, or an existing prepared `.font`, and writes two files with the same stem:

| File | Purpose |
| --- | --- |
| `<stem>.nwb` | Independent `font face` and `font_atlas atlas` declarations collected in an `asset_bunch`. Bake settings, editable vertical layout metrics and every glyph mapping remain readable; face facts and image layouts come from the prepared source. |
| `<stem>.font` | FON2 version 1 prepared source: exact original SFNT bytes and compact R8/RG8/RGB8/RGBA8 signed-distance images, with internal lengths and integrity hashes. |

The standard asset bunch publishes `<metadata virtual path>/face` and `<metadata virtual path>/atlas`, resolving `atlas.font = face` as a typed dependency. Both cookers derive the paired `.font` path from the original `.nwb` basename. The font cooker reads the exact SFNT without allocating image payloads; the atlas cooker admits the readable glyph mappings and settings against the source face and image directory, validates image hashes, and copies original positioning tables from the SFNT. Cooking reconstructs ordinary `Font` and `FontAtlas` runtime assets without rerasterizing. The input TTF/OTF can remain outside the repository, and `.font` remains usable for later atlas regeneration.

The `font face;` declaration has no fields. The atlas declares its Font reference, `bake_ppem`, `spread_pixels`, `raster_mode`, editable `ascender_units`/`descender_units`/`line_gap_units`, and `glyphs`. Face index, units per em, glyph count, group dimensions/channels, and the fixed one-texel guard are derived during cooking; `.nwb` rejects those copied facts, `groups`, version/revision fields, and hashes.

Every source glyph has one record in source order. Empty glyphs declare only required `advance_units`. Bitmap glyphs declare `group`, `channel`, `x`, `y`, positive `width` and `height`, `plane_left`, `plane_top`, and `advance_units`. Cooking derives glyph ID from the list ordinal, drawable state from bitmap-field presence, and right/bottom plane bounds from the origin, bitmap size, units per em, and bake ppem. Metadata rejects authored `id`, `drawable`, `plane_right`, `plane_bottom`, and fixed-zero geometry on empty glyphs. Float values use nine significant decimal digits so their f32 bits survive cooking.

FON2 stores a 56-byte header, 48-byte image directory entries, the exact SFNT, then tightly interleaved compact image bytes. Internal source/pixel hashes stay in that binary. Previous prepared-source envelopes must be regenerated from the original font.

## Build and use

The directory launcher is discovered as `font-builder` and builds the `nwb_font_builder` target. Arguments after `--` go to the utility.

```text
python -m launcher font-builder --build-only --config opt
python -m launcher font-builder --config opt -- --font "/absolute/path/source.ttf" --output "/absolute/path/assets/latin.nwb" --ppem 32 --extent 1024
python -m launcher font-builder --skip-build --config opt -- --font "/absolute/path/assets/latin.font" --output "/absolute/path/another/latin.nwb" --ppem 32 --extent 1024
```

The first command prepares the utility without baking a font. The launcher configures
and builds as needed; see [the launcher guide](../../launcher/README.md) for build options.

Use `--overwrite` to replace an existing complete pair. A partial pair, occupied `.tmp` or `.old` work path, or invalid source fails without publishing. The builder stages, flushes, and byte-verifies both files before replacement; existing files are moved to backups while the new files are published, and restored if publication fails. It publishes `.font` first and the `.nwb` declaration last.

Defaults are 64 pixels per em, spread 8, a maximum square packing extent of 1024 pixels and up to 8 four-plane groups. `--ppem` accepts 16..256, `--spread` 2..32, `--extent` 32..2048, and `--max-groups` 1..8. Non-power-of-two packing extents are supported. After placement, each group is automatically cropped to its glyph bounds including the one-texel guard, and unused trailing channels are removed. Final groups can be rectangular and have one through four channels. Total staging capacity is at most 128 MiB. Capacity failure never drops glyphs or changes the requested bake size.

`--renderer bitmap` is the default. FreeType renders an unhinted grayscale bitmap, then its pinned bitmap-SDF renderer produces the field. `--renderer outline` requests direct outline SDF and fails if the face has an unsupported outline. Both FreeType 2.14.3 SDF renderers come from the vendored revision recorded in `3rd_parties/freetype/nwb_update.txt`.

## Compact images and rendering

All source glyph IDs, including nondrawable whitespace and shaped ligatures, have atlas records. Packing uses four independent signed-distance planes in R, G, B, A; alpha is a fourth distance field. Every final group stores the channels through its last occupied plane as lossless linear R8, RG8, RGB8 or RGBA8 bytes with no color conversion or mip generation. The crop preserves the original SDF values and every guard texel. Packing is deterministic: descending glyph height and width, then ascending ID, on guarded shelves in R/G/B/A order, followed by exact rectangular/channel compaction.

The internal FreeType unsigned-byte SDF encoding uses byte 128 for zero distance and positive distance inside the glyph. The shader decodes `(sample * 255 - 128) * spread / 128` and uses derivatives for antialiasing. Authors choose `raster_mode = "bitmap"` or `"outline"`; the encoding is fixed by the builder and cooker. Current UI SDF rendering is qualified only between 0.75 and 1.5 times the atlas bake ppem. Outside that range the renderer uses native grayscale coverage from the same shaping face; downscaling a baked SDF can lose the bottom antialiasing row of small descenders.

The cooked atlas retains the original `kern`, `GPOS`, and `GDEF` table bytes with lengths and internal hashes. This preserves OpenType `kern` pairs, GPOS class matrices, script and language selection, devices, and lookup order without expanding into a quadratic pair list. Runtime HarfBuzz shaping uses the same SFNT; copied table bytes must not be applied to shaped positions again. Runtime codecs remain FON1 version 1 and FTA1 version 2, separate from the FON2 source envelope.

Generated output excludes timestamps and host/source paths. Repeating a bake on one platform with identical settings and source bytes produces byte-identical pairs. Cross-platform byte equality still requires execution on both platforms. CLI integration checks generation, metadata cooking, exact glyph float bits, compact image bytes and guards, positioning tables, corruption rejection, deterministic repetition, partial-pair/work-path refusal, overwrite preservation and relocation of both files.

## Library result contracts

`Bake` returns `Expected<Impl::FontAtlasPayload>`, `Rasterize` returns its glyph collection, and `BuildPreparedFont` returns `Expected<AssetBytes, AStringView>` with borrowed static diagnostics. Prepared-font readers/serializers return their arena-owned candidates directly. Check success before moving results or publishing outputs; FreeType lifecycle, atlas packing and positioning retain mutation of the existing owner/payload. See [produced values and expected failures](../../docs/expected_results.md).
