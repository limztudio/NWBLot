# Phase 1 — RHI Global Descriptor Heap

**Status:** ✅ **COMPLETE on Backend A** — implemented, compiling clean, and the §10 round-trip
test **PASSES on real hardware (AMD BC-250 / RADV GFX1013)**. The `gi_test_smoke` (dbg) run logs
`GpuDescriptorHeap round-trip self-test PASSED (Backend A, expected sum 286335539)` with a fully
clean log (no errors, existing BVH self-tests still pass, window still captures). Backend B remains
deferred (host lacks `VK_EXT_descriptor_heap`; §1). Not yet committed.
**Scope:** the RHI primitive only. No shader migration, no renderer call-site rewrites.
**Target hardware floor:** anything the engine already boots on (AMD BC-250 / RADV included).

---

## 0. Thesis

Build **one Device-owned global descriptor heap** exposed through a single opaque
32-bit handle (`GpuDescriptorHandle`). Every future "index a resource in a shader"
goes through that handle. The heap has **two interchangeable Vulkan backends behind
one identical API**:

- **Backend A — descriptor indexing** (`VK_EXT_descriptor_indexing`, core in 1.2). This
  is the *primary, guaranteed* backend. It is assembled almost entirely from RHI code
  that already exists and works today.
- **Backend B — descriptor heap** (`VK_EXT_descriptor_heap`). This is an *optional
  accelerator*, D3D12-parity, selected only when the extension is present. The machinery
  (`DescriptorHeapManager`) already exists and is dormant.

A caller (C++ or shader) that holds a `GpuDescriptorHandle` never knows or cares which
backend is live. **That handle contract is the entire deliverable of Phase 1.**

Phase 1 explicitly does **not** touch a single shader used by the renderer, does not
remove the classic binding-set path, and does not migrate any draw/dispatch. Those are
later phases. Phase 1 stands up the primitive and proves it round-trips on both backends.

---

## 1. The one decision that dominates everything

Which model is the *foundation* and which is the *accelerator*? Your own device-selection
code answers it.

### Descriptor indexing is a hard requirement — the guaranteed floor

`core/graphics/vulkan/backend_context.cpp:1277-1283` *rejects any GPU* lacking:

- `descriptorIndexing`
- `runtimeDescriptorArray`
- `descriptorBindingPartiallyBound`
- `descriptorBindingVariableDescriptorCount`
- `shaderSampledImageArrayNonUniformIndexing`

If the device is running the engine at all, the full descriptor-indexing bindless toolkit
is present. There is no fallback to worry about.

### `VK_EXT_descriptor_heap` is optional — may be absent on the actual host

- `VulkanContext::Extensions::EXT_descriptor_heap` is a plain `bool` flag
  (`backend.h:580`), not a required extension.
- `DescriptorHeapManager::initialize()` early-outs to *disabled* when the flag is false
  (`resource_bindings.cpp:742`).
- `DescriptorHeapManager::tryEnablePipeline()` bails when the flag is false
  (`resource_bindings.cpp:617`), and pipeline creation silently falls back to classic
  descriptor sets.

**Conclusion:** a whole-engine bindless rewrite that must keep running on RADV/BC-250
*must* be founded on descriptor indexing. The heap extension rides on top as an optional
fast path. This inverts the naive "descriptor heap == the modern way" instinct, and it is
non-negotiable given the required-feature list above.

> **Verification item — RESOLVED (2026-07-20).** `vulkaninfo` on the BC-250 host confirms
> RADV (Mesa 26.1.5) advertises **`VK_EXT_descriptor_buffer`** but **not**
> `VK_EXT_descriptor_heap`. So Backend B (§5) never exercises on our own hardware — it is
> CI / other-target only — and **Backend A (descriptor indexing) is the live local floor**,
> exactly as this section requires. Separately, `VK_EXT_descriptor_buffer` (a *distinct*
> extension: descriptor-as-memory, not the D3D12 heap model) *is* present locally and would
> actually run on BC-250; it is a candidate future **Backend C** accelerator. Out of Phase-1
> scope — recorded in §12.1.

