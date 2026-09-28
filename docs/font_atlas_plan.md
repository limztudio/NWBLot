# Offline RGBA font atlas proposal

Status: proposed and unimplemented. This document defines a possible next rendering increment; it does not describe a supported asset format or an available utility.

The native font, HarfBuzz shaping, text layout, and immutable R8 coverage work remains the foundation. Its verification continues independently. The proposed atlas supplies another glyph image source and retains native coverage as a fallback. Adopting this design does not retire shaping, introduce a second layout engine, or switch the current application to an unfinished renderer.

## 1. Intended result

Add `utilities/font_atlas`, a Win32/Linux-neutral C++ command-line generator, and `impl/assets_font_atlas`, a CPU asset domain with the typed asset `FontAtlas` and metadata keyword `font_atlas`.

The generator reads a supported static `.ttf` or `.otf` face, renders a scalar signed distance field for each selected glyph ID, and packs four independent atlas pages into the R, G, B, and A channels of one RGBA8 image. More than four pages produce additional RGBA images. A glyph samples exactly one channel of one image. This is scalar SDF packing; it does not use the median-of-RGB reconstruction of MSDF.

The generator writes:

- `atlas.nwb`: readable face identity, encoding, page groups, glyph metrics, and explicitly scoped kerning metadata.
- `atlas_000.rgba`, `atlas_001.rgba`, and subsequent groups: lossless interleaved RGBA8 bytes with no container header.
- An optional preview image and generation report. Preview output is diagnostic and is never the runtime payload.

The cooker validates metadata and payloads and emits one versioned `FontAtlas` binary asset. Runtime loading requires no source image decoder, FreeType SDF rasterizer, or filesystem sidecar access. HarfBuzz still reads the matching `Font` asset to shape text.

Packing four channels uses the same uncompressed byte count as four equal-size R8 pages. Its expected benefits are fewer texture objects/descriptors and a convenient authored package; it does not provide fourfold compression. A glyph fetch also brings channels belonging to other pages, so measure the bandwidth tradeoff.

## 2. Why use a separate lossless asset

The existing texture pipeline is appropriate for ordinary images but has different contracts:

| Existing path | Relevant constraint |
| --- | --- |
| `utilities/tex_conv/encode.cpp` | LDR input is encoded as UASTC; `--linear` controls color interpretation, not losslessness. Ordinary filtered mip generation is enabled. |
| `impl/assets_texture/loader.cpp` | The LDR loader selects ASTC, BC7, or decoded RGBA8 from the compressed source. Selecting RGBA8 does not recover pre-compression distances. |
| `impl/assets_texture/runtime.cpp` and `cook_metadata.cpp` | Validation requires a complete mip chain. |
| `global/texture_payload.h` | The current payload formats are UASTC LDR and HDR; raw RGBA8 is not implemented. |

`FontAtlas` will therefore own raw RGBA8 distance bytes directly. It references its source face through `Core::Assets::AssetRef<Font>`; it does not pretend these bytes are a currently supported `Texture` asset. GPU residency belongs to `impl/ui/gpu`, outside the CPU asset codec.

Schema 1 uses one base mip, linear `RGBA8_UNORM`, and bilinear clamp sampling. No sRGB conversion, premultiplication, opacity optimization, lossy compression, alpha-channel replacement, or ordinary image mip generation may touch the payload. A is a fourth distance page, not transparency for RGB.

A future general-purpose lossless texture transport could absorb the pixel payload. That requires explicit changes to the shared payload format, metadata, mip policy, runtime codec, decoder, and loader; it is outside this bounded increment.

## 3. Ownership and module boundaries

Proposed files and responsibilities:

| Domain | Files or module | Responsibility |
| --- | --- | --- |
| Generator | `utilities/font_atlas/main.cpp`, `command_line.cpp` | Logger/arena ownership, validated arguments, portable paths, exit status, and publication policy. |
| Generator | `font_source.cpp`, `glyph_set.cpp` | Match the supported `Font` admission rules, select a static face, calculate content identity, enumerate glyph IDs. |
| Generator | `sdf_raster.cpp` | FreeType setup, explicit render settings, SDF bytes, baseline-relative padded bounds, and raster failure reporting. |
| Generator | `atlas_pack.cpp` | Deterministic page packing and RGBA group interleaving. |
| Generator | `kerning.cpp` | Bounded legacy pair/GPOS pair and class extraction, preserving declared script/language/feature scope and lookup order. |
| Generator | `asset_writer.cpp`, `preview.cpp` | Canonical metadata, raw payloads, checksums, optional previews, and publication after all outputs are ready. |
| Generator | `CMakeLists.txt`, `launch.py` | Normal utility registration, using the existing launcher conventions. |
| CPU asset | `impl/assets_font_atlas/asset.h`, `runtime.cpp`, `binary_payload.h` | Typed identity, immutable records/bytes, shared validation, versioned binary decoding. |
| Cooker | `cook.h`, `cook_metadata.cpp`, `cook.cpp`, `volume_entry.cpp` | Strict metascript schema, adjacent payload paths, source dependencies, validated binary encoding, registrars. |
| UI text | `impl/ui/text/` | Resolve a matching atlas after shaping, calculate geometry, select native coverage fallback. |
| GPU | `impl/ui/gpu/renderer_sdf_resources.*` | Immutable image/descriptor versions, upload readiness, graph imports, and retirement. |
| Shader | `impl/assets/graphics/ui/` | SDF material, channel selection, derivative antialiasing, shared CPU/shader ABI. |

Use project scalar/container/path types and caller-owned arenas. The generator uses vendored FreeType and existing asset/schema facilities, not GDI, DirectWrite, fontconfig, system font discovery, or OS rendering APIs. Font files and output paths are explicit inputs. Win32 and Linux use the same algorithm and byte format. Native execution on both systems is an exit gate, not an implication of cross-target compilation.

`impl/ecs_ui` continues to borrow UI/text/render services. It does not own font decoding, atlas generation, kerning extraction, or GPU retirement.

## 4. FreeType SDF prerequisite

The vendored FreeType 2.14.3 headers expose `FT_RENDER_MODE_SDF`, but the current package omits `src/sdf`. `3rd_parties/freetype/CMakeLists.txt` generates a restricted module list with smooth and monochrome renderers only.

Before implementing the generator:

1. Restore the SDF sources from the exact revision recorded in `3rd_parties/freetype/nwb_update.txt`.
2. Add the SDF amalgamation and register `ft_sdf_renderer_class` and `ft_bitmap_sdf_renderer_class` in the generated module list.
3. Update the vendor manifest to describe the restored sources and preserve the upstream licences.
4. Verify an actual `FT_Render_Glyph(..., FT_RENDER_MODE_SDF)` call and the configured spread; the presence of the enum is not proof of runtime support.

Start with unhinted scalable outlines and explicit bake size/spread. Outline SDF can fail or show artifacts on small features and intersecting contours, as documented in the vendored `ftdriver.h`. Offer an explicit bitmap-to-SDF mode for problematic outlines; record the selected mode per build or per exceptional glyph. Never silently substitute another algorithm. A failed glyph aborts generation unless a documented missing-glyph policy explicitly permits it.

Suggested qualification settings are 64 pixels per em, spread 8 source pixels, and one additional guard texel. These are starting settings to validate, not universal quality guarantees.

## 5. Font identity, glyph coverage, and shaping

A `FontAtlas` identifies one exact static face using all of:

- A typed `AssetRef<Font>` for the logical asset identity.
- SHA-256 of the exact original font bytes and the face index.
- Units per em and the source glyph count, validated against the resolved face.
- The atlas schema, generator version, raster settings, and payload content identity.

The runtime font generation remains useful for caching and retirement, but a generation counter alone cannot prove that independently generated atlas data matches font bytes. Reject a content mismatch before publishing any atlas binding. A font or atlas hot reload creates a fresh immutable version; existing snapshots keep their old matching versions.

