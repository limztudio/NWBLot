# Async-render lane foundation, ray effects, hybrid-AVBOIT, and deferred-lighting prototype

**Status:** Phase-zero backend foundation, the M2 shadow-visibility schedule, M3 acceptance-aware timing and
failure-injection coverage, the M5 software-caustics migration, M6 surfel-GI migration, M7 hardware
`dispatchRays`-caustics migration, M8 hybrid AVBOIT migration, and M9 deferred-lighting migration are implemented
behind the experimental async-compute switch. The M4 target-hardware benchmark/validation harness is available;
distinct-family pixel-parity and performance results, including the caustics, surfel-GI, AVBOIT, and deferred-lighting
slices, remain pending.

When the switch resolves `AsyncCompute` to a dedicated Compute family, shadow visibility and either caustic producer
join the same accepted Compute submission. Vulkan specifies `vkCmdTraceRaysKHR` for command pools that support
`VK_QUEUE_COMPUTE_BIT`, which includes the selected dedicated Compute family. Surfel GI also joins that packet on both
trace backends because its hardware variant uses inline `RayQuery` from an ordinary compute dispatch. Unsupported or
disabled hardware retains the existing one-Graphics-submission topology exactly.

## 0. Decision and boundary

Before moving any rendering job, establish a two-lane foundation: a logical **Graphics** lane and a
logical **AsyncCompute** lane, explicit cross-lane resource contracts, and acceptance-aware submission
dependencies. The first job migration moves **only shadow visibility** onto a dedicated Compute queue. The next
bounded migrations move both **software-caustics** and hardware **`dispatchRays` caustics** with it. The current
bounded migration also adds **surfel GI** on both its software-BVH and inline-RayQuery trace backends. AVBOIT's
depth-warp and integration dispatches now use the dedicated Compute lane, bracketed by the required Graphics raster
phases. Deferred lighting now runs on AsyncCompute after the AVBOIT chain; presentation composite remains on
Graphics.

This is deliberately a narrow proof point. It tests the hard parts of multi-queue rendering —
resource sharing, queue-family ownership, timeline dependencies, timing, and partial submission
failure — without turning the renderer into a general frame graph.

Out of scope for this foundation and prototype:

- porting AVBOIT's raster phases or presentation composite to Compute;
- creating a second queue from the Graphics queue family as a substitute for a dedicated Compute
  family;
- reordering the established GPU effects sequence or changing the CPU-recording wave;
- a general-purpose cross-queue render graph or automatic scheduler.

## 1. Phase-zero lane contract — required before any job migration

### 1.1 Logical lanes are policy; Vulkan queues are the resolution

Renderer code should select one of two logical lanes, not reach directly for a Vulkan queue:

| Logical lane | Preferred physical queue | Effective fallback |
|---|---|---|
| `Graphics` | Graphics queue | Always available. |
| `AsyncCompute` | A separately created compute-only queue family | Resolve to Graphics and preserve queue order when no distinct Compute queue exists or the experiment is disabled. |

The resolution is immutable for a device lifetime and must be logged as requested versus effective
mode, with Graphics and Compute family indices. The fallback is a mapping decision, not a second
`Queue` wrapper around the Graphics `VkQueue`: no alias queue, self-timeline wait, or queue-family
transfer is created on unsupported hardware.

The existing `CommandQueue::Graphics` and `CommandQueue::Compute` remain physical backend queue
identities. A small renderer-facing lane resolver maps a logical lane to one of them when it creates a
command list or submits a packet. This preserves the current Graphics-only path while allowing a later
job migration to use the same scheduling code on both hardware classes.

### 1.2 Submission dependencies are explicit, accepted tokens

Each accepted lane submission produces a token containing its resolved queue and queue-timeline value.
A dependent submission consumes that token:

- when both lanes resolve to Graphics, queue order is the dependency and no semaphore wait is emitted;
- when they resolve to distinct queues, the consumer submission waits on the producer's timeline value;
- a rejected submission produces no usable token, so later work cannot accidentally wait on an
  unaccepted value.

The token describes *execution completion*, while a state handoff describes only resource state and
ownership. Keeping those two contracts separate makes it possible to validate both the resource
transition and the scheduler edge that makes it safe.

The new path should pass waits as submission-local data (for example, a `QueueSubmissionDesc`) rather
than leave them in the current queue-wide pending-wait vectors. That prevents one failed or unrelated
submission from consuming another packet's dependency, and gives rollback a precise accepted-submission
boundary.

### 1.3 One allocation; two different sharing models

Cross-lane use does not normally copy or "transfer" a buffer/image. Both queues access the same Vulkan
allocation. What changes is access ordering, visibility, resource state, and—only for an exclusive
resource whose queues use different families—queue-family ownership.