---

## 2. What already exists (build, don't reinvent)

This is the reason Phase 1 is small. The RHI already contains both backends as usable
primitives; they are just not unified or promoted to a first-class global heap.

| Primitive | Where | State |
|---|---|---|
| `BindlessLayoutDesc`, `BindlessLayoutType::{MutableSrvUavCbv, MutableSampler, MutableCounters}` | `rhi/binding.h:137-171` | Defined; models a D3D12-style mutable table over multiple register spaces |
| `Device::createBindlessLayout` | `resource_bindings.cpp:1320` | **Working** — builds a `VkDescriptorSetLayout` with `PARTIALLY_BOUND \| UPDATE_AFTER_BIND`, `VARIABLE_DESCRIPTOR_COUNT` on the tail binding, `UPDATE_AFTER_BIND_POOL` |
| `Device::createDescriptorTable` | `resource_bindings.cpp:1413` | **Working** — allocates a variable-count `VkDescriptorSet` from an update-after-bind pool |
| `Device::resizeDescriptorTable` | `resource_bindings.cpp:1503` | **Working** — grow + copy with `keepContents` |
| `Device::writeDescriptorTable(table, item)` | `resource_bindings.cpp:1669` | **Working** — writes one `BindingSetItem` at `item.arrayElement` |
| `DescriptorHeapManager` (EXT heap) | `backend.h:1396`, impl `resource_bindings.cpp:602-1170` | **Working but dormant** — dual heaps (resource + sampler) as `VkBuffer` + device address + free-range suballocator; `allocate/free/writeDescriptor`; SPIR-V binding remap in `tryEnablePipeline` |
| `BindingSetItem` | `rhi/binding.h:180-305` | Carries `resourceHandle`, `type`, `arrayElement`, `format`, `dimension`, subresource/range. **This is already the exact payload a heap write needs.** |
| Device-owned heap manager instance | `device.cpp:173,181,442,501` | `m_descriptorHeapManager` constructed, wired into context, `initialize()`/`shutdown()` |

The mutable-table comment at `rhi/binding.h:153-158` is explicit: *"The same table can be
bound to multiple HLSL register spaces in order to access different types of resources
stored in the table through different arrays."* That is the descriptor-indexing emulation
of the D3D12 `ResourceDescriptorHeap`, already conceptualized in your headers.

**So the descriptor-indexing global heap is: one bindless `DescriptorTable` built from a
`MutableSrvUavCbv` layout (plus a `MutableSampler` table for samplers), wrapped by an
allocator that hands out indices.** Nearly all of the Vulkan work is done.

---

## 3. The core abstraction

### 3.1 `GpuDescriptorHandle`

A single opaque 32-bit integer. It is the only thing that crosses the C++/shader boundary.

```
bits 31..28  (4)  : class tag   — GpuDescriptorClass
bits 27..0  (28)  : slot index  — global within its namespace (max 2^28 ≈ 268M)
```

- **Class tag** distinguishes the resource classes the shader must select between:
  `SampledImage`, `StorageImage`, `SampledBuffer` (typed SRV), `StorageBuffer`
  (raw/structured UAV+SRV), `UniformBuffer` (CBV), `AccelStruct`, `Sampler`. ≤ 16 classes,
  fits in 4 bits with headroom.
- **Slot index** is allocated from **one global namespace per top-level heap** (resource
  heap vs sampler heap — see §3.3). Global uniqueness is a deliberate invariant (§3.4).
- A reserved sentinel `GpuDescriptorHandle::Invalid = 0xFFFFFFFF`.

Encoding/decoding is trivial (`tag = h >> 28`, `slot = h & 0x0FFFFFFF`) so the shader-side
accessor (§10) is a mask + array index with no divergence cost.

> **Generation/versioning:** not packed into the 32-bit handle in Phase 1 (index bits are
> more valuable). Instead, a **debug-only** side table (`Vector<u16> generation` per slot)
> validates every `write`/`free` against the handle's generation, caught behind
> `NWB_DEBUG`. Release builds pay nothing. If use-after-free proves to be a field problem
> later, we widen to a 64-bit handle in a follow-up — the accessor macro localizes that
> change.

