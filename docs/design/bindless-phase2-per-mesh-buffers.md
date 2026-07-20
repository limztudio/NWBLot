# Phase 2 — Migrate the per-mesh RT/GI/shadow/caustic buffer arrays to the global heap

**Status:** 📐 **DESIGN** — not implemented. Builds on Phase 1
(`docs/design/bindless-phase1-rhi-heap.md`), which stood up `GpuDescriptorHeap` (Backend A,
descriptor indexing) and proved the `GpuDescriptorHandle` round-trip for the buffer classes.
**Scope:** make the heap *live* and migrate its first real consumer — the 20 bounded per-mesh
descriptor arrays shared by the six RT/GI/shadow/caustic passes. This is the first phase that
**deletes** a classic binding path and reroutes production passes.
**Target hardware floor:** AMD BC-250 / RADV, unchanged.

---

## 0. Thesis

The 20 per-mesh bounded descriptor arrays are, physically, **7 backing buffer pointer-arrays**
owned in `renderer_state.h` and populated only by `rt_swbvh.cpp`; the six passes each re-declare
a subset as a bounded `[[vk::binding]]` array and index it by `NonUniformResourceIndex(meshSlot)`.

Phase 2 registers each backing buffer **once** in the Phase-1 global heap (at BVH-build /
scene-mutation time), stores the resulting `GpuDescriptorHandle` per mesh, carries the slot in the
existing per-instance record, and has every pass fetch geometry through the heap accessor instead
of a bespoke array. Then it deletes the six bounded layouts, sets, fill loops, and mesh-count
rebuilds.

This makes the heap the **first production consumer** and retires the first slice of the classic
per-mesh binding path — a concrete step toward the end goal of full non-bindless retirement.

---

## 1. Why this is the right first target (the inventory answered the scope question)

A full inventory of the per-mesh arrays (verified against the tree) makes the scope decision for us:

| Property | Finding | Consequence for Phase 2 |
|---|---|---|
| Count | **Exactly 20** bounded arrays, 6 binding layouts, 3 source files | Bounded, enumerable work |
| Resource class | **17 `ByteAddressBuffer` (raw SRV) + 3 `StructuredBuffer<NwbBvhNode>` (structured SRV)** | All one Vulkan type — `STORAGE_BUFFER` |
| Images / samplers / accel-structs | **Zero** | Phase 1's *deferred* image/sampler shader path is **not** on the critical path; AccelStruct's Backend-A rejection is irrelevant here |
| Heap class needed | `GpuDescriptorClass::StorageBuffer` only | The **one class Phase 1 proved end-to-end** |
| Backing store | 7 pointer-arrays (3 HW cap-32 + 4 SW cap-64) in `renderer_state.h:431-433,484-487`, filled only in `rt_swbvh.cpp` | Register once, consume six times |
| Index pattern | `NonUniformResourceIndex(material.meshSlot)` in **all six** passes already | Heap keeps the exact pattern; no shader-logic change to indexing |
| Motivating pain | The **32-mesh HW cap** is forced by the single-stage compute descriptor budget of the inline-RayQuery passes (`shadow/binding_slots.h:40-44`) | This target directly delivers the win bindless exists for |

**Conclusion:** the naive worry — "the per-mesh arrays include textures, so Phase 2 must first close
Phase 1's image/sampler gap" — is false. The target is homogeneous SSBO. Phase 2 rides Phase 1's
proven class and stays narrow.

### 1.1 The array inventory (migration checklist)

All in **set 0** of their pass layout; cap 32 (HW) or 64 (SW). `R` = RawBuffer_SRV,
`S` = StructuredBuffer_SRV(`NwbBvhNode`).

| Pass (layout site) | Arrays | Shader header |
|---|---|---|
| HW shadow (`rt_shadow.cpp:690`) | idx `:705` R, attr `:706` R, pos `:709` R | `shadow/occlusion.slangi:60,65,70` |
| SW shadow (`rt_shadow.cpp:1028`) | nodes `:1028` S, pos `:1029` R, idx `:1030` R, attr `:1031` R | `shadow/sw_shadow_geometry.slangi:52,55,58,63` |
| SW caustic (`rt_caustics.cpp:635`) | nodes `:635` S, pos `:636` R, idx `:637` R, attr `:638` R | `caustic/caustic_photon_sw_cs.slang` |
| HW caustic (`rt_caustics.cpp:~1220`) | idx `:1227` R, attr `:1228` R (no pos; reuses shadow) | `caustic/caustic_hw_common.slangi:103,106` |
| SW GI (`rt_surfel_gi.cpp:221`) | nodes `:221` S, pos `:222` R, idx `:223` R, attr `:224` R | `gi/gi_sw_trace.slangi:69,72,75,78` |
| HW GI (`rt_surfel_gi.cpp:685`) | pos `:685` R, idx `:686` R, attr `:687` R | `gi/gi_hw_trace.slangi:49,52,55` |

