# Phase 2 — Migrate the per-mesh RT/GI/shadow/caustic buffer arrays to the global heap

**Status:** ✅ **SHIPPED** — designed, implemented, validated, and pushed to both remotes. Built on
Phase 1 (`docs/design/bindless-phase1-rhi-heap.md`), which stood up `GpuDescriptorHeap` (Backend A,
descriptor indexing) and proved the `GpuDescriptorHandle` round-trip for the buffer classes.
**What shipped:** the heap was made *live* and gained its first real consumer — the 20 bounded
per-mesh descriptor arrays shared by the six RT/GI/shadow/caustic passes were migrated onto it and
their classic binding paths deleted. This was the first phase to **delete** a classic binding path
and reroute production passes.
**Target hardware floor:** AMD BC-250 / RADV, unchanged.
**Commit range:** primitive + M1 register-alongside `b6e3d19e` → HW-side cap retirement `42d2ea55`
(the per-step log is §4). Post-migration comment/doc housekeeping continued through `ccb9d4c9`.

> **Reading the line references.** The §2 (P1–P4) anchors point at the **shipped** code and were
> re-verified against the tree. The §1 inventory and §3 (M1–M4) anchors are **as-of design time**
> (the pre-migration tree): the arrays, fill loops, and `MESH_*` slots they cite were the migration's
> starting point and no longer exist after the sweep — they are retained as the historical map.

---

## 0. Thesis

The 20 per-mesh bounded descriptor arrays were, physically, **7 backing buffer pointer-arrays**
owned in `renderer_state.h` and populated only by `rt_swbvh.cpp`; the six passes each re-declared a
subset as a bounded `[[vk::binding]]` array and indexed it by `NonUniformResourceIndex(meshSlot)`.

Phase 2 registered each backing buffer **once** in the Phase-1 global heap (at BVH-build /
scene-mutation time), stored the resulting `GpuDescriptorHandle` per mesh, carried the slot in the
existing per-instance record, and had every pass fetch geometry through the heap accessor instead of
a bespoke array. It then deleted the bounded layouts, sets, fill loops, and mesh-count rebuilds.

This made the heap the **first production consumer** and retired the first slice of the classic
per-mesh binding path — a concrete step toward the end goal of full non-bindless retirement.

---

## 1. Why this was the right first target (the inventory answered the scope question)

A full inventory of the per-mesh arrays (verified against the tree at design time) made the scope
decision for us:

| Property | Finding | Consequence for Phase 2 |
|---|---|---|
| Count | **Exactly 20** bounded arrays, 6 binding layouts, 3 source files | Bounded, enumerable work |
| Resource class | **17 `ByteAddressBuffer` (raw SRV) + 3 `StructuredBuffer<NwbBvhNode>` (structured SRV)** | All one Vulkan type — `STORAGE_BUFFER` |
| Images / samplers / accel-structs | **Zero** | Phase 1's *deferred* image/sampler shader path was **not** on the critical path; AccelStruct's Backend-A rejection was irrelevant here |
| Heap class needed | `GpuDescriptorClass::StorageBuffer` only | The **one class Phase 1 proved end-to-end** |
| Backing store | 7 pointer-arrays (3 HW cap-32 + 4 SW cap-64) in `renderer_state.h:431-433,484-487`, filled only in `rt_swbvh.cpp` | Register once, consume six times |
| Index pattern | `NonUniformResourceIndex(material.meshSlot)` in **all six** passes already | Heap kept the exact pattern; no shader-logic change to indexing |
| Motivating pain | The **32-mesh HW cap** was forced by the single-stage compute descriptor budget of the inline-RayQuery passes (`shadow/binding_slots.h:40-44`) | This target directly delivered the win bindless exists for |

**Conclusion:** the naive worry — "the per-mesh arrays include textures, so Phase 2 must first close
Phase 1's image/sampler gap" — was false. The target was homogeneous SSBO. Phase 2 rode Phase 1's
proven class and stayed narrow.

### 1.1 The array inventory (the migration checklist, as-of design time)

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

Caustic/GI **aliased** the shadow backing buffers (HW caustic/GI reused `m_shadowMesh*`; SW
caustic/GI reused `m_swShadowMesh*`) — so once the shadow buffers were registered (§3.1), the four
non-shadow passes needed only accessor rewrites, not new registration. **How many live reads each
pass actually migrated** turned out to vary — see §3.3, which is where the plan and the build first
diverged.

Excluded (not bounded arrays, stayed classic this phase): the scalar `MESH_INSTANCES`
`StructuredBuffer_SRV(...,1)` and the scalar `RayTracingAccelStruct` TLAS in each layout.