The initial glyph policy is `all`: enumerate the face's glyph IDs, including glyph 0. A drawable glyph must have one atlas record. Space and other empty-outline glyphs have explicit nondrawable records and consume no packed rectangle. The source font remains authoritative for advances and shaping.

A Unicode-to-glyph table is optional diagnostic metadata. It is not the lookup key because GSUB may select ligatures, contextual forms, alternates, and glyphs without a direct Unicode mapping. Any later subset mode must compute substitution closure for its declared scripts/languages/features, preserve glyph IDs or map them explicitly, and report its exact coverage. Merely baking the input text's Unicode cmap entries is insufficient. Schema 1 avoids that complexity by baking all glyph IDs and failing clearly when capacity is insufficient.

Runtime flow:

1. HarfBuzz shapes the text with the selected font and explicit direction/script/language.
2. Existing font fallback resolves missing clusters; the resulting glyph retains its actual `SharedFontFace`.
3. Match that face to a `FontAtlas` using both typed identity and content identity.
4. Look up the shaped glyph ID, then combine its padded plane bounds with the shaped baseline position and offset.
5. If no matching atlas or baked glyph is available, use native coverage for the same selected face. If that path also fails, return an explicit text/paint failure.

Do not reinterpret a glyph ID through a different face or switch fonts after shaping. Mixed atlas/native output may occur in one run without changing its advances or cluster ranges.

## 6. Packing, metrics, and byte layout

Each logical page is a separate scalar plane of the same configured extent. Logical page `p` maps to RGBA texture group `p / 4` and channel `p % 4`, ordered R, G, B, A. Glyph rectangles may overlap across channels; rectangles in the same channel must not overlap, including their guards.

A group payload has exactly `width * height * 4` bytes. Rows run top to bottom; pixels run left to right; each pixel stores consecutive R, G, B, A bytes. Unused channels/texels contain encoded exterior distance. No row padding or mip data is implied. Metadata records each group's byte count and SHA-256; the binary asset copies exact bytes and retains explicit offsets/lengths.

For each drawable glyph store:

- Glyph ID, group index, and channel index.
- Integer `(x, y, width, height)` for the sampled SDF bitmap rectangle, excluding its extra packing guard.
- Padded plane bounds `(left, top, right, bottom)` in font design units, relative to the glyph origin on the baseline, with positive Y downward.
- Optional unshaped horizontal advance for inspection, explicitly labelled as font units.

The plane bounds include the SDF spread around the outline, so the quad renders the distance field's exterior instead of clipping it to the original ink bounds. Derive them from the rendered bitmap's actual bearings and dimensions; do not assume the normal grayscale bitmap and SDF bitmap have identical dimensions.

UVs are pixel-boundary coordinates divided by the group extent. Logical geometry scales design units by `requestedFontSize / unitsPerEm`, then adds the HarfBuzz placement. The caller's normal display scale and clip handling still apply. No ppem-specific integer rounding enters logical advances.

Packing is deterministic: order candidates by descending padded height, then descending width, then ascending glyph ID; use a specified shelf algorithm with fixed page/channel/group traversal. Rotate no glyphs in schema 1. Allocate another group when the fourth plane fills. An individually oversized glyph or exhausted capacity is an error that reports the glyph ID, requested footprint, configured limits, and suggested remedies. Do not discard glyphs, wrap channels, or downscale only the overflowing glyph.

## 7. Kerning metadata and its limits

HarfBuzz remains the sole authority for shaped advances and offsets. Atlas pair metadata is for inspection and potential future simple text consumers; current shaped rendering must never apply it again.

Export supported legacy `kern` format-0 pairs and OpenType `GPOS` pair adjustments in design units. Modern fonts may store their useful kerning entirely in GPOS, so a legacy-only export does not meet the default-font gate. GPOS PairPos format 1 has explicit pairs; format 2 uses glyph classes. Preserve class maps/matrices instead of expanding every possible glyph pair. Retain coverage, script/language/feature selection, lookup order, and both glyphs' placement/advance adjustments. A supported positioning-extension lookup may reference the same pair formats. These structures follow the [OpenType GPOS specification](https://learn.microsoft.com/en-us/typography/opentype/spec/gpos).