| Resource class | Creation / ownership contract | Example |
|---|---|---|
| Common cross-lane inputs | Concurrent Graphics/Compute sharing when the effective families differ; still wait for the producer before reading. | Normalized G-buffer, scene and tracing buffers, descriptor-buffer segments, slot cbuffers, TLAS/material inputs. |
| Compute-local result or scratch | One owner at a time, but no cross-lane handoff when its only consumer is also Compute. | `shadowVisibility`, `causticIrradiance`, `surfelIrradiance`, soft-shadow scratch, and delayed-readback staging. |
| Concurrent Compute-to-Graphics result | Concurrent sharing plus a timeline dependency; no queue-family ownership transfer. | `opaqueColor`, written by deferred-lighting Compute and sampled by Graphics composite. |

The RHI creation API should express an engine-level queue-sharing mask, never raw Vulkan family
indices. The Vulkan backend resolves that mask to `VK_SHARING_MODE_CONCURRENT` only when the effective
Graphics and Compute families are distinct; the default remains exclusive. We must enumerate the
resources that genuinely need concurrent access instead of marking all renderer allocations concurrent.

### 1.4 Exclusive cross-lane results need the full ownership round trip

The phase-zero infrastructure proof must cover the cyclic lifetime of a reusable exclusive output, not only
the easy Compute-to-Graphics half. That remains the contract for any future exclusive cross-lane result:

```text
Compute owns / acquires slot → writes shadowVisibility → releases to Graphics
                                     │ timeline signal
                                     ▼
Graphics waits → acquires slot → deferred lighting samples it → releases to Compute
                                                                  │ timeline signal
                                                                  ▼
                                                        next reuse acquires on Compute
```

On its first use, a newly created slot may be claimed by the first queue that uses it; if Graphics
initializes it first, the same Graphics-to-Compute release/acquire applies. On the Graphics fallback,
the ownership half collapses to normal same-queue state transitions, but the logical lifecycle remains
the same. M9 removes this cycle from the current ray-effect results because deferred lighting now consumes them on
AsyncCompute; it does not weaken the backend proof or the contract for a future Graphics consumer.

This reverse handoff is required before shadow moves: otherwise a frame-slot reuse can begin on Compute
while Vulkan still considers Graphics its owner. It also gives the failure path an unambiguous safe
owner after a partially accepted frame.

### 1.5 Owner-aware state handoffs

`CommandListResourceStateHandoff` must gain the minimum metadata to carry, per tracked resource state:

- the resource state/layout;
- its sharing contract; and
- for exclusive resources, its current logical/physical queue owner.

Normal same-queue transitions stay on the existing barrier path. A distinct-family handoff emits a
producer release barrier and a consumer acquire barrier with the resolved source/destination family
indices. Concurrent resources carry no exclusive owner and must never receive an ownership transfer.
`buildFanIn` must reject incompatible final state, sharing contract, or exclusive owner; it must not
infer ownership from submission order. The scheduler separately verifies that an importing lane has the
producer token it needs.

### 1.6 Phase-zero proof gates

Phase zero changes infrastructure only; no shadow, GI, caustics, or AVBOIT packet is assigned to
AsyncCompute yet.

1. Resolve and log both logical lanes, with a Graphics fallback that retains the current one-submission
   topology.
2. Add submission-local dependency tokens and test same-queue collapse, distinct-queue waits, and
   rejection without a consumable token.
3. Add queue-sharing intent at buffer/texture and descriptor-buffer creation, plus owner-aware state
   handoffs and fan-in validation.
4. On a distinct-family validation device, run a minimal non-rendering probe that proves an exclusive
   texture or buffer can complete Compute → Graphics → Compute ownership transfer and be safely reused.
5. Inject failures after each accepted probe submission, including the recovery acquire when a release
   has been accepted but its intended consumer has not.

Only after all five gates pass does the shadow-visibility schedule below become eligible. This keeps the
first rendering migration focused on pass behavior rather than discovering basic lane semantics at the
same time.

### 1.7 Implementation status

The following phase-zero pieces are now in the backend, behind the experimental
`Graphics::setAsyncComputeLaneEnabled(true)` switch:

- `RenderLane::Graphics` and `RenderLane::AsyncCompute` resolve to physical queues at device creation;
  AsyncCompute falls back to Graphics when no compute-only family is available, and the requested/effective
  result is logged.
- Logical-lane command-list creation and `QueueSubmissionToken` / `QueueSubmissionDesc` provide accepted
  per-submission timeline dependencies. Same-queue dependencies collapse to queue order; duplicate producer
  tokens coalesce to the greatest timeline value; unsignalled/fabricated values are rejected before submit.
- `TextureDesc`, `BufferDesc`, and `RayTracingAccelStructDesc` expose the engine-level
  `ResourceQueueSharing` contract. Vulkan creates concurrent resources only for an effective distinct
  Graphics/AsyncCompute family pair. The global descriptor-buffer segments already opt into that contract.
- State handoffs now retain sharing and exclusive-owner metadata. Explicit producer-side
  `releaseTextureOwnership` / `releaseBufferOwnership` records the release barrier at close; the importing
  command list records the matching acquire barrier before its first use. Normal barriers explicitly use
  `VK_QUEUE_FAMILY_IGNORED`.
