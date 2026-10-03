# Font assets

`Font` stores immutable SFNT bytes and a face index for HarfBuzz shaping and FreeType rasterization. Its runtime asset type is `font`; the runtime and serialization targets are `nwb_assets_font` and `nwb_assets_font_cook`.

Fonts have an independent `font` metadata importer. A standalone `font asset;` declaration loads its same-stem `.font` companion and derives its virtual identity from the `.nwb` path. Font metadata requires `schema_version = 1`, `face_index`, `units_per_em`, `glyph_count`, and `source_sha256`; the importer checks every visible value against the prepared binary. Unknown fields, stale metadata, malformed binary data, and missing companions are cook failures that preserve the previous parsed entry. Standalone Font import needs no atlas file.

`font_builder` emits one `.nwb` document with separately declared `font face;` and `font_atlas atlas;` objects collected by `asset_bunch bunch = [face, atlas];`. The atlas's `font = face` local reference becomes a typed reference to the published Font identity. Both importers derive the same-stem `.font` and `.atlas` companions from the original document path, so metadata carries no duplicate source filenames. For `latin.nwb`, the bunch publishes `<virtual directory>/latin/face` and `<virtual directory>/latin/atlas`. The atlas importer verifies its exact source hash, metrics, glyph count, and positioning tables against the paired font before publication. `font_bundle` is no longer an asset type.

The original `.ttf` or `.otf` is an external input to `utilities/font_builder`, while the prepared `.font` retains the exact SFNT data required by shaping and rasterization.

The prepared payload supports one static scalable SFNT face with Unicode character mapping: TrueType outlines or CFF outlines. The face index is zero. Font collections, variable fonts (`fvar`/`CFF2`), compressed web fonts, bitmap-only faces, and non-Unicode faces are unsupported. Source bytes must be nonempty and at most 32 MiB; SFNT directories must have 1..256 distinct tables whose ranges stay inside the source buffer. Required outline and metrics tables are checked before FreeType opens the face.

The binary contains a packed 24-byte FON1 header followed by exactly the declared SFNT bytes. Load accepts only the current binary version and checks reserved flags, face index, byte count, SFNT structure, and native face admission before replacing existing state. Native validation uses the source buffer's explicit asset arena for temporary FreeType allocations. Glyph rasterization remains a text-service operation.

## Dependencies

FreeType 2.14.3 and HarfBuzz 14.5.0 are pinned static packages exposed as `nwb::freetype` and `nwb::harfbuzz`. FreeType builds the TrueType/CFF/SFNT drivers with raster, smoothing, and autohint modules; optional compression/image/shaping integrations are disabled. HarfBuzz uses its internal Unicode implementation and OpenType font/shaping functions without linking FreeType or platform text libraries. This software uses the FreeType library under its FreeType License; the upstream licence texts are preserved in `3rd_parties/freetype`.