Enumerate supported records under strict input/count bounds rather than querying every glyph-count-squared pair. Canonically sort explicit pair records within their original lookup while preserving ordered lookups and class-zero behavior. Report unsupported contextual/device/variation/lookup forms explicitly; do not present their omission as a complete table. A strict export option fails when its requested scope cannot be represented.

The vendored FreeType headers describe only limited GPOS pair extraction when its optional GPOS-kerning build option is enabled; that option is currently disabled. Even basic pair extraction cannot express contextual positioning, script/language selection, feature settings, mark attachment, cursive attachment, or general placement adjustments. An empty legacy `kern` export therefore does not imply that the font has no kerning or positioning. Preserve the original source font so HarfBuzz can execute its OpenType tables.

Metadata records the extraction mode, completion status for that declared scope, ordered lookups, explicit pair records, and any class definitions/matrices. A compact pair-only example appears below; the full GPOS class schema must be finalized before implementation. Do not label these records complete OpenType shaping. The bundled Latin font's `AV` adjustment is a required GPOS comparison against HarfBuzz, alongside a synthetic legacy-pair fixture and a class-zero/exception fixture.

## 8. Proposed declarative metadata

This example illustrates the proposed schema. Hash placeholders and abbreviated glyph lists make it non-runnable. No current parser accepts `font_atlas`.

```text
font_atlas asset;

asset.schema_version = 1;
asset.font = "project/ui/fonts/body";
asset.font_sha256 = "<64 lowercase hexadecimal digits>";
asset.face_index = 0;
asset.units_per_em = 1000;
asset.source_glyph_count = 200;
asset.glyph_policy = "all";
asset.bake_ppem = 64;
asset.sdf_renderer = "outline";
asset.spread_pixels = 8;
asset.distance_encoding = "freetype_sdf_u8_v1";
asset.guard_texels = 1;
asset.payload_format = "rgba8_linear";
asset.mip_count = 1;
asset.groups = [
    {
        "extent": [1024, 1024],
        "data": "atlas_000.rgba",
        "byte_count": 4194304,
        "sha256": "<64 lowercase hexadecimal digits>"
    }
];
asset.glyphs = [
    {"glyph_id": 3, "drawable": false, "advance_units": 260.0},
    {
        "glyph_id": 36,
        "drawable": true,
        "group": 0,
        "channel": 2,
        "rect": [1, 1, 52, 64],
        "plane_bounds_units": [-125.0, -875.0, 687.5, 125.0],
        "advance_units": 640.0
    }
];
asset.kerning_mode = "legacy_kern_format0";
asset.kerning_complete_for_mode = true;
asset.kerning_pairs = [
    {"left_glyph_id": 36, "right_glyph_id": 57, "x_advance_units": -40.0}
];
```

The complete output must enumerate every glyph under the `all` policy. The bounds example describes the padded bitmap, not a claim about a particular bundled font. Paths are adjacent payload basenames; reject absolute paths, directories, traversal, duplicate group files, and unknown metadata fields. The source font reference is an asset identity, not a source-file path.

The cooked binary needs magic/version/reserved fields, fixed-layout count/offset records, typed font identity, exact hashes, and immutable data sections. Perform overflow-safe range/count checks before allocation, require nonoverlapping canonical sections and exact total size, reject nonfinite metrics and invalid channel/rectangle values, and publish only after complete validation. A failed load preserves the previous usable asset.

## 9. Zoom-aware SDF sampling

FreeType's documented encoding is `byte = clamp(128 * (distance / spread + 1), 0, 255)`, with positive distance inside the glyph. Consequently the zero-distance sample is `128 / 255`, not exactly `0.5`.

Conceptual shader for this encoding:

```text
sample = texture.SampleLevel(linearClampSampler, uv, 0)[channel];
distance = (sample * 255 - 128) * spreadPixels / 128;
width = max(fwidth(distance), epsilon);
coverage = saturate(0.5 + distance / width);
output = premultipliedVertexColor * coverage;
```

