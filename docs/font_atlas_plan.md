# Offline RGBA SDF font atlases

Status: implemented on `custom_ui`. The offline utility, CPU asset/cooker, immutable UI image bindings, GPU resource owner, and channel-selecting shader are available. This increment extends the existing native font and HarfBuzz text service; interactive controls and ImGui retirement remain later toolkit work.

## 1. Delivered result

`utilities/font_atlas` reads an admitted static `.ttf` or `.otf` face and bakes every source glyph ID into a scalar signed distance field. Four independent pages occupy R, G, B, and A of one lossless linear RGBA8 image. Further pages allocate another RGBA group. Each glyph selects exactly one channel; alpha is another distance plane.

The utility publishes a readable `font_atlas asset;` `.nwb` document and adjacent content-addressed `.rgba`, `.kern`, `.GPOS`, and `.GDEF` payloads as needed. The regular asset pipeline validates and cooks that package into one versioned `FontAtlas` asset. Runtime decoding needs no image decoder or sidecar filesystem access. The matching original `Font` remains necessary for shaping and native coverage fallback.

Four packed channels use the same uncompressed pixel bytes as four equally sized R8 pages. This arrangement reduces texture objects and descriptors; it is not compression. GPU performance gains require measurements of the actual workload.

The bundled Latin atlas has 3,748 indexed glyphs, six logical pages, and two 1024-square RGBA groups (8 MiB). The Korean atlas has 24,964 indexed glyphs, thirteen logical pages, and four 2048-square groups (64 MiB). Both use 32 ppem, spread 8, the bitmap renderer, and one guard texel. Their typed asset identities are `engine/ui/fonts/default/latin_atlas` and `engine/ui/fonts/default/korean_atlas`. They match the separately retained source fonts and licences.

## 2. Ownership and source boundaries

| Domain | Sources | Responsibility |
| --- | --- | --- |
| Utility | `main.cpp`, `command_line.cpp`, `global.h`, `launch.py` | Process logger/arenas, arguments, portable paths, and exit status. |
| Utility | `font_source.*`, `sdf_raster.cpp` | Exact source admission, native face lifetime, glyph enumeration, explicit FreeType rendering, and padded bounds. |
| Utility | `atlas_pack.cpp`, `kerning.cpp`, `bake.cpp` | Deterministic packing, exact positioning-table export, and candidate construction. |
| Utility | `asset_writer.cpp` | Canonical metadata, hashes, verified payload staging, and final publication. |
| CPU asset | `impl/assets_font_atlas/model.h`, `asset.h`, `validation.cpp`, `positioning_validation.cpp`, `source_match.cpp` | Typed identity, immutable records, format-wide bounds, positioning admission, and exact font matching. |
| CPU codec | `binary_payload.h`, `binary.cpp`, `runtime.cpp` | Explicit little-endian serialization, bounded decoding, and failure-atomic asset loading. |
| Cooker | `cook_metadata*`, `cook.cpp`, `volume_entry.cpp` | Strict declarative schema, adjacent dependencies, normal package cooking, and registrars. |
| UI text | `impl/ui/text/baked_atlas.*`, `sdf_page.*`, `service_paint.cpp` | Strong immutable image versions, shaped glyph geometry, scale policy, and native coverage fallback. |
| Paint | `impl/ui/paint_images.cpp`, `paint_sdf.cpp` | Atomic mixed-image admission, clipping, material selection, and adjacent batching. |
| GPU | `impl/ui/gpu/renderer_sdf_resources.*`, `renderer_image_cache.cpp` | Upload readiness, descriptors, graph imports, shared image capacity, and completion lifetimes. |
| Shader | `impl/assets/graphics/ui/glyph_sdf.slangi`, `ps.slang`, `push_constants.h` | One-channel reconstruction, antialiasing, and checked CPU/shader ABI. |
| ECS adapter | `impl/ecs_ui/layer_system_fonts.cpp` | Load explicit typed font/atlas bindings and borrow the existing text service. |

The utility uses vendored FreeType, project scalar/container/path types, and caller-owned arenas. It does not depend on system font discovery or platform font rendering APIs. Win32 and Linux share the algorithm and byte format. `core/os` continues to own clipboard/native selection and future IME services; font generation does not move those responsibilities into UI or ECS.

## 3. Generator admission and publication

Build `nwb_font_atlas` or use `utilities/font_atlas/launch.py`. Source paths, output paths, and source font asset identity are explicit. See the [utility README](../utilities/font_atlas/README.md) and [default font README](../impl/assets/ui/fonts/default/README.md) for executable examples and reproduction commands.

The source must be one regular static SFNT `.ttf` or `.otf` file, at most 32 MiB, using face index zero. The generator admits the exact initial bytes and rejects truncation or trailing bytes rather than allocating from a later size probe. All glyph IDs, including glyph zero and nondrawable spaces, have indexed records. It does not silently drop glyphs, change bake size, or select another font.