Caustic/GI **alias** the shadow backing buffers (HW caustic/GI reuse `m_shadowMesh*`; SW
caustic/GI reuse `m_swShadowMesh*`) — so once the shadow buffers are registered (§4.1), the four
non-shadow passes need only accessor rewrites, not new registration.

Excluded (not bounded arrays, stay classic this phase): the scalar `MESH_INSTANCES`
`StructuredBuffer_SRV(...,1)` and the scalar `RayTracingAccelStruct` TLAS in each layout.

---

## 2. What must change in the primitive first (prerequisites)

Four items, all small; §2.2 and §2.4 were flagged "deferred to Phase 2" by the Phase-1 doc.

### 2.1 P1 — Make the heap live

- `Device` constructs `m_gpuDescriptorHeap` (`device.cpp:178`) but **never calls `initialize()`** in
  production — only the self-test does, then tears it down. Add
  `m_gpuDescriptorHeap.initialize(desc)` right after `m_descriptorHeapManager.initialize()`
  (`device.cpp:447`). `shutdown()` is already wired (`device.cpp:508`).
- Capacity: this domain needs ≤ (64 SW × 4) + (32 HW × 3) ≈ 350 resource slots; the default
  `resourceCapacity = 16384` (clamped to the ~8M device limit) is ample. Effective caps already logged.
- **Frame driver (new):** Phase 1's deferred-free quarantine (`advanceFrame()`) was only pumped by the
  self-test. A live heap needs `heap.advanceFrame()` once per frame from the real frame loop so freed
  slots recycle. Wire it and log the slot high-water mark.
- The self-test switches from its private `initialize/shutdown` to allocating a few handles from the
  live heap and freeing them — so it also proves coexistence with the production consumer.

### 2.2 P2 — Pin the heap to a reserved fixed set (8 / 9)

- **Problem.** `createPipelineLayoutForBindingLayouts` (`resource_bindings.cpp:464-542`) assigns each
  layout's SPIR-V set **positionally** (flat concat in list order). `BindlessLayoutDesc`
  (`rhi/binding.h:159-171`) has **no** set-index field — only the classic `BindingLayoutDesc` has
  `registerSpaceIsDescriptorSet`/`registerSpace` (`:113-129`). So today the heap lands at whatever
  position it is added (set 0/1 in the self-test, because it is added first/second), which collides
  with a migrated pipeline's own set 0.
- **Change.** Add an explicit descriptor-set index to `BindlessLayoutDesc` (mirror the classic flag:
  `descriptorSetIndex` + `descriptorSetIndexIsExplicit`, or reuse the `registerSpace*` naming). Teach
  `createPipelineLayoutForBindingLayouts` to place each descriptor set at its explicit index, filling
  any gap set with a **cached empty `VkDescriptorSetLayout`** (zero bindings — valid in a pipeline
  layout). Set the heap resource table to set **8**, sampler table to set **9**
  (`m_resourceSetIndex`/`m_samplerSetIndex`, `backend.h:1575-1576`).
- **Room.** `maxBoundDescriptorSets = 32` on BC-250 (verified) — sets 8/9 clear the 8 classic sets
  (`s_MaxBindingLayouts = 8`, `foundation.h:137`) with 22 to spare, even with up-to-8 empty gap sets.
- Bind side already uses those members as `firstSet`, so only their values change.
- Shader contract: `bindless/binding_slots.h` `NWB_BINDLESS_HEAP_RESOURCE_SET` 0→8, `_SAMPLER_SET`
  1→9. One shared value serves every migrated pipeline — the whole point of a fixed reserved set.
- The sampler set (9) is unused this phase but is still declared/bound so the contract is frozen for
  later phases; an empty sampler table is harmless.

### 2.3 P3 — Read-only raw + structured shader views over the StorageBuffer class

- Consumers read `ByteAddressBuffer` (17×) and `StructuredBuffer<NwbBvhNode>` (3×). The Phase-1
  `.slangi` declares only `RWByteAddressBuffer g_NwbHeapStorageBuffers[]`.
- Add, **co-located at the same** `[[vk::binding(STORAGE_BUFFER, RESOURCE_SET)]]`, read-only aliases:
  - `ByteAddressBuffer  g_NwbHeapRawBuffers[]`
  - `StructuredBuffer<NwbBvhNode> g_NwbHeapBvhNodeBuffers[]`
  All three alias one `STORAGE_BUFFER` descriptor array — legal because they are the identical Vulkan
  descriptor type; access (read-only vs RW) and element stride are per-view SPIR-V decorations.
