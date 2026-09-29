# Font assets

`Font` stores immutable SFNT bytes and a face index for HarfBuzz shaping and FreeType rasterization. Its typed asset keyword is `font`; runtime, cook, and volume-entry targets are `nwb_assets_font`, `nwb_assets_font_cook`, and `nwb_assets_font_volume_entry`.

The bundled UI font path uses one same-stem authoring triplet per face: `latin.nwb`, `latin.font`, and `latin.atlas`. The `font_builder` utility converts an external TTF/OTF into the runtime-ready `FON1` `.font` payload and binary `FTA1` `.atlas` payload before cooking. `font_bundle` metadata has no source filename or font identity field. Its paired cooker validates both sidecars, then publishes typed `Font` at the metadata-derived path and `FontAtlas` at that path with `_atlas` appended. The original `.ttf` or `.otf` is not kept in the asset tree; the `.font` payload still contains the SFNT tables required at runtime.

The earlier standalone `font asset;` schema 1 importer remains available for existing projects:

Schema 1 accepts one static scalable SFNT face with Unicode character mapping: TrueType outlines in `.ttf` or CFF outlines in `.otf`. The face index is zero. Font collections, variable fonts (`fvar`/`CFF2`), compressed web fonts, bitmap-only faces, and non-Unicode faces are outside this schema. Source bytes must be nonempty and at most 32 MiB; SFNT directories must have 1..256 distinct tables whose ranges stay inside the source buffer. The required outline/metrics tables are checked before FreeType opens the face.

```text
font asset;
asset.schema_version = 1;
asset.source = "NotoSans-Regular.ttf";
asset.face_index = 0;
```

For this legacy importer, `face_index` may be omitted and defaults to zero. `source` names an adjacent `.ttf` or `.otf` file; directories and absolute paths are rejected. The virtual identity comes from the metadata path under its asset root. Unknown metadata fields are rejected. The bundled path avoids repeated source-file admission and produces the same runtime font representation.

The versioned binary contains a packed 24-byte header followed by exactly the declared source bytes. Load checks the version, reserved flags, face index, byte count, SFNT structure, and native face admission before replacing existing state. Metadata parse and asset construction also commit only after validation succeeds. Native validation uses the source buffer's explicit asset arena for its temporary FreeType allocations. This checks face admission and supported structure; glyph rasterization remains a text-service operation.

The asset does not own font size, paragraph layout, shaping policy, glyph atlases, GPU textures, IME state, or clipboard state. Those belong to the UI text, rendering, and OS domains. A text service copies source bytes into an immutable generation before native libraries borrow them, so replacing an asset cannot invalidate submitted text or atlas data.

FreeType 2.14.3 and HarfBuzz 14.5.0 are pinned static packages exposed as `nwb::freetype` and `nwb::harfbuzz`. FreeType builds the TrueType/CFF/SFNT drivers with raster, smoothing, and autohint modules; optional compression/image/shaping integrations are disabled. HarfBuzz uses its internal Unicode implementation and OpenType font/shaping functions without linking FreeType or platform text libraries. This software uses the FreeType library under its FreeType License; the upstream licence texts are preserved in `3rd_parties/freetype`.

The deterministic defaults and their pinned source hashes/licences are documented in `impl/assets/ui/fonts/default/README.md`. Integration tests load and round-trip both prepared defaults, check SFNT byte identity, exercise registrars, and reject malformed headers, unsupported font containers, invalid table ranges, variable tables, unsupported metadata, and invalid face metrics while preserving prior state.
