# Font assets

`Font` owns the immutable SFNT bytes and face index used by HarfBuzz shaping and FreeType rasterization. Its asset type is `font`; runtime consumers link `nwb_assets_font`, and cook consumers link `nwb_assets_font_cook`.

## Two-file authoring

`utilities/font_builder` produces exactly two same-stem files. `name.nwb` contains readable Font and FontAtlas metadata, including every glyph mapping. `name.font` is a `FON2` source container holding the prepared SFNT and compact atlas image pixels. The original `.ttf` or `.otf` is an external builder input and is not a checked-in runtime asset.

The metadata declares independent assets and gathers them through the ordinary asset bunch:

```text
font face;
face.face_index = 0;
face.units_per_em = 2048;
face.glyph_count = 846;

font_atlas atlas;
atlas.font = face;
// The builder writes atlas metrics, groups, and all glyph records here.

asset_bunch bunch = [face, atlas];
```

The values shown are illustrative; each Font value must match its prepared SFNT. The Font declaration accepts exactly `face_index`, `units_per_em`, and `glyph_count`. Every field is required, and unknown fields fail cooking. Image descriptions and glyph mappings belong to the separate FontAtlas declaration.

For `latin.nwb`, the bunch publishes `<virtual directory>/latin/face` and `<virtual directory>/latin/atlas`. Its local `atlas.font = face` reference resolves to a typed `AssetRef<Font>` before either importer consumes it. Both assets derive `latin.font` from the original metadata path, so there are no authored source filenames or hash fields.

A standalone `font asset;` document uses the same three fields and finds its sibling `.font` by replacing the document extension. Its identity follows the metadata path. Font-only import validates the complete FON2 directory and file length, verifies the SFNT integrity hash, and reads only SFNT bytes; it does not allocate atlas image pixels. Missing, truncated, malformed, or mismatched source data preserves the previously parsed entry.

## Prepared source and runtime payload

The FON2 authoring container retains exact shaping SFNT bytes and compact image groups with internal integrity hashes. Readable atlas mappings stay in `.nwb`; source cooking reconstructs the independent runtime assets from the pair. The prepared source supports one static scalable SFNT face with Unicode character mapping: TrueType outlines or CFF outlines, with face index zero. Font collections, variable fonts (`fvar`/`CFF2`), compressed web fonts, bitmap-only faces, and non-Unicode faces are unsupported.

SFNT bytes must be nonempty and at most 32 MiB. Directories must have 1..256 distinct tables whose ranges stay inside the source buffer. Required outline and metrics tables are checked before FreeType opens the face. Native validation uses the source buffer's explicit asset arena for temporary allocations.

The cooked Font runtime payload remains a packed 24-byte `FON1` header followed by exactly its declared SFNT bytes. Runtime loading accepts only the current payload version and checks reserved flags, face index, byte count, SFNT structure, and native face admission before replacing existing state. FON2 authoring files are consumed by the source cooker rather than the runtime asset loader. Glyph rasterization remains a UI text-service operation.

## Dependencies

FreeType 2.14.3 and HarfBuzz 14.5.0 are pinned static packages exposed as `nwb::freetype` and `nwb::harfbuzz`. FreeType builds the TrueType/CFF/SFNT drivers with raster, smoothing, and autohint modules; optional compression/image/shaping integrations are disabled. HarfBuzz uses its internal Unicode implementation and OpenType font/shaping functions without linking FreeType or platform text libraries. This software uses the FreeType library under its FreeType License; the upstream licence texts are preserved in `3rd_parties/freetype`.