---

## 2. What changed in the primitive first (prerequisites — all shipped)

Four items, all small; §2.2 and §2.4 had been flagged "deferred to Phase 2" by the Phase-1 doc. All
four landed together in the primitive commit `b6e3d19e`.

### 2.1 P1 — Made the heap live

- `Device` constructs `m_gpuDescriptorHeap` but the pre-Phase-2 tree **never called `initialize()`**
  in production — only the self-test did, then tore it down. Phase 2 added
  `m_gpuDescriptorHeap.initialize(heapDesc)` in the production device path (`device.cpp:462`);
  `shutdown()` was already wired.
- Capacity: this domain needs ≤ (64 SW × 4) + (32 HW × 3) ≈ 350 resource slots; the default
  `resourceCapacity = 16384` (clamped to the ~8M device limit) was ample. Effective caps are logged.
- **Frame driver (new):** Phase 1's deferred-free quarantine (`advanceFrame()`) had only been pumped
  by the self-test. A live heap needs `advanceFrame()` once per frame so freed slots recycle — Phase 2
  wired `device->getDescriptorHeap().advanceFrame()` into the real frame loop (`module.cpp:706`) and
  logs the slot high-water mark.
- The self-test switched from its private `initialize/shutdown` to allocating a few handles from the
  live heap and freeing them — so it also proves coexistence with the production consumer.

### 2.2 P2 — Pinned the heap to a reserved fixed set (8 / 9)

- **Problem.** `createPipelineLayoutForBindingLayouts` assigned each layout's SPIR-V set
  **positionally** (flat concat in list order). `BindlessLayoutDesc` had **no** set-index field —
  only the classic `BindingLayoutDesc` had `registerSpaceIsDescriptorSet`/`registerSpace`. So the
  heap landed at whatever position it was added, which would collide with a migrated pipeline's own
  set 0.
- **Change (shipped).** Added an explicit descriptor-set index to `BindlessLayoutDesc` and taught
  `createPipelineLayoutForBindingLayouts` to place each descriptor set at its explicit index, filling
  any gap set with a **cached empty `VkDescriptorSetLayout`** (zero bindings — valid in a pipeline
  layout). The heap resource table is set **8**, sampler table set **9**
  (`m_resourceSetIndex`/`m_samplerSetIndex`).
- **Room.** `maxBoundDescriptorSets = 32` on BC-250 (verified) — sets 8/9 clear the 8 classic sets
  with 22 to spare, even with up-to-8 empty gap sets.
- Shader contract: `bindless/binding_slots.h` `NWB_BINDLESS_HEAP_RESOURCE_SET = 8`, `_SAMPLER_SET = 9`
  (`binding_slots.h:21-22`). One shared value serves every migrated pipeline — the whole point of a
  fixed reserved set.
- The sampler set (9) is unused this phase but is still declared/bound so the contract is frozen for
  later phases; an empty sampler table is harmless.

### 2.3 P3 — Read-only raw + structured shader views over the StorageBuffer class

- Consumers read `ByteAddressBuffer` (17×) and `StructuredBuffer<NwbBvhNode>` (3×). The Phase-1
  `.slangi` declared only `RWByteAddressBuffer g_NwbHeapStorageBuffers[]`.
- Phase 2 added read-only aliases at the **same** `STORAGE_BUFFER` binding:
  - `ByteAddressBuffer g_NwbHeapRawBuffers[]` (`bindless_heap.slangi:58`)
  - `StructuredBuffer<NwbBvhNode> g_NwbHeapBvhNodeBuffers[]` (`bindless_heap_bvh.slangi:30` — split
    into its own include, but pinned to the identical `[[vk::binding(STORAGE_BUFFER, RESOURCE_SET)]]`)
  All three alias one `STORAGE_BUFFER` descriptor array — legal because they are the identical Vulkan
  descriptor type; access (read-only vs RW) and element stride are per-view SPIR-V decorations.
- Accessor helpers `NwbHeapRawBuffer(slot)` (`bindless_heap.slangi:74`) / `NwbHeapBvhNodes(slot)`
  (`bindless_heap_bvh.slangi:39`) wrap `NonUniformResourceIndex`.
- **Novel-risk item — resolved.** The Slang/DXC aliased-binding codegen was validated early against
  the `bindless_roundtrip_cs` cross-check; it worked. The structured view is used directly by the 3
  SW passes, so the documented fallback (manual `ByteAddressBuffer.Load<NwbBvhNode>()`) was **not**
  needed.

