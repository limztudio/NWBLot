# RT Shadow + Distance-Field Fallback Plan

Updated: 2026-06-18

## Goal

Add shadows to the deferred renderer.

- **Default:** hardware ray-traced shadows.
- **Fallback (no RT hardware):** software **triangle-BVH occlusion tracing** (GPU-built per-mesh BVH + per-frame scene BVH) — an *emulation* of the same ray query, not a separate shadow technique. (Was an SDF sphere-march; pivoted 2026-06-18 — see Phase 3.)
- **One unified ray abstraction.** Hardware RT and software SDF are two backends behind a single `traceOcclusion` / `traceClosest` primitive. Effects are written once against the abstraction. This mirrors the engine's existing mesh-shader path + compute-emulation fallback pattern (note rules 15-20).
- **Built as a reusable RT subsystem**, not a shadow one-off. Shadows are the first (cheapest) consumer; reflections / indirect light / caustics are future consumers of the same foundation.

Two chapters, implemented in order:

1. **Shadow system now** — including colored/transmittance shadows for transparent occluders.
2. **Caustics** — a later, optional, additive light-side effect.

## Why this shape (decisions already made)

- Unifying on the *ray primitive* (not the *effect*) is the only fallback strategy that also covers future reflections/GI; otherwise each effect would need its own divergent fallback (shadow maps, then SSR, then SSGI...). It also lets us **delete the would-be raster shadow-atlas subsystem entirely**.
- The fallback's scene structure cannot be the opaque hardware acceleration structure, so a software-readable structure is required. **Only one structure exists per machine at runtime** (RT-capable builds the accel struct; non-RT builds the distance field) — the duality is in builder code, not runtime memory, and the per-mesh extraction + the bindless instance/material table are shared by both.
- RT pipeline (`dispatchRays`) over inline ray query: chosen because RT will grow into reflections/GI, which need closest-hit shading + recursion + SER (`Feature::ShaderExecutionReordering`), and the SBT/pipeline machinery is reusable. Shadows themselves only exercise a thin occlusion slice of it.

## Baseline (current engine state)

