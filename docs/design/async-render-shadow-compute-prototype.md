# Async-render lane foundation and shadow-visibility prototype

**Status:** Phase-zero backend foundation and the M2 shadow-visibility schedule are implemented behind the
experimental async-compute switch. Distinct-family validation, pixel parity, failure injection, and performance
measurement remain pending.

When the switch resolves `AsyncCompute` to a dedicated Compute family, shadow visibility records on that lane and
uses the four-submission topology below. Unsupported or disabled hardware retains the existing one-Graphics-submission
topology exactly.

## 0. Decision and boundary

Before moving any rendering job, establish a two-lane foundation: a logical **Graphics** lane and a
logical **AsyncCompute** lane, explicit cross-lane resource contracts, and acceptance-aware submission
dependencies. The first job migration, after that foundation is proven, moves **only shadow visibility**
onto a dedicated Compute queue. Caustics, surfel GI, AVBOIT, deferred lighting, and composite remain
on Graphics.

This is deliberately a narrow proof point. It tests the hard parts of multi-queue rendering —
resource sharing, queue-family ownership, timeline dependencies, timing, and partial submission
failure — without turning the renderer into a general frame graph.

Out of scope for this foundation and prototype:

- moving caustics, surfel GI, AVBOIT, lighting, or composite to Compute;
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
| Exclusive cross-lane result | One owner at a time; paired release/acquire plus a timeline dependency. | `shadowVisibility` written by Compute and sampled by deferred lighting on Graphics. |
| Single-lane scratch or history | Exclusive to its one lane; no handoff merely because it is related to an async pass. | Compute-only soft-shadow scratch and delayed-readback staging. |

The RHI creation API should express an engine-level queue-sharing mask, never raw Vulkan family
indices. The Vulkan backend resolves that mask to `VK_SHARING_MODE_CONCURRENT` only when the effective
Graphics and Compute families are distinct; the default remains exclusive. We must enumerate the
resources that genuinely need concurrent access instead of marking all renderer allocations concurrent.

### 1.4 Exclusive results need the full ownership round trip

The first infrastructure proof must cover the cyclic lifetime of a reusable exclusive output, not only
the easy Compute-to-Graphics half. For a reused `shadowVisibility` frame slot, the normal lifecycle is:

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
the same.

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

- `RendererSystem` creates the shadow command list for logical `AsyncCompute`; it becomes a Compute command list only
  when the lane has a dedicated family. Otherwise it resolves to Graphics and follows the legacy batch unchanged.
- The concurrent input inventory is limited to normal/world-position/depth, scene-shading and light buffers, deferred
  slot cbuffer, trace-context buffers, software-BVH inputs, mesh geometry, and TLAS/BLAS backing allocations. The
  descriptor-buffer segments already carry the same contract. `shadowVisibility` and soft-shadow scratch/history stay
  exclusive.
- A selective state handoff prevents Compute from importing unrelated Graphics-only caustic, GI, and AVBOIT state.
  Compute scratch/history remains in a private cross-frame handoff; only `shadowVisibility` joins the Graphics fan-in.
- Accepted submissions commit independently. A rejected packet after Compute triggers a small Graphics recovery list
  that acquires `shadowVisibility` and releases it back to Compute. Failure to submit that recovery suspends rendering
  until resource/device recreation rather than guessing ownership.
- Timestamp reservations are split by submission. The software edge-stat readback records the accepted shadow timeline
  token and maps only after its producing queue has completed.

Still required before enabling the path by default:

- run the backend probe and renderer schedule with Vulkan validation on at least one distinct-family target;
- run hardware and software pixel-parity coverage, including resize and transparent/CSG cases;
- inject failures around all four renderer submissions and the recovery list; and
- add the cross-queue critical-path timing metric and collect overlap/performance data.

## 2. Current and target GPU schedules

Today, shadow preparation is its own Graphics submission. The rest of the frame is one ordered
Graphics submission:

```text
Graphics: shadow prepare

Graphics: mesh-view + scene-shading + deferred clear + G-buffer + normalize
          + shadow + caustics + surfel GI + AVBOIT + deferred lighting + composite
```

The prototype keeps preparation and the visible GPU order, but creates four submissions after
preparation:

```text
Graphics prefix:  mesh-view + scene-shading + deferred clear + G-buffer + normalize
                         │
                         ├─────────────── timeline wait ───────────────┐
                         │                                              │
Graphics effects: caustics + surfel GI + AVBOIT                         │
                                                                        ▼
Compute shadow:   shadow visibility; release shadowVisibility to Graphics
                         │
                         └─────────────── timeline wait ───────────────┐
                                                                        ▼
Graphics final:   acquire shadowVisibility + deferred lighting + composite
                  + release shadowVisibility to Compute for its next frame-slot reuse
```

`Graphics effects` is queued after `Graphics prefix` on the same Graphics queue, but has no wait
on Compute. It can therefore overlap `Compute shadow` after the prefix completes. `Graphics final`
is ordered after effects by its queue and explicitly waits for the Compute timeline before consuming
the shadow result.

The CPU may continue to record shadow, caustics, surfel GI, and AVBOIT as the current sibling wave
after G-buffer normalization. The change is in command-list queue type, state handoff metadata, and
submission/commit order — not in CPU work ownership.

## 3. Capability and fallback contract

`DeviceCreationParameters::enableComputeQueue` currently requires a compute-only family. The
prototype changes that from a device-selection failure into a best-effort capability:

| Hardware condition | Renderer behavior |
|---|---|
| Distinct Compute family available and async prototype enabled | Create that queue; phase zero remains no-op, then use the four-submission schedule only after its proof gates pass. |
| No distinct Compute family | Do not create an alias `Queue` for the Graphics `VkQueue`; retain the current single-Graphics schedule. |
| Prototype disabled | Retain the current schedule regardless of hardware. |
| Queue/device failure | Stop submitting dependent work, restore only unaccepted CPU state, repair ownership when possible, and make the next frame safe. |

The initial switch is experimental and off by default. It must be independently observable in logs:
requested/effective mode, Graphics and Compute family indices, and the reason for fallback.

Using a second queue from the Graphics family is a separate optimization. It adds queue-count and
driver-scheduling questions while failing to exercise the queue-family ownership path this prototype
exists to prove, so it is intentionally deferred.

## 4. Resource-sharing and ownership model

### 4.1 Inputs shared by Compute shadow and Graphics effects

Shadow and the concurrent Graphics effects all read the normalized G-buffer and common trace data.
An exclusive ownership transfer of those inputs would move ownership away from Graphics and serialize
the work that is supposed to overlap. Those resources must instead be created with concurrent
Graphics/Compute sharing when a distinct Compute family is active.

The resource-creation inventory must include at least:

- G-buffer world position, normal, and depth;
- scene-shading, lighting, mesh-view, scene-BVH, instance, material, mesh, and TLAS inputs used by
  shadow and the Graphics effects;
- the backing buffers of the TLAS and every shared BLAS: hardware traversal shares those allocations
  even though the acceleration-structure RHI object is not itself a `BufferDesc`;
- deferred-target slot buffer and the global descriptor-buffer resource and sampler segments;
- any shadow software-BVH/statistics input used by the software path.

The resource-creation API needs an explicit queue-family sharing intent; the Vulkan backend maps it
to `VK_SHARING_MODE_CONCURRENT` only for this enumerated cross-queue set. Existing resources retain
their exclusive behavior by default. `RayTracingAccelStructDesc` must propagate the same intent to its
internally created backing buffer; adding the mask only to ordinary buffers and textures would leave
the shared TLAS/BLAS allocations exclusive.

### 4.2 Shadow result

`shadowVisibility` has one producer on Compute and is first consumed by deferred lighting on
Graphics. It remains exclusive and uses an explicit paired ownership round trip:

1. Compute records a release barrier after its final write and signals its queue timeline.
2. Graphics final waits for that timeline and records the matching acquire barrier before deferred
   lighting samples the image.
3. The state handoff records both image/buffer state and owning queue family, so later frame setup
   never guesses ownership.
4. After Graphics has completed its final read, it releases the reusable frame slot back to Compute;
   Compute waits for and acquires that release before its next use of the slot.

Soft-shadow scratch/history images and delayed-readback buffers stay Compute-owned unless a real
Graphics consumer is discovered. They do not need a gratuitous handoff.