- The headless descriptor-buffer suite proves the logical fallback path. It also contains a dedicated-family
  Compute → Graphics → Compute probe covering an exclusive output, a concurrently shared input, timeline
  waits, and frame-slot reuse. It skips cleanly on adapters without a compute-only family.

The renderer now consumes these primitives as follows:

- `RendererSystem` creates shadow, caustics, and surfel-GI command lists for logical `AsyncCompute`; they become
  Compute command lists only when the lane has a dedicated family. The caustics list is selected on both trace
  backends: the hardware producer's `dispatchRays` command is valid on the selected `VK_QUEUE_COMPUTE_BIT` family.
  Surfel GI is likewise selected on both trace backends because its hardware path uses inline RayQuery from a compute
  pipeline. Otherwise the legacy Graphics batch remains unchanged.
- The concurrent input inventory includes every G-buffer attachment (including albedo), scene-shading and light
  buffers, the deferred slot cbuffer, trace-context buffers, software-BVH inputs, mesh geometry, camera mesh-view
  data, caustic emission targets, surfel constants, and TLAS/BLAS backing allocations. The descriptor-buffer segments
  already carry the same contract. `opaqueColor` is also concurrent because AsyncCompute lighting writes it before
  Graphics composite samples it; `shadowVisibility`, `causticIrradiance`, `surfelIrradiance`, and private
  scratch/history remain exclusive to Compute.
- Selective state handoffs prevent Compute from importing unrelated Graphics-only state. Shadow, both caustic
  producers, and surfel GI retain independent private cross-frame scratch/result handoffs; deferred lighting fans
  those results in on Compute and exports only `opaqueColor` for Graphics composite.
- Accepted submissions commit independently. A rejected dependent packet after accepted Compute work triggers a small
  Graphics recovery join on the latest accepted Compute token. No ray-effect ownership repair is necessary because
  Graphics never imports those exclusive outputs. Failure to submit that join requests termination of the current
  graphics-device generation; its owner must then tear down and recreate device resources rather than allowing another
  pass or presentation to observe indeterminate execution.
- `Device::isDeviceLost()` is a sticky renderer-facing signal set for queue submit, queue/timeline wait, swap-chain
  acquire, present, and device-idle loss. Recovery acquires run only while that signal is clear; a lost device skips
  the extra submit and immediately requests device recreation.
- Timestamp reservations are split by submission. `render.frame` is an acceptance-aware Graphics critical-path
  transaction: it begins in prefix, completes in accepted final, and records a non-publishing recovery endpoint if a
  later packet is rejected. Packet envelopes report prefix/shadow/effects/final duration; when Vulkan supports
  Graphics+Compute timestamps, `render.async_shadow_effects_overlap` reports the measured shadow/effects intersection
  (zero means both packets completed without overlap). The M4 metric remains deliberately shadow-focused; M5 keeps the
  existing caustic and surfel pass scopes on the accepted Compute ticket and requires separate performance analysis
  before changing that benchmark's decision rule.
- The software edge-stat and surfel live-count readbacks record accepted producer timeline tokens and map only after
  their producing queues have completed.
- A DEBUG/TEST-only pre-submit rejection seam exercises prefix, shadow, effects, final, and recovery rejection
  boundaries without manufacturing invalid Vulkan command buffers. The dedicated-family suite verifies that deferred
  lighting retains `shadowVisibility`, `causticIrradiance`, and `surfelIrradiance` on Compute through next reuse while
  Graphics imports only the opaque-color composite handoff; a rejected recovery stops before reuse, matching the
  renderer's suspension/recreation policy.

Still required before enabling the path by default:

- run the backend probe and renderer schedule with Vulkan validation on at least one distinct-family target;
- run hardware and software pixel-parity coverage, including resize and transparent/CSG cases;
- collect critical-path and overlap/performance data on a distinct-family target.

## 2. Current and target GPU schedules

Today, shadow preparation is its own Graphics submission. The rest of the frame is one ordered
Graphics submission:

```text
Graphics: shadow prepare

Graphics: mesh-view + scene-shading + deferred clear + G-buffer + normalize
          + shadow + caustics + surfel GI + AVBOIT + deferred lighting + composite
```

The prototype keeps preparation and the visible GPU order, but on a frame with transparent work creates an ordered
Graphics/Compute chain after preparation:

```text
Graphics prefix:  mesh-view + scene-shading + deferred clear + G-buffer + normalize
                         ├── timeline wait ──→ Compute packet: shadow visibility
                         │                     + caustics (software-BVH or dispatchRays)
                         │                     + surfel GI (software-BVH or inline RayQuery)
                         │
                         └── queue order ────→ Graphics AVBOIT pre: clear + CSG/occupancy raster
                                                    │ timeline wait
                                                    ▼
                                               Compute: AVBOIT depth-warp dispatch
                                                    │ timeline wait
                                                    ▼
                                               Graphics: AVBOIT extinction raster
                                                    │ timeline wait
                                                    ▼
                                               Compute: AVBOIT integration dispatch
                                                    │ timeline wait
                                                    ▼
                                               Graphics: AVBOIT accumulation raster
                                                    │
                                                    └── timeline wait ──→ Compute: deferred lighting
                                                                             reads shadow + caustic + surfel
                                                                             writes concurrent opaqueColor
                                                                                         │
                                                                                         ▼
                                                                               Graphics final: composite
                                                                                              reads opaqueColor
```

