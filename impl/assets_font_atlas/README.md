# Font atlas assets

`FontAtlas` is the CPU asset for a complete static font face baked into independent scalar SDF pages. Each image group owns exact linear R8, RG8, RGB8, or RGBA8 bytes, with one distance page per stored channel. Alpha, when present, is the fourth distance plane; no channel is opacity. The format has one base mip, a one-texel exterior guard, and FreeType scalar SDF encoding v1 (zero at byte 128, positive inside).

The offline generator lives in `utilities/font_builder`. The package cooker imports ordinary `font` and `font_atlas` declarations, which can share one metadata document through `asset_bunch`. Runtime users link `nwb_assets_font_atlas` and include `asset.h`.

## Paired binary authoring

The generator emits exactly three same-stem files for each face: `name.nwb` holds separate readable `font face;` and `font_atlas atlas;` metadata declarations plus `asset_bunch bunch = [face, atlas];`, `name.font` is a prepared `FON1` binary containing the SFNT shaping data, and `name.atlas` is a binary `FTA1` atlas containing the glyph records, compact SDF image groups, and original positioning tables. The atlas declares its typed font relation as `atlas.font = face;`. The cooker derives sibling binary paths from the original metadata filename; there are no authored font or atlas filenames. Binary sidecars are not parsed as `.nwb` text.

Both declarations have their own visible metadata. The font records schema version, face index, units per em, glyph count, and source SHA-256. The atlas also records bake size, spread, guard, raster mode, vertical metrics, and each image group's width, height, channel count, and pixel SHA-256. All current fields are mandatory and checked against the binaries. The source hash identifies the decoded SFNT bytes rather than the prepared file's envelope.

Named declarations publish under the metadata identity with `/face` and `/atlas` appended. For example, the generated `engine/ui/fonts/default/latin` document publishes `engine/ui/fonts/default/latin/face` and `engine/ui/fonts/default/latin/atlas`. Ordinary standalone declarations in separate `.nwb` documents use each document's basename to find its binary sidecar and an explicit typed font reference for the atlas.

This path avoids parsing large base64 arrays and reconstructing the atlas during every cook. The original `.ttf` or `.otf` is only an input to `font_builder`, not a checked-in asset. The `.font` file retains recoverable SFNT tables because HarfBuzz and FreeType use them at runtime. The atlas sidecar has a local stem identity that the cooker checks and replaces with the declared typed font identity before publishing the runtime asset. The retired `font_bundle` declaration, hash-named payload sidecars, and embedded Zstd/base64 metadata are unsupported.

## Compact image groups

`FontAtlasGroup::width` and `height` are independent dimensions; neither must be square or a power of two. `channelCount` is 1..4, and tightly interleaved rows contain exactly `width * channelCount` bytes. The group payload has exactly `width * height * channelCount` bytes, with no row padding or mip chain.

The builder crops each group's guarded occupied bounds across every used channel and removes unused trailing channels. Glyph pixels, SDF spread, and one-texel exterior guards are retained without resampling. Glyph coordinates are rebased to the cropped image and its content hash is recomputed. Groups may have different dimensions and channel counts.

The UI uploader uses the corresponding linear UNORM GPU format. A three-channel group uses RGB8 when the device supports filtered sampling; otherwise it expands once to RGBA8 for upload. This preserves the three logical distance pages and their exact values. Cropping still reduces the texture extent on that device, while dropping the unused fourth channel only reduces GPU storage where RGB8 is supported. Actual GPU allocation sizes also depend on driver alignment and tiling.

## Identity and metrics

`FontAtlasPayload::font` is a typed `AssetRef<Font>` selected by the atlas declaration. The exact source SHA-256, face index, units per em, source glyph count, bake size, spread, raster mode, and vertical metrics are retained. The all-glyph policy requires one indexed record for every source glyph ID, including explicit nondrawable records for spaces or empty outlines. Drawable records include channel/group, bitmap rectangle, padded plane bounds in design units, and advance.

The UI font installation layer verifies this identity against the actual shaping font before binding an atlas. `ValidateFontAtlasSourceMatch` also requires the complete original positioning-table set and bytes to match that font, catching altered or omitted exports. A mismatched atlas must use the native coverage fallback for that selected font. HarfBuzz supplies glyph IDs and positioned advances/offsets; atlas advances and kerning metadata are never applied a second time.

## Positioning data

The atlas stores exact original `kern`, `GPOS`, and `GDEF` table bytes, sorted by numeric SFNT tag, with SHA-256 hashes of their content. This preserves pair/class maps, script/language/feature references, lookup order, and contextual/mark records without expanding all possible glyph pairs.

The admission validator fully checks OpenType `kern` format-0 pairs, GPOS roots, SinglePos, PairPos formats 1 and 2, coverage/class definitions, value records, device bounds, and positioning extensions. Other lookup families remain original opaque bytes with bounded root admission. This is a lossless table export contract and a bounded asset validator, not a general OpenType sanitizer or a normalized complete pair listing. The retained original font remains the authoritative shaping source.

## Bounds and validation

Shared limits in `model.h` apply to the generator, cooker, runtime binary, and GPU admission: 65,535 glyphs, eight image groups, 1..4 channels and at most 2048 by 2048 per group, 128 MiB total image bytes, and 32 MiB total positioning bytes. Bake size is 16..256 ppem; spread is 2..32 pixels.

Font and atlas metadata accept only their current schemas and reject unknown fields. Companion files are mandatory, bounded before allocation, and derived from the original metadata basename even inside an asset bunch. The cooker verifies the atlas marker, visible metadata, source hash, metrics, and original positioning table set against the paired font before publication.

Shared payload admission validates exact decoded content hashes, per-group byte counts, existing channel selection, padded plane extents against bitmap size, and guarded region overlap within the same image channel. Different channels can share the same coordinates. Invalid channel counts and oversized extents fail before pixel-size multiplication or allocation.

The runtime binary codec encodes scalar values explicitly in little endian, with a fixed 164-byte version 2 header, canonical sequential sections, bounded counts before allocation, zero reserved fields, and exact total payload consumption. Each 48-byte group header stores width, height, channel count, byte count, and pixel SHA-256 followed by its tightly interleaved pixels. Only the current version is accepted. Failed parsing, loading, or serialization preserves the prior published output.

Integration tests under `tests/integration/assets_font_atlas` cover paired publication, visible metadata admission, and missing, corrupt, or mismatched companions. `font_atlas_tests.cpp` covers runtime binary corruption, compact non-power-of-two groups with every channel count, exact exterior-guard bounds, glyph geometry, positioning bytes, source matching, and failed-load preservation. Production defaults are same-stem `.nwb`/`.font`/`.atlas` assets generated by `font_builder`.
