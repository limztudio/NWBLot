# Font atlas assets

`FontAtlas` is the CPU asset for a complete static font face baked into four independent scalar SDF pages per RGBA texture. It owns exact linear RGBA8 bytes. Alpha is the fourth distance plane; no channel is opacity. The format has one base mip, a one-texel exterior guard, and FreeType scalar SDF encoding v1 (zero at byte 128, positive inside).

The offline generator lives in `utilities/font_atlas`. The ordinary package cooker discovers `font_atlas asset;` documents through `nwb_assets_font_atlas_volume_entry`. Runtime users link `nwb_assets_font_atlas` and include `asset.h`.

## Identity and metrics

`FontAtlasPayload::font` is a typed `AssetRef<Font>`. The exact source SHA-256, face index, units per em, source glyph count, bake size, spread, raster mode, and vertical metrics are retained. The all-glyph policy requires one indexed record for every source glyph ID, including explicit nondrawable records for spaces or empty outlines. Drawable records include channel/group, bitmap rectangle, padded plane bounds in design units, and advance.

The UI font installation layer verifies this identity against the actual shaping font before binding an atlas. `ValidateFontAtlasSourceMatch` also requires the complete original positioning-table set and bytes to match that font, catching altered or omitted exports. A mismatched atlas must use the native coverage fallback for that selected font. HarfBuzz supplies glyph IDs and positioned advances/offsets; atlas advances and kerning metadata are never applied a second time.

## Positioning data

`kerning_mode = "opentype_tables"` stores exact original `kern`, `GPOS`, and `GDEF` table bytes, sorted by numeric SFNT tag, with SHA-256 hashes. Adjacent lossless sidecars are named by each record. This preserves pair/class maps, script/language/feature references, lookup order, and contextual/mark records without expanding all possible glyph pairs.

The admission validator fully checks supported legacy format-0 pairs, GPOS roots, SinglePos, PairPos formats 1 and 2, coverage/class definitions, value records, device bounds, and positioning extensions. Other lookup families remain original opaque bytes with bounded root admission. This is a lossless table export contract and a bounded asset validator, not a general OpenType sanitizer or a normalized complete pair listing. The retained original font remains the authoritative shaping source.

## Bounds and validation

Shared limits in `model.h` apply to the generator, cooker, runtime binary, and GPU admission: 65,535 glyphs, eight RGBA groups, 2048 by 2048 per group, 128 MiB total image bytes, and 32 MiB total positioning bytes. Bake size is 16..256 ppem; spread is 2..32 pixels.

Cook metadata rejects unknown fields, unsupported fixed tokens, incomplete glyph lists, malformed hash strings, duplicate payload basenames, traversal/absolute payload paths, changed file sizes, and invalid channel/rectangle/metric values. Runtime admission additionally validates exact content hashes, padded plane extents against bitmap size, and guarded region overlap within the same image channel. Different channels can share the same coordinates.

The binary codec encodes scalar values explicitly in little endian, with a fixed 164-byte versioned header, canonical sequential sections, bounded counts before allocation, zero reserved fields, and exact total payload consumption. Failed parsing, loading, building, or serialization preserves the prior published output.

See `tests/integration/assets_font_atlas/fixtures/atlas.nwb` for a small runnable metadata fixture. Production default atlases are generated from the engine font assets.