The first AVBOIT Graphics packet is queued after `Graphics prefix` on the same queue but has no wait on the shadow
packet, so it can overlap that longer Compute work. AVBOIT's subsequent cross-lane edges are intentionally strict:
each compute dispatch waits for its raster producer and each raster consumer waits for its dispatch. Deferred lighting
waits for AVBOIT accumulation and is already ordered behind the ray-effect packet on the same Compute queue. Graphics
final waits only for the lighting token before sampling `opaqueColor`.

The CPU records shadow, caustics, surfel GI, and the AVBOIT pre-raster packet as the first sibling wave after
G-buffer normalization; AVBOIT's dependent slices record only after the preceding state handoff is available. The
change is in command-list queue type, state handoff metadata, and submission/commit order — not in CPU work
ownership.

## 3. Capability and fallback contract

`DeviceCreationParameters::enableAsyncComputeLane` requests a best-effort capability: a missing
compute-only family does not fail device selection and instead maps the lane to Graphics.

| Hardware condition | Renderer behavior |
|---|---|
| Distinct Compute family available and async prototype enabled | Create that queue; run the shadow/caustic/surfel packet, AVBOIT's Graphics → Compute → Graphics hybrid chain, and deferred-lighting Compute dispatch. Presentation composite stays on Graphics. |
| No distinct Compute family | Do not create an alias `Queue` for the Graphics `VkQueue`; retain the current single-Graphics schedule. |
| Prototype disabled | Retain the current schedule regardless of hardware. |
| Queue/device failure | Stop submitting dependent work, restore only unaccepted CPU state, repair ownership when possible, and make the next frame safe. |

The initial switch is experimental and off by default. It must be independently observable in logs:
requested/effective mode, Graphics and Compute family indices, and the reason for fallback.

Using a second queue from the Graphics family is a separate optimization. It adds queue-count and
driver-scheduling questions while failing to exercise the queue-family ownership path this prototype
exists to prove, so it is intentionally deferred.

## 4. Resource-sharing and ownership model

### 4.1 Inputs shared by Compute producers and Graphics effects

Shadow, both caustic producers, surfel GI, and the concurrent Graphics effects all read the normalized G-buffer and
common trace data.
An exclusive ownership transfer of those inputs would move ownership away from Graphics and serialize
the work that is supposed to overlap. Those resources must instead be created with concurrent
Graphics/Compute sharing when a distinct Compute family is active.

The resource-creation inventory must include at least:

- G-buffer albedo, world position, normal, and depth, plus the concurrent `opaqueColor` lighting output;
- scene-shading, lighting, mesh-view, scene-BVH, instance, material, mesh, caustic-emission, surfel-constants, and
  TLAS inputs used by shadow, both caustic producers, surfel GI, and the Graphics effects;
- the backing buffers of the TLAS and every shared BLAS: hardware traversal shares those allocations
  even though the acceleration-structure RHI object is not itself a `BufferDesc`;
- deferred-target slot buffer and the global descriptor-buffer resource and sampler segments;
- AVBOIT's coverage, depth-warp, control, packed-extinction, and overflow buffers plus its writable
  transmittance volume, which cross the AVBOIT Graphics/Compute stages in a fixed order;
- any shadow software-BVH/statistics input used by the software path.

The resource-creation API needs an explicit queue-family sharing intent; the Vulkan backend maps it
to `VK_SHARING_MODE_CONCURRENT` only for this enumerated cross-queue set. Existing resources retain
their exclusive behavior by default. `RayTracingAccelStructDesc` must propagate the same intent to its
internally created backing buffer; adding the mask only to ordinary buffers and textures would leave
the shared TLAS/BLAS allocations exclusive.

### 4.2 Shadow result

`shadowVisibility` is produced and consumed on AsyncCompute: the shadow packet writes it and deferred lighting reads
it later on the same physical Compute queue. It stays exclusive to Compute, with its retained next-frame handoff built
from the accepted lighting state rather than from a Graphics ownership return. Soft-shadow scratch/history images and
delayed-readback buffers use the same Compute-local rule unless a real Graphics consumer is introduced.

### 4.3 Caustics result

On a dedicated Compute lane, the caustics command list follows shadow in the same accepted Compute submission on
both trace backends. The software producer uses its software-BVH tables; the hardware producer records
`vkCmdTraceRaysKHR`, whose command-pool requirement is `VK_QUEUE_COMPUTE_BIT`. The selected dedicated lane has that
capability, so no separate queue-level ray-tracing capability is needed. Its accumulator, resolve half/history,
geometry cache, and full-resolution `causticIrradiance` result remain Compute-local through deferred lighting.

### 4.4 Surfel-GI result