The restored SDF sources and module registrations come from the exact FreeType 2.14.3 revision `0a0221a1347e2f1e07c395263540026e9a0aa7c7` recorded in the vendor manifest. The default `--renderer bitmap` creates unhinted grayscale coverage and then invokes the bitmap-SDF renderer. `--renderer outline` explicitly requests direct outline SDF. A native raster error aborts that bake; renderer selection never changes silently.

Default utility settings are 64 ppem, spread 8, extent 1024, and at most eight RGBA groups. Schema 1 bounds are:

- 65,535 glyphs, eight RGBA groups, and group extent at most 2048 by 2048.
- At most 128 MiB of decoded RGBA bytes and 32 MiB of positioning-table bytes.
- Bake size 16..256 ppem, spread 2..32 source pixels, one guard texel, and one base mip.

Packing sorts descending bitmap height, descending width, then ascending glyph ID. A deterministic shelf traversal visits logical pages in ascending order without rotating glyphs. An oversized glyph or exhausted group budget reports failure instead of scaling or omitting the glyph.

The complete candidate is validated before publication. Payload filenames contain their full SHA-256, and any existing file at that identity must match its exact bytes. New payloads are written and verified before the staged metadata is atomically renamed into place. Replacing existing metadata requires `--overwrite`. Failed admission/publication preserves the previous package; old content-addressed and unrelated files remain available. An occupied staging path is preserved and reported as a publication failure.

Metadata order and locale-independent numeric formatting are stable. Generator comments record the pinned implementation rather than timestamps or host paths. Repeated native Windows bakes are byte-identical. Equivalent native Linux execution remains a qualification gate.

## 4. Pixel, glyph, metadata, and binary contracts

Logical page `p` maps to group `p / 4` and channel `p % 4`, in R/G/B/A order. Rows are top to bottom, pixels left to right, and each pixel has four consecutive bytes. A group has exactly `width * height * 4` bytes, with no row padding or mip data. Unused texels/channels contain exterior zero.

Schema 1 uses linear `RGBA8_UNORM`, bilinear clamp sampling, and the FreeType scalar distance encoding. No sRGB conversion, premultiplication, alpha processing, lossy compression, or ordinary image mip generation touches these payloads. `FontAtlas` owns raw bytes because the current general texture pipeline uses compressed transport and a complete mip chain.

Every drawable glyph records its source glyph ID, group/channel, integer sampled rectangle, padded baseline-relative plane bounds, and inspection advance. Plane bounds use font design units with positive Y downward and come from the SDF bitmap's actual bearings and dimensions, including its spread. The extra guard lies outside the sampled rectangle. Same-channel guarded rectangles cannot overlap; different channels may share coordinates.

The `.nwb` also records schema/encoding tokens, a typed `AssetRef<Font>`, exact font SHA-256, face index, source glyph count, units per em, vertical metrics, raster settings, group hashes/lengths, and positioning-table sidecars/hashes. Unknown fields, malformed hashes, unsupported tokens, incomplete glyph lists, unsafe paths, duplicate basenames, invalid metrics, altered payloads, and inconsistent bounds fail admission. See a [small runnable metadata fixture](../tests/integration/assets_font_atlas/fixtures/atlas.nwb) or the full default atlas documents for the implemented syntax.

The binary codec encodes values explicitly in little endian. It has a fixed 164-byte header, canonical sequential sections, 52-byte glyph records, 44-byte group headers followed by exact pixels, and 40-byte positioning headers followed by exact table bytes. Counts/ranges are checked before allocation; reserved fields must be zero and the final section must consume the exact asset length. Failed parsing, decoding, or loading leaves previously published output intact.

## 5. Font identity, shaping, and kerning

`ValidateFontAtlasSourceMatch` compares typed font identity, exact original byte SHA-256, face index, units per em, glyph count, and the complete original positioning-table set and bytes. Font installation attaches an atlas only after that match and native face validation. An unavailable or mismatched atlas leaves the selected face usable through native coverage.

`kerning_mode = "opentype_tables"` exports the exact original `kern`, `GPOS`, and `GDEF` table bytes. This preserves legacy pairs, class matrices and class-zero behavior, script/language/feature selection, lookup order, contextual structures, and device data without a quadratic glyph-pair expansion. It is lossless table transport, not a normalized pair list or an evaluated shaping result.

The bounded validator checks admitted table versions/roots, legacy format-0 pairs, supported GPOS single/pair adjustments and extensions, coverage/classes, value/device records, and relevant GDEF roots. Other positioning families retain their original opaque bytes after bounded root checks. This validator is not a complete OpenType sanitizer.

