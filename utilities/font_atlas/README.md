# Offline font atlas utility

`font_atlas` turns every glyph ID in one admitted static `.ttf` or `.otf` SFNT face into a scalar signed distance field. Four independent pages occupy R, G, B and A of a lossless linear RGBA texture. The utility emits `font_atlas` metadata plus adjacent content-addressed raw payloads for the `impl/assets_font_atlas` cooker/runtime. Alpha is a fourth distance plane, not image opacity.

## Build and usage

The directory launcher is discovered as `font-atlas`. It configures and builds the `nwb_font_atlas` CMake target before starting the utility. Windows uses the common engine application entry and standalone logger; the source has the same portable file and FreeType paths on Linux.

```text
python -m launcher font-atlas --help
python -m launcher font-atlas --config opt -- --help
python -m launcher font-atlas --skip-build --config opt -- --help
```

Arguments before `--` belong to the repository launcher; arguments after it go to the utility. The launcher starts the program from its runtime output directory, so source/output paths relative to the repository checkout need to be resolved before launch. Use absolute source/output paths, including quoted paths containing spaces. For example, from this Windows checkout:

```text
python -m launcher font-atlas --config opt -- --font "C:/WorkStation/NWBLot/impl/assets/ui/fonts/default/NotoSans-Regular.ttf" --font-asset engine/ui/fonts/default/latin --output "C:/WorkStation/NWBLot/__artifacts/font_atlas/latin.nwb"
python -m launcher font-atlas --skip-build --config opt -- --font "C:/WorkStation/NWBLot/impl/assets/ui/fonts/default/NotoSansKR-Regular.otf" --font-asset engine/ui/fonts/default/korean --output "C:/WorkStation/NWBLot/__artifacts/font_atlas/korean.nwb" --ppem 32 --extent 2048
```

Replace the checkout prefix with your own absolute path on Windows or Linux. You may also build `nwb_font_atlas` directly and run `font_atlas` from a chosen working directory.

The required font asset identity names the separately cooked `Font`, not the source filename. Default settings are 64 pixels per em, spread 8, square 1024-pixel planes and up to 8 RGBA groups. `--ppem` accepts 16..256, `--spread` accepts 2..32, `--extent` accepts 32..2048, and `--max-groups` accepts 1..8. Total decoded group capacity remains at most 128 MiB. An individually oversized glyph or exhausted capacity fails with a glyph/footprint diagnostic. It never drops glyphs or changes the bake size.

`--renderer bitmap` is the default: FreeType renders an unhinted grayscale bitmap, then its pinned bitmap-SDF renderer produces the field. This handles intersecting outlines consistently. `--renderer outline` requests direct outline SDF; native unsupported-outline errors fail explicitly, without switching algorithms. Both exact FreeType 2.14.3 SDF renderers are built from the vendored commit recorded in `3rd_parties/freetype/nwb_update.txt`.

## Output contract

Metadata records the source SHA-256, face index, glyph count, units per em, ascender/descender/line gap, bake settings, renderer, and distance encoding. Every source glyph has a record, including whitespace with its design-unit advance and no drawable rectangle. Shaped ligatures and contextual forms therefore remain addressable by their actual glyph IDs.

A drawable record identifies a group/channel and sampled pixel rectangle. Its design-unit plane bounds come from the SDF bitmap's actual bearing and dimensions, including the distance spread. Geometry and logical advances must use HarfBuzz's positions, offsets and selected face. The utility's unshaped advance is inspection data.

Packing sorts descending bitmap height, descending width, then ascending glyph ID. A deterministic shelf traversal checks logical pages in R/G/B/A group order without rotation. Sampled rectangles have one exterior guard texel. Rows are top to bottom and pixels are interleaved RGBA bytes, without row padding or mip data. Empty texels and channels are encoded exterior zero. No lossy compressor, sRGB conversion, alpha processing or ordinary mip generation is applied.

For `freetype_sdf_u8_v1`, byte 128 is zero distance and positive distance is inside the glyph. The shader selects one channel and decodes `(sample * 255 - 128) * spread / 128`; derivative-based coverage adjusts antialiasing to display scale. Scalar fields do not guarantee arbitrary zoom or preserve detail missing from the bake.

The current UI renderer admits SDF only when physical ppem is between **0.5 and 1.5 times the atlas bake ppem**, inclusive. Physical ppem combines requested font size with display scale. The shipped 32-ppem atlases therefore use SDF at 16..48 physical pixels per em and use native grayscale coverage outside that range, preserving the selected face and shaped layout. The measured Latin `A`/`o`/`e` and Korean `한` fixtures met a one-screen-pixel edge bound and mean coverage error below 0.04 across 36 comparisons. The wider initial range failed the sharply tipped Latin `A` at 64 physical ppem; this is why the current runtime range is narrower. Custom fonts and bake settings require their own rendering qualification before widening the policy.

## Kerning scope

`kerning_mode = "opentype_tables"` retains exact original `kern`, `GPOS` and `GDEF` table bytes with their independent lengths and hashes. This includes legacy pairs and modern GPOS class matrices, class-zero behavior, script/language/feature selection, contextual data, devices and ordered lookups without a quadratic glyph-pair expansion. The utility does not emit a normalized pair list or evaluate positioning.

Admission checks bounded table versions, roots and lookup references; legacy format-0 pairs; GPOS single/pair adjustments, coverage/class definitions, value/device records and pair positioning extensions; and GDEF classes and relevant child roots. Other legacy formats and GPOS contextual/mark/cursive structures are retained opaque after bounded root/format checks. This is a raw-data transport contract, not a complete OpenType sanitizer. Runtime shaping executes the admitted original source `Font` through HarfBuzz, never the exported atlas table bytes. Do not apply exported kerning again after shaping.

## Publication and reproducibility

A bake validates the complete candidate before touching output. Payload names contain their full SHA-256. Existing payloads with the same name must match every byte. The utility writes and verifies new payloads before atomically renaming the staged `.nwb` file into place. `--overwrite` is required to replace metadata. Failed capacity, raster or validation admission leaves the previous package usable; failed final publication leaves its metadata and payloads intact. Old content-addressed files remain available and unrelated output files are never deleted.

Record order and locale-independent numeric formatting are stable. Metadata excludes timestamps and host/source paths, and records the pinned generator/FreeType revision in comments. Repeat bakes on one platform must be byte-identical. Native Win32/Linux byte equivalence requires actual execution on both platforms; syntax compilation alone does not establish it.

`tests/integration/font_atlas` covers independent channels, transitions beyond four pages, bounded legacy/class kerning records, malformed offsets, full Latin/Korean generation, payload identity, repeat bakes, overwrite refusal and preservation of an existing package after failure.