### 4.3 State-tracking work

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
| Compute shadow | soft-shadow temporal advance, delayed-readback submission ID, Compute-side resource ownership/release |
| Graphics effects | caustic, surfel-GI, and AVBOIT temporal/output state |
| Graphics final | lighting/composite state, final frame completion, and presentation-visible completion |

Only work in a submission that was not accepted may be restored from the pre-frame snapshot.
`finalizeSoftShadowTemporalHistory()` must occur after Compute acceptance, but only after all CPU
sibling recording has stopped reading the pre-swap bindless handles.

The software-shadow delayed readback must track the accepted **Compute** submission ID and check the
Compute queue's completed timeline before mapping; a frame-count delay alone is no longer a valid
completion proof.

An exclusive release without a later acquire is a special recovery case. If Compute was accepted but
Graphics final was not, the renderer must submit a small Graphics recovery command list that waits
for Compute and performs the acquire into a documented safe state before the resource is reused. If
that recovery submission cannot be accepted, the device is treated as unusable rather than allowing
the next frame to assume a false owner. This recovery path is a required failure-injection test, not
an optional cleanup.

## 6. Timing and observability

The current `GpuTimingSubmissionTicket` represents one complete submission. The async schedule
needs one accepted-submission ticket each for Graphics prefix, Compute shadow, Graphics effects, and
Graphics final, with each pass scope assigned to its owning ticket.

The old whole-frame scope starts in the first Graphics command list and ends in composite. It cannot
be reused with the current single-submission ticket rules. Before enabling the feature, timing must
gain a multi-submission frame transaction (or an equivalent frame-critical-path metric) that:

- preserves an end-to-end graphics-frame measurement without summing overlapping queue work;
- accounts for partial acceptance and retires any query whose begin was submitted even if a later
  end was not; and
- reports individual prefix, shadow, effects, and final timings plus whether Compute actually
  overlapped Graphics effects.

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

### M2 — Four-submission shadow schedule (implemented; validation pending)

- Create the shadow command list for Compute only when M0's effective capability is true.
- Submit prefix, Compute shadow, Graphics effects, and Graphics final with the two existing queue
  timeline waits.
- Record the full `shadowVisibility` cycle: Compute release → Graphics acquire for lighting → Graphics
  release → Compute acquire before frame-slot reuse.

**Gate:** hardware and software shadow paths are pixel-parity tested against the synchronous path
over cold start, steady temporal frames, scene mutation, resize, opaque/transparent shadow cases,
and CSG paths.

### M3 — Acceptance-aware timing and recovery (partially implemented)

- Implemented: one timing ticket per accepted submission; accepted prefix/shadow/effects/final CPU-state commits;
  Graphics recovery for a stranded Compute release; and Compute completion IDs for software-shadow readback.
- Pending: a whole-frame cross-queue critical-path metric, overlap reporting, and injected failures before/after each
  packet plus the recovery acquire.

**Gate:** every injected rejection leaves the next valid frame correct; no stale temporal history,
descriptor retirement, query leak, or ownership validation error remains.

### M4 — Performance and rollout decision

- Capture queue timelines/timestamps that prove effects overlap shadow on supported hardware.
- Compare frame critical path, not a sum of queue durations, against the current path.
- Keep the feature opt-in until the validation and failure gates pass on the target Vulkan drivers.

**Gate:** a measurable overlap exists without a correctness or timing regression. A flat or negative
result is a valid outcome: retain the fallback and use the data to decide whether caustics or GI
deserve a separate proposal.

## 8. Implementation anchors

| Concern | Current owner |
|---|---|
| Frame packet recording, state fan-in, and monolithic submit | `impl/ecs_render/kernel/system.cpp` |
| Shadow recording and soft-shadow temporal/readback state | `impl/ecs_render/raytrace/rt_shadow.cpp`, `rt_softshadow.cpp` |
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
2. **Recovery policy after device-level submit failure:** use the recovery acquire when the Graphics
   queue remains usable; otherwise invalidate/recreate the device rather than continue with unknown
   ownership. The device-loss path needs an explicit renderer-facing signal.

These decisions are intentionally limited to infrastructure required by this shadow prototype. They
do not authorize moving another render pass to Compute.
