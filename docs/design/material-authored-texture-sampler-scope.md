# Material-authored texture and sampler support — historical fixture delivery plan

**Status:** superseded. This document preserves the original fixture-only milestone and its design
rationale. The implemented material path now distinguishes built-ins from project texture assets in
the cooked resource metadata; it does not infer texture assets from fixture names or path prefixes.

## Current implementation

- `[fixture("builtin/material_fixture/checker_rgba8")] texture2d` and
  `[fixture("builtin/material_fixture/linear_clamp")] sampler` remain explicit built-in resources.
- `[texture_asset("project/path/to/texture")] texture2d` is an explicit project `Texture` asset
  reference. It is valid only for a `texture2d` field in a `[material_constant]` block.
- The cooked material records the resource kind, source (`Builtin` or `TextureAsset`), identity, and
  constant-word offset. Material binaries use the current single `MTL7` layout and reject older or
  malformed resource records.
- `RendererMaterialSystem` lazily creates built-ins, loads `Texture` assets through
  `TextureAssetLoader`, caches them by asset identity, registers their global bindless slots, and
  releases both cache classes during renderer invalidation.
- Texture cooking, UASTC payload validation, ASTC/RGBA8 upload selection, and the UV-mapped sphere
  smoke scene are implemented separately in `impl/assets_texture` and `tests/smoke`.

The remaining sections are the historical planning record for the original fixture milestone.

## 1. Goal

Allow a surface-authored material to declare and use a sampled 2D texture and a sampler without
adding any pipeline-local descriptors. The delivered first slice carries fixture identities at cook
time, resolves them to the existing global descriptor heap at runtime, and exposes the resulting
resources to every consumer of its surface hook. A later asset-import slice will replace those fixed
identities with general material asset references.

The first slice should support only immutable, per-material `Texture2D<float4>` and `SamplerState`
parameters. Numeric `material_mutable` fields remain as they are; per-instance texture/sampler
overrides are a later feature.

## 2. What already exists

- The global descriptor heap already exposes sampled 2D images at set 8 / binding 0 and samplers at
  set 9 / binding 0 through `NwbHeapSampledImage2D()` and `NwbHeapSampler()`.
- Every material mesh, compute-emulation, AVBOIT, CSG, software-tracing, and hardware-tracing
  pipeline already attaches those two heap layouts. No pipeline-layout migration is needed.
- A material's constant bytes are copied into the regular draw typed-word buffer and into the
  shadow/trace typed-word buffer. The latter is reused by transparent shadows, GI, and caustics.
  A material constant byte offset plus an instance's mutable byte offset already reconstructs the
  same authored surface in every path.

The gaps are deliberately above the descriptor heap:

- `.bind` accepts only scalar/vector numeric types today.
- A cooked `Material` serializes only numeric typed-layout data and bytes; descriptor slots cannot
  be serialized because they are device-lifetime values.
- There is no texture or sampler asset type, image importer, runtime image upload path, or material
  resource cache. Existing textures are renderer/UI-owned RHI allocations.

## 3. Delivered fixture milestone

The initial implementation deliberately has a tiny static catalog:

- `[fixture("builtin/material_fixture/checker_rgba8")] texture2d` resolves to a shared 2x2 RGBA8
  checker texture.
- `[fixture("builtin/material_fixture/linear_clamp")] sampler` resolves to a shared linear-clamp
  sampler.
- Both resource kinds are valid only in `[material_constant]` blocks. Their cooked resource records
  store block/field identity, fixture identity, kind, and the four-byte constant-byte offset.
- The renderer registers the shared RHI resources in the global sampled-image and sampler heaps,
  patches a working copy of each material's constants with the resulting slots, and restores the
  unpatched copy when descriptors are invalidated.
- Generated Slang keeps opaque resources out of aggregate structs/loaders and emits standalone
  `Texture2D<float4>` / `SamplerState` accessors instead.

This validates the complete material ABI, including the normal raster, AVBOIT, CSG, and trace
surface paths, without pretending that a general image-asset pipeline already exists.

## 4. Follow-up general-resource scope

### In scope

- `texture2d` fields in a `[material_constant]` block, emitted as `Texture2D<float4>` in generated
  Slang.
- `sampler` fields in a `[material_constant]` block, emitted as `SamplerState`.
- One texture asset reference and one sampler asset reference per declared field, replacing the
  fixture-only identities above.
- Static 2D sampled textures only; an authored resource is shared by all instances of the material.
- Generated accessor functions usable from the normal G-buffer surface, AVBOIT, CSG cap/receiver
  paths, transparent shadow dispatch, surfel GI, and caustics.
- Runtime deduplication and global-heap registration with normal deferred descriptor retirement.

### Out of scope

- `Texture2DArray`, `Texture3D`, cube textures, storage images, comparison samplers, texture arrays,
  bindless resource arrays authored by a material, and sampler feedback.
- Resource fields in `[material_mutable]` blocks or `MaterialInstanceComponent` resource overrides.
- Pipeline-local bindings, fallback descriptor sets, or a second material descriptor table.
- Streaming, hot reload, virtual texturing, mip generation policy beyond the selected texture-asset
  input contract, and arbitrary external image-format support.

Keeping the first slice static matters: it keeps a resource's ownership with the material cache and
allows the current typed-byte deduplication to continue unchanged. Per-instance replacement needs a
separate cache key, change tracking, and trace-context update path, so it should not be hidden inside
the initial feature.