- Accessor helpers `NwbHeapRawBuffer(slot)` / `NwbHeapBvhNodes(slot)` wrap `NonUniformResourceIndex`.
- **Novel-risk item:** validate the Slang/DXC aliased-binding codegen early against one migrated
  shader (see §6.1). **Fallback if aliasing misbehaves:** drop the structured view and load nodes from
  the raw view via `ByteAddressBuffer.Load<NwbBvhNode>(i * sizeof(NwbBvhNode))` in the 3 SW passes —
  removes the alias entirely at the cost of a slightly more verbose node fetch.

### 2.4 P4 — Formally enable storage-buffer non-uniform indexing (correctness / portability)

- `backend_context.cpp` requires/enables only `shaderSampledImageArrayNonUniformIndexing`
  (`:1283`,`:1403`); the string `shaderStorageBufferArrayNonUniformIndexing` **does not appear**. The
  existing bounded SSBO arrays use `NonUniformResourceIndex` and work — RADV is permissive — but Phase
  1's self-test only ever used a *uniform* index, so **Phase 2 is the first to formally rely on
  non-uniform SSBO indexing through the heap.**
- Enable `shaderStorageBufferArrayNonUniformIndexing`, and audit
  `descriptorBindingStorageBufferUpdateAfterBind` (the heap's storage table is UPDATE_AFTER_BIND).
  Since the engine already can't render these passes without SSBO non-uniform indexing, make it a
  required feature with a clear capability log — this closes a latent portability gap, not a local bug.

---

## 3. The migration shape

Backing store → heap registration → per-instance slot → shader fetch → delete bounded path.

### 3.1 M1 — Register buffers at BVH-build time

- In `rt_swbvh.cpp`: `buildSceneTlas` (HW, `:132`, assigns `m_shadowMesh*Buffers[meshSlot]` at
  `:210-212`) and `buildSceneSwBvh` (SW, `:337`, assigns `m_swShadowMesh*Buffers[meshSlot]` at
  `:424-429`) are the single registration points. At each assignment, `heap.allocate(StorageBuffer)`
  and `heap.write(handle, BindingSetItem::RawBuffer_SRV(0, buffer))` (or `StructuredBuffer_SRV` for
  the node buffers — `write()` forces the canonical `StructuredBuffer_UAV`→`STORAGE_BUFFER` type
  internally, `gpu_descriptor_heap.cpp:307-341`, so the SRV/UAV factory choice is immaterial).
- **Handle storage:** parallel per-mesh handle arrays in `renderer_state.h` next to the buffer arrays
  (e.g. `m_shadowMeshIndexHandles[32]`, one per backing buffer). On scene mutation, `heap.free()` the
  old handles before reallocating (the reset points already zero `m_*MeshCount`, `:164`,`:382`).
- **Lifetime:** re-registration happens only when the distinct-mesh set changes — the same trigger as
  today's rebuild — not per frame. In-flight safety comes from the Phase-1 deferred-free quarantine
  (needs the §2.1 per-frame `advanceFrame()`).

### 3.2 M2 — Carry the slot to the shader

- Today `meshSlot` (`instance_material.slangi:30`) indexes several **parallel** bounded arrays with one
  value. In the heap, index/attribute/position/node are **independent** global slots.
- **Widen the per-instance/mesh GPU record** with per-buffer heap slots: `indexSlot`, `attributeSlot`,
  `positionSlot`, and `nodeSlot` (SW only). Populate from the M1 handles. It rides the existing scalar
  `MESH_INSTANCES` structured buffer the passes already read (a single scalar binding — stays classic).
- This is the crux data change: `meshSlot` (array-local) → several `*Slot` (global heap) on the record.
  Add the fields **additively** and always populate them, so passes still on the bounded path during
  the staged rollout keep working.

### 3.3 M3 — Rewrite the six shader accessors

Per pass, replace `g_Nwb<Pass>Mesh<X>[NonUniformResourceIndex(meshSlot)]` with
`NwbHeapRawBuffer(record.<x>Slot)` / `NwbHeapBvhNodes(record.nodeSlot)`. Include `bindless_heap.slangi`;
delete the per-pass bounded array declarations and their `NWB_*_BINDING_MESH_*` slots. Files:
`occlusion.slangi`, `sw_shadow_geometry.slangi`, `caustic_hw_common.slangi`,
`caustic_photon_sw_cs.slang`, `gi_hw_trace.slangi`, `gi_sw_trace.slangi`.

### 3.4 M4 — Delete the bounded C++ paths

Per pass: remove the per-mesh `addItem({Raw,Structured}Buffer_SRV(..., MAX_MESHES))` layout items, the
descriptor **fill loops** (`rt_shadow.cpp:766-770,1311-1316`; `rt_caustics.cpp:763-768,1373-1376`;
`rt_surfel_gi.cpp:623-628,773-777`), the mesh-count keys in the `ensure*BindingSet` guards
(`rt_shadow.cpp:861-866`, etc.), and finally the `NWB_*_MAX_MESHES` caps. The pipelines add the heap's
resource layout (set 8) to their `bindingLayouts` list instead.

---

## 4. Staged rollout (parity-gated — do NOT big-bang)

Each step has an independent capture gate so a regression is localized:

1. **P1–P4 (primitive).** Heap live, set 8/9, raw/structured views, feature enabled.
   *Gate:* self-test still passes; **all** smoke/stress captures unchanged (heap live, no consumer yet).
2. **M1 registration alongside the existing bounded arrays.** Both paths populated; consumers still on
   bounded arrays. *Gate:* captures unchanged; log confirms handles minted, slot count sane.
3. **Migrate ONE pass — HW shadow** (smallest: 3 arrays, `occlusion.slangi`). M2 record widen + M3
   accessor + M4 delete HW-shadow bounded path. *Gate:* `soft-shadow-test` (authoritative) + shadow
   smoke/stress capture parity.
4. **Sweep the remaining five**, one at a time, each gated on its own capture: SW shadow → HW caustic →
   SW caustic → HW GI → SW GI. Caustic/GI alias the shadow buffers, so after step 2 they need only
   accessor rewrites. Gates: `tests/ab` caustic harnesses; `gi_test_smoke` + stress.
5. **Cleanup.** Remove dead `NWB_*_MAX_MESHES` caps and `MESH_*` slot macros; confirm no bounded
   per-mesh array remains in the six passes.

---

## 5. Validation & exit criteria

- **Per-pass parity:** each migrated pass matches its pre-migration baseline within the pass's
  established capture tolerance. Shadow → `soft-shadow-test`; caustic → `tests/ab` harnesses; GI →
  `gi_test_smoke` + stress.
- **The 32-mesh HW cap is gone:** a scene with > 32 distinct meshes that previously clamped/overflowed
  (`rt_swbvh.cpp:200-205`) now renders every mesh. Prove it with a scene or assertion — this is the
  motivating win, not incidental.
- **No bounded per-mesh array remains** in the six passes; `NWB_*_MAX_MESHES` deleted.
- Heap capacity usage logged; no exhaustion under the stress scene.
- No new runtime env-var reads outside tests (house rule).

---

## 6. Risks & open items

1. **Slang aliased-binding codegen (P3)** — the one genuinely novel shader risk. Validate DXC/Slang
   output on one migrated shader before the sweep. Documented fallback: manual
   `ByteAddressBuffer.Load<NwbBvhNode>()` for the 3 node arrays.
2. **Record widening (M2)** — the per-instance record is shared C++/shader; widen additively, keep
   alignment, always populate, so passes mid-rollout on the bounded path are unaffected.
3. **`advanceFrame()` wiring (M1)** — miss the per-frame pump and freed slots never recycle (slow
   leak). Low pressure (re-registration only on scene mutation), but wire it and log the high-water.
4. **Non-uniform feature portability (P4)** — formally enabling
   `shaderStorageBufferArrayNonUniformIndexing`; a target lacking it makes the heap SSBO path invalid
   there. Local RADV supports it de-facto; gate with a required-feature check + clear log.
5. **Cross-pass slot aliasing** — caustic/GI reuse the shadow buffers and must consume the **same**
   heap slots the shadow registration minted (one registration, many consumers). The record carries one
   slot per backing buffer, not per pass.
6. **Empty gap sets (P2)** — placing the heap at set 8 while these RT passes use only set 0 means 7
   empty descriptor sets in the pipeline layout. Harmless at `maxBoundDescriptorSets = 32`, but confirm
   validation is clean.

---

## 7. What Phase 2 hands to Phase 3

- A **live** heap with a real production consumer and a **frozen set-8/9 contract**.
- Proven **non-uniform SSBO bindless fetch** through the heap (Phase 1 only proved uniform-index).
- The reusable pattern — *register at create → slot in record → heap fetch → delete bounded path* —
  ready for the next domain: ordinary renderer **textures/samplers**, which will finally exercise Phase
  1's deferred image/sampler shader path; and eventually **TLAS** via the descriptor-buffer backend
  (Backend C), the one class Backend A cannot host.