Surfel GI is a normal compute-dispatch chain on both trace backends: the hardware variant uses inline RayQuery rather
than `dispatchRays`. Its persistent pool, hash table, snapshots, indirect arguments, readback staging, and
full-resolution `surfelIrradiance` result remain private to Compute, including the first-use clear. The per-frame
surfel constants are concurrent Graphics/Compute input, while the delayed live-count readback records the accepted
Compute token before CPU mapping.

### 4.5 Deferred-lighting output

Deferred lighting is a screen-space compute dispatch with the same cooked BXDF dispatch module as the former
fullscreen pixel shader. It reads the normalized G-buffer plus the Compute-local ray results and writes the existing
`opaqueColor` target through a StorageImage heap view. `opaqueColor` is created as a concurrent Graphics/AsyncCompute
resource with a known `Common` close state. The Graphics composite packet waits for the lighting token and imports only
that target, its AVBOIT accumulation inputs, and the already-Graphics slot-buffer state; it does not acquire or track
the exclusive ray-effect outputs.

### 4.6 Hybrid AVBOIT work

AVBOIT cannot move as one command list because occupancy, extinction, and accumulation are material raster passes.
The two pure dispatches are split at their natural producer/consumer boundaries:

1. Graphics clears the AVBOIT targets, builds transparent CSG intervals, and rasterizes occupancy into coverage.
2. Compute waits for that packet and dispatches depth-warp, reading coverage and writing depth-warp/control buffers.
3. Graphics waits, rasterizes extinction into the packed buffers, then Compute waits and integrates the transmittance
   volume.
4. Graphics waits for integration and rasterizes accumulation. Compute deferred lighting then waits for the final
   AVBOIT packet before importing only the shared G-buffer inputs it needs.

The five work buffers and transmittance volume use `GraphicsAndAsyncCompute` sharing, so the timeline waits establish
execution and memory visibility without ownership transfers. Low-resolution raster and accumulation framebuffer
attachments remain Graphics-only. A no-transparency frame keeps its clear-only AVBOIT packet on Graphics and emits no
extra cross-lane submissions. If a Graphics AVBOIT consumer is rejected after depth-warp or integration was accepted,
an empty Graphics recovery packet waits for that Compute token before the next frame may reuse the concurrent work
resources; this is a timeline join, not an ownership transfer.

### 4.7 State-tracking work

`CommandListResourceStateHandoff` currently carries only layouts/access states. The prototype adds
the minimum owner metadata needed to distinguish:

- ordinary same-queue state transitions;
- concurrently shared resources, for which no ownership transfer is emitted; and
- an exclusive release/acquire transfer between two known queue families.

`buildFanIn` must reject incompatible final layout, access, **or owner** state. It must not infer a
queue family from submission order. Vulkan barriers in the backend need a dedicated ownership
transfer path that fills `srcQueueFamilyIndex` and `dstQueueFamilyIndex`; the existing normal
transition path remains unchanged and explicitly uses `VK_QUEUE_FAMILY_IGNORED`.

## 5. Submission acceptance, rollback, and recovery

The present single submit supports an all-or-nothing CPU rollback. That is no longer valid once one
queue submission has been accepted: accepted GPU work may execute even if a later submission is
rejected.

The async path therefore commits by accepted submission:

| Accepted submission | State that becomes committed |
|---|---|
| Graphics prefix | its resource-state handoff and any uploads/cache changes recorded in the prefix |
| Compute shadow + caustics + surfel GI | soft-shadow temporal advance, delayed-readback submission IDs, caustic temporal advance, surfel temporal/readback state, and Compute-local producer state |
| Graphics AVBOIT pre | its clear latch; a later AVBOIT-stage rejection must not restore a clear already accepted by Graphics |
| Compute AVBOIT depth-warp / integration | their concurrent work-buffer state only; AVBOIT has no cross-frame Compute ownership return |
| Graphics AVBOIT extinction / accumulation | their raster output state; the latter publishes the AVBOIT branch for Compute deferred lighting |
| Compute deferred lighting | the retained ray-result state for next Compute reuse and the concurrent opaque-color output |
| Graphics final | composite state, final frame completion, and presentation-visible completion |

Only work in a submission that was not accepted may be restored from the pre-frame snapshot.
`finalizeSoftShadowTemporalHistory()` must occur after Compute acceptance, but only after all CPU
sibling recording has stopped reading the pre-swap bindless handles.

The software-shadow delayed readback must track the accepted **Compute** submission ID and check the
Compute queue's completed timeline before mapping; a frame-count delay alone is no longer a valid
completion proof.

If Compute was accepted but a later dependent packet was not, the renderer submits a small Graphics recovery join
that waits for the latest accepted Compute token before another Graphics prelude may reuse concurrent work. The
exclusive ray outputs never left Compute, and their latest retained producer/lighting snapshot supplies next-frame
reuse. If that join cannot be accepted, the device is treated as unusable rather than allowing the next frame to
assume completion. This recovery path is a required failure-injection test, not an optional cleanup.

## 6. Timing and observability