Channel and encoding are uniform within a draw. Use a shared CPU/Slang ABI for the channel and any SDF parameters; do not reinterpret unused padding without naming and checking the new contract. Keep the existing coverage material separate. UI output remains linear premultiplied RGBA16F, with the existing final SDR/HDR composition unchanged.

Screen derivatives adjust the edge transition with display scale and zoom. They do not restore outline detail absent from the stored field. Scalar SDF can round sharp corners; extreme magnification exposes the field grid/quantization, and extreme minification loses thin features. Ordinary downsampled atlas mips are outside schema 1 because distance averaging and cross-glyph bleeding need a separate treatment.

Initially qualify 0.5x, 1x, 2x, and 4x relative to the bake size, with 1x, 1.5x, and 2x display scales. Publish the range supported by measured output. Outside that range, the application may select native coverage or a differently baked atlas; there is no unlimited-zoom guarantee.

## 10. Snapshot, GPU, and retry contracts

Add an immutable SDF atlas-version binding to paint snapshots rather than changing the meaning of the existing R8 `GlyphPage`. The binding retains its exact atlas metadata/font match and identifies the selected RGBA group. A draw adds group/channel/encoding selection; batching requires matching material, immutable image version, channel, parameters, and clip, and only combines adjacent compatible commands.

`GpuSdfAtlasVersion` owns each created texture, persistent sampled descriptor, strong CPU atlas owner, and authoritative upload completion. Create/upload images during preparation, never while recording a draw task. Use a linear RGBA8 view, import exact physical-queue readiness into every graph consumer, and retain versions through pending recording, accepted producer prefixes, and final output completion.

A failed prepare may have accepted uploads. Preserve those tokens and images and retry only unfinished setup. Cache replacement cannot overwrite an image still referenced by a frame. Eviction of an unreferenced but uploading image must be justified by native upload retention or postponed until the recorded physical completion. Descriptor allocation/publication failure must not erase accepted-upload bookkeeping.

The existing offscreen UI branch and final scene/UI join remain the integration point. No new scene dependency, global service lookup, or ECS-owned asset uploader is required.

## 11. Capacity and reproducibility

Proposed initial hard limits, enforced by generator, cooker, runtime codec, and paint admission:

| Quantity | Initial limit |
| --- | --- |
| Source bytes/face type | Reuse current `Font` schema: at most 32 MiB, one supported static SFNT face. |
| Glyph records | At most 65,535, additionally matching the admitted face's actual glyph count for `all`. |
| Group dimensions | Nonzero, at most 2048 by 2048. Default 1024 by 1024. |
| RGBA groups per atlas | At most 8: 32 logical scalar pages. |
| Decoded RGBA payload | At most 128 MiB per atlas, including all groups. |
| Exported kerning pairs | At most 262,144; reject excess instead of truncating. |
| Bake size/spread | Positive bounded integers; initially 16..256 ppem and 2..32 spread pixels, subject to native property admission. |
| Unique images in one paint snapshot | At most 64 across SDF groups and dynamic coverage pages, admitted atomically before emitting a label. |

These are proposal values to confirm against the bundled font fixtures. Atlas capacity limits do not silently change the existing dynamic coverage service's page/entry limits. Keep limits in shared constants rather than separate utility/runtime literals.

Record the source hash, generator revision, FreeType revision, selected raster algorithm, all numeric options, and canonical packing order. Use locale-independent number formatting, stable record ordering, explicit byte order in the cooked binary, and zero-initialized padding. Keep timestamps and host paths out of content identity. Repeat builds on one platform must be byte-identical; compare Win32/Linux outputs and document or eliminate any native-raster differences before claiming cross-platform byte reproducibility.

Validate all output in a staging location before publishing. Write payloads before the final metadata publication, use content-addressed payload names when replacing existing packages, and keep the previous metadata valid if publication fails. Require an explicit overwrite option and do not delete unrelated files in the output directory.

## 12. Implementation order and exit gates