- **Deferred renderer**, G-buffer = albedo / normal / world-position (`RGBA16F`) + depth. Geometry pass → fullscreen deferred lighting → AVBOIT transparency → composite. See [impl/ecs_render/system.cpp](../impl/ecs_render/system.cpp).
- **Exactly one directional light.** `SceneShadingGpuData` (3×`Float4`: dir, color/intensity, camera pos) at [renderer_push_constants_private.h:54](../impl/ecs_render/renderer_push_constants_private.h#L54), packed by `ResolveSceneShadingState` at [renderer_scene_private.h:40](../impl/ecs_render/renderer_scene_private.h#L40), uploaded in `updateSceneShadingBuffer` at [deferred_lighting.cpp:102](../impl/ecs_render/deferred_lighting.cpp#L102).
- Shading is hardcoded single-directional in `nwbSceneApplyDirectionalShading` at [scene/lighting.slangi](../impl/assets/graphics/scene/lighting.slangi), fed by the `NwbSceneShadingBuffer` cbuffer at [scene/buffer.slangi](../impl/assets/graphics/scene/buffer.slangi), consumed by [deferred/lighting_ps.slang](../impl/assets/graphics/deferred/lighting_ps.slang).
- `LightType` = `{ Directional, Point }`; `Point` is defined but never shaded. `LightComponent` at [impl/ecs_scene/lighting.h:37](../impl/ecs_scene/lighting.h#L37).
- **Full RT API already exists** (BLAS/TLAS, RT pipeline, SBT, `dispatchRays`, ray query, feature gating) in [core/graphics/vulkan/raytracing.cpp](../core/graphics/vulkan/raytracing.cpp) and [core/graphics/api.h](../core/graphics/api.h) — but **nothing builds a BLAS from a mesh or a TLAS from the scene**. The scene→structure bridge is the real prerequisite.
- No shadow code anywhere.

So "multiple shadows" is two coupled problems: there is no multi-light path yet, and there are no shadows. The light loop must come first.

## Architecture

### Unified ray abstraction

```
nwbTraceOcclusion(origin, dir, tMax) -> float3 transmittance   // shadows (1 = lit, 0 = blocked, tinted by transparency)
nwbTraceClosest(ray) -> Hit                                    // reflections / GI (future)
```

Backend selected once at init by feature gating (note rules 48/49: gate on real feature structs + Volk entry points, not extension names):
- **Hardware:** RT pipeline + accel structures (existing `raytracing.cpp` stack).
- **Software:** compute stackless any-hit traversal of a GPU-built triangle BVH (per-mesh + scene).

### Two layers

- **Layer 1 — foundation (shared by all RT effects, both backends):**
  - Scene→structure bridge: BLAS cache (static build + compact via `compactBottomLevelAccelStructs`; skinned refit per frame from skinned runtime position buffers) + per-frame TLAS from visible instances; **and** the software triangle BVH (per-mesh BVH built/refit on GPU + per-frame scene BVH), assembled by the same scene traversal.
  - **Bindless instance/material table** indexed by `RayTracingInstanceDesc::instanceID`. Backend-agnostic: HW closest-hit and SW traversal read the same table. v1 populates only the fields shadows need; material/texture binding lands with reflections.
  - **SBT ray-type convention:** hit-group index = `instanceContributionToHitGroupIndex + rayTypeOffset + rayTypeStride × geometryIndex`; stride = number of ray types, offset = ray-type index. v1 has one ray type (occlusion); new effects add ray-type indices + records, no restructuring.
  - **Instance mask bits** (`RayTracingInstanceDesc::instanceMask`): shadow-caster / reflective / GI-participant categories defined up front.
  - Shared `.slangi` includes: an occlusion-trace helper and a BRDF that takes surface + light data explicitly (callable from the raster lighting pass *and* future closest-hit shaders).
- **Layer 2 — effects:** shadows (this chapter); reflections / GI / caustics later.

### Light system (prerequisite, shared by both backends)

- **Light-list structured buffer**; deferred lighting loops the list. No clustered culling in v1 (loop all lights — fine for modest counts); clustered/froxel culling is a later perf phase.
- **Shadow-slot allocator:** a bounded pool of high-quality shadow slots assigned per frame by importance. This is the answer to "do we limit light count?" — neither path caps *lights*; what's bounded is *shadow slots* (a memory/time budget), filled by importance with graceful degradation. Symmetric across backends:
  - RT: slotted lights get soft/filtered visibility; the long tail gets a cheap hard/transmittance trace (no storage budget).
  - SW: slotted lights get full visibility; the tail degrades to unshadowed (adding casters costs traversal time).

### Shadow visibility representation

- Per-light visibility is a **transmittance color** (`float3`): `1` lit, `0` blocked, tinted for colored transparency.
- Slotted lights → a layer in a visibility texture array (soft/filtered, later). Tail → packed/hard.
- Both backends produce the *same* representation; the deferred lighting pass multiplies each light's contribution by it. The lighting pass is the single place lighting happens; RT/SW only answer visibility.

### v1 defaults (revisit if needed)

- **Hard shadows** first (1 ray/light, noise-free). Soft (area-light sampling + spatiotemporal denoise) is a later quality phase.
- **Light types:** directional + point + spot all representable in the data model; the shadow-slot budget bounds how many cast shadows.
- **Colored/transmittance shadows for transparent occluders:** in scope this chapter.

## Chapter 1 — Shadow system now

Build/test gate after every phase (CMake + Ninja + Clang; `slangc` for shaders). Keep the existing single-directional path working until the multi-light path replaces it.

### Phase 1 — Multi-light foundation (no RT yet)

1. **Scene light gather** (`impl/ecs_scene/lighting.h/.cpp`): a runtime `SceneLight` record + `GatherSceneLights()` producing a list for all light types (directional/point/spot), with validation, alongside the existing directional resolve. *(First unit — see below.)*
2. **GPU light record + structured light buffer** (renderer): `SceneLightGpuData` (aligned `Float4` lanes per note rules 37/40), a structured buffer + light count, packed each frame. Keep camera position in the scene-shading cbuffer.
3. **Shader light loop:** refactor `nwbSceneApplyDirectionalShading` → a per-light BRDF summed over a `StructuredBuffer<NwbSceneLight>` loop; `lighting_ps` loops the list. Add the light-list binding to `scene/binding_slots.h` + the deferred lighting binding layout/set.
4. **Validate:** multiple lights shade correctly; no shadows yet.

### Phase 2 — RT foundation (Layer 1, hardware backend)

Acceleration structures are **runtime-built, never cooked** — the `VkAccelerationStructureKHR` blob is opaque and device/driver-specific, so it cannot be shipped. The cook stage already produces geometry (vertex/index buffers); RT just needs those buffers created with the accel-struct build-input usage flag at runtime.

- **BLAS** = one mesh's geometry (triangle BVH, object space); one per unique mesh, shared by instances.
- **TLAS** = the scene's instances (BLAS pointer + world transform + instance id + mask + hit-group offset), a BVH in world space.

Work:
- **Static mesh BLAS**: build once at mesh resource creation, then **compact** (`compactBottomLevelAccelStructs`), keep resident. Flags: `PREFER_FAST_TRACE | ALLOW_COMPACTION`.
- **Skinned mesh BLAS**: build/refit **per frame** from the skinned runtime position buffer (same buffer CSG receivers use). Flags: `PREFER_FAST_BUILD | ALLOW_UPDATE`; refit most frames, occasional full rebuild.
- **TLAS**: rebuild **per frame** from visible instances.
- **Instance mask bits** (shadow-caster / reflective / GI) defined up front.
- **Bindless instance table**: shadow-needed fields now; material/texture fields when reflections land.
- **Feature gating** (notes 48/49): `Core::Feature::RayTracingAccelStruct` / `RayTracingPipeline` / `RayQuery`, backed by feature structs + Volk entry points, selects HW vs SW backend.
- **AS serialization (pipeline-cache style) is deferred**: the serialize/deserialize + `vkGetDeviceAccelerationStructureCompatibilityKHR` path mirrors `VkPipelineCache`, but AS builds are already fast, so it is a marginal, device-local optimization only — add later if load-time BLAS builds become a measured cost. Never cache TLAS or skinned BLAS.

Phase 2 units: (1 ✅) RT capability gating + `RendererRayTracingSystem`/`RendererRayTracingState` skeleton; (2 ✅) static BLAS build at load (compaction deferred); (3 ✅) skinned BLAS per-frame **refit** (in-place `MODE_UPDATE`) + periodic full rebuild on a resident `AllowUpdate` BLAS; runtime + validation-layer verified; (4 ✅) per-frame TLAS from scene instances (resident, rebuilt each frame; validation-verified); (5) bindless instance table (minimal).

#### Unit 2 detail (decided: reconstruct the index buffer at runtime — do NOT cook it)

Key realization: a BLAS reads the vertex+index buffers only **during the build**; afterward the AS is self-contained and no longer references them. So the index buffer is **transient build scaffolding**, not persistent geometry — no reason to cook it. The meshlet data already encodes the topology; expand it to a flat u32 index buffer at build time. **No asset-format change** (the earlier MSH5→MSH6 cook plan is dropped).

- **Reconstruct on CPU at mesh load**, reusing the existing C++ decode helpers (`DecodeMeshletPositionRef`, [meshlet_ref_decode.h:284]) — meshlet streams are already in CPU memory at load; no compute shader; easy to verify. Produce a flat u32 array (positionStream-space, 3 per triangle, count == `meshletPrimitiveIndexCount`) → upload to a GPU buffer with `setIsAccelStructBuildInput(true)` + device address.
  - ✅ RESOLVED: chain is `primitiveIndices[u8] → meshletLocalVertexRefs[localVertexOffset + u8].localDeformedPosition → DecodeMeshletPositionRef → positionStream index` (matches `nwbMeshBuildMeshletVertex`). Implemented in [meshlet_triangle_indices.h] (shared core template; raw-stream + `MeshGeometryPayload` overloads). Runtime-verified — female mesh: 233118 indices == 678 meshlets × ~114.6 prims × 3, matches cook metrics.
- **Lifetime:**
  - Static mesh: build BLAS (positionBuffer Float3U + transient index buffer; `PREFER_FAST_TRACE | ALLOW_COMPACTION`) → compact → **release the index buffer** after the build's GPU completion (tracked command buffers retain AS build inputs until done — note rules 50/53). positionBuffer just needs the AS-build-input usage flag (kept for rendering anyway).
  - Skinned mesh (Unit 3 ✅): reconstruct the index buffer once at upload (in `uploadRuntimeMeshBuffers`, gated on RT support), **keep it resident**; a resident `AllowUpdate` BLAS is **refit in place** (`MODE_UPDATE`) from skinnedPositionBuffer each frame, with a periodic full rebuild for BVH quality.
- **Storage:** `RayTracingAccelStructHandle blas` on `MeshResources`; builds orchestrated by `RendererRayTracingSystem::buildMeshBlas` (serves both static + runtime), gated on `graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)`. position/index buffers gain AS-build-input usage **only when RT is supported** (static: `mesh_resources.cpp`; skinned: `runtime_cache_resources.cpp`).
- (Future NV-only fast path: `VK_NV_cluster_acceleration_structure` lets meshlets feed the AS build directly — engine has stubs — but it's not portable, so not the default.)

#### Unit 3 detail (skinned BLAS — DONE, runtime + validation-layer verified)

- Reconstruction (`BuildMeshletTriangleIndices`) runs once in `MeshSkinningRuntimeCache::uploadRuntimeMeshBuffers`, gated on a single `rtSupported`; uploads a resident `triangleIndexBuffer` (R32_UINT, AS-build-input). Topology is frame-invariant under skinning, so it is NOT re-decoded per frame. A count guard rejects `triangleIndices.size() != meshletPrimitiveIndexCount` (loud fail vs silent OOB). Scratch uses `SkinningArenaScope::s_RuntimeBlasIndexArena`.
- `RuntimeMeshDesc`/`MeshResources` carry `triangleIndexBuffer` (RT-only, nullable, NOT in `valid()`); `createRuntimeMeshResources` sets `blasBuildPending = (triangleIndexBuffer != nullptr)`.
- **Resident BLAS + refit**: `RendererRayTracingSystem::buildMeshBlas` creates the BLAS once (runtime ⇒ `PreferFastTrace | AllowUpdate`) and keeps it on `MeshResources::blas`. Each frame `buildPendingMeshBlas` (renderer CL, after the skinning pass) refits it in place via `MODE_UPDATE` from the fresh skinned positions, forcing a full rebuild every `s_BlasMaxRefitsBeforeRebuild` (=8) frames to restore BVH quality (refit keeps the build-pose tree topology; SAH cost drifts as the pose moves — see Catto's Dynamic BVH talk). Static meshes build once then clear `blasBuildPending`. Cadence tracked by `MeshResources::blasRefitsSinceRebuild`.
- **Backend** (`buildBottomLevelAccelStruct`): honors `PerformUpdate` (mode=UPDATE + `srcAccelerationStructure` + `updateScratchSize`) gated on `AllowUpdate` + a per-AS `m_built` flag, and emits an acceleration-structure WRITE→WRITE/READ memory barrier before any re-build/refit (covers the cross-frame WAW on a single queue). `PerformUpdate` (0x20) is a mode signal only — `ConvertAccelStructBuildFlags` never maps it to a Vk flag.
- **Scratch-alignment fix** (pre-existing latent bug, surfaced by validation): `attachAccelStructBuildScratchBuffer` now aligns `scratchData.deviceAddress` to `minAccelerationStructureScratchOffsetAlignment` (over-allocate + `AlignUp`) — VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03710. Fixes both BLAS + TLAS scratch.
- **Lifetime is safe**: `AccelStruct : RefCounter`; `buildBottomLevelAccelStruct` `retainResource`s the BLAS + its vertex/index inputs. With refit the BLAS is now **resident** (one AS + storage reused every frame — no per-frame alloc churn). (A 4-agent adversarial review's UAF/cross-CL-sync/index-race flags were each verified false positives.)
- **Verified**: with Vulkan validation layers ON, a ~16s run (hundreds of refits + dozens of periodic rebuilds) produced ZERO validation messages; BLAS = `runtime true, 59586 vertices, 233118 indices`. To re-enable validation set `m_deviceCreationParams.enableDebugRuntime = true` in the `Graphics` ctor (SDK 1.4.341 + `VK_LAYER_KHRONOS_validation` installed).
- **Remaining optimizations** (not blocking): skip refit/rebuild when the skinned pose didn't change this frame (needs a per-frame pose-dirty signal from the skinning system); per-AS (vs global) build barrier so multiple skinned-BLAS refits can overlap; BLAS compaction for static meshes.

#### Unit 4 detail (per-frame scene TLAS — DONE, validation-layer verified)

- `RendererRayTracingSystem::buildSceneTlas(commandList, scratchArena)` runs on the renderer CL right after `buildPendingMeshBlas` (`system.cpp:175`). It enumerates `world().view<RendererComponent>()` exactly like the material draw pass (`resolveRenderableMesh` → `meshSystem().find{Runtime,}MeshResources` find-only), skips entities whose `MeshResources::blas` isn't built, and assembles a `Vector<Core::RayTracingInstanceDesc, ScratchArena>`.
- **Transform**: per-instance transforms are stored DECOMPOSED (quaternion + position + scale in `TransformComponent`/`InstanceGpuData`), not as matrices. `buildSceneTlas` composes the world matrix in SIMD via the general `MatrixAffineTransformation(scale, 0, rotation, position)` and `StoreInstanceTransform` writes it to `AffineTransform` (now `= Float34`) with `StoreFloat(SIMDMatrix, Float34*)`. The engine SIMD math is column-vector + row-major, so `SIMDMatrix.v[0..2]` are exactly the Vulkan 3×4 rows and the result **matches** the shader's `nwbRotateVectorByQuaternion` (`common/math.slangi` / `runtime.slangi:152`) — ray-traced geometry aligns with rasterized geometry. (Skinned BLAS is model-local, so the instance transform = the entity world transform.) The four `Matrix{Affine}Transformation{2D}` helpers were also optimized (translation-matrix folding) and numerically verified identical to the originals.
- **Resident TLAS**: stored on `RendererRayTracingState::m_tlas` (+ `m_tlasMaxInstances`, `m_tlasDeviceAddress`), created once via `setTopLevelMaxInstances(capacity)` (capacity = `s_TlasInitialInstanceCapacity`=128, grown by doubling), then rebuilt (`MODE_BUILD`, the only TLAS mode) every frame via `commandList.buildTopLevelAccelStruct(...)` (which uploads the instance buffer + retains the BLASes internally). `invalidateResources` resets it.
- **Barrier**: `buildTopLevelAccelStructFromInstanceData` now emits an acceleration-structure WRITE→WRITE/READ memory barrier before the TLAS build — orders this frame's BLAS builds before the TLAS reads them AND the previous frame's TLAS build before this one (cross-frame WAW), single-queue.
- **Verified**: validation layers ON, ~16s run (per-frame TLAS rebuilds + BLAS refits) → ZERO validation messages; log shows `created scene TLAS (capacity 128 instances)`. `m_tlasDeviceAddress` is cached for the upcoming trace (Phase 4).
- **Remaining** (not blocking): instanceID is currently a per-frame running index (revisit when hit shaders need entity identity); instanceMask is 0xFF (no per-light/layer mask convention yet); TLAS grows but never shrinks.

### Phase 3 — Software triangle-BVH fallback (software backend) — DIRECTION CHANGED 2026-06-18

**This supersedes the original SDF approach.** The earlier plan made the SW fallback a per-mesh cooked SDF sphere-march; that froze skinned meshes in bind pose (a grid SDF stores distances and cannot be linearly skinned). Per the user's call ("we should consider the skinned/movable cases"), the SW fallback is now a **GPU-built software triangle-BVH occlusion tracer** that structurally mirrors the HW BLAS/TLAS path, so it traces the actual per-frame deformed triangles (via refit) and produces HW/SW-matching shadows. The entire SDF path has been **DELETED** (analytic-box runtime path, `cook_sdf_build.inl` baker + `MeshCookEntry` sdf* fields, `shadow_march_cs.slang/.nwb`, `NWB_SHADOW_SDF_*` slots, `s_MarchComputeShaderName`, `renderSdfShadowVisibility`/`uploadSdfInstances`/`ensureSdfShadow*`, the `m_sdf*` state). `meshlet_triangle_indices.h` was also dropped from `cook.cpp` (the new BVH builds at runtime, not cook-time).

Why a BVH, not a deforming SDF: a deforming mesh would need re-voxelization (too costly for the no-RT GPUs the fallback targets) or capsule/per-bone approximations; a triangle BVH instead traces the real posed triangles. GPU build/refit beats CPU-build+upload decisively because skinned positions are already GPU-resident (CPU build would need a readback that costs more than the whole GPU build). Research-verified (see memory [[rt-shadow-feature]] for citations — Karras HPG2012, H-PLOC HPG2024, NVIDIA RTX best-practices, SRDH, Kopta I3D2012): GPU LBVH of the 77k-tri character < ~0.15 ms on modern GPUs, refit ~10× cheaper than a full build, and LBVH's lower build quality is the mildest case for occlusion (any-hit + early-out; SAH is mis-specified for shadows).

The deferred consumer (`scene/lighting.slangi nwbSceneApplyLighting` reading `DeferredFrameTargets::shadowVisibility`, R32_UINT 32-light bitmask) is FIXED; the SW path writes the same image by tracing the BVH.

Settled decisions: GPU LBVH (Morton → radix sort → Karras/Apetrei topology → bottom-up AABB fit); per-mesh BVH = BLAS-analog (static built once / skinned GPU-refit per frame + full rebuild every 8 frames, reusing the HW cadence); per-frame scene/instance BVH = TLAS-analog; reuse the resident R32_UINT `triangleIndexBuffer` + skinned `positionBuffer` (no cook changes, no extra index buffer); all-opaque any-hit (instanceMask 0xFF, accept-first-hit); CPU-driven plain `dispatch` (counts known CPU-side); new GPU state in a dedicated `RendererBvhShadowState` (mirrors `RendererAvboitState`); reusable build kernels + `bvh_common.slangi` in a new `impl/assets/graphics/bvh/` subsystem, the shadow trace kernel stays in `shadow/`.

Ordered units:
- **U0** — GPU data layout + binding/name scaffolding: `bvh_common.slangi` (NwbBvhNode / NwbBvhInstance / NwbBvhMeshRange std430), new binding-slot + shader-name headers, SW-BVH buffer handles + `swBvhRefitsSinceRebuild` on `MeshResources`.
- **U1** — GPU radix sort over u32 Morton keys (histogram → scan → scatter, ping-pong). Hardest reusable block; needs a readback sanity test (a wrong sort silently corrupts the BVH).
- **U2** — per-mesh LBVH build (centroid+AABB → 30-bit Morton over `csgLocalBounds` → U1 sort → Karras topology → bottom-up AABB fit via atomic parent-visit counter). Root AABB ≈ `csgLocalBounds`.
- **U3** — skinned refit (re-run only the fit pass from fresh skinned positions) + full rebuild every `s_BvhMaxRefitsBeforeRebuild` (=8). MUST run after the skinning compute pass (same ordering the HW BLAS refit relies on).
- **U4** — per-frame scene/instance BVH over visible instances (`world().view<RendererComponent>()`, object→world + world→object, doubling-grow instance buffer).
- **U5** — stackless compute any-hit traversal kernel: per pixel, loop ≤32 lights, transform the ray into each instance via world→object, traverse instance BVH then per-mesh BVH, set the light bit on miss, write R32_UINT.
- **U6** — `renderGpuBvhShadowVisibility` orchestration + wire into the `system.cpp` else-branch (replacing the deleted SDF call); dedicated `RendererBvhShadowState` with invalidate/reset.

Riskiest (from the survey): GPU radix-sort correctness (readback test, not just validation-clean); bottom-up fit UAV-barrier race; skinning-before-build ordering; buffer-growth churn (double, never shrink); perf on low-end no-RT GPUs (measure via the `s_ShadowVisibility` timing scope; refit cadence is the cost knob).

### Phase 4 — Trace abstraction + shadow effect

`nwbTraceOcclusion` (HW: RT-pipeline occlusion ray, terminate-on-first-hit, skip closest-hit, miss = lit; SW: stackless any-hit traversal of the software triangle BVH, accept-first-hit, miss = lit). A screen-space visibility producer pass writing the visibility representation. **HW side DONE** (Units 1-4 + the deferred integration); the SW visibility producer is Phase 3 (U0–U6) above.

### Phase 5 — Deferred integration

Lighting loop multiplies per-light contribution by per-light visibility. Shadow-slot allocation by importance.

### Phase 6 — Transparent / colored transmittance shadows

Visibility becomes `float3` transmittance: HW any-hit accumulates colored transmittance through transparent hits (terminate only on opaque); SW any-hit accumulates colored transmittance through transparent triangle hits (terminate only on opaque). Per-light, composes with the multi-light loop.

### Later (quality/perf, post-Chapter-1)

Clustered/froxel light culling; soft shadows + denoiser; point-light cube/6-tile handling refinements.

## Chapter 2 — Caustics (later, optional, additive)

Separate from shadows by construction: shadow rays are receiver→light and subtractive; caustics are light→receiver and additive (focused light *brightens* spots — the wine-glass crescent). Approach:

- Light-side photon / caustic-map pass: shoot photons from the light, refract (Snell) through refractive surfaces, deposit energy into a caustic buffer, *add* it during lighting. Reuses the TLAS + bindless materials from Layer 1.
- **Colored caustics are free** — each photon carries RGB energy tinted (Beer-Lambert) by the glass before deposit.
- **Dispersion** (wavelength-dependent refraction → rainbow fringes) is the only part that costs extra (per-channel or spectral tracing); optional hero/cinematic feature, or faked.

## Invariants to honor

- `.helper/standard.md` is the source of truth; **re-read it before every code change**. Note the new rule: raw namespace blocks close with a trailing `;` (`namespace foo{ ... };`).
- Banner + long-separator + UTF-8 + CRLF + exact-EOF rules for source and `.slang`/`.slangi`/`.bind`.
- Aligned `Float4` lane packs for GPU-facing/runtime vec4 data (note rules 37/40).
- Shader-driven material contract; support both mesh-shader and compute-emulation paths where geometry is drawn.
- `IDevice` is a non-null invariant; no defensive null checks in the graphics layer (note rule 1-2).
