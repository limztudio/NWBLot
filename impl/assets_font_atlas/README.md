# Font atlas assets

`FontAtlas` is the CPU asset for a complete static font face baked into four independent scalar SDF pages per RGBA texture. It owns exact linear RGBA8 bytes. Alpha is the fourth distance plane; no channel is opacity. The format has one base mip, a one-texel exterior guard, and FreeType scalar SDF encoding v1 (zero at byte 128, positive inside).

The offline generator lives in `utilities/font_atlas`. The ordinary package cooker discovers `font_atlas asset;` documents through `nwb_assets_font_atlas_volume_entry`. Runtime users link `nwb_assets_font_atlas` and include `asset.h`.

## Single-file authoring

The generator emits one UTF-8, CRLF `.nwb` document per atlas using authoring `schema_version = 2` and `payload_encoding = "zstd_base64"`. All glyph metadata, RGBA group data, and original positioning tables are embedded in that document. New packages have no adjacent hash-named `.rgba`, `.kern`, `.GPOS`, or `.GDEF` files. The native shaping font remains a separate typed `Font` asset referenced by the atlas.

Each group or table record declares its decoded `byte_count` and SHA-256, its `packed_byte_count`, and a `data_base64` list. The list contains canonical base64 chunks of at most 4,096 characters; every nonfinal chunk has exactly 4,096 characters. The decoded packed bytes contain one deterministic Zstd frame generated at fixed level 9, with declared content size and checksum, without a dictionary or worker threads. Compression is lossless: the cooker restores the exact RGBA and OpenType table bytes before applying their existing content and geometry validators.

`source_payload.h` and target `nwb_assets_font_atlas_source` own the shared authoring compression contract used by the generator and cooker. The cooker continues to import legacy authoring schema 1 documents with adjacent payload files. Authoring schema 2 does not change the runtime `FTA1` payload, which remains version 1.

## Identity and metrics

`FontAtlasPayload::font` is a typed `AssetRef<Font>`. The exact source SHA-256, face index, units per em, source glyph count, bake size, spread, raster mode, and vertical metrics are retained. The all-glyph policy requires one indexed record for every source glyph ID, including explicit nondrawable records for spaces or empty outlines. Drawable records include channel/group, bitmap rectangle, padded plane bounds in design units, and advance.

The UI font installation layer verifies this identity against the actual shaping font before binding an atlas. `ValidateFontAtlasSourceMatch` also requires the complete original positioning-table set and bytes to match that font, catching altered or omitted exports. A mismatched atlas must use the native coverage fallback for that selected font. HarfBuzz supplies glyph IDs and positioned advances/offsets; atlas advances and kerning metadata are never applied a second time.

## Positioning data

`kerning_mode = "opentype_tables"` stores exact original `kern`, `GPOS`, and `GDEF` table bytes, sorted by numeric SFNT tag, with SHA-256 hashes of their decoded content. Schema 2 embeds these lossless exports in the atlas document. This preserves pair/class maps, script/language/feature references, lookup order, and contextual/mark records without expanding all possible glyph pairs.

The admission validator fully checks supported legacy format-0 pairs, GPOS roots, SinglePos, PairPos formats 1 and 2, coverage/class definitions, value records, device bounds, and positioning extensions. Other lookup families remain original opaque bytes with bounded root admission. This is a lossless table export contract and a bounded asset validator, not a general OpenType sanitizer or a normalized complete pair listing. The retained original font remains the authoritative shaping source.

## Bounds and validation

Shared limits in `model.h` apply to the generator, cooker, runtime binary, and GPU admission: 65,535 glyphs, eight RGBA groups, 2048 by 2048 per group, 128 MiB total image bytes, and 32 MiB total positioning bytes. Bake size is 16..256 ppem; spread is 2..32 pixels.

Cook metadata rejects unknown fields, unsupported fixed tokens, incomplete glyph lists, malformed hash strings, and invalid channel/rectangle/metric values. Schema 2 validates chunk counts, exact chunk extents, canonical base64 alphabet and padding, and encoded byte bounds before allocating packed data. Packed data is bounded by `ZSTD_compressBound` for the declared decoded size. The source decoder requires exactly one complete frame with the declared content size and checksum; it rejects unknown-size, dictionary, excessive-window, skippable, concatenated, truncated, and trailing data. Legacy schema 1 additionally checks distinct adjacent payload basenames, traversal/absolute payload paths, and exact file sizes.

Shared payload admission validates exact decoded content hashes, padded plane extents against bitmap size, and guarded region overlap within the same image channel. Different channels can share the same coordinates.

The runtime binary codec encodes scalar values explicitly in little endian, with a fixed 164-byte version 1 header, canonical sequential sections, bounded counts before allocation, zero reserved fields, and exact total payload consumption. Failed parsing, decompression, loading, building, or serialization preserves the prior published output.

See `tests/integration/assets_font_atlas/fixtures/atlas.nwb` for a small runnable legacy schema 1 fixture and `font_atlas_source_tests.cpp` in that test directory for schema 2 admission and corruption cases. Production default atlases are generated from the engine font assets as single schema 2 documents.
