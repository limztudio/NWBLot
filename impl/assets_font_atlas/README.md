# Font atlas assets

`FontAtlas` is the CPU asset for a complete static font face baked into independent scalar SDF pages. Each image group owns exact linear R8, RG8, RGB8, or RGBA8 bytes, with one distance page per stored channel. Alpha, when present, is the fourth distance plane; no channel is opacity. Images have one base mip, a one-texel exterior guard, and a fixed internal FreeType scalar SDF encoding (zero at byte 128, positive inside).

The offline generator lives in `utilities/font_builder`. Runtime users link `nwb_assets_font_atlas` and include `asset.h`; source cooking uses the ordinary `font_atlas` importer.

## Two-file authoring

The builder emits exactly `name.nwb` and `name.font`. The readable `.nwb` owns separately declared `font face;` and `font_atlas atlas;` objects gathered by `asset_bunch bunch = [face, atlas];`. The `.font` file is a `FON2` prepared-source container holding the exact SFNT shaping data and compact image pixels. All glyph mappings stay in the readable metadata; cooking builds the complete runtime atlas from these mappings and the paired source images.

The atlas declares its typed Font relation with `atlas.font = face;`. Bunch expansion resolves that local reference to the published Font identity. Both importers derive the sibling `.font` from the original document filename, including when importing a named bunch child. Named declarations publish under the metadata identity with `/face` and `/atlas` appended. For example, `engine/ui/fonts/default/latin` publishes `engine/ui/fonts/default/latin/face` and `engine/ui/fonts/default/latin/atlas`.

A standalone `font_atlas asset;` document provides an explicit Font virtual-path string and finds its same-stem `.font` source. The Font metadata and atlas metadata can be authored independently, or gathered in a single bunch. The original `.ttf` or `.otf` is only a builder input; the prepared source retains the exact SFNT required by HarfBuzz and FreeType.

## Readable metadata contract

Every current atlas field is mandatory, and unknown fields fail cooking:

| Field | Meaning |
| --- | --- |
| `font` | Typed Font reference, resolved from a local bunch reference or an explicit virtual path. |
| `bake_ppem`, `spread_pixels` | Author-selected bake size and SDF spread. |
| `raster_mode` | Exact token `bitmap` or `outline`. |
| `ascender_units`, `descender_units`, `line_gap_units` | Editable finite vertical layout metrics in font design units. |
| `glyphs` | One ordered dictionary for every source glyph ID, including nondrawable glyphs. |

The list contains exactly one record per source glyph in source order, including glyphs without a bitmap. An empty glyph has exactly `{ "advance_units": number }`; the advance is required. A bitmap glyph has exactly `group`, `channel`, `x`, `y`, positive `width` and `height`, `plane_left`, `plane_top`, and `advance_units`. Bitmap fields select an existing group/channel and a guarded rectangle; left/top specify the padded plane origin in font design units.

The presence of `width` selects the bitmap record shape. The cooker derives glyph ID from the list ordinal, drawable state from the record shape, and right/bottom plane bounds from left/top plus bitmap width/height multiplied by `units_per_em / bake_ppem`. Empty records receive zero geometry internally. Authored `id`, `drawable`, `plane_right`, `plane_bottom`, and fixed-zero geometry on an empty record are rejected.

The cooker derives face index, units per em, and glyph count from the paired SFNT, and image dimensions/channel counts from the prepared source directory. The exterior guard is fixed at one texel. Metadata does not repeat these values or a `groups` list; those retired fields are rejected. Glyph lists use ordinary metascript dictionary syntax. Integrity hashes stay in the prepared source and cooked runtime payload; metadata does not expose hashes, binary source paths, or version/revision fields.

## Compact image groups

`FontAtlasGroup::width` and `height` are independent dimensions; neither must be square or a power of two. `channelCount` is 1..4, and tightly interleaved rows contain exactly `width * channelCount` bytes. Each group stores exactly `width * height * channelCount` bytes, without row padding or a mip chain.

The builder crops guarded occupied bounds across every used channel and removes unused trailing channels. Glyph pixels, SDF spread, and exterior guards remain unchanged without resampling. Glyph coordinates are rebased to the cropped image. Groups may have different dimensions and channel counts.

The UI uploader uses the corresponding linear UNORM GPU format. A three-channel group uses RGB8 when filtered sampling is supported; otherwise the uploader expands once to RGBA8. The three logical distance pages retain their exact values. Cropping reduces texture extent on either path; removing the fourth channel reduces GPU storage where RGB8 is supported. Actual GPU allocations also depend on driver alignment and tiling.

## Identity and positioning

`FontAtlasPayload::font` is a typed `AssetRef<Font>`. Its internal source hash, face index, units per em, and glyph count identify the shaping source. The UI installation layer checks that identity against the actual Font before binding an atlas. A mismatched atlas uses the selected font's native coverage fallback. HarfBuzz supplies glyph IDs and positioned advances/offsets; atlas advances and positioning data are not applied a second time.

`CopyFontAtlasPositioningTables` copies the exact nonempty `kern`, `GPOS`, and `GDEF` tables directly from the prepared SFNT, sorts them by numeric tag, and computes internal content hashes. Both the builder and source cooker use this shared atlas-domain helper. `ValidateFontAtlasSourceMatch` requires the complete original table set and exact bytes to match the Font, preserving pair/class maps, script/language/feature selection, lookup order, and contextual/mark records without expanding possible glyph pairs.

The admission validator checks OpenType `kern` format-0 pairs, GPOS roots, SinglePos, PairPos formats 1 and 2, coverage/class definitions, value records, device bounds, and positioning extensions. Other lookup families remain original opaque bytes with bounded root admission. The retained SFNT remains the authoritative shaping source.

## Bounds and failure atomicity

Shared limits in `model.h` apply to the builder, cooker, runtime binary, and GPU admission: 65,535 glyphs, eight groups, 1..4 channels and at most 2048 by 2048 per group, 128 MiB total image bytes, and 32 MiB total positioning bytes. Bake size is 16..256 ppem; spread is 2..32 pixels.

Prepared-source reads check the complete container layout before allocation and verify SFNT and image integrity before atlas construction. Metadata admission checks bounded integer fields, finite editable metrics, complete source glyph order, and a resolved typed Font reference; image layouts are read from the prepared source. Shared atlas validation checks byte counts and hashes, valid channel selection, padded plane extents against bitmap size, and guarded region overlap within the same channel. Different channels can share coordinates. Failed metadata parsing or runtime loading preserves the previously published output.

The cooked runtime atlas layout remains `FTA1` version 2: explicitly little-endian scalars, a fixed 164-byte header, sequential sections, bounded counts, zero reserved fields, and exact payload consumption. Every 48-byte group header stores width, height, channel count, byte count, and an internal pixel hash followed by tightly interleaved pixels. Glyph records and original positioning tables are serialized into this runtime asset after metadata admission.

Integration tests under `tests/integration/assets_font_atlas` cover bunch publication and failed-child recovery, standalone import, strict metadata fields, malformed glyph mappings, and missing, truncated, corrupt, or mismatched `.font` sources. Runtime codec tests additionally cover compact non-power-of-two images, every channel count, exterior guards, geometry, positioning bounds, source matching, and failed-load preservation.
