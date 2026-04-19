# NWBLot: Virtual Geometry, Skinning, Morphs, and Surface CSG Handoff

Implementation document for adding editable deformable character geometry to the NWBLot engine.

| Field | Value |
|---|---|
| Audience | Developer implementing the feature in NWBLot |
| Scope | Skinned mesh + morph targets + rest-space surface hole editing + displacement detail |
| Primary rule | Edit topology in canonical rest space; deform at runtime after the rebuild |
| Repo basis | Public GitHub `main` branch as viewed on 2026-04-18 |

> **One-sentence goal:** Build a new deformable runtime path beside the current static `Geometry` path: base character data stays editable in rest space, topology changes create a per-instance runtime mesh, and a compute deformer evaluates morphs, skinning, and displacement before rendering.

## Table of contents

- [1. Non-negotiable design decisions](#1-non-negotiable-design-decisions)
- [2. Current NWBLot baseline](#2-current-nwblot-baseline)
- [3. Target architecture](#3-target-architecture)
- [4. Exact implementation sequence](#4-exact-implementation-sequence)
- [5. Proposed data model](#5-proposed-data-model)
- [6. Runtime evaluation order](#6-runtime-evaluation-order)
- [7. Rest-space hole edit algorithm](#7-rest-space-hole-edit-algorithm)
- [8. Attribute transfer rules](#8-attribute-transfer-rules)
- [9. Test plan and acceptance gates](#9-test-plan-and-acceptance-gates)
- [10. Risks and mitigations](#10-risks-and-mitigations)
- [11. Final handoff checklist](#11-final-handoff-checklist)
- [12. Source links and research references](#12-source-links-and-research-references)
- [Appendix A - Suggested public/internal APIs](#appendix-a---suggested-publicinternal-apis)
- [Appendix B - Minimal v1 definition of done](#appendix-b---minimal-v1-definition-of-done)

## 1. Non-negotiable design decisions

| Decision | Why it matters |
|---|---|
| Keep current `Geometry` static | Do not mutate or overload the existing `Geometry` asset. It is a cooked draw buffer format, not an editable character source format. |
| Add a new `DeformableGeometry` asset | Store typed source streams: rest positions, normals, tangents, UVs, skin weights, morphs, displacement metadata, and source-surface provenance. |
| Edit topology in rest space | User can pick on the posed mesh, but the actual hole/CSG rebuild happens on the canonical rest mesh. |
| Make edits per instance | Two entities can share the same base character asset. If only one gets an earring hole, only that entity receives a new runtime mesh revision. |
| Use displacement for geometry detail | Normal maps remain shading data. Geometry motion should come from scalar or vector displacement evaluated after morph/skin deformation. |
| Start with local surface holes, not full CSG | Piercings are surface-edit operations. General solid booleans are a later backend after the local edit path works. |

## 2. Current NWBLot baseline

These observations are the assumptions this handoff is built on. Confirm them again before implementation if the repo has moved significantly.

| Repo area | Current state | Implementation implication |
|---|---|---|
| `impl/assets_graphics/geometry_asset.h` | `Geometry` stores `vertexStride`, raw `vertexData`, raw `indexData`, and `use32BitIndices`. | Good for static cooked geometry; insufficient as a source of truth for skinning, morphs, CSG, or displacement provenance. |
| `impl/assets_graphics/shader_asset_cooker.cpp` | Geometry metadata parser requires `vertex_stride`, `index_type`, `vertex_data`, and `index_data`. | Add a separate parser branch for `deformable_geometry` instead of expanding the flat geometry schema. |
| `Testbed/assets/meshes/cube.nwb` | The cube uses `vertex_stride = 24` and six `f32` values per vertex: position xyz + color rgb. | Current shader and emulation path are position/color oriented. They need a separate deformed-vertex contract. |
| `impl/ecs_graphics/components.h` | `RendererComponent` contains geometry, material, and visible only. | Add `DeformableRendererComponent` or extend draw submission with an instance/runtime mesh handle. |
| `impl/assets_graphics/shader_asset_cooker.cpp` | Material shader stages are validated against the ECS renderer material contract, which allows `mesh` and `ps`. | Do deformation as an engine-managed compute prepass, not as an author-authored material stage. |
| `impl/ecs_graphics/renderer_system.h` | `RendererSystem` has `MeshShader` and `ComputeEmulation` paths and caches `GeometryResources` by geometry name. | Runtime meshes must be cached per entity/revision, not just by asset name, or edits will alias across instances. |
| `impl/assets/graphics/bxdf_ms.glsl` | The mesh shader builds clip position from `source.position` and shades from `source.color`. | Normals, UVs, tangents, and material inputs need a new vertex interface before displacement/normal maps are useful. |

## 3. Target architecture

> **Architecture rule:** Use two representations: an immutable/cooked source asset and a per-instance runtime mesh revision. The runtime mesh can change topology; the source asset remains reloadable, shareable, and cacheable.

```text
DeformableGeometry asset  -->  RuntimeMeshInstance(entity, editRevision)
          |                                |
          |                          CPU rest-space edits
          |                                |
          v                                v
   rest typed streams  -------->  edited rest streams + provenance
                                           |
                                           v
                              DeformerSystem compute pass
                              morphs -> skinning -> displacement
                                           |
                                           v
                              deformed vertex buffer
                                           |
                                           v
                         RendererSystem mesh/emulation path
```

### New engine modules

| File / module | Responsibility |
|---|---|
| `impl/assets_graphics/deformable_geometry_asset.h/.cpp` | Typed asset, codec, validation, and binary serialization for deformable characters. |
| `impl/ecs_graphics/deformer_system.h/.cpp` | Compute pass that evaluates morphs, skinning, and displacement into a deformed vertex buffer. |
| `impl/assets/graphics/deformer_cs.glsl` | GPU shader for morph + skin + displacement evaluation. |
| `core/geometry/mesh_topology.h/.cpp` | CPU topology representation for local patch edits, adjacency, boundaries, and local remeshing. |
| `core/geometry/surface_edit.h/.cpp` | Rest-space hole insertion and later boolean backends. |
| `core/geometry/attribute_transfer.h/.cpp` | Barycentric/provenance transfer and inpainting helpers for UVs, weights, morphs, and detail coordinates. |
| `Testbed/assets/characters/*` | Small proxy character/ear asset and scenes for repeatable tests. |

## 4. Exact implementation sequence

Implement in this order. Each step is intentionally small enough to review as its own PR. Do not start with CSG; start by making the renderer capable of per-instance, typed, deformable geometry.

### PR 0 - Preserve baseline and add tests

**Work items**

- Build current testbed and resource cooker from a clean checkout.
- Add a visual regression scene with two static geometry entities sharing the same cube asset.
- Record acceptance screenshots or automated render hash if your test harness supports it.

**Acceptance criteria**

- Current cube/sphere/tetrahedron render path still works.
- No public asset schema changes yet.

### PR 1 - Add per-instance transform data to rendering

**Work items**

- Update draw submission so a draw item can identify an entity or instance index.
- Create a GPU instance buffer populated from `TransformComponent`.
- Update mesh shader and compute-emulation path to read world transform from instance data instead of relying only on view state.

**Acceptance criteria**

- Two entities using the same `Geometry` asset render at different transforms.
- Transparent and opaque paths still fit push-constant limits; transforms are not added to `TransparentDrawPushConstants`.

### PR 2 - Add `DeformableGeometry` asset and cooker schema

**Work items**

- Create `DeformableGeometry` `TypedAsset` and codec.
- Add a `deformable_geometry` branch to the graphics asset cooker.
- Keep old `geometry` parsing untouched.
- Store typed streams rather than one interleaved opaque byte array.

**Acceptance criteria**

- A minimal deformable asset with positions, normals, tangents, `uv0`, indices, and empty skin/morph data cooks and loads.
- Malformed stream lengths fail with clear logger errors.

### PR 3 - Add runtime deformable mesh cache

**Work items**

- Add `RuntimeMeshInstance` keyed by entity or explicit handle plus `editRevision`.
- Cache source `DeformableGeometry` by asset path, but cache runtime edited buffers per instance.
- Add dirty flags: `TopologyDirty`, `AttributesDirty`, `DeformerInputDirty`, `GpuUploadDirty`.

**Acceptance criteria**

- Two entities can share one base asset, and only one can get a separate runtime mesh revision.
- Destroying an entity releases runtime buffers.

### PR 4 - Add compute deformer pass with morphs only

**Work items**

- Create `DeformerSystem` and `deformer_cs.glsl`.
- Evaluate sparse morph deltas into a deformed vertex buffer without skinning first.
- Render the deformed buffer through both `MeshShader` and `ComputeEmulation` paths.

**Acceptance criteria**

- A test mesh with one morph visibly changes shape with a weight slider.
- Static `Geometry` renderer remains unchanged.

### PR 5 - Add skinning

**Work items**

- Add joint palette upload.
- Add `SkinInfluence4` stream.
- Evaluate morph first, then linear blend skinning; dual-quaternion skinning can be a later optional upgrade.

**Acceptance criteria**

- A two-bone cylinder bends correctly.
- Morph and skinning combine deterministically: `p_rest + morph_delta`, then skin.

### PR 6 - Upgrade vertex outputs for normals, tangents, UVs

**Work items**

- Define a `DeformedVertex` GPU layout containing position, normal, tangent, `uv0`, and optional color.
- Update `bxdf` mesh path, emulation path, and pixel shader inputs together.
- Recompute or renormalize normals/tangents after deformation.

**Acceptance criteria**

- Both render paths produce visually matching results.
- A checker texture or UV debug shader maps correctly.

### PR 7 - Add scalar displacement as geometry detail

**Work items**

- Add a displacement descriptor to `DeformableGeometry`.
- Sample a scalar displacement texture/field in the deformer after morph+skinning.
- Displace along a stable local frame normal; keep normal map as residual shading only.

**Acceptance criteria**

- Displacement amplitude can be toggled and scaled.
- Normals update or are corrected enough for lighting to be stable.

### PR 8 - Add posed picking with rest-space hit recovery

**Work items**

- Raycast against the current posed/deformed mesh.
- Return hit triangle, barycentrics, entity, and `editRevision`.
- Convert hit to a `SurfaceSample` on the rest/edited source surface.

**Acceptance criteria**

- Clicking the animated ear returns a stable rest-space location across animation frames.
- Picked location remains inside the same local neighborhood after pose changes.

### PR 9 - Add local surface hole edit

**Work items**

- Implement a CPU rest-space hole operation: collect local patch, cut loop, delete inner faces, insert rim/tunnel patch, locally remesh.
- For v1, use circular/elliptic hole parameters and avoid arbitrary solid booleans.
- Record `SourceSample`/provenance for every new vertex where possible.

**Acceptance criteria**

- A hole appears on the rest mesh and renders on the posed mesh.
- Old mesh revision can be discarded/rebuilt without corrupting the source asset.

### PR 10 - Add attribute transfer and inpainting

**Work items**

- Barycentrically transfer UVs, normals, tangents, skin weights, morph deltas, masks, and displacement coordinates for matched vertices.
- For unmatched vertices, run local smooth inpainting over the patch.
- Normalize skin weights and validate all per-vertex streams.

**Acceptance criteria**

- The hole rim follows bones and morphs without tearing.
- No NaNs, no invalid indices, no unnormalized skin weights.

### PR 11 - Add earring attachment flow

**Work items**

- Create high-level API: `BeginSurfaceEdit`, `PreviewHole`, `CommitHole`, `AttachAccessory`.
- Place an accessory transform from the hole frame.
- Add testbed UI for clicking ear, choosing radius/depth, committing edit, and attaching an earring.

**Acceptance criteria**

- User can make a hole on a posed character, then the character animates with the hole and attached earring.

### PR 12 - Optional: full CSG backend

**Work items**

- Add a second backend for robust mesh booleans only after local hole editing is stable.
- Use it for arbitrary cutters, not for the first earring feature.
- Require watertight shell rules or a surface-boolean fallback for character skins.

**Acceptance criteria**

- Full boolean path is isolated behind the same edit API and does not destabilize the local piercing workflow.

## 5. Proposed data model

Names below are suggestions. Match existing NWBLot naming/style, but keep the separation between source asset, runtime mesh, and deformation inputs.

```cpp
struct DeformableVertexRest {
    float3 position;
    float3 normal;
    float4 tangent;     // xyz = tangent, w = handedness
    float2 uv0;
    float4 color0;
};

struct SkinInfluence4 {
    uint16_t joint[4];
    float    weight[4];
};

struct SurfaceSample {
    uint32_t sourceTri;
    float    bary[3];   // barycentric coordinates on sourceTri
};

struct SparseMorphDelta {
    uint32_t vertex;
    float3   deltaPosition;
    float3   deltaNormal;
    float4   deltaTangent;
};

struct RuntimeMeshInstance {
    EntityId entity;
    AssetRef<DeformableGeometry> source;
    uint32_t editRevision;
    CpuEditableMesh restMesh;        // per-instance edited rest topology
    GpuBuffer restVertexBuffer;
    GpuBuffer deformedVertexBuffer;
    GpuBuffer indexBuffer;
    DirtyFlags dirty;
};
```

### Suggested `deformable_geometry` metadata

```nwb
deformable_geometry asset;

asset.positions = [ ... ];      // float3 array
asset.normals   = [ ... ];      // float3 array
asset.tangents  = [ ... ];      // float4 array
asset.uv0       = [ ... ];      // float2 array
asset.colors    = [ ... ];      // optional float4 array
asset.indices   = [ ... ];      // u16/u32 triangles
asset.index_type = "u32";

asset.skin = {
  "joints0":  [ ... ],         // uint16x4 per vertex
  "weights0": [ ... ],         // float4 per vertex; must sum to 1 after validation
};

asset.morphs = {
  "smile": {
    "vertex_ids":      [ ... ],
    "delta_position":  [ ... ],
    "delta_normal":    [ ... ],
    "delta_tangent":   [ ... ],
  }
};

asset.displacement = {
  "space": "tangent",
  "mode": "scalar",           // later: "vector"
  "texture": "project/textures/body_displacement",
  "amplitude": 0.01,
};
```

## 6. Runtime evaluation order

> **Do not reverse this order:** Morphs modify rest shape. Skinning poses it. Displacement adds geometric detail after coarse deformation. Normal maps shade last and must not drive topology.

```cpp
// Per frame, per deformable runtime mesh instance
P_rest = RestPosition(vertexId);
N_rest = RestNormal(vertexId);
T_rest = RestTangent(vertexId);

for each active morph:
    P_rest += weight * MorphDeltaPosition(morph, vertexId);
    N_rest += weight * MorphDeltaNormal(morph, vertexId);
    T_rest += weight * MorphDeltaTangent(morph, vertexId);

P_pose, N_pose, T_pose = Skin(P_rest, N_rest, T_rest, SkinInfluence4, JointPalette);

Detail = EvalDisplacement(uv0, poseParams, expressionParams);
P_final = P_pose + LocalFrame(N_pose, T_pose) * Detail;

WriteDeformedVertex(P_final, normalize(N_pose), normalize(T_pose), uv0, color0);
```

## 7. Rest-space hole edit algorithm

This is the exact first surface-edit target. It is enough for earrings and piercings without requiring full solid CSG.

| # | Action | Implementation detail |
|---|---|---|
| 1 | Pick posed mesh | Raycast the visible deformed mesh and collect entity, triangle id, barycentrics, and surface normal. |
| 2 | Resolve rest sample | Use runtime provenance to map hit barycentrics back to an edited rest-space `SurfaceSample`. |
| 3 | Define local frame | Build tangent/bitangent/normal frame at rest hit. Use it to place a circular or elliptical loop. |
| 4 | Collect local patch | Find faces inside a radius plus margin around the loop. Keep this patch small and deterministic. |
| 5 | Cut boundary | Intersect loop with patch triangles; split edges/faces as required. Record provenance for new boundary vertices. |
| 6 | Remove interior | Delete triangles inside the loop. Ensure boundary is a clean single loop for v1. |
| 7 | Insert rim/tunnel | Create annulus/tunnel faces with depth and bevel parameters. Assign initial provenance from nearest boundary or procedural samples. |
| 8 | Local remesh | Retriangulate only the affected patch. Preserve original vertices outside the patch. |
| 9 | Transfer attributes | Barycentric copy where provenance is valid; inpaint weights/morphs for unmatched vertices; normalize all weights. |
| 10 | Upload and mark dirty | Bump `editRevision`, upload rest/index buffers, and let `DeformerSystem` rebuild deformed vertices next frame. |

### Pseudo-code for `CommitHole`

```cpp
bool CommitHole(EntityId entity, HoleEditParams params) {
    RuntimeMeshInstance* mesh = FindRuntimeMesh(entity);
    if (!mesh) return false;

    RestHit hit = ResolveRestHit(mesh, params.posedHit);
    LocalPatch patch = CollectPatch(mesh->restMesh, hit, params.radius * 2.0f);

    CutResult cut = CutLoopIntoPatch(patch, hit.frame, params.radius, params.ellipseRatio);
    if (!cut.validSingleBoundaryLoop()) return false;

    RemoveFacesInsideLoop(patch, cut.boundaryLoop);
    InsertTunnelPatch(patch, cut.boundaryLoop, params.depth, params.bevel);
    LocalRemesh(patch, params.targetEdgeLength);

    TransferAttributes(mesh->source, mesh->restMesh, patch);
    ValidateAndNormalize(mesh->restMesh);

    mesh->editRevision++;
    mesh->dirty |= TopologyDirty | AttributesDirty | GpuUploadDirty | DeformerInputDirty;
    return true;
}
```

## 8. Attribute transfer rules

| Attribute | Transfer method | Validation |
|---|---|---|
| Position | Generated by edit operation | Must be in edited rest space. |
| Normal/tangent | Recompute locally, then smooth/orthonormalize | Do not blindly interpolate across a sharp hole rim if the rim should have a crease. |
| UV | Barycentric copy for old-surface vertices; procedural unwrap for tunnel/rim | Keep a separate UV island for inner wall if needed. |
| Skin weights | Barycentric copy for matched vertices; smooth inpainting for unmatched; normalize | Reject vertices with zero total weight; fallback to nearest valid boundary. |
| Morph deltas | Sample morph delta field using `SourceSample`; zero/procedural deltas for inner wall | If cut is in face/lips, expect manual corrective work later. |
| Displacement coordinates | Barycentric copy or procedural UVs for rim/tunnel | The displacement texture is detail; it should not decide topology. |
| Material id | Copy from source triangle; assign rim/tunnel material override if required | Piercing wall can have skin material or a special wound/rim material. |

## 9. Test plan and acceptance gates

| Test | Scenario | Pass condition |
|---|---|---|
| Static geometry regression | Existing cube/sphere/tetrahedron scene renders unchanged. | Screenshot or render hash matches baseline within expected tolerance. |
| Instance transform test | Two cubes share the same `Geometry` asset but render at different transforms. | No cache duplication bug; both mesh/emulation paths work. |
| Morph-only deformation | Simple plane/cube with a sparse morph target. | Weight 0 = rest, weight 1 = authored delta, intermediate values interpolate smoothly. |
| Skinning deformation | Two-bone cylinder or arm segment. | No exploding vertices; weights remain normalized after cook/load. |
| Morph + skinning order | Same mesh with both morph and bone rotation. | Result is morph in rest space, then skinning; not the reverse. |
| Displacement | Scalar displacement ramp texture. | Vertex positions move with scale; shading normal map does not change topology. |
| Rest-space picking | Animated ear proxy. | Clicking the posed ear resolves to stable rest-space `SurfaceSample`. |
| Hole edit | Create a small circular hole in ear proxy. | Clean boundary, no invalid indices, rim follows animation. |
| Per-instance edit isolation | Two characters share base asset; edit only one. | Only edited entity gets the hole; other remains untouched. |
| Morph after hole | Apply smile/ear morph after hole edit. | Boundary has no major tearing; inner wall has defined morph behavior. |
| Stress validation | Repeated edit/rebuild/reset cycles. | No leaks, stale buffers, NaNs, or source asset corruption. |

## 10. Risks and mitigations

| Risk | Failure mode | Mitigation |
|---|---|---|
| Trying to use normal map as geometry source | Normal maps do not contain unique displacement height or tangential motion. | Use scalar/vector displacement for geometry. Keep normal map as final shading residual. |
| CSG on posed mesh | Topology, skin weights, and morph deltas become frame-dependent. | Always map pick to rest space and rebuild rest topology. |
| Runtime cache keyed only by asset name | Editing one entity edits all entities sharing the same asset. | Runtime mesh cache must include entity/runtime handle plus `editRevision`. |
| Changing current `Geometry` schema too much | Can break existing static assets and renderer assumptions. | Add `DeformableGeometry` beside `Geometry`; keep existing path as regression harness. |
| Full boolean first | Arbitrary solid CSG adds robustness problems before the earring feature works. | Implement local surface holes first; put full CSG behind the same edit API later. |
| Morph deltas tied only to old vertex ids | Topology edits create new vertices with no deltas. | Treat morphs as a sampled deformation field via `SourceSample` and inpaint where needed. |
| Weight transfer artifacts around hole | New rim/tunnel vertices may have bad or zero weights. | Barycentric transfer first; inpaint and normalize; validate before upload. |

## 11. Final handoff checklist

- Keep existing `Geometry` path compiling and rendering throughout the work.
- Create `DeformableGeometry` instead of mutating `Geometry` into a character format.
- Add `DeformerSystem` as an engine prepass, not a material stage.
- Render all deformable instances from deformed buffers written by the deformer pass.
- Make runtime edits per instance and revision.
- Implement local rest-space hole editing before general CSG.
- Store and propagate `SourceSample`/provenance for every edited vertex where possible.
- Transfer UVs, weights, morphs, and displacement coordinates after every topology rebuild.
- Normalize/validate all weights and index ranges before GPU upload.
- Treat normal maps as shading only; use displacement maps for real geometry offsets.

## 12. Source links and research references

Repo links are included so the implementer can quickly confirm the baseline. Research links are recommended background for the algorithms, not required dependencies.

| Reference | Link |
|---|---|
| NWBLot repository root | <https://github.com/limztudio/NWBLot> |
| README/build workflow | <https://github.com/limztudio/NWBLot#readme> |
| Geometry asset header | <https://raw.githubusercontent.com/limztudio/NWBLot/main/impl/assets_graphics/geometry_asset.h> |
| Graphics asset cooker / geometry parser | <https://github.com/limztudio/NWBLot/blob/main/impl/assets_graphics/shader_asset_cooker.cpp> |
| Renderer components | <https://raw.githubusercontent.com/limztudio/NWBLot/main/impl/ecs_graphics/components.h> |
| Renderer system header | <https://raw.githubusercontent.com/limztudio/NWBLot/main/impl/ecs_graphics/renderer_system.h> |
| `bxdf` mesh shader | <https://github.com/limztudio/NWBLot/blob/main/impl/assets/graphics/bxdf_ms.glsl> |
| Cube mesh metadata example | <https://github.com/limztudio/NWBLot/blob/main/Testbed/assets/meshes/cube.nwb> |
| BoolSurf: Boolean Operations on Surfaces | <https://dl.acm.org/doi/10.1145/3550454.3555466> |
| Robust Skin Weights Transfer via Weight Inpainting | <https://www.dgp.toronto.edu/~rinat/projects/RobustSkinWeightsTransfer/index.html> |
| Deformation Transfer for Triangle Meshes | <https://people.csail.mit.edu/sumner/research/deftransfer/> |
| Displaced Subdivision Surfaces | <https://gfx.cs.princeton.edu/pubs/Lee_2000_DSS/index.php> |

## Appendix A - Suggested public/internal APIs

```cpp
// Component, beside RendererComponent
struct DeformableRendererComponent {
    Core::Assets::AssetRef deformableGeometry;
    Core::Assets::AssetRef material;
    RuntimeMeshHandle runtimeMesh;
    bool visible = true;
};

struct HoleEditParams {
    PosedHit posedHit;
    float radius;
    float ellipseRatio;
    float depth;
    float bevel;
    float targetEdgeLength;
};

class SurfaceEditService {
public:
    PreviewHandle previewHole(EntityId entity, const HoleEditParams& params);
    bool commitHole(EntityId entity, const HoleEditParams& params);
    bool resetEdits(EntityId entity);
};

class DeformerSystem final : public Core::ECS::ISystem {
public:
    void update(Core::ECS::World& world, f32 delta) override;
    void dispatch(Core::ICommandList& commandList);
private:
    bool ensureRuntimeMesh(EntityId entity, DeformableRendererComponent& component);
    bool uploadDirtyInputs(RuntimeMeshInstance& mesh);
    bool dispatchDeform(RuntimeMeshInstance& mesh);
};
```

## Appendix B - Minimal v1 definition of done

Version 1 is complete when the following user story is true:

> **V1 user story:** A user loads a skinned character, animates it, clicks the posed ear, commits a small circular hole, attaches an earring, and the character continues to animate with the hole, morphs, skin weights, and displacement detail intact. Existing static geometry samples still render unchanged.

Do not include these in v1 unless they become blockers: arbitrary solid CSG, learned wrinkle synthesis, dual-quaternion skinning, complex facial corrective transfer near mouth/eyes, and full vector-displacement authoring.