## 5. Proposed general-resource data path

```text
.bind resource field + material .nwb resource reference
                  │
                  ▼
material cook validates and serializes typed resource metadata (paths, kind, block/field)
                  │
                  ▼
renderer material cache loads texture/sampler assets, creates RHI resources, registers heap slots
                  │
                  ▼
renderer patches the resolved uint slots into a copy of the material constant typed bytes
                  │
                  ▼
existing draw and trace uploads carry those words unchanged
                  │
                  ▼
generated accessor loads the slot, then fetches from global set 8 or set 9
```

The important boundary is that the cooked asset stores references, never a `GpuDescriptorHandle` or
its slot. Slots are device-local and only exist after the renderer resolves the referenced resource.

Reusing the typed-word payload avoids new push constants, an `InstanceGpuData` layout change, and a
`NwbRtInstanceMaterial` / `RayTraceMaterialContextSlots` ABI change. A resource field occupies one
four-byte constant word, but has its own field kind so it cannot be confused with an author-supplied
`uint`.

## 6. Authoring and cook contract

The delivered fixture spelling is intentionally small:

```text
[material_constant]
struct NwbSurfaceMaterial {
    [fixture("builtin/material_fixture/checker_rgba8")]
    texture2d base_color_map;
    [fixture("builtin/material_fixture/linear_clamp")]
    sampler base_color_sampler;
    half4 base_color;
};
```

The general-resource follow-up would replace each fixture name with a typed material asset reference,
for example a texture asset path for `surface.base_color_map` and a sampler asset path for
`surface.base_color_sampler`. The cooker must reject missing, duplicate, mismatched, mutable, or
unsupported resource fields; it must also reject numeric parameter syntax for a resource field.

Add resource-layout metadata beside the current typed layout rather than extending the numeric
`MaterialParameterValueType` enum. Each record needs at least:

- block and field identity;
- resource kind (`SampledImage2D` or `Sampler`);
- the referenced asset virtual path; and
- the four-byte constant-word offset to patch, or enough layout identity to validate and derive it.

The material binary needs a new version/magic and strict bounds/type validation on load. The layout
hash must include resource-field kind and placement so two interfaces that differ only by `uint` versus
`texture2d` cannot be treated as compatible.

Before committing the generated interface shape, add a shader-cook probe that proves Slang accepts
resource-valued generated accessors and the desired block-loader behavior. If opaque resource handles
cannot safely be struct members in the current compiler path, preserve the scalar numeric block loader
and generate standalone texture/sampler accessors instead.

## 7. Runtime and shader contract

`RendererMaterialSystem::createMaterialSurfaceInfo()` is the natural ownership point:

1. load or find each referenced texture/sampler asset (or, in the delivered slice, resolve its fixed
   fixture);
2. obtain or create a cached RHI texture/sampler and its global heap descriptor;
3. retain the RHI object and descriptor handle in a renderer-owned resource cache;
4. copy the material's constant typed bytes and patch the resource-word offsets with heap slots; and
5. fail surface creation cleanly if a referenced resource cannot be resolved.

The cache should key textures by their cooked asset identity and samplers by their canonical cooked
description/asset identity. It owns heap handles until renderer cache eviction; freeing a handle must
continue to use the heap's existing in-flight retirement rules.

Generated resource accessors should load the patched `uint` word and use dedicated non-uniform heap
helpers, even when a raster draw happens to be uniform. Trace invocations can hit different materials
in one wave. Vulkan's existing `shaderSampledImageArrayNonUniformIndexing` feature covers sampled
images, samplers, and combined image samplers, and is already required and enabled by the renderer;
there is no separate sampler-only non-uniform-indexing feature.

## 8. Texture/sampler asset prerequisite

There is currently no texture asset pipeline. The material work therefore needs two bounded layers:

1. **Resource plumbing:** material resource metadata, code generation, runtime caching, descriptor
   registration, typed-word patching, and a deterministic test texture/sampler.
2. **Author-facing texture import:** texture asset metadata, source-image decoding, format/color-space
   policy, mip policy, cooked payload, GPU upload, and validation.

Layer 1 can prove the material ABI with a cooked deterministic RGBA8 fixture. It is not a substitute
for general image import. Layer 2 must choose an image input format and color/mip policy explicitly;
the repository currently contains no image decoder or source image assets to reuse.

## 9. Verification gates

1. Unit tests parse valid resource fields and reject invalid field class/type/reference combinations.
2. Cook/runtime tests round-trip resource metadata, reject corrupt payloads, and prove resource kinds
   participate in interface/layout validation.
3. Generated-source tests assert texture/sampler accessors, correct typed-word offsets, and non-uniform
   heap helper use.
4. Descriptor tests verify material-owned sampled-image and sampler registration, cache deduplication,
   release/recreate behavior, and no pipeline-local descriptor write.
5. A shader-cook probe compiles a surface that samples an authored 2D texture.
6. A smoke scene verifies the sampled result in G-buffer/deferred output and exercises a transparent
   material so AVBOIT and the shared trace surface path consume the same resource contract.
7. Vulkan capability coverage verifies the existing sampled-image non-uniform-indexing feature (which
   also covers sampler arrays) is enabled or device creation fails with a clear diagnostic.

## 10. Next decision

The implemented fixture is a stable vertical slice, not artist-facing texture import. The next decision
is whether the follow-up should begin with PNG, DDS, or KTX input; that choice determines the importer,
format/color-space, and mip policy that must be designed with it.