### 2.4 P4 — Formally enabled storage-buffer non-uniform indexing (correctness / portability)

- The pre-Phase-2 `backend_context.cpp` required/enabled only
  `shaderSampledImageArrayNonUniformIndexing`; `shaderStorageBufferArrayNonUniformIndexing` **did not
  appear**. The existing bounded SSBO arrays used `NonUniformResourceIndex` and worked (RADV is
  permissive), but Phase 1's self-test only ever used a *uniform* index — so **Phase 2 was the first
  to formally rely on non-uniform SSBO indexing through the heap.**
- Phase 2 made `shaderStorageBufferArrayNonUniformIndexing` a **required + enabled** feature
  (`backend_context.cpp:1287,1418`) with a clear capability log, closing a latent portability gap
  rather than a local bug.

---

## 3. The migration shape (as built)

Backing store → heap registration → per-instance slot → shader fetch → delete bounded path.

### 3.1 M1 — Registered buffers at BVH-build time

- In `rt_swbvh.cpp`: `buildSceneTlas` (HW, `:132`, assigned `m_shadowMesh*Buffers[meshSlot]`) and
  `buildSceneSwBvh` (SW, `:337`, assigned `m_swShadowMesh*Buffers[meshSlot]`) were the single
  registration points. At each assignment, `heap.allocate(StorageBuffer)` and
  `heap.write(handle, BindingSetItem::RawBuffer_SRV(0, buffer))` (or `StructuredBuffer_SRV` for the
  node buffers — `write()` forces the canonical type internally, so the SRV/UAV factory choice was
  immaterial).
- **Handle storage:** parallel per-mesh handle arrays in `renderer_state.h` next to the buffer
  arrays. On scene mutation, the old handles were `heap.free()`d before reallocating (the reset points
  already zero `m_*MeshCount`).
- **Lifetime:** re-registration happens only when the distinct-mesh set changes — the same trigger as
  the old rebuild — not per frame. In-flight safety comes from the Phase-1 deferred-free quarantine
  (the §2.1 per-frame `advanceFrame()`).

M1 shipped **register-alongside** in `b6e3d19e`: both paths were populated while consumers still read
the bounded arrays, so registration could be validated before any accessor moved.

### 3.2 M2 — Carried the slot to the shader

- Previously `meshSlot` indexed several **parallel** bounded arrays with one value. In the heap,
  index/attribute/position/node are **independent** global slots.
- Phase 2 **widened the per-instance/mesh GPU record** with per-buffer heap slots: `indexSlot`,
  `attributeSlot`, `positionSlot`, and `nodeSlot` (SW only), populated from the M1 handles. They ride
  the existing scalar `MESH_INSTANCES` structured buffer the passes already read (a single scalar
  binding — stayed classic). Added additively and always populated, so passes still on the bounded
  path during the staged rollout kept working.
