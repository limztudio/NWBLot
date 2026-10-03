# Offline font bundles and RGBA SDF atlases

Status: implemented on `main`. The UI text service shapes with the original SFNT bytes and optionally paints from a matching signed-distance atlas. The offline builder prepares those two payloads before normal asset cooking. Toolkit work is tracked in [the custom UI plan](custom_ui_plan.md).

## Authoring contract

`utilities/font_builder` accepts a static `.ttf` or `.otf` face, or an existing prepared `.font`, and publishes exactly three same-stem files:

| File | Content |
| --- | --- |
| `<stem>.nwb` | A small `font_bundle asset;` declaration with `asset.schema_version = 1;`. It names neither a source file nor another asset. |
| `<stem>.font` | Binary FON1 header followed by the exact admitted SFNT bytes. The original TTF/OTF does not need to remain in the asset tree. |
| `<stem>.atlas` | Binary FTA1 atlas with indexed glyph records, linear RGBA8 SDF groups, original positioning tables, metrics, and content hashes. |

The asset builder scans `.nwb` declarations. The bundle cooker opens the adjacent `.font` and `.atlas` with the same stem, admits both as a unit, derives the `Font` virtual identity from the `.nwb` path, and publishes `FontAtlas` at that identity plus `_atlas`. It checks the atlas's local stem marker, source SHA-256, face index, units per em, glyph count, and exact positioning-table bytes against the font before rebinding its full virtual identity. Missing, mismatched, malformed, or partial trios fail without publishing either cooked asset. Cook time reads bounded binary payloads directly; it does not parse a glyph-sized text document or rerasterize the face.

This is the only authoring contract. Standalone `font` and `font_atlas` declarations, older external atlas payloads and zstd/base64 text embedding are no longer imported. Regenerate old assets with `font_builder`; runtime codecs retain only their current version.

The current runtime `Font` and `FontAtlas` codecs remain FON1 and FTA1 version 1. A prepared `.font` stores the same SFNT bytes used by FreeType and HarfBuzz, so later atlas regeneration and native-coverage fallback do not require the original external file. This is a transport and cook-time change, not a font-outline conversion. The original font notices remain next to the default bundles; see the [default-font README](../impl/assets/ui/fonts/default/README.md).

## Builder and publication

Build the utility with `python -m launcher build nwb_font_builder`, or build and run it with `python -m launcher font-builder`; see the [utility README](../utilities/font_builder/README.md) for executable examples. The utility admits a readable static SFNT face of at most 32 MiB, using face index zero, and retains every source glyph ID, including glyph zero and nondrawable whitespace. It refuses unsupported fonts, truncation, invalid bake options, and capacity exhaustion rather than silently dropping glyphs or changing settings.

The pinned FreeType 2.14.3 SDF sources come from vendor revision `0a0221a1347e2f1e07c395263540026e9a0aa7c7`. `--renderer bitmap` is the default: it renders unhinted grayscale coverage and then a bitmap SDF. `--renderer outline` requests direct outline SDF and fails if the face cannot support that path. Default settings are 64 pixels per em, spread 8, square 1024-pixel groups, and at most eight groups. Shared bounds are 65,535 glyphs, group extents no greater than 2048², at most 128 MiB of RGBA pixels and 32 MiB of positioning tables, ppem 16..256, and spread 2..32.

Packing sorts glyphs by descending bitmap height, descending width, then ascending ID. It traverses guarded shelves in R/G/B/A page order, without glyph rotation. The builder validates the complete candidate, stages and flushes all three outputs, verifies their bytes, then publishes the `.nwb` declaration last. Replacement of an existing complete trio requires `--overwrite`; a partial trio or occupied work path fails. Failed rasterization, validation, staging, or replacement preserves the previous bundle. Repeating a bake from identical SFNT bytes and settings on one platform produces byte-identical output. Native Linux execution is still needed to qualify cross-platform byte equality.

## Atlas format and rendering

Each logical scalar page maps to one RGBA channel: page `p` uses group `p / 4` and channel `p % 4`. Alpha is a fourth distance plane, not image opacity. Rows are top to bottom with interleaved RGBA bytes and no padding or mips. Empty texels contain exterior zero. The decoded GPU texture is linear `RGBA8_UNORM` with bilinear clamp sampling; sRGB conversion, premultiplication, ordinary image mip generation, and lossy compression are not applied to these source pixels.