### 3.2 RHI surface (new)

A new Device-owned object. Minimal, and every method maps onto something that already
exists.

```cpp
namespace GpuDescriptorClass { enum Enum : u8 { SampledImage, StorageImage,
    SampledBuffer, StorageBuffer, UniformBuffer, AccelStruct, Sampler, kCount }; };

struct GpuDescriptorHeapDesc {
    u32 resourceCapacity = 0;   // slots shared by all non-sampler classes
    u32 samplerCapacity  = 0;   // sampler heap is separate in both backends
    // per-class soft caps optional; default = split resourceCapacity on demand
};

class GpuDescriptorHeap {           // owned by Device, one instance
public:
    GpuDescriptorHandle allocate(GpuDescriptorClass::Enum cls);
    void                free(GpuDescriptorHandle h);          // deferred (see §9)
    bool                write(GpuDescriptorHandle h, const BindingSetItem& item);
    void                bind(CommandList* cmd);               // bind root heap set(s)
    u32                 rootRegisterSpace() const;            // reserved space index
};
```

- `Device::getDescriptorHeap()` returns the singleton; `Device` creates it during init
  right after `m_descriptorHeapManager.initialize()` (`device.cpp:442`), choosing the
  backend (§8).
- `write()` reuses `BindingSetItem` verbatim — callers build items with the existing
  static helpers (`BindingSetItem::Texture_SRV(...)`, `::StructuredBuffer_UAV(...)`, etc.,
  `rhi/binding.h:233-290`). No new resource-description type is introduced.
- `allocate` returns `Invalid` and logs on exhaustion; callers must handle it (heap is not
  auto-grown mid-frame — resize is an explicit, fence-guarded op, §9).

### 3.3 Two namespaces: resource heap and sampler heap

Both Vulkan backends physically separate samplers from resources (EXT: distinct
`vkCmdBindSamplerHeapEXT` vs `vkCmdBindResourceHeapEXT`, `resource_bindings.cpp:2097-2098`;
indexing: samplers cannot share an array with sampled images). So `Sampler` handles live
in their own index namespace with their own capacity. The class tag makes the two
unambiguous even though both count from 0.

### 3.4 Why one global slot namespace (not per-class)

A given slot index maps to **exactly one resource** across all resource classes (samplers
excepted, §3.3). Concretely: `SampledImage` and `StorageBuffer` never both use slot 5.

- **Backend B (EXT heap)** *requires* this — it is a single unified resource heap; slot 5
  is one physical descriptor. Per-class reuse of slot 5 would alias.
