# Font atlas assets

`FontAtlas` is the CPU asset for a complete static font face baked into four independent scalar SDF pages per RGBA texture. It owns exact linear RGBA8 bytes. Alpha is the fourth distance plane; no channel is opacity. The format has one base mip, a one-texel exterior guard, and FreeType scalar SDF encoding v1 (zero at byte 128, positive inside).

The offline generator lives in `utilities/font_builder`. The package cooker discovers `font_bundle asset;` documents and their paired sidecars through `nwb_assets_font_atlas_volume_entry`. Runtime users link `nwb_assets_font_atlas` and include `asset.h`.

## Paired binary authoring

The generator emits exactly three same-stem files for each face: `name.nwb` is the small readable `font_bundle` metadata, `name.font` is a prepared `FON1` binary containing the SFNT shaping data, and `name.atlas` is a binary `FTA1` atlas containing the glyph records, linear RGBA SDF groups, and original positioning tables. The authoring `.nwb` names neither the font nor the atlas; the bundle cooker derives both sibling paths from its filename, validates the binaries and their source match, and publishes `Font` at the metadata-derived identity plus `FontAtlas` at that identity with `_atlas` appended. Binary sidecars are not parsed as `.nwb` text.

This path avoids parsing large base64 arrays and reconstructing the atlas during every cook. The original `.ttf` or `.otf` is only an input to `font_builder`, not a checked-in asset. The `.font` file retains recoverable SFNT tables because HarfBuzz and FreeType use them at runtime. The atlas sidecar has a local stem identity that the cooker checks and replaces with the derived typed font identity before publishing the runtime asset.

The bundle is the only authoring format. Standalone `font` and `font_atlas` declarations, hash-named payload sidecars, and embedded Zstd/base64 metadata are unsupported.

## Identity and metrics

`FontAtlasPayload::font` is a typed `AssetRef<Font>`. The bundle derives its published identity from the paired metadata path; applications do not specify the relation in `.nwb`. The exact source SHA-256, face index, units per em, source glyph count, bake size, spread, raster mode, and vertical metrics are retained. The all-glyph policy requires one indexed record for every source glyph ID, including explicit nondrawable records for spaces or empty outlines. Drawable records include channel/group, bitmap rectangle, padded plane bounds in design units, and advance.

The UI font installation layer verifies this identity against the actual shaping font before binding an atlas. `ValidateFontAtlasSourceMatch` also requires the complete original positioning-table set and bytes to match that font, catching altered or omitted exports. A mismatched atlas must use the native coverage fallback for that selected font. HarfBuzz supplies glyph IDs and positioned advances/offsets; atlas advances and kerning metadata are never applied a second time.

## Positioning data

The atlas stores exact original `kern`, `GPOS`, and `GDEF` table bytes, sorted by numeric SFNT tag, with SHA-256 hashes of their content. This preserves pair/class maps, script/language/feature references, lookup order, and contextual/mark records without expanding all possible glyph pairs.

The admission validator fully checks OpenType `kern` format-0 pairs, GPOS roots, SinglePos, PairPos formats 1 and 2, coverage/class definitions, value records, device bounds, and positioning extensions. Other lookup families remain original opaque bytes with bounded root admission. This is a lossless table export contract and a bounded asset validator, not a general OpenType sanitizer or a normalized complete pair listing. The retained original font remains the authoritative shaping source.

## Bounds and validation

Shared limits in `model.h` apply to the generator, cooker, runtime binary, and GPU admission: 65,535 glyphs, eight RGBA groups, 2048 by 2048 per group, 128 MiB total image bytes, and 32 MiB total positioning bytes. Bake size is 16..256 ppem; spread is 2..32 pixels.

Bundle metadata accepts only its current schema and rejects unknown fields. Both companion files are mandatory, bounded before allocation, and derived from the declaration basename. The cooker verifies the atlas marker, source hash, metrics, and original positioning table set against the paired font before publication.

Shared payload admission validates exact decoded content hashes, padded plane extents against bitmap size, and guarded region overlap within the same image channel. Different channels can share the same coordinates.

The runtime binary codec encodes scalar values explicitly in little endian, with a fixed 164-byte version 1 header, canonical sequential sections, bounded counts before allocation, zero reserved fields, and exact total payload consumption. Failed parsing, loading, or serialization preserves the prior published output.

`tests/integration/assets_font_atlas/font_bundle_tests.cpp` covers paired publication, rejected metadata, and missing, corrupt, or mismatched companions. `font_atlas_tests.cpp` covers runtime binary corruption, payload bounds, glyph geometry, positioning bytes, and source matching. Production defaults are same-stem `.nwb`/`.font`/`.atlas` bundles generated by `font_builder`.