1. **Generator and native SDF qualification.** Restore pinned SDF modules; render the default fixtures; establish stable settings; add bounded deterministic packing and RGBA output. Exit when repeated generation produces identical payload hashes, more than four pages correctly creates another group, and capacity failures leave the previous package usable.
2. **Asset contract.** Implement strict metadata and binary codecs with typed font identity and exact payloads. Exit when cook/load round trips preserve every channel byte and glyph record, malformed/count/overflow/path/hash/font-mismatch cases fail atomically, and the new asset registrar participates in ordinary package builds.
3. **Text and GPU consumption.** Resolve atlases after shaping, emit immutable bindings, upload/import images, and add the SDF material. Exit when existing shaped advances/offsets/clusters are unchanged, ligature and fallback glyph IDs select their own face's images, and clipped SDF draws retain correct UVs and premultiplied tint.
4. **Measured rendering qualification.** Run actual Win32 and Linux utility tests, the supported native renderer routes, and zoom/DPI readbacks. Exit when the following checks pass and their artifacts/settings are retained.

Required measurable checks:

- Synthetic images with four distinct channel patterns select the requested channel only; encoded exterior/interior values produce coverage 0/1 within readback tolerance, and half coverage with half-opacity tint gives alpha 0.25 within 0.01.
- The zero-distance value is tested at `128/255`; no sRGB conversion, alpha-as-opacity treatment, or second tint-alpha multiplication occurs.
- Default Latin ligatures/kerning, a combining-mark case, an explicitly shaped RTL case, and adjacent clusters requiring different fallback faces preserve the existing HarfBuzz glyph IDs, advances, offsets, and UTF-8 cluster ranges exactly.
- Whitespace produces advances with no bitmap allocation; native coverage handles a deliberately omitted/mismatched atlas without changing the selected font or layout.
- UV clipping and batches cover changes in channel, group, material, and clip. A rejected 65th unique image leaves existing geometry and bindings unchanged.
- Injected prepare/descriptor/graph failures retain accepted uploads. Atlas reload and cache eviction during multiple in-flight frames never reuse live descriptors or mutate sampled texels; exact physical-queue retirement is observed.
- At each qualified zoom/DPI setting, compare against a high-resolution grayscale reference. Record coverage error and edge displacement; require no atlas bleeding, missing glyphs, or clipped SDF padding, and an initial maximum edge displacement of 1 screen pixel on the named fixture set. If a setting fails, narrow the published range or revise the bake; do not waive the check as unlimited scaling.
- Capture cold upload bytes/time, warm upload bytes, resident bytes, descriptor count, draw count, and UI raster time. Warm rendering of an unchanged atlas must upload zero atlas bytes. Compare costs to native coverage without assuming RGBA packing is automatically faster.

The native coverage path remains available after these gates. Removing it, adding MSDF, outline/shadow effects, automatic mixed-direction paragraph analysis, font collections/variations, color emoji, advanced mip generation, and arbitrary Unicode subset closure are later scoped decisions.

## 13. Repository references

- [Custom UI plan](custom_ui_plan.md)
- [Current font asset contract](../impl/assets_font/README.md)
- [Native face/shaping implementation](../impl/ui/text/font.cpp)
- [Text service and coverage paint](../impl/ui/text/service.cpp)
- [Current immutable R8 page contract](../impl/ui/text/glyph_page.h)
- [Current GPU page lifetime/upload implementation](../impl/ui/gpu/renderer_glyph_resources.cpp)
- [UI material shader](../impl/assets/graphics/ui/ps.slang)
- [Texture transport description](../utilities/tex_conv/README.md)
- [Texture payload validation](../impl/assets_texture/runtime.cpp)
- [FreeType build module list](../3rd_parties/freetype/CMakeLists.txt)
- [Pinned FreeType package provenance](../3rd_parties/freetype/nwb_update.txt)
- [Vendored SDF spread/renderer documentation](../3rd_parties/freetype/include/freetype/ftdriver.h)
- [Vendored SDF encoding and kerning API documentation](../3rd_parties/freetype/include/freetype/freetype.h)