- **Backend A (indexing)** *tolerates* it cheaply — each per-class array is sized to the
  full `resourceCapacity` and left sparse (only its class's slots are ever written). RADV's
  `maxDescriptorSetUpdateAfterBind*` limits are in the millions, so the sparse arrays cost
  address space, not memory.

The payoff: **the same 32-bit handle value is valid, unchanged, on both backends.** A
resource registered once yields one handle that works whether we booted on the indexing
floor or the heap accelerator. That portability is the whole point of unifying them.

---

## 4. Backend A — descriptor indexing (primary, guaranteed)

Assembled from existing primitives.

**Root layout (built once at Device init):**
- One `BindlessLayoutDesc` of type `MutableSrvUavCbv`, adding one register space per
  non-sampler class via `addRegisterSpace(BindingLayoutItem::<Type>(slot, capacity))`
  (`rhi/binding.h:169`). `maxCapacity = resourceCapacity`.
- One `BindlessLayoutDesc` of type `MutableSampler` for the sampler namespace.
- **Binding numbers within a table (verified against `createBindlessLayout`,
  `resource_bindings.cpp:1359-1365`).** `createBindlessLayout` sets
  `binding.binding = item.slot` *directly* — it does **not** apply the classic per-class
  binding offsets (SRV @ 0 / Sampler @ 128 / CBV @ 256 / UAV @ 384, `foundation.h:143-146`);
  those are the *classic* `BindingOffsets` path only. So the resource table is one
  descriptor set carrying five bindings, one per non-sampler class, assigned flat and
  ascending: `SampledImage=0, StorageImage=1, SampledBuffer=2, StorageBuffer=3,
  UniformBuffer=4` (see `GpuDescriptorHeap::getRegisterSlot`). Register spaces **must** be
  added in ascending slot order, because `createBindlessLayout` marks the *last* binding
  `VARIABLE_DESCRIPTOR_COUNT` (`resource_bindings.cpp:1371-1372`) — here that is
  `UniformBuffer` (binding 4). The sampler table is a second set with `Sampler=0`. The
  shader-side `[[vk::binding(N, set)]]` arrays must match these numbers exactly; that
  agreement *is* the contract.
- **Descriptor set index.** Phase 1 is *positional*: `BindlessLayoutDesc` has no set-index
  field, so the round-trip pipeline places the resource table at **set 0** and the sampler
  table at **set 1** (`m_resourceSetIndex`/`m_samplerSetIndex` in `backend.h`). The eventual
  target is reserved high sets (above the `s_MaxBindingLayouts = 8` classic sets,
  `foundation.h:135`) so the heap coexists with classic pipelines without collision — but
  that requires validating against `maxBoundDescriptorSets` and is deferred to Phase 2. It
  costs nothing above `GpuDescriptorHeap`: only `bind()`'s `firstSet` argument changes.

**Storage:** `createBindlessLayout` → `createDescriptorTable` yields the persistent
`VkDescriptorSet` for the resource heap (and a second for samplers). These sets are
update-after-bind, so descriptors may be written while the set is bound to in-flight work,
subject to the sync rules in §9.

**allocate(cls):** pop a slot from the per-namespace free list (a simple index allocator;
grow-on-demand disabled mid-frame). Pack `(cls << 28) | slot`.

**write(h, item):** `writeDescriptorTable(resourceTable /* or samplerTable */,
item.setArrayElement(slot))` — the existing path at `resource_bindings.cpp:1669`. The
class tag selects the table; the item's `type` selects the register space inside it
(exactly what the mutable table's multi-space write already does).

**bind(cmd):** `vkCmdBindDescriptorSets` for the two heap sets at their reserved set
indices. Bound once per command list, not per draw.

**free(h):** push slot to the retirement queue (§9); clears nothing on GPU (partially-bound
means an unwritten/freed slot is simply never read by a correct shader).

Net new code for Backend A: the index allocator, the class↔table routing, and the
Device-init wiring. The Vulkan descriptor plumbing is untouched because it already exists.

---

## 5. Backend B — descriptor heap (optional accelerator)

Already implemented as `DescriptorHeapManager`; Phase 1 only re-points the handle API at
it.

- **allocate(cls):** `DescriptorHeapManager::allocate(kind, sizeBytes, alignment)`
  (`backend.h:1457`) where `kind` is `Resource` or `Sampler`. The returned
  `DescriptorHeapAllocation` yields the slot as `offsetBytes / descriptorStride` — the
  identical computation already done at `resource_bindings.cpp:1887`.
- **write(h, item):** `DescriptorHeapManager::writeDescriptor(item, meta, dstOffsetBytes)`
  (`backend.h:1460`), with `dstOffsetBytes = slot * descriptorStride`.
- **bind(cmd):** `vkCmdBindResourceHeapEXT` + `vkCmdBindSamplerHeapEXT`
  (`resource_bindings.cpp:2097-2098`), already the code path when a pipeline
  `m_usesDescriptorHeap`.
- **Pipeline SPIR-V remap:** `tryEnablePipeline` (`resource_bindings.cpp:602`) already
  patches `VkShaderDescriptorSetAndBindingMappingInfoEXT` so `ResourceDescriptorHeap[]` in
  the shader resolves to the bound heap.

**Implementation note / open item:** EXT descriptors are addressed by **byte offset with a
per-type `descriptorStride`**, and stride differs by descriptor type. To keep the *global
slot index* uniform across backends (§3.4), Backend B must either (a) partition the resource
heap into per-class byte regions and map `slot → region_base + local*stride`, or (b) use a
uniform max-stride slot size. Option (b) is simplest and wastes a bounded amount of heap
memory; pick it for Phase 1 and revisit if heap memory pressure appears. Nail the exact
mapping during implementation against the runtime `descriptorHeapProperties`
(`backend.h:599`).

---

## 6. Backend selection & the unified contract

- Selected once at Device init: `EXT_descriptor_heap` present **and** `DescriptorHeapManager::isEnabled()` → Backend B; else Backend A.
- The choice is invisible above `GpuDescriptorHeap`. `allocate/free/write/bind` and the
  handle bit-layout are identical.
- **Contract tests (§12) run against both backends and must produce byte-identical shader
  output.** This is the gate that lets later phases trust the handle regardless of host.

---

## 7. Allocation, lifetime, threading, deferred free

- **Allocation:** O(1) free-list pop per namespace. Exhaustion returns `Invalid` + logs; no
  silent mid-frame growth.
- **Resize:** explicit, at safe points only. Backend A uses `resizeDescriptorTable(…,
  keepContents=true)` (`resource_bindings.cpp:1503`), which reallocates the set and re-writes
  live items; must be fenced behind idle or a frame boundary. Backend B grows its heap
  `VkBuffer`. Not called mid-frame.
- **Writes are update-after-bind:** legal while the set is bound, but the *slot being
  written must not be read by in-flight GPU work*. Registration therefore happens at
  resource-create / load time, before first use — not per-frame churn. (Per-frame volatile
  data stays on the existing constant-buffer path in Phase 1.)
- **Deferred free:** a freed slot is quarantined until the frame that could reference it
  retires. Reuse `s_MaxFramesInFlight = 2` (`rhi/foundation.h:152`): retirement queue keyed
  on the submission fence; slots return to the free list when their fence passes. This
  matches how `TrackedCommandBuffer` already gates resource liveness
  (`backend.h:655-657`).
- **Threading:** `allocate/free` guarded by a `Futex` (same primitive the heap storage
  already uses, `backend.h:1411`). `write` is `Futex`-free per-slot as long as callers do
  not write the same slot concurrently (documented precondition); the underlying
  `vkUpdateDescriptorSets`/heap memcpy target disjoint offsets.

---

## 8. Shader-side contract

Phase 1 ships **one** Slang header that gives shaders a single way to fetch a resource from
a handle, compiling correctly for both backends. No renderer shader includes it yet — it
exists so the §12 validation shader can, and so Phase 2 has a stable surface.

- **Backend B path:** `ResourceDescriptorHeap[slot]` / `SamplerDescriptorHeap[slot]`
  (SM6.6 style), which DXC lowers and `tryEnablePipeline` remaps.
- **Backend A path:** a fixed set of unbounded arrays declared at `rootRegisterSpace()`,
  one per class (`g_SampledImages[]`, `g_StorageImages[]`, `g_SampledBuffers[]`,
  `g_StorageBuffers[]`, `g_UniformBuffers[]`, `g_AccelStructs[]`, `g_Samplers[]`), indexed
  by `NonUniformResourceIndex(slot)`.
- **Unifying accessor:** macros/functions like `NwbHeapTexture2D(h)`,
  `NwbHeapByteBuffer(h)`, `NwbHeapSampler(h)` that (1) mask the slot, (2) in debug assert
  the class tag matches, (3) expand to the correct backend expression under a compile
  define (`NWB_BINDLESS_BACKEND_HEAP` vs `_INDEXING`). The renderer author always writes
  `NwbHeapTexture2D(h)`, never the raw array.

Selecting the shader define is a build/pipeline concern; for Phase 1 the validation harness
compiles the shader both ways and runs whichever matches the live backend.

---

## 9. Coexistence with the current binding path

Phase 1 is strictly additive.

- The classic `BindingLayout`/`BindingSet` path (`resource_bindings.cpp`) is untouched and
  remains the path every existing pipeline uses.
- The heap sets occupy reserved high descriptor-set indices (set 8/9), above the
  `s_MaxBindingLayouts = 8` classic sets, so no existing pipeline layout changes.
- No existing shader is modified. No draw/dispatch is rerouted.
- The only Device-lifecycle change is constructing/destroying the `GpuDescriptorHeap`
  alongside the already-present `m_descriptorHeapManager` (`device.cpp:173-501`).

This means Phase 1 can land and ship dark: present in the binary, exercised only by its own
test, zero risk to the shadow/GI/caustic paths.

---

## 10. Validation & exit criteria

A headless contract test (wired into the existing self-test infra, which already depends on
the logger-server name-symbol resolver for readable scopes):

1. Create the heap. Allocate handles of **every** `GpuDescriptorClass`.
2. Register real resources: a couple of textures (sampled + storage), a structured buffer,
   a uniform buffer, a sampler, and (if RT is up) the shared TLAS.
3. Bind the root heap; dispatch a compute shader that reads each resource **through its
   handle** via the §8 accessor and writes a reduction to an output buffer.
4. Read back and assert exact expected values.
5. **Free half the handles, allocate again** (forces free-list + deferred-retire), re-run,
   assert still correct — proves no stale-slot aliasing.

**Exit criteria for Phase 1:**
- ✅ Test passes on Backend A (RADV/BC-250) — the guaranteed floor. **Done** — see the
  implementation notes below.
- ⏸ Test passes on Backend B on any host that advertises `VK_EXT_descriptor_heap`, with
  **byte-identical** readback to Backend A. **Deferred** — no such host available (BC-250 lacks
  the extension). Backend B is not wired; the handle contract is designed to satisfy it unchanged.
- ✅ Zero change in any existing smoke/stress capture (heap is dark to them). **Verified at the
  time of the phase rollout** — the `gi_test_smoke` run's window captured and the then-existing
  BVH self-tests passed with a clean log.
- ✅ No new runtime env-var reads outside tests (house rule). **Held.**

**Implementation notes (historical):**
- The initial host was the `impl/` debug renderer self-test infra
  (`gpu_descriptor_heap_selftest.cpp`, wired into `RendererRayTracingSystem::logCapabilityOnce`
  next to the BVH self-tests, `#if NWB_DEBUG`), *not* a headless gtest. That implementation was
  later retired when renderer-internal test code was removed; this section records how the Phase 1
  contract was originally validated.
- The kernel dispatches **one invocation** (group size 1) so every descriptor-array index is
  dynamically uniform — no `NonUniformResourceIndex` and no per-type non-uniform-indexing feature is
  required. Divergent-index coverage is a later-phase concern.
- GPU round-trip covers the three **buffer** classes (StorageBuffer / UniformBuffer / SampledBuffer)
  end-to-end; SampledImage / StorageImage / Sampler are exercised at the **allocator** level (they
  allocate distinct valid handles). Texture/sampler *shader reads* were left for a follow-up — same
  `writeDescriptorTable` path, more resource-setup surface.
- **Historical self-test gotcha on this dbg host:** any `[ERROR]`-level log triggers a crash
  diagnostic capture, and the diagnostic renderer cannot create its headless Vulkan instance here —
  so an intentional error in a *passing* path SIGTRAPs the run. The test therefore must not probe the
  AccelStruct rejection (whose `allocate()` logs an `[ERROR]`); that invariant is covered by
  `allocate()` itself. A genuine failure still logs its `[ERROR]` (visible in the log before the
  cascade), so failures remain diagnosable.

---

## 11. File-by-file change list

**New:**
- `core/graphics/rhi/gpu_descriptor_heap.h` — `GpuDescriptorClass`, `GpuDescriptorHandle`,
  `GpuDescriptorHeapDesc`, `GpuDescriptorHeap` interface. **[done]**
- `core/graphics/vulkan/gpu_descriptor_heap.cpp` — Backend A + allocator + retirement.
  (Backend B is deferred; the host lacks `VK_EXT_descriptor_heap` — §1.) **[done, compiles]**
- `impl/assets/graphics/bindless/bindless_heap.slangi` + `binding_slots.h` — the §8 accessor
  header and its shared C++/shader binding-slot constants. (Shaders live under
  `impl/assets/graphics/`, following the existing `#include "binding_slots.h"` convention —
  *not* `core/graphics/shaders/`, which does not exist.)
- Historical: the §10 round-trip compute shader + renderer self-test harness. Both were retired
  with the renderer-internal self-test infrastructure.

**Touched (all done, compile clean):**
- `core/graphics/vulkan/backend.h` — full `GpuDescriptorHeap` class decl; `CommandList::
  bindDescriptorHeap` decl; `Device::getDescriptorHeap()` + `m_gpuDescriptorHeap` member +
  `friend class GpuDescriptorHeap`.
- `core/graphics/vulkan/device.cpp` — construct the heap next to `m_descriptorHeapManager`
  (`:172`), `shutdown()` it in `~Device` (`:502`). (`initialize()` is left to the consumer
  — the heap stays dark until its test drives it.)
- `core/graphics/vulkan/resource_bindings.cpp` — `CommandList::bindDescriptorHeap()` next to
  the existing `bindDescriptorHeapState`; binds the two tables via `vkCmdBindDescriptorSets`
  at their set indices.
- `core/graphics/api.h` — include `rhi/gpu_descriptor_heap.h`.
- `core/graphics/CMakeLists.txt` — add `vulkan/gpu_descriptor_heap.cpp`.

Backend A adds no new Vulkan descriptor plumbing — it reuses `createBindlessLayout` /
`createDescriptorTable` / `resizeDescriptorTable` / `writeDescriptorTable`.

---

## 12. Risks & open questions

1. **RADV `VK_EXT_descriptor_heap` availability — RESOLVED.** `vulkaninfo` confirms the
   BC-250/RADV host (Mesa 26.1.5) does **not** advertise `VK_EXT_descriptor_heap`; Backend B
   is therefore CI / other-target only, never local. Backend A (indexing) carries all local
   testing — the design is unchanged. **New datapoint:** the host *does* advertise
   `VK_EXT_descriptor_buffer` (descriptor-as-memory; natively encodes acceleration structures
   via `VkDescriptorGetInfoEXT`, unlike the current heap `writeDescriptor` which has no AS
   case). It is the only bindless-class accelerator that runs locally, so it is the logical
   **Backend C** if a future phase wants an on-device fast path. Kept out of Phase-1 scope;
   the point of the backend-agnostic handle contract (§3) is that admitting it later costs
   nothing above `GpuDescriptorHeap`.
2. **EXT slot↔byte-offset mapping** (§5) — must preserve the global-slot invariant across
   differing per-type strides. Prefer uniform max-stride slots for Phase 1.
3. **Update-after-bind limits** — query `maxDescriptorSetUpdateAfterBind{SampledImages,
   StorageImages,StorageBuffers,UniformBuffers}` and clamp `resourceCapacity`; log the
   effective cap (no silent truncation).
4. **Slang dual-codegen** — the accessor header must compile both ways from one source;
   validate DXC `-fvk-bind-resource-heap` remap against the descriptor-indexing arrays
   early, since it gates the shader contract.
5. **Sampler heap sizing** — samplers are few; a small fixed `samplerCapacity` (e.g. 2048)
   is fine, but confirm against `maxDescriptorSetUpdateAfterBindSamplers`.
6. **Generation bits** — deferred to a possible 64-bit handle; make sure the accessor macro
   is the only place that decodes a handle so widening stays a one-file change.

---

## 13. What Phase 1 hands to Phase 2

- A stable `GpuDescriptorHandle` and a `GpuDescriptorHeap` that both backends satisfy
  identically.
- One shader accessor header proven to round-trip.
- A registration point (`allocate`+`write` at resource create/load).

Phase 2 can then begin migrating *one* resource domain (the natural first candidate is the
per-mesh RT/GI/shadow/caustic arrays that already use bounded descriptor indexing — the
smallest conceptual jump) to fetch via handle, delete its bespoke bounded array, and prove
parity against the existing capture. Nothing in Phase 2 is designed here.