- **The host-index / shader-layout split (this is where the field's meaning changed).** `meshSlot`
  **survived** — but only host-side: it still indexes the per-mesh Vectors and keys the descriptor
  barriers. It became **shader-vestigial**: the shaders now read the new `*Slot` fields, not
  `meshSlot`. Documentation and comments (through `ccb9d4c9`) were reworded to keep this split
  explicit — *host indexing* vs *shader access through the descriptor-index heap layout*.

### 3.3 M3 — Rewrote the shader accessors (and one deletion the plan did not anticipate)

The plan was "replace `g_Nwb<Pass>Mesh<X>[NonUniformResourceIndex(meshSlot)]` with
`NwbHeapRawBuffer(record.<x>Slot)` / `NwbHeapBvhNodes(record.nodeSlot)`" in all six passes. Five
passes matched that. **HW shadow did not**, and this is the first substantive divergence from the
design: the HW shadow trace uses a `FORCE_OPAQUE` opaque first-hit and reads **no geometry at all**,
so `occlusion.slangi` held only a scalar. Its "accessor rewrite" was therefore a **deletion of
already-dead bindings**, not a reroute (commit `753f3c84`).

Corrected per-pass accounting of what actually migrated:

| Pass | Live geometry reads migrated | Note |
|---|---|---|
| HW shadow | **0** | `FORCE_OPAQUE` reads no geometry → M3 was a pure dead-binding deletion |
| SW shadow | 4 (nodes, pos, idx, attr) | full rewrite → `NwbHeapBvhNodes` / `NwbHeapRawBuffer` |
| SW caustic | 4 (nodes, pos, idx, attr) | mirror of SW shadow; reuses the shared `m_swShadowMesh*` buffers |
| HW caustic | **1** (attr) | idx array was already dead (dropped, not rerouted); no pos (reused shadow) |
| SW GI | 4 (nodes, pos, idx, attr) | full rewrite (`gi_sw_trace.slangi`) |
| HW GI | 3 (pos, idx, attr) | inline RayQuery in a compute shader → no nodes; reuses `heap.bindCompute` |

Files touched: `occlusion.slangi`, `sw_shadow_geometry.slangi`, `caustic_hw_common.slangi`,
`caustic_photon_sw_cs.slang`, `gi_hw_trace.slangi`, `gi_sw_trace.slangi`.

### 3.4 M4 — Deleted the bounded C++ paths (and retired the caps by making the arrays dynamic)

Per pass, Phase 2 removed the per-mesh `addItem({Raw,Structured}Buffer_SRV(..., MAX_MESHES))` layout
items, the descriptor **fill loops**, the mesh-count keys in the `ensure*BindingSet` guards, and the
per-mesh layout bindings. The pipelines add the heap's resource layout (set 8) to their
`bindingLayouts` list instead.

Two implementation facts the one-line plan ("delete the `NWB_*_MAX_MESHES` caps") compressed:

- **The caps were removed by making the backing arrays dynamic, not by deleting a constant.** The
  fixed-size per-mesh arrays became `Vector<…, Core::Alloc::GlobalArena>` (SW-side `986ed836`,
  HW-side `42d2ea55`), which is what let `NWB_*_MAX_MESHES` (SW 64 / HW 32) and their aliases
  (`NWB_CAUSTIC_*`/`NWB_GI_*`) retire. **Load-bearing gotcha:** `ArenaAllocator::operator=` is a
  no-op, so an arena `Vector` cannot be rebound after default construction — the arena must be
  threaded through the constructor. `RtShadowState(GlobalArena&)` now binds all 14 Vector members
  (6 HW + 8 SW) at construction; forwarded from `RendererRayTracingState(GlobalArena&)` and the
  `RendererSystem` ctor. `m_swShadowMeshCount` / the mirror counts were **kept** — consumers still
  read them.
- **Removed `MESH_*` slot macros were left as gaps, not renumbered.** Renumbering would shift the
  descriptor layout of surviving bindings; the gaps (e.g. HW-shadow slots 8/9/12) are intentional and
  must not be closed. `g_NwbShadowMeshInstanceIndex` remained live for SW transparent-transmittance
  traversal.

---

## 4. Staged rollout (parity-gated — how it actually landed)

The rollout stayed step-gated as designed, but the **order and the gate** both diverged from the
plan, for one reason: BC-250/RADV HW-hybrid rendering reproducibly wedges the graphics ring and
causes device loss, so HW passes could not be pixel-captured. The build therefore did the **SW side
first** (fully pixel-gated) and validated the HW passes **structurally** (self-test checksum, zero
VUID, heap handle high-water, byte-identical builder registration), with HW runtime accepted as
driver-blocked. Each pass migrated in three committed sub-steps: **4a** layout-scaffolding (inert),
**4b** accessor rewrite, **4c** bounded-path teardown.

Per-step commit log:

1. **Primitive P1–P4 + M1 register-alongside** — `b6e3d19e`. *Gate:* self-test passes; captures
   unchanged (heap live, no consumer yet); handles minted, slot count sane.
2. **HW shadow (M2 + M3)** — record-widen `623b6ef4`, then `occlusion.slangi` dead-binding deletion
   `753f3c84` (not an accessor rewrite; see §3.3).
3. **SW sweep** (each pixel-gated on its own capture, within the pass's temporal noise floor):
   - SW shadow — `86d53634` (4a) / `4400a0e5` (4b) / `7709f2d7` (4c)
   - SW caustic — `90b28695` (4a) / `0fd030af` (4b) / `9e7f83a8` (4c)
   - SW GI — `ce336658` (4a) / `e737355d` (4b) / `82d93ee5` (4c) → **SW side done**
4. **HW sweep** (structural gate — runtime driver-blocked):
   - HW caustic — `d7f9aa09` (4a) / `0c274d66` (4b) / `b973b3d7` (4c); 4b added the reusable core
     method `GpuDescriptorHeap::bindRayTracing` (RT sibling of `bindCompute`)
   - HW GI — `d55ad2b3` (4a) / `41ef8a4d` (4b) / `1c5eb608` (4c) → **entire sweep done**
   - HW shadow teardown — `3e4f3581` (4c; the 4a/4b equivalents were already covered by step 2's
     deletion)
5. **Cap retirement (M4).** Fixed arrays → arena Vectors, `NWB_*_MAX_MESHES` retired: SW-side
   `986ed836`, HW-side `42d2ea55`. *Confirmed:* no bounded per-mesh array remains in the six passes.

---

## 5. Validation & outcome

- **Per-pass parity (SW passes — pixel-gated).** Each migrated SW pass matched its pre-migration
  baseline within the pass's established temporal noise floor: shadow via `soft-shadow-test`;
  caustic via the `tests/ab` harnesses (parity max-diff at/below the noise floor); GI via
  `gi_test_smoke` (migration before→after within the R11/G1/B9-class noise floor). Zero VUID, no
  `bad_alloc`, heap handle high-water sane at each step.
- **HW passes — runtime pixel A/B blocked, accepted as driver-blocked.** BC-250/RADV wedges the
  graphics ring under HW-hybrid rendering (an environment fault, proven independent of the migration
  — identical SW builds also hit it). There is **no `after_hw.bmp`**. The HW validation record is:
  dbg self-test checksum unchanged, zero VUID/error/device-loss/bad_alloc, heap sets 8/9 bound with
  expected handle counts, byte-identical opt relink (the HW builder registers the same
  mesh→buffer), and clean SW-side regression captures. HW-table fill (`buildSceneTlas`) is
  `hardwareShadowSupported`-gated, so it was not SW-exercisable.
- **The 32-mesh HW cap — removed by construction.** The per-mesh arrays are now dynamic arena Vectors
  with **no compile-time cap** (§3.4), so the limit is gone structurally. **Honest caveat:** the
  plan's exit criterion — "prove a scene with > 32 distinct meshes now renders every mesh" — was
  **not** separately captured, because that demonstration needs HW runtime, which stayed
  driver-blocked. The removal is verified structurally (no cap constant, dynamic table, clean
  build/relink), not by a runtime overflow-scene capture.
- **No bounded per-mesh array remains** in the six passes; `NWB_*_MAX_MESHES` and the
  `NWB_CAUSTIC_*`/`NWB_GI_*` aliases are deleted.
- **Heap capacity** high-water is logged; no exhaustion under the stress scene.
- **No new runtime env-var reads** outside tests (house rule held).

**Where the build diverged from the design (summary):**
1. HW shadow's M3 was a **deletion**, not an accessor rewrite (`FORCE_OPAQUE` reads no geometry).
2. HW caustic migrated only **1** live read (attr); its index array was already dead and was dropped.
3. The caps were retired by converting the fixed arrays to **arena Vectors**, not by deleting a
   constant in place — pulling in the `RtShadowState(GlobalArena&)` constructor-binding requirement.
4. Removed `MESH_*` slots were **gapped, not renumbered**; `meshSlot` survived host-side (shader-vestigial).
5. Rollout order was **SW-first, HW-structural** (not the plan's HW-shadow-first, all-pixel-gated),
   because HW runtime pixel A/B is driver-blocked on this host.

---

## 6. Risks & open items (as resolved)

1. **Slang aliased-binding codegen (P3)** — the one genuinely novel shader risk. **Resolved:**
   validated on `bindless_roundtrip_cs` before the sweep; the manual-`Load` fallback was not needed.
2. **Record widening (M2)** — **held:** widened additively, alignment kept, always populated;
   `meshSlot` retained host-side so mid-rollout bounded-path passes were unaffected.
3. **`advanceFrame()` wiring (M1)** — **wired** at `module.cpp:706`; high-water logged, no leak.
4. **Non-uniform feature portability (P4)** — **closed:** `shaderStorageBufferArrayNonUniformIndexing`
   is a required + enabled feature with a capability log.
5. **Cross-pass slot aliasing** — **held:** caustic/GI consume the **same** heap slots the shadow
   registration minted (one registration, many consumers); the record carries one slot per backing
   buffer, not per pass.
6. **Empty gap sets (P2)** — **clean:** placing the heap at set 8 leaves empty descriptor sets in the
   layout; validation is clean at `maxBoundDescriptorSets = 32`. Retired `MESH_*` slots were left
   gapped for the same layout-stability reason.

---

## 7. What Phase 2 handed to Phase 3

- A **live** heap with a real production consumer and a **frozen set-8/9 contract**.
- Proven **non-uniform SSBO bindless fetch** through the heap (Phase 1 only proved uniform-index).
- The reusable pattern — *register at create → slot in record → heap fetch → delete bounded path* —
  ready for the next domain: ordinary renderer **textures/samplers**, which will finally exercise
  Phase 1's deferred image/sampler shader path; and eventually **TLAS** via the descriptor-buffer
  backend (Backend C), the one class Backend A cannot host.