FTA1 stores a fixed 164-byte little-endian header, 52-byte records for all glyph IDs, 44-byte group headers followed by exact pixels, and 40-byte positioning-table headers followed by exact table bytes. Each group and table has a decoded SHA-256. Bounded counts and lengths are checked before allocation; reserved fields must be zero and the final section must consume exactly the file. The cooker also checks guarded same-channel rectangle overlap and geometry bounds. Invalid reads preserve previously loaded assets.

Drawable records hold group/channel, pixel rectangle, baseline-relative plane bounds in design units, and an inspection advance. Plane bounds include the SDF spread; the guard texel lies outside the sampled rectangle. Shape and placement still come from HarfBuzz's selected face, glyph ID, cluster, advance, and offset. Atlas advances and positioning exports are never applied again after shaping.

The atlas carries the original `kern`, `GPOS`, and `GDEF` table bytes with hashes. That preserves legacy pairs, class matrices, script/language/feature selection, lookup order, contextual structures, and device data without expanding all glyph pairs. The admission validator checks bounded roots and supported pair/class structures; other positioning families retain their original bytes after bounded root checks. It is not a general OpenType sanitizer.

For `freetype_sdf_u8_v1`, byte 128 represents zero distance and positive distance is inside the glyph. The shader chooses one channel and reconstructs `(sample * 255 - 128) * spread / 128`, using derivatives to adjust coverage. The qualified SDF interval is **0.75..1.5 times the bake ppem** in physical pixels; outside it, the same selected face uses native grayscale coverage. The default 32-ppem Latin and Korean atlases therefore serve 24..48 physical pixels per em. Below that interval, native coverage preserves the antialiased bottom row of small descenders. The quality test compares Latin `A`/`o`/`e`/`g`/`q` and Korean `한` at 48 glyph/scale combinations within this interval, requiring at most one pixel of edge displacement and mean coverage error no greater than 0.04. Larger magnification failed at sharp corners, so the current field does not promise unlimited zoom.

Installed font versions copy SFNT bytes and immutable atlas image records. Existing layouts and paint snapshots retain their versions across asset release, font replacement, and display-scale changes. The GPU path uploads the RGBA group, selects a single channel per glyph draw, imports readiness into the frame graph, and retains resources through the final compositor consumer. The independently prepared UI layer and premultiplied SDR/HDR composition are unchanged by the authoring migration.

### UASTC trial

The conditional UASTC preference was tested against the shipped four-channel atlases. The pinned LDR `tex_conv --linear` encoder compressed block-aligned 96x96 crops from the actual RGBA pages; the pinned Basis decoder reconstructed their first mip. A probe selected each glyph's assigned channel and compared bilinear SDF coverage with the lossless page at 24, 32, and 48 physical pixels per em. The sample included Latin `A`, `o`, `e`, `i`, `m`, `g`, `q` and Korean `한`, `가`.

The compressed `g` and `q` edges moved up to **2 pixels** at 48 pixels per em; `A` and `한` also reached 2 pixels. At the stored distance threshold, `g` changed sign in 43 of 1,344 glyph texels and `한` in 69 of 1,980. The largest additional mean coverage error against the lossless page was 0.0306 (`한` at 32 pixels per em). This representative comparison did not repeat the native-outline qualification, but it already exceeds the current one-pixel edge budget relative to the lossless atlas. FTA1 therefore keeps exact RGBA8 pages for this packing and encoder. Directly sampled UASTC could reduce the default raw texel payloads from 8 to 2 MiB and 64 to 16 MiB; a per-channel compression strategy would need its own native-outline quality gate before changing the format.

## Verification

The paired-output integration suite exercises complete Latin and Korean generation, FON1 and FTA1 decoding, all-glyph coverage, four-channel packing, exact source positioning bytes, deterministic repetition, raw-SFNT and prepared-font equivalence, overwrite refusal, failed-capacity preservation, and relocation of a whole trio. The font-bundle cooker tests cover missing/mismatched companions and transactional publication. The quality test compares real default atlases against supersampled native outlines across the qualified scale interval. UI smoke and Testbed exercise the cooked default pair through the normal asset pipeline.

Windows ARM64 builds and tests can run on the current host. Changed Linux code can be checked with the Linux x86_64 syntax harness, but that does not establish native Linux linking, cross-platform bake byte equality, or compositor behavior. Live HDR presentation and other fonts' visual quality remain separate qualification work.
