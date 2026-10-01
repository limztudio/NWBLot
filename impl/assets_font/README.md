# Font assets

`Font` stores immutable SFNT bytes and a face index for HarfBuzz shaping and FreeType rasterization. Its runtime asset type is `font`; the runtime and serialization targets are `nwb_assets_font` and `nwb_assets_font_cook`.

Fonts are authored only through same-stem `font_bundle` declarations and their `.font`/`.atlas` sidecars. The bundle importer in `impl/assets_font_atlas` derives both runtime identities, validates their source match, and publishes them together. There is no standalone `font asset;` importer. The original `.ttf` or `.otf` is an external input to `utilities/font_builder`, while the prepared `.font` retains the exact SFNT data required by shaping and rasterization.

The prepared payload supports one static scalable SFNT face with Unicode character mapping: TrueType outlines or CFF outlines. The face index is zero. Font collections, variable fonts (`fvar`/`CFF2`), compressed web fonts, bitmap-only faces, and non-Unicode faces are unsupported. Source bytes must be nonempty and at most 32 MiB; SFNT directories must have 1..256 distinct tables whose ranges stay inside the source buffer. Required outline and metrics tables are checked before FreeType opens the face.

The binary contains a packed 24-byte FON1 header followed by exactly the declared SFNT bytes. Load accepts only the current binary version and checks reserved flags, face index, byte count, SFNT structure, and native face admission before replacing existing state. Native validation uses the source buffer's explicit asset arena for temporary FreeType allocations. Glyph rasterization remains a text-service operation.

## Dependencies

FreeType 2.14.3 and HarfBuzz 14.5.0 are pinned static packages exposed as `nwb::freetype` and `nwb::harfbuzz`. FreeType builds the TrueType/CFF/SFNT drivers with raster, smoothing, and autohint modules; optional compression/image/shaping integrations are disabled. HarfBuzz uses its internal Unicode implementation and OpenType font/shaping functions without linking FreeType or platform text libraries. This software uses the FreeType library under its FreeType License; the upstream licence texts are preserved in `3rd_parties/freetype`.
