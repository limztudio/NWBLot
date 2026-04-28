# NWBLot Future Work Handoff — Editable Deformable Geometry

**Repository:** <https://github.com/limztudio/NWBLot>
**Generated:** 2026-04-25
**Scope:** Follow-up handoff after the current deformable-geometry, picking, surface-edit, accessory, and GPU deformer updates.

---

## 0. Executive summary

NWBLot now has the first real version of the editable deformable-character stack. The repo is no longer only a static `Geometry` renderer. It now contains a typed `DeformableGeometry` asset, runtime deformable mesh instances, CPU picking against deformed meshes, local surface hole editing, accessory attachment data, surface edit state serialization/deserialization, and a GPU deformer path for morphs, skinning, and scalar displacement.

The next work should **not** be another first-pass implementation. The next work should make the system durable and production-like:

1. Add a replay/apply path for saved surface edit state.
2. Fix edit-state persistence so asset references are stable and human-readable.
3. Support multiple holes/accessories instead of assuming the latest edit owns every accessory.
4. Upgrade displacement from `ScalarUvRamp` into texture/vector/pose-driven displacement.
5. Improve morph deformer performance and normal/tangent correctness.
6. Add import, debugging, validation, and authoring tools around the system.

The highest-priority target is:

> Save an edited deformable character with one or more holes and accessories, reload it from persistent state, replay the edits onto a fresh runtime mesh, spawn/rebind the accessories, and verify that morphs, skinning, displacement, and rendering still work.

---

## 1. Current repo state observed

### 1.1 Deformable asset path exists

Relevant files:

- [`impl/assets_graphics/deformable_geometry_asset.h`](https://github.com/limztudio/NWBLot/blob/main/impl/assets_graphics/deformable_geometry_asset.h)
- [`impl/assets_graphics/deformable_geometry_asset.cpp`](https://github.com/limztudio/NWBLot/blob/main/impl/assets_graphics/deformable_geometry_asset.cpp)
- [`impl/assets_graphics/deformable_geometry_validation.h`](https://github.com/limztudio/NWBLot/blob/main/impl/assets_graphics/deformable_geometry_validation.h)
- [`impl/assets_graphics/CMakeLists.txt`](https://github.com/limztudio/NWBLot/blob/main/impl/assets_graphics/CMakeLists.txt)

What exists:

- `DeformableVertexRest`
- `SkinInfluence4`
- `SourceSample`
- `DeformableDisplacement`
- `DeformableMorphDelta`
- `DeformableMorph`
- `DeformableGeometry`
- `DeformableGeometryAssetCodec`
- validation helpers for rest vertices, tangent frames, barycentrics, skin weights, source samples, triangles, and morph payloads

Current limitations:

- `DeformableDisplacementMode` currently supports only:
  - `None`
  - `ScalarUvRamp`
- Morph names are serialized by `NameHash`, not by full string path/name.
- The asset codec is currently a binary typed asset codec, not a full DCC importer.
- There is no real texture-backed displacement asset model yet.

---

### 1.2 ECS deformable components exist

Relevant file:

- [`impl/ecs_graphics/components.h`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/components.h)

What exists:

- `RuntimeMeshDirtyFlag`
- `RuntimeMeshHandle`
- `DeformableMorphWeightsComponent`
- `DeformableJointPaletteComponent`
- `DeformableDisplacementComponent`
- `DeformableRendererComponent`
- `DeformableAccessoryAttachmentComponent`

This is the right split. The static `RendererComponent` can remain for static assets while deformable meshes use `DeformableRendererComponent` and `RuntimeMeshHandle`.

Current limitations:

- The accessory component anchors to a wall-vertex span from a hole result.
- It is good enough for earrings/piercings, but it is not yet a general attachment/constraint system.
- It stores the target entity and runtime mesh handle, so reload/rebind needs a stable restore procedure.

---

### 1.3 Runtime mesh cache exists

Relevant files:

- [`impl/ecs_graphics/deformable_runtime_mesh_cache.h`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformable_runtime_mesh_cache.h)
- [`impl/ecs_graphics/deformable_runtime_mesh_cache.cpp`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformable_runtime_mesh_cache.cpp)

What exists:

- `DeformableRuntimeMeshInstance`
- source asset reference
- rest vertices
- indices
- source triangle count
- skin
- source samples
- displacement
- morphs
- rest/index/deformed GPU buffers
- edit revision
- dirty flags
- handle-to-entity mapping
- source caching / reference counting

This is a strong foundation. It correctly separates immutable asset data from per-instance edited runtime data.

Current limitations:

- Persistent edit replay is not yet represented as a public cache workflow.
- Runtime mesh handles are not persistent identifiers; saved state must not depend on raw handle values.
- Buffer lifetime/reuse should be improved once multiple edits and many characters are in play.

---

### 1.4 CPU picking exists

Relevant files:

- [`impl/ecs_graphics/deformable_picking.h`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformable_picking.h)
- [`impl/ecs_graphics/deformable_picking.cpp`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformable_picking.cpp)

What exists:

- `DeformablePickingRay`
- `DeformablePickingInputs`
- `DeformablePosedHit`
- `BuildDeformablePickingVertices`
- `ResolveDeformableRestSurfaceSample`
- `RaycastDeformableRuntimeMesh`
- `RaycastVisibleDeformableRenderers`

This is the right model: users pick in posed/deformed space, but edits are resolved back to rest-space source samples.

Current limitations:

- Picking is CPU-side and likely builds/uses temporary deformed vertices.
- This is acceptable for tool/editor/confirmation workflows, but should not become a high-frequency gameplay query path without profiling.
- No editable/restricted/forbidden region masks are visible yet.

---

### 1.5 Surface hole editing and edit-state serialization exist

Relevant files:

- [`impl/ecs_graphics/deformable_surface_edit.h`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformable_surface_edit.h)
- [`impl/ecs_graphics/deformable_surface_edit.cpp`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformable_surface_edit.cpp)

What exists:

- `DeformableHoleEditParams`
- `DeformableHoleEditResult`
- `DeformableSurfaceEditSession`
- `DeformableHolePreview`
- `DeformableSurfaceHoleEditRecord`
- `DeformableSurfaceEditRecord`
- `DeformableAccessoryAttachmentRecord`
- `DeformableSurfaceEditState`
- `BeginSurfaceEdit`
- `PreviewHole`
- `CommitHole`
- `AttachAccessory`
- `ResolveAccessoryAttachmentTransform`
- `SerializeSurfaceEditState`
- `DeserializeSurfaceEditState`
- `CommitDeformableRestSpaceHole`

The `.cpp` implementation also contains validation for hole records, wall vertex spans, surface edit state headers, and accessory records. The current binary surface-edit state uses magic `SEF1` and version `3`.

Current limitations:

- `DeformableSurfaceEditRecordType` currently only has `Hole`.
- Edit-state serialization/deserialization exists, but there is no obvious public `ApplySurfaceEditState` / `ReplaySurfaceEditState` function in the header.
- Accessory persistence currently stores geometry/material as `NameHash`, which can be fragile for long-term save files unless the name registry is guaranteed to reconstruct names.
- Current validation appears to require accessories to match the latest committed edit, which blocks multiple accessories attached to earlier holes after additional holes are made.

---

### 1.6 GPU deformer exists

Relevant files:

- [`impl/ecs_graphics/deformer_system.h`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformer_system.h)
- [`impl/ecs_graphics/deformer_system.cpp`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/deformer_system.cpp)
- [`impl/assets/graphics/deformer_cs.glsl`](https://github.com/limztudio/NWBLot/blob/main/impl/assets/graphics/deformer_cs.glsl)
- [`impl/assets/graphics/deformer_cs.nwb`](https://github.com/limztudio/NWBLot/blob/main/impl/assets/graphics/deformer_cs.nwb)

What exists:

- morph ranges and morph deltas GPU structs
- skin influence GPU struct
- joint palette buffer
- compute pipeline dispatch
- default empty buffers
- runtime resources cached by runtime mesh handle/revision/payload signature
- rest-to-deformed copy path when no active deformation is needed
- shader path:
  - read rest vertex data
  - apply morph deltas
  - orthonormalize frame
  - apply skinning
  - apply scalar UV ramp displacement
  - write deformed vertex payload

Current limitations:

- The shader currently loops over active morph ranges and deltas per vertex. This is simple but can become expensive for dense meshes or many morphs.
- Displacement is currently `position += normal * (clamp(uv0.x, 0, 1) * amplitude)`. That is a placeholder ramp, not a production displacement map.
- Normal/tangent are not recomputed from displaced triangle geometry; they are transformed/orthonormalized but not derived from the final displaced surface.
- Skinning appears to use linear blend skinning. Dual-quaternion skinning, inverse-transpose normal handling for non-uniform scale, and joint palette import are future work.

---

### 1.7 Renderer shader contract is now richer

Relevant files:

- [`impl/assets/graphics/bxdf_ms.glsl`](https://github.com/limztudio/NWBLot/blob/main/impl/assets/graphics/bxdf_ms.glsl)
- [`impl/assets/graphics/mesh_emulation_vs.glsl`](https://github.com/limztudio/NWBLot/blob/main/impl/assets/graphics/mesh_emulation_vs.glsl)
- [`impl/assets/graphics/bxdf_ps.glsl`](https://github.com/limztudio/NWBLot/blob/main/impl/assets/graphics/bxdf_ps.glsl)

What exists:

- Mesh shader authoring now uses `source.position`, `source.normal`, `source.tangent`, `source.uv0`, and `source.color`.
- Pixel shader receives color, normal, tangent, uv, and world position.
- Debug directional shading uses the normal/tangent/world position.

Current limitations:

- The material path is still a debug/simple BxDF path.
- No residual normal map, displacement texture, or material texture binding workflow is visible yet.
- Static and deformable render paths need to remain layout-compatible or explicitly separate.

---

### 1.8 Build integration exists

Relevant files:

- [`impl/ecs_graphics/CMakeLists.txt`](https://github.com/limztudio/NWBLot/blob/main/impl/ecs_graphics/CMakeLists.txt)
- [`impl/assets_graphics/CMakeLists.txt`](https://github.com/limztudio/NWBLot/blob/main/impl/assets_graphics/CMakeLists.txt)

The deformable files are included in the static libraries. This is good. Future work should keep new geometry/edit code out of `renderer_system.cpp` and avoid turning ECS graphics into a general mesh-boolean library.

---

## 2. Highest-priority future work

## 2.1 Add persistent edit replay/apply

### Problem

`SerializeSurfaceEditState` and `DeserializeSurfaceEditState` exist, but saved edit state is only useful if it can be replayed onto a fresh `DeformableRuntimeMeshInstance` after loading a character.

A runtime mesh handle cannot be saved as a durable identity. After reload, a new runtime mesh handle will be allocated.

### Required API

Add something like this to `deformable_surface_edit.h`:

```cpp
struct DeformableSurfaceEditReplayResult {
    u32 appliedEditCount = 0;
    u32 restoredAccessoryCount = 0;
    u32 finalEditRevision = 0;
    bool topologyChanged = false;
};

struct DeformableSurfaceEditReplayContext {
    Core::Assets::AssetManager* assetManager = nullptr;
    Core::ECS::World* world = nullptr;
    Core::ECS::EntityID targetEntity = Core::ECS::ENTITY_ID_INVALID;
};

[[nodiscard]] bool ApplySurfaceEditState(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context,
    DeformableSurfaceEditReplayResult* outResult = nullptr
);
```

### Implementation rules

- Always replay from the clean base runtime mesh, not from an already-edited mesh, unless explicitly doing incremental append.
- Treat `DeformableSurfaceEditRecord::result` as validation/reference data, not as the source of geometry truth.
- Recompute each hole result during replay.
- Confirm replayed result matches stored result enough to restore accessory anchors.
- Rebuild GPU buffers after replay.
- Bump runtime edit revision after each replayed edit.
- Restore accessories after all edits are replayed.

### Acceptance tests

Add tests for:

- one hole → serialize → deserialize → replay → same vertex/index counts
- one hole + accessory → replay → accessory transform resolves
- two holes → replay in order → final edit revision matches
- corrupted state magic/version/count → fails safely
- replay onto wrong source mesh → fails safely

---

## 2.2 Fix persistent asset references in edit-state binary

### Problem

`SurfaceEditAccessoryRecordBinary` currently stores accessory geometry/material using `NameHash`. That is compact, but not ideal for user save files. A hash alone is not self-describing, is hard to debug, and can become fragile if name reconstruction depends on runtime name-table state.

### Recommended change

Version the surface edit state to `4` and add a string table or asset-ID table:

```cpp
struct SurfaceEditStateHeaderV4 {
    u32 magic;
    u32 version;
    u64 editCount;
    u64 accessoryCount;
    u64 stringTableByteCount;
};

struct SurfaceEditAccessoryRecordBinaryV4 {
    u32 editRevision;
    u32 firstWallVertex;
    u32 wallVertexCount;
    f32 normalOffset;
    f32 uniformScale;
    u32 geometryPathOffset;
    u32 materialPathOffset;
};
```

### Rules

- Keep reading v3 for backward compatibility.
- Write v4 by default.
- Use canonical asset virtual paths, not runtime handles.
- Add a debug tool that prints edit-state records in human-readable form.

### Acceptance tests

- v3 file still loads.
- v4 file round-trips exact virtual paths.
- unknown asset path fails with a clear error.
- state dump shows readable paths and edit records.

---

## 2.3 Support multiple holes and accessories correctly

### Problem

The current validation flow appears to require accessories to match the latest committed edit. That makes sense for the first earring prototype, but it blocks this sequence:

```text
make left-ear hole
attach left earring
make right-ear hole
attach right earring
save and reload both earrings
```

If every accessory must match the latest edit, the left earring becomes invalid after the right-ear edit.

### Recommended data model

Introduce stable edit IDs:

```cpp
using DeformableSurfaceEditId = u32;

struct DeformableSurfaceEditRecord {
    DeformableSurfaceEditId editId;
    DeformableSurfaceEditRecordType::Enum type;
    DeformableSurfaceHoleEditRecord hole;
    DeformableHoleEditResult result;
};

struct DeformableAccessoryAttachmentRecord {
    DeformableSurfaceEditId anchorEditId;
    Core::Assets::AssetRef geometry;
    Core::Assets::AssetRef material;
    u32 firstWallVertex;
    u32 wallVertexCount;
    f32 normalOffset;
    f32 uniformScale;
};
```

### Validation rules

- Edits still replay in order.
- Each accessory references one existing edit record.
- Accessory wall vertex span must match the referenced edit result, not necessarily the latest edit.
- Multiple accessories may reference the same hole.

### Acceptance tests

- two holes, two accessories, save, reload, replay.
- two accessories on same hole.
- accessory referencing missing edit ID fails.
- accessory referencing a non-hole edit fails or uses a specific compatible anchor interface.

---

## 2.4 Add edit undo/redo and edit history operations

### Problem

Right now the system is shaped around committing a hole and serializing the final edit state. Users will need to preview, confirm, undo, redo, and remove edits.

### Recommended design

Do not mutate saved state as raw mesh deltas. Keep operation records:

```text
base deformable asset
+ ordered edit operation records
+ accessory records
+ optional cached rebuilt mesh
```

Add operation types:

```cpp
namespace DeformableSurfaceEditRecordType {
    enum Enum : u32 {
        Hole = 1,
        HealHole = 2,
        ResizeHole = 3,
        MoveHole = 4,
        PatchReplace = 5,
    };
}
```

### First implementation

- `UndoLastSurfaceEdit(instance, state)`
- Rebuild from base and replay all records except the last one.
- This is slower but deterministic and easy to validate.

### Later implementation

- Incremental undo with stored inverse patch data.
- Patch-level topology cache.

---

## 2.5 Upgrade displacement from ramp to real displacement assets

### Problem

The current displacement mode is a scalar UV ramp. It proves the deformation path works, but it is not a real normal/displacement-map workflow.

### Recommended modes

Extend `DeformableDisplacementMode`:

```cpp
namespace DeformableDisplacementMode {
    enum Enum : u32 {
        None = 0,
        ScalarUvRamp = 1,          // keep as debug/test mode
        ScalarTexture = 2,         // height map in uv space
        VectorTangentTexture = 3,  // tangent-space vector displacement
        VectorObjectTexture = 4,   // rest/object-space vector displacement
        PoseDrivenScalar = 5,
        PoseDrivenVector = 6,
    };
}
```

Add a descriptor:

```cpp
struct DeformableDisplacementTextureDesc {
    Core::Assets::AssetRef texture;
    u32 mode;
    f32 amplitude;
    f32 bias;
    f32 uvScale[2];
    f32 uvOffset[2];
};
```

### GPU work

- Add texture/sampler bindings to the deformer compute pipeline.
- Sample height/vector displacement using `uv0`.
- Apply scalar height along final normal.
- Apply vector displacement in tangent frame for face details.
- Keep normal map as residual shading detail, not as the source of geometry.

### Acceptance tests

- scalar ramp still works.
- scalar texture displacement changes positions.
- vector tangent displacement affects tangential direction.
- disabled displacement produces rest/morph/skinned output only.
- invalid texture asset fails cleanly.

---

## 2.6 Recompute normals and tangents after topology edits and displacement

### Problem

The current deformer transforms/orthonormalizes normals and tangents, but displacement changes final geometry. If positions move but normals do not reflect the final surface, shading will become wrong around holes, wrinkles, and texture displacement.

### Recommended staged approach

#### Stage A — CPU rebuild after surface edits

After `CommitHole`, recompute rest-space normal/tangent frames for the locally edited patch.

- Recompute face normals for changed triangles.
- Accumulate and normalize vertex normals for affected vertices.
- Recompute tangents from UVs where possible.
- Fall back to a stable tangent when UVs are degenerate.

#### Stage B — GPU analytic normal for scalar texture displacement

For scalar texture displacement, estimate derivatives in UV space:

```text
height_u = sample(u + du, v) - sample(u - du, v)
height_v = sample(u, v + dv) - sample(u, v - dv)
```

Use tangent/bitangent to adjust the normal.

#### Stage C — GPU triangle normal recompute pass

For production geometry displacement:

1. Deformer writes final positions.
2. A second compute pass accumulates face normals/tangents.
3. A normalize pass writes final vertex normals/tangents.

### Acceptance tests

- hole wall shading uses non-degenerate normals.
- displaced surface changes debug lighting as expected.
- tangent handedness remains valid.
- no NaN normals after degenerate UV cases.

---

## 2.7 Improve morph deformer performance

### Problem

The current shader does this per vertex:

```text
for each active morph:
    for each delta in morph:
        if delta.vertex == current vertex:
            accumulate
```

That is simple but can become expensive:

```text
O(vertex_count * active_morph_delta_count)
```

### Recommended fix

Build a per-frame sparse blended morph payload on CPU:

```cpp
struct DeformerVertexMorphRangeGpu {
    u32 firstDelta;
    u32 deltaCount;
};

struct DeformerBlendedMorphDeltaGpu {
    vec4 deltaPosition;
    vec4 deltaNormal;
    vec4 deltaTangent;
};
```

Then the shader does:

```text
range = vertexMorphRanges[vertexId]
for delta in range:
    apply already-weighted delta
```

This changes runtime cost to:

```text
O(vertex_count + active_blended_delta_count)
```

### Implementation notes

- Keep the existing path as a fallback/debug mode.
- Sort deltas by vertex ID in the CPU payload builder.
- Merge multiple morph contributions to the same vertex before upload.
- Rebuild only when morph weights or edit revision changes.

### Acceptance tests

- output matches old shader path within epsilon.
- duplicate morph weights sum correctly.
- sparse morph with only 5 changed vertices does not scan every delta for every vertex.
- edit revision invalidates the payload.

---

## 2.8 Improve skinning quality and correctness

### Current state

The deformer supports four joint influences per vertex and a joint matrix palette. That is enough for the first implementation.

### Future work

- Add inverse bind matrices in import/cook path.
- Support dual-quaternion skinning for high-quality shoulders/elbows/knees.
- Add normal matrix handling for non-uniform scale.
- Validate joint indices against skeleton count, not only palette count.
- Add skeleton asset or animation-pose component rather than only raw matrix palette component.

### Acceptance tests

- identity palette produces rest/morphed output.
- one-joint translation moves all fully weighted vertices correctly.
- two-joint blend matches CPU reference.
- invalid joint indices are ignored or logged deterministically.
- non-uniform scale does not destroy normals in the final production path.

---

## 2.9 Import real characters

### Problem

The current system has an internal typed asset, but production needs importing.

### Recommended first importer

Start with glTF/GLB before FBX.

Required mapping:

```text
glTF positions            -> DeformableVertexRest.position
glTF normals              -> DeformableVertexRest.normal
glTF tangents             -> DeformableVertexRest.tangent
glTF texcoord_0           -> DeformableVertexRest.uv0
glTF color_0              -> DeformableVertexRest.color0
glTF joints_0/weights_0   -> SkinInfluence4
glTF morph targets        -> DeformableMorph / DeformableMorphDelta
glTF indices              -> indices
generated source samples  -> SourceSample
generated displacement    -> DeformableDisplacement descriptor
```

### Import validation

- Generate missing normals/tangents if absent.
- Normalize skin weights.
- Drop influences beyond 4 using highest weights, then renormalize.
- Reject morph targets with duplicate or out-of-range vertex IDs.
- Generate `SourceSample` for base vertices as identity samples.

### Acceptance tests

- import one simple skinned quad.
- import one simple morph target.
- import one skinned mesh with identity bind pose.
- render in Testbed.
- perform one hole edit after import.

---

## 2.10 Move surface topology code into a geometry module

### Problem

`deformable_surface_edit.cpp` is already large and contains geometry/topology operations that are not inherently ECS rendering logic.

### Recommended module split

Add:

```text
core/geometry/mesh_topology.h/.cpp
core/geometry/surface_patch_edit.h/.cpp
core/geometry/attribute_transfer.h/.cpp
core/geometry/tangent_frame_rebuild.h/.cpp
```

Keep `impl/ecs_graphics/deformable_surface_edit.*` as the bridge between:

```text
ECS/runtime mesh/cache/accessory components
and
core geometry editing algorithms
```

### First extraction targets

- boundary loop construction
- wall triangle generation
- local normal/tangent rebuild
- skin weight inpainting
- morph delta transfer

### Acceptance tests

- geometry module tests run without graphics/ECS.
- same hole edit result before/after extraction.
- no renderer dependency in geometry unit tests.

---

## 2.11 Generalize CSG/surface editing after hole editing is stable

### Keep first goal narrow

For earrings/piercings, local surface surgery is enough. Do not start by building arbitrary full-mesh CSG.

### Future operations

1. Local circular/elliptical hole.
2. Hole resize.
3. Hole heal.
4. Patch replace.
5. Surface loop cut.
6. Full mesh boolean difference/union/intersection.

### Data model requirement

All topology edits must preserve or regenerate:

```text
rest vertices
indices
source samples
skin weights
morph deltas
UVs
tangents
normals
material IDs
accessory anchors
```

### Acceptance tests

- local edit does not corrupt unaffected triangles.
- source samples remain valid.
- morphs still replay after local edit.
- skin weights remain normalized.
- repeated edits do not create invalid triangles.

---

## 2.12 Add user/edit safety masks

### Problem

Users should not be allowed to cut arbitrary regions such as eyelids, lips, high-deformation facial areas, or hidden rig-critical regions unless the asset explicitly allows it.

### Recommended asset addition

Add an edit mask stream:

```cpp
namespace DeformableEditMaskFlag {
    enum Enum : u8 {
        Editable = 1 << 0,
        Restricted = 1 << 1,
        Forbidden = 1 << 2,
        RequiresRepair = 1 << 3,
    };
}
```

Per vertex or per triangle:

```cpp
Vector<u8> editMaskPerTriangle;
```

### Picking integration

When `RaycastVisibleDeformableRenderers` returns a hit:

- resolve rest source triangle
- check edit mask
- preview allowed/restricted/forbidden state
- refuse commit if forbidden

### Acceptance tests

- forbidden triangle cannot commit a hole.
- restricted triangle previews warning.
- editable triangle commits normally.

---

## 2.13 Add debug visualization

This project will be difficult to debug without strong debug views.

Add overlays for:

```text
runtime mesh handle / edit revision
rest-space hit point
posed-space hit point
source triangle ID
barycentric coordinates
hole preview frame
removed triangles
boundary loop
wall vertices
accessory anchor frame
skin weights
morph deltas
displacement magnitude
normal/tangent basis
invalid triangle markers
```

Recommended files:

```text
impl/ecs_graphics/deformable_debug_draw.h/.cpp
impl/assets/graphics/deformable_debug_ms.glsl
impl/assets/graphics/deformable_debug_ps.glsl
```

Acceptance criteria:

- Toggle debug overlays in Testbed.
- Inspect a committed hole and accessory anchor visually.
- Visualize which vertices are wall vertices.

---

## 2.14 Strengthen tests

Existing tests should be expanded into a full deformable test matrix.

### Asset validation tests

- invalid normal/tangent frame fails.
- invalid skin weights fail.
- invalid source sample fails.
- duplicate morph name fails.
- duplicate morph delta vertex fails.

### Runtime cache tests

- same source asset creates separate runtime mesh instances.
- editing one instance does not edit another instance.
- edit revision increments.
- GPU upload dirty state clears after upload.

### Surface edit tests

- preview does not mutate topology.
- commit mutates topology.
- wall vertex count is valid.
- source samples are valid after edit.
- skin weights are normalized after edit.
- morph deltas transfer to new rim/wall vertices.

### Serialization/replay tests

- v3/v4 state round-trip.
- replay reproduces topology counts.
- corrupted header rejected.
- wrong source asset rejected.
- multiple holes/accessories restore.

### Deformer tests

- CPU reference vs GPU output for:
  - rest only
  - morph only
  - skin only
  - displacement only
  - morph + skin + displacement

---

## 3. Suggested PR sequence

### PR 1 — Surface edit replay

Files:

```text
impl/ecs_graphics/deformable_surface_edit.h
impl/ecs_graphics/deformable_surface_edit.cpp
tests/ecs_graphics/ecs_graphics_tests.cpp
```

Work:

- Add `ApplySurfaceEditState`.
- Rebuild from base source and replay hole records.
- Add tests for save/load/replay.

Done when:

- One hole survives save/load/replay.
- Runtime edit revision is restored correctly.

---

### PR 2 — Stable persisted asset references

Files:

```text
impl/ecs_graphics/deformable_surface_edit.cpp
impl/ecs_graphics/deformable_surface_edit.h
tests/ecs_graphics/ecs_graphics_tests.cpp
```

Work:

- Add v4 surface edit binary format.
- Store geometry/material virtual paths in a string table.
- Keep reading v3.

Done when:

- Edit-state binary can be inspected and shows readable asset paths.

---

### PR 3 — Multi-hole/multi-accessory support

Files:

```text
impl/ecs_graphics/deformable_surface_edit.h
impl/ecs_graphics/deformable_surface_edit.cpp
impl/ecs_graphics/components.h
tests/ecs_graphics/ecs_graphics_tests.cpp
Testbed/*
```

Work:

- Add stable edit IDs.
- Let accessories bind to any valid hole edit, not only the latest edit.
- Test two earrings.

Done when:

- Left and right earrings both survive a second edit and reload.

---

### PR 4 — Displacement v2

Files:

```text
impl/assets_graphics/deformable_geometry_asset.h
impl/assets_graphics/deformable_geometry_asset.cpp
impl/assets_graphics/deformable_geometry_validation.h
impl/ecs_graphics/deformer_system.h
impl/ecs_graphics/deformer_system.cpp
impl/assets/graphics/deformer_cs.glsl
```

Work:

- Add scalar texture displacement mode.
- Add vector tangent texture mode.
- Add texture asset binding.
- Keep `ScalarUvRamp` as debug mode.

Done when:

- A test texture visibly changes geometry.
- Residual normal maps can remain material-only.

---

### PR 5 — Morph deformer optimization

Files:

```text
impl/ecs_graphics/deformer_system.h
impl/ecs_graphics/deformer_system.cpp
impl/assets/graphics/deformer_cs.glsl
```

Work:

- Preblend active morph deltas by vertex.
- Upload per-vertex morph ranges.
- Keep old path under debug flag if useful.

Done when:

- GPU output matches previous morph result.
- Morph-heavy meshes avoid scanning all deltas for every vertex.

---

### PR 6 — Normal/tangent rebuild

Files:

```text
core/geometry/tangent_frame_rebuild.h/.cpp
impl/ecs_graphics/deformable_surface_edit.cpp
impl/assets/graphics/deformer_cs.glsl
```

Work:

- CPU rebuild for local edited patches.
- Optional GPU normal recompute pass for displaced geometry.

Done when:

- Debug shading around holes and displacement looks stable.

---

### PR 7 — Real character import

Files:

```text
impl/assets_graphics/*
resource_cooker/*
Testbed/assets/*
```

Work:

- Add glTF/GLB import path.
- Generate `DeformableGeometry` from a skinned/morphed mesh.
- Add testbed asset.

Done when:

- A simple skinned/morphed character imports, renders, deforms, and accepts one hole edit.

---

## 4. Risks and mitigation

### Risk: edit replay diverges from saved result

Mitigation:

- Store operation parameters, not raw mesh deltas.
- Replay deterministically from base source.
- Validate result counts and wall spans.
- Add state versioning.

### Risk: accessory anchors break after multiple edits

Mitigation:

- Anchor by stable edit ID.
- Store local wall coordinate or ring parameter, not only first vertex/count.
- Re-resolve anchor after replay.

### Risk: morph transfer becomes incorrect after topology changes

Mitigation:

- Keep `SourceSample` for every new vertex.
- Barycentrically resample deltas from source triangles.
- Add smoothing/inpainting around wall/interior vertices.
- Test morphs around edited areas.

### Risk: displacement makes shading wrong

Mitigation:

- Recompute normals/tangents after topology edits.
- Add analytic displacement normals for texture displacement.
- Add optional final normal recompute pass.

### Risk: deformer becomes too slow

Mitigation:

- Preblend morphs by vertex.
- Cache payloads by morph signature and edit revision.
- Use dirty flags aggressively.
- Profile before adding virtual geometry.

---

## 5. Long-term direction

After the above work is stable, the long-term research/engine direction is:

```text
editable deformable geometry
+ local surface surgery
+ persistent edit replay
+ skinned/morphed/vector-displaced runtime mesh
+ debug/authoring tools
+ eventually meshlet/virtual-geometry rendering
```

Do not start Nanite-like virtual geometry until these are stable:

1. real imported skinned/morphed character
2. multiple persistent edits
3. accessory anchors surviving reload
4. real displacement texture mode
5. correct normals/tangents
6. reliable tests

Only after that should the engine move toward:

```text
meshlet clustering
cluster bounds under skinning/morph/displacement
cluster LOD
streaming
visibility buffer
GPU-driven culling
```

---

## 6. Minimal next milestone

The next milestone should be small and concrete:

> A testbed character can receive two earring holes, attach two accessories, save its edit state, reload/replay it into a fresh runtime mesh, and animate with morph + skin + displacement still working.

That milestone proves the core product feature, and it will expose most of the real problems before the system expands into full CSG or virtual geometry.