The current `GpuTimingSubmissionTicket` represents one complete submission. The async schedule needs one
accepted-submission ticket for Graphics prefix, the Compute shadow packet (including caustics and surfel GI), the
AVBOIT pre/warp/extinction/integration/accumulation slices, Compute deferred lighting, and Graphics final, with each
pass scope assigned to its owning ticket.

The old whole-frame scope starts in the first Graphics command list and ends in composite. It cannot
be reused with the current single-submission ticket rules. Before enabling the feature, timing must
gain a multi-submission frame transaction (or an equivalent frame-critical-path metric) that:

- preserves an end-to-end graphics-frame measurement without summing overlapping queue work;
- accounts for partial acceptance and retires any query whose begin was submitted even if a later
  end was not; and
- reports individual prefix, shadow, AVBOIT slice, and final timings plus whether Compute actually overlapped the
  initial Graphics AVBOIT packet.

Query reset remains in the Graphics preamble; the Compute submission waits for the prefix before
writing timestamps, which makes that reset ordering explicit.

## 7. Delivery milestones

### M0 — Logical lane resolver and no-op gate

- Make dedicated Compute selection best-effort and expose effective queue capability.
- Add an experimental async-render setting, requested/effective logging, and the renderer-local lane
  plan that currently flattens to the existing Graphics submission.
- Resolve a lane before command-list creation; a fallback `AsyncCompute` packet must be created as a
  Graphics command list, never as a Compute list submitted to Graphics.
- Add backend coverage for no dedicated Compute family and ensure it retains the existing path.

**Gate:** existing renderer and descriptor-buffer tests pass unchanged; disabled and fallback modes
produce the current one-Graphics-submission topology.

### M1 — Cross-queue resource and submission model, still no migrated render job

- Add submission-local accepted tokens and dependency waits.
- Add resource queue-sharing intent to buffer/texture and descriptor-buffer creation.
- Propagate the same intent through `RayTracingAccelStructDesc` to TLAS/BLAS backing buffers.
- Mark the enumerated shared inputs, descriptor-buffer segments, and slot buffer concurrent.
- Add owner-aware state handoffs and unit tests for ordinary transitions, concurrent resources,
  release/acquire pairs, and invalid fan-in rejection.
- Add the minimal Compute → Graphics → Compute exclusive-resource probe and failure-injection recovery
  coverage.

**Gate:** validation layers report no queue-family/layout errors on a distinct-family device; the
Graphics fallback creates no accidental concurrent resources or duplicate queue wrapper; the probe
completes the full ownership reuse cycle without copying its allocation.

### M2 — Initial shadow schedule (implemented; ownership cycle superseded by M9)

- Create the shadow command list for Compute only when M0's effective capability is true.
- Submit prefix, Compute shadow, Graphics effects, and Graphics final with explicit timeline waits.
- The original Compute → Graphics → Compute `shadowVisibility` ownership cycle remains covered by the phase-zero
  RHI probe; M9 keeps the live renderer result on Compute through deferred lighting instead.

**Gate:** hardware and software shadow paths are pixel-parity tested against the synchronous path
over cold start, steady temporal frames, scene mutation, resize, opaque/transparent shadow cases,
and CSG paths.

### M3 — Acceptance-aware timing and recovery (implemented; hardware validation pending)

- One timing ticket per accepted submission; accepted prefix/shadow/effects/final CPU-state commits; a Graphics
  recovery join for accepted Compute work; and Compute completion IDs for software-shadow readback.
- `GpuTimingFrameTransaction` preserves the end-to-end `render.frame` measurement without summing concurrent work.
  An accepted prefix whose final packet is rejected receives a non-publishing Graphics recovery endpoint, so its timer
  query retires safely rather than leaking or masquerading as a complete frame.
- Packet envelopes report prefix, shadow, effects, and final duration. On timestamp-capable dedicated-queue devices,
  the shadow/effects timestamp intersection reports actual overlap rather than merely schedule eligibility.
- Deterministic pre-submit failure injection covers prefix, shadow, effects, final, and recovery submission
  boundaries. Successful recovery proves the next Compute reuse has an accepted completion edge; failed recovery
  deliberately prevents reuse until device/resource recreation.

**Gate:** every injected rejection leaves the next valid frame correct; no stale temporal history,
descriptor retirement, query leak, or ownership validation error remains.

### M4 — Performance and rollout decision

- Capture queue timelines/timestamps that prove effects overlap shadow on supported hardware.
- Compare frame critical path, not a sum of queue durations, against the current path.
- Keep the feature opt-in until the validation and failure gates pass on the target Vulkan drivers.

`tests/ab/async_shadow_m4/run.py` automates this decision with a paired, fixed-yaw stress-scene A/B:

- `nwb_async_shadow_m4_sync_benchmark` preserves the Graphics-only baseline and
  `nwb_async_shadow_m4_async_benchmark` requests the experimental lane before device creation;
