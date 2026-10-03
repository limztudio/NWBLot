# Offline font builder

`font_builder` prepares one static SFNT face for the UI asset cooker. It takes an external `.ttf` or `.otf`, or an existing prepared `.font`, and writes three files with the same stem:

| File | Purpose |
| --- | --- |
| `<stem>.nwb` | Small `font_bundle` declaration, schema 1. It contains no font path or embedded image. |
| `<stem>.font` | FON1 shaping payload: a checked header followed by the exact original SFNT bytes. FreeType and HarfBuzz still receive those bytes. |
| `<stem>.atlas` | FTA1 binary atlas: every glyph ID, design metrics, kerning/positioning tables, and RGBA8 signed-distance groups. |

The bundle cooker derives the font identity from the `.nwb` path and the atlas identity by appending `_atlas`. It verifies that the atlas's local font marker matches the shared filename stem, that its source hash matches the `.font` bytes, and that its positioning tables match the source. Cooking reads the prepared binary payloads directly; it does not parse base64 or re-rasterize glyphs. The input TTF/OTF can remain outside the repository. The `.font` file retains the exact SFNT bytes required for runtime shaping and for later atlas regeneration.

## Build and use

The directory launcher is discovered as `font-builder` and builds the `nwb_font_builder` target. Arguments after `--` go to the utility.

```text
python -m launcher font-builder --build-only --config opt
python -m launcher font-builder --config opt -- --font "/absolute/path/source.ttf" --output "/absolute/path/assets/latin.nwb" --ppem 32 --extent 1024
python -m launcher font-builder --skip-build --config opt -- --font "/absolute/path/assets/latin.font" --output "/absolute/path/another/latin.nwb" --ppem 32 --extent 1024
```

The first command prepares the utility without baking a font. The launcher configures
and builds as needed; see [the launcher guide](../../launcher/README.md) for build options.

Use `--overwrite` to replace an existing complete trio. A partial trio, occupied `.tmp` or `.old` work path, or invalid source fails without publishing. The builder stages, flushes, and byte-verifies all three files before replacement; existing files are moved to backups while the new files are published, and restored if publication fails. It publishes the `.nwb` declaration last.

Defaults are 64 pixels per em, spread 8, square 1024-pixel pages and up to 8 RGBA groups. `--ppem` accepts 16..256, `--spread` 2..32, `--extent` 32..2048, and `--max-groups` 1..8. Total decoded group capacity is at most 128 MiB. Capacity failure never drops glyphs or changes the requested bake size.

`--renderer bitmap` is the default. FreeType renders an unhinted grayscale bitmap, then its pinned bitmap-SDF renderer produces the field. `--renderer outline` requests direct outline SDF and fails if the face has an unsupported outline. Both FreeType 2.14.3 SDF renderers come from the vendored revision recorded in `3rd_parties/freetype/nwb_update.txt`.

## Binary atlas and rendering

All source glyph IDs, including nondrawable whitespace and shaped ligatures, have atlas records. The packed texture uses four independent signed-distance planes in R, G, B, A; alpha is a fourth distance field, not opacity. Groups are lossless linear RGBA8 with no color conversion, alpha processing, or mip generation. Packing is deterministic: descending glyph height and width, then ascending ID, on guarded shelves in R/G/B/A order.

For `freetype_sdf_u8_v1`, byte 128 is zero distance and positive distance is inside the glyph. The shader decodes `(sample * 255 - 128) * spread / 128` and uses derivatives for antialiasing. Current UI SDF rendering is qualified only between 0.75 and 1.5 times the atlas bake ppem. Outside that range the renderer uses native grayscale coverage from the same shaping face; downscaling a baked SDF can lose the bottom antialiasing row of small descenders.

The atlas retains the original `kern`, `GPOS`, and `GDEF` table bytes with lengths and hashes. This preserves OpenType `kern` pairs, GPOS class matrices, script and language selection, devices, and lookup order without expanding into a quadratic pair list. Runtime HarfBuzz shaping uses the `.font` SFNT; exported table bytes are for verified transport and must not be applied to shaped positions again.

Generated output excludes timestamps and host/source paths. Repeating a bake on one platform with identical settings and source bytes produces byte-identical triplets. Cross-platform byte equality still requires execution on both platforms.