HarfBuzz shapes the matching original font and remains the authority for advances, offsets, clusters, ligatures, and fallback face selection. Atlas inspection advances and exported positioning tables are never applied again. Painting looks up each actual shaped face/glyph ID and adds its padded plane geometry to the shaped baseline position. A label may mix SDF and coverage images while preserving all layout positions.

Installed font versions copy their source bytes and needed atlas rendering records/images. Existing layouts and paint snapshots retain strong versions after asset release, font replacement, or DPI changes. Typed asset references identify assets; they do not retain loaded versions by themselves.

## 6. SDF reconstruction and qualified scale

For `freetype_sdf_u8_v1`, byte 128 represents zero distance and positive distance is inside. The shader selects one channel and computes:

```text
distance = (sample * 255 - 128) * spreadPixels / 128;
coverage = saturate(0.5 + distance / max(fwidth(distance), 0.001));
output = premultipliedVertexColor * coverage;
```

The minimum derivative width keeps near-zero FP32 residuals stable in flat fields. The shared push ABI is 48 bytes with named channel/spread/encoding fields and explicit layout assertions. Each draw uses one image version, one channel, one encoding, and one clip; adjacent compatible commands batch in painter order.

The runtime chooses SDF when the physical raster size is within **0.5..1.5 times the bake ppem**. Physical size is `ceil(logicalFontSize * max(pixelScaleX, pixelScaleY))`. Outside that interval, the same shaped face uses native coverage. For the shipped 32-ppem atlases, this means 16..48 physical pixels per em. Logical layout remains independent of that image-source choice.

This interval follows measured output, not an unlimited-zoom promise. Thirty-six comparisons of Latin `A`, `o`, `e`, and Korean `한` across multiple zoom and DPI inputs stayed within one pixel of supersampled native edge displacement; maximum mean coverage error was 0.0068433573. The broader candidate magnification range failed the one-pixel edge gate at sharp corners, so it was not adopted. Other fonts still require visual qualification. Higher bake resolution or future MSDF support may widen useful magnification; ordinary SDF mips need a separate design.

## 7. GPU ownership and graph integration

Paint preparation atomically admits combined R8 and SDF image bindings before adding glyph quads. A capacity failure cannot partially publish image upgrades. Immutable SDF bindings identify source/font generation, atlas identity, group, dimensions, encoding, spread, and exact pixel content.

GPU preparation creates the linear RGBA texture, queues the full upload, and creates the persistent sampled descriptor before graph draw recording. An accepted upload retains its authoritative native readiness token across descriptor publication failure/retry, so retry does not repeat the upload. Native command ownership keeps upload images alive through physical completion. Accepted frames retain texture, descriptor, CPU binding, and resource versions through the final compositor consumer.

Each snapshot-local SDF image has a distinct graph identity and explicit ready/read import. R8 and SDF versions share a bounded 64-image cache. Completed frame owners retire before new descriptor allocation. When the requested working set would exceed cache capacity, unused logical keys are pruned across both image kinds while requested keys and in-flight versions remain retained. This avoids repeated uploads for an unchanged full-budget working set. The existing independent UI upload/raster branch, premultiplied `RGBA16_FLOAT` layer, and final SDR/HDR composition contracts remain in use.

## 8. Validation and remaining gates

Windows ARM64 / Clang, `opt`, validation for this increment:

- Production libraries, utility, asset builder, UI-only smoke, and Testbed build. Their pipelines cook 119 engine-only and 136 Testbed assets, including both default atlases and the updated shader.
- 42 UI tests, 12 font-atlas asset tests, six bake tests, eight source-font asset tests, and 12 affected graphics/composition/shader checks pass.
- Ten real command-line integration cases and all 46 existing repository launcher tests pass. Integration covers full default generation, deterministic repeat output, channel/group packing, kerning-table preservation, malformed inputs, capacity failure, overwrite refusal, and preservation of an existing package.
- The separate quality test passes all 36 comparisons and verifies that both real defaults attach to their shaping faces.
- Both GPU-validation acquired-backbuffer smoke tests pass: 25 numeric probes per capture across three captures, plus Latin/Korean/clipping/edge checks. The resized Testbed capture at 901 by 607 was visually inspected.
- All 55 unique affected production and test translation units pass Linux x86_64 syntax compilation using actual Linux libc/libstdc++/FreeType/HarfBuzz/platform headers. Logs and source hashes are retained in the disposable local sysroot artifacts. This Windows host has no Linux runtime, so Linux linking, native baking equivalence, and GPU execution remain unqualified.

The following remain separate increments: interactive state/layout/focus/input, controls and editing, OS IME for Win32/Linux, additional font/scale qualification, live HDR presentation, native Linux execution, and eventual ImGui removal. The raw table export remains the declared kerning representation; a normalized positioning evaluator would require its own format and scope.