- the runner skips rather than accepts a Graphics fallback when no dedicated compute-only family is available;
- it captures the packet timestamp envelopes and requires measurable
  `render.async_shadow_effects_overlap`, compares median `render.frame` critical path, captures fixed-scene pixels,
  and scans Vulkan/ownership logs; and
- it writes a JSON and Markdown report so a flat or negative result remains an auditable rollout decision.

The benchmark targets and invocation details live beside the runner in
`tests/ab/async_shadow_m4/README.md`. The harness can run with Vulkan validation; actual target-driver validation and
the resulting default-on/off decision are still pending.

**Gate:** a measurable overlap exists without a correctness or timing regression. A flat or negative
result is a valid outcome: retain the fallback and use the data to decide whether another render job
deserves a separate proposal.

### M5 — Software-caustics migration (implemented; distinct-family validation pending)

- On a dedicated AsyncCompute lane with no hardware ray-tracing support, append the ordinary-compute software-caustics
  command list to the accepted Compute shadow submission. M7 later applies the same output/ownership contract to the
  hardware `dispatchRays` producer.
- Mark the extra Graphics-to-Compute read-only inputs — camera mesh-view data and caustic emission targets — concurrent.
  Keep temporal accumulator/resolve scratch Compute-private.
- M9 keeps `causticIrradiance` on Compute through deferred lighting, together with `shadowVisibility`, instead of
  releasing either result to Graphics.
- Treat the combined Compute packet as one CPU-state acceptance boundary. A later dependent rejection preserves its
  accepted software-caustic temporal state and joins accepted Compute work before reuse.
- Cover the Compute-local result fan-in/reuse contract in the dedicated-family descriptor-buffer suite. It skips
  cleanly on hosts without a compute-only family.

**Gate:** on a distinct Compute-family, no-hardware-ray-tracing target, validate Vulkan ownership/layout cleanliness
and sync/async pixel parity over cold start, temporal steady state, resize, no-caustic-work frames, and refractive
caustic scenes. Extend performance analysis separately before claiming a new default-on benefit.

### M6 — Surfel-GI migration (implemented; distinct-family validation pending)

- On a dedicated AsyncCompute lane, append the surfel-GI compute command list to the accepted Compute shadow packet on
  both trace backends. The hardware variant remains eligible because it uses inline RayQuery in a compute shader, not
  `dispatchRays`.
- Keep the persistent surfel field, snapshot buffers, indirect arguments, and count-readback staging Compute-private.
  Move the first-use field clear into that same packet. Mark only the per-frame surfel constants concurrent with the
  Graphics preparation upload.
- M9 keeps `surfelIrradiance` on Compute through deferred lighting alongside shadow and caustic outputs. A later
  dependent rejection retains accepted surfel temporal/readback state and joins accepted Compute work before reuse.
- Bind periodic surfel counter readback to the accepted producer token, and cover the three-output
  shadow/caustic/surfel Compute-local fan-in and reuse contract in the dedicated-family descriptor-buffer suite.

**Gate:** on distinct Compute-family targets, run Vulkan validation and sync/async pixel parity on both software-BVH
and inline-RayQuery surfel paths over cold start, temporal steady state, resize/recreation, GI-disabled frames, and
first-use/reseed frames. Collect a separate critical-path/overlap analysis before changing the experimental default.

### M7 — Hardware dispatch-rays caustics migration (implemented; distinct-family validation pending)

- Select the existing asynchronous caustics command list on every dedicated AsyncCompute lane, not only on the
  no-hardware-ray-tracing fallback. Vulkan defines `vkCmdTraceRaysKHR` for command pools supporting
  `VK_QUEUE_COMPUTE_BIT`, which is the required capability of the selected dedicated lane.
- Import the normalized TLAS backing allocation and hardware corner-attribute buffers alongside the shared
  material, emission, G-buffer, and camera inputs. These are concurrent Graphics/Compute inputs; the caustic
  accumulator and resolve scratch remain Compute-private.
- Keep the same Compute-local `causticIrradiance` result contract, timing ticket, and CPU temporal-state acceptance
  boundary as the software producer; M9 removes the former Graphics ownership round trip.
- Generalize the dedicated-family result fan-in/reuse probe because it validates the common result contract regardless
  of which producer recorded it.

**Gate:** on a distinct Compute-family device supporting `VK_KHR_ray_tracing_pipeline`, run Vulkan validation and
sync/async pixel parity over cold start, temporal steady state, resize/recreation, no-caustic-work frames, and
refractive caustic scenes. Collect a separate critical-path/overlap analysis before changing the experimental default.

### M8 — Hybrid AVBOIT migration (implemented; distinct-family validation pending)

- Keep occupancy, extinction, and accumulation on Graphics because each is a material raster pass, but split the
  depth-warp and integration dispatches onto AsyncCompute.
- Mark AVBOIT's coverage, depth-warp, control, packed-extinction, overflow, and transmittance work resources as
  concurrent Graphics/Compute. The low-raster and accumulation attachments remain Graphics-only.
- Submit the strict dependency chain `Graphics pre → Compute warp → Graphics extinction → Compute integration →
  Graphics accumulation`; only the first Graphics packet remains independent of the shadow/caustic/surfel packet.
- Build narrow Compute input handoffs and Graphics fan-ins at both boundaries so a compute-only command list never
  imports unrelated exclusive Graphics state. A clear-only, no-transparency frame retains the one Graphics AVBOIT
  packet.
- When a rejected Graphics consumer follows an accepted AVBOIT compute dispatch, submit a Graphics recovery join on
  that compute token before reuse; concurrent sharing removes ownership barriers, not the need for execution safety.
- Cover the five-submission sequence in the dedicated-family descriptor-buffer suite using concurrent work buffers
  and a transmittance volume. It skips cleanly when the adapter exposes no distinct compute-only queue family.

**Gate:** on a distinct Compute-family target, run Vulkan validation and sync/async transparent pixel parity over
cold start, clear-only frames, transparent CSG, resize/recreation, and mixed opaque/transparent scenes. Capture the
AVBOIT slice timings and the shadow/initial-AVBOIT overlap before considering any default-on change.

### M9 — Deferred-lighting migration (implemented; distinct-family validation pending)

- Replace the fullscreen deferred-lighting pixel pipeline with an 8×8 compute dispatch using the same generated BXDF
  dispatch module and texel-centre G-buffer sampling convention.
- Mark albedo and `opaqueColor` concurrent Graphics/AsyncCompute resources. `opaqueColor` gains a StorageImage heap
  descriptor and a known `Common` close state; format selection now requires storage-image write support.
- Build narrow Compute lighting fan-in from normalized G-buffer/scene inputs, the three exclusive ray results, and the
  final AVBOIT G-buffer state. The ray results remain on Compute through their only consumer.
- Submit Compute lighting after Graphics AVBOIT accumulation, then submit Graphics composite waiting only for the
  lighting token. Composite imports the opaque-color output, AVBOIT accumulation textures, and Graphics slot-buffer
  state; it does not acquire ray-effect resources.
- Replace the former ownership-repair packet with a generic Graphics recovery join on the latest accepted Compute
  token. Retain ray-result state immediately after producer acceptance and update it after lighting acceptance.
- Cover the narrow handoff sequence in the dedicated-family descriptor-buffer suite: ray outputs are exclusive and
  reused directly on Compute, while only concurrent `opaqueColor` reaches Graphics composite.

**Gate:** on a distinct Compute-family target, run Vulkan validation and sync/async opaque pixel parity over cold
start, temporal steady state, resize/recreation, no-ray-work frames, transparent/CSG scenes, and both ray-tracing
backends. Compare deferred-lighting and frame-critical-path timing before changing the experimental default.

## 8. Implementation anchors

| Concern | Current owner |
|---|---|
| Frame packet recording, state fan-in, and monolithic submit | `impl/ecs_render/kernel/system.cpp` |
| Shadow recording and soft-shadow temporal/readback state | `impl/ecs_render/raytrace/rt_shadow.cpp`, `rt_softshadow.cpp` |
| Software/hardware caustic producers and temporal resolve | `impl/ecs_render/raytrace/rt_caustics.cpp` |
| Surfel GI resources, trace, resolve, and readback | `impl/ecs_render/raytrace/rt_surfel_gi.cpp` |
| Deferred-lighting compute shader, output heap view, and composite handoff | `impl/ecs_render/deferred/deferred_lighting.cpp`, `impl/assets/graphics/deferred/lighting_cs.slang`, `deferred_targets.cpp` |
| Normalized G-buffer / trace resource transition inventory | `impl/ecs_render/raytrace/rt_detail.cpp` |
| Queue families and device queue creation | `core/graphics/vulkan/backend_context.cpp` |
| Queue timelines, submit, and cross-queue wait helper | `core/graphics/vulkan/queue.cpp` |
| Command-list queue selection and per-queue command pools | `core/graphics/vulkan/commandlist.cpp` |
| State handoffs and Vulkan barriers | `core/graphics/rhi/command.h`, `core/graphics/vulkan/state_tracking.cpp` |
| Resource sharing mode at creation | `core/graphics/vulkan/texture.cpp`, `core/graphics/vulkan/buffer.cpp`, `resource_bindings.cpp` |
| Timing ticket lifetime and query retirement | `core/graphics/gpu_timing.*` |

## 9. Open decisions to settle before implementation

1. **Frame timing transaction:** extend `GpuTimingSubmissionTicket` into a segmented ticket versus
   add a distinct multi-submission frame ticket. The latter keeps the existing single-submit contract
   simple and is preferred unless query ownership can be proven equally clear in an extension.
2. **Recovery policy after device-level submit failure:** implemented. Use the recovery join only while the Graphics
   queue remains usable; `Device::isDeviceLost()` skips that submit and `Graphics::requestDeviceRecreation()` marks
   the current device generation terminal before later passes or presentation can continue.

These decisions are intentionally limited to the current shadow/caustics/surfel-GI, hybrid-AVBOIT, and deferred-
lighting prototype. They do not authorize porting AVBOIT raster phases or presentation composite to Compute.
