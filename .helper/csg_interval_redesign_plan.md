# CSG Interval Rendering Redesign Plan

Updated: 2026-06-12

## Goal

Fix opaque CSG cap rendering for skinned and other complex receiver meshes by replacing the current screen-space receiver mask pairing with explicit per-pixel receiver intervals.

The target result is:

1. Cube and simple static receivers continue to render correct clipped surfaces and caps.
2. Skinned receivers render caps clipped to the actual skinned receiver surface, not to unrelated front/back depth pairs.
3. Transparent CSG can later reuse the same interval representation instead of remaining discard-only.

## Current Diagnosis

The current opaque flow is:

1. `RendererSystem::render()` dispatches cutter interval peel.
2. It draws a `CsgReceiverSurface` pass that writes receiver opening and receiver back-surface masks.
3. It draws the clipped opaque receiver.
4. It runs fullscreen cap fill.

Relevant files:

- `impl/ecs_render/system.cpp`
- `impl/assets/graphics/csg/receiver_surface_ps.slang`
- `impl/assets/graphics/csg/interval_sample.slangi`
- `impl/assets/graphics/csg/interval_cap_fill_ps.slang`

The weak point is in `interval_sample.slangi`: cap visibility is accepted only when the sampled cutter interval can be matched to a receiver opening depth and a receiver back-surface depth. Those two masks are stored independently, so the shader has to infer which surfaces form a receiver span.

That inference works for a cube because each pixel usually has one simple convex receiver span. It fails on the skinned character because one pixel can hit multiple independent receiver layers, such as torso, arm, hand, leg, and back-facing curved shell surfaces. The cap code can pair an opening from one layer with a back surface from another layer and accept cap pixels outside the real receiver volume.

Diagnostics already performed:

1. Moving the skinned receiver closer to the camera did not remove the issue.
2. Padding runtime CSG bounds did not materially change the issue.
3. Bypassing receiver-span validation in cap fill produced the full projected cutter cap, proving cutter interval peel data exists.
4. Disabling the cap-fill 3x3 neighbor fallback did not materially change the issue.

Conclusion: the failure is in receiver span reconstruction/matching, not camera distance, SDF interval generation, or neighbor fallback dilation.

## Target Architecture

Replace independent receiver opening/back masks with explicit per-pixel receiver surface events and derived receiver spans.

The redesigned opaque flow should be:

1. Build receiver surface events.
2. Sort or insert them by depth per pixel.
3. Pair events into receiver spans.
4. Build cutter intervals with the existing SDF ray peel.
5. Intersect receiver spans with cutter intervals.
6. Draw clipped receiver surface and fullscreen caps from the valid intersection intervals.

Conceptually:

```text
receiver events:
    depth, receiverIndex, faceSign

receiver spans:
    receiverIndex, enterDepth, exitDepth

cutter intervals:
    receiverIndex, cutterIndex, enterDepth, exitDepth, capNormal

removed intervals:
    receiverSpan intersect cutterInterval
```

Caps should be generated only from `removed intervals`, not from a later heuristic that tries to recover the receiver span from separate masks.

## Proposed GPU Data

Start with fixed-size per-pixel buffers. Keep the first implementation simple and debuggable before optimizing.

```cpp
struct CsgReceiverSurfaceEventGpuData{
    float depth;
    uint receiverIndex;
    uint faceSign;
    uint flags;
};

struct CsgReceiverSpanGpuData{
    float enterDepth;
    float exitDepth;
    uint receiverIndex;
    uint flags;
};

struct CsgRemovedIntervalGpuData{
    float enterDepth;
    float exitDepth;
    float3 capNormal;
    uint receiverIndex;
};
```

Suggested initial limits:

- Receiver events per pixel: 16 or 32.
- Receiver spans per pixel: 8 or 16.
- Cutter intervals per pixel: keep current peel count first.

Add overflow counters or an overflow mask from day one. Silent overflow will look similar to the current bug and make debugging difficult.

Use linear view depth for interval ordering and comparisons. Keep normalized hardware depth only for final `SV_Depth` output. This reduces depth tolerance confusion near camera and at grazing angles.

## Render Pass Design

### 1. Receiver Event Pass

Rasterize CSG receiver meshes two-sided and write one event per visible receiver surface fragment.

Important settings:

- Use skinned runtime position buffers for skinned receivers.
- Avoid depending on the already-written opaque depth buffer for correctness.
- Use depth/stencil only as an optimization after correctness is established.
- Write both front and back events with `SV_IsFrontFace`.

Implementation options:

1. Fixed K-buffer with atomic insertion.
2. Multi-pass depth peeling.
3. Linked list A-buffer.

Recommendation: start with fixed K-buffer because the implementation and validation are simpler. Add overflow visualization and counters.

### 2. Receiver Span Build Pass

For each pixel:

1. Load receiver events.
2. Sort by linear depth.
3. Group by receiver index.
4. Pair front/back events into spans.
5. Store receiver spans.

For closed manifold receivers, front-facing/back-facing winding can define enter/exit events. For imperfect meshes, use conservative rules:

- Ignore invalid zero-length spans.
- Clamp spans to frame near/far range.
- Mark ambiguous event sequences in a debug mask.

### 3. Cutter Interval Pass

Keep the existing SDF interval peel as the first version, but output linear-depth intervals or convert immediately into the same depth space used by receiver spans.

The existing ray/SDF logic is useful and diagnostics indicate it is not the primary failure.

### 4. Interval Combine Pass

For each pixel:

1. Iterate receiver spans.
2. Iterate cutter intervals for the same receiver.
3. Compute intersection.
4. Store removed intervals.

```text
enter = max(receiver.enterDepth, cutter.enterDepth)
exit  = min(receiver.exitDepth, cutter.exitDepth)
valid if enter < exit
```

Cap depth for subtract should come from the cutter boundary that is visible inside the receiver span. The cap normal remains the cutter normal, oriented toward the camera for G-buffer lighting.

### 5. Receiver Surface Draw

Keep material surface drawing mostly as-is:

- CSG receiver pixels still use SDF discard.
- Interval data can optionally be sampled to stabilize edge disagreement.
- The receiver material remains responsible for normal/base color output.

### 6. Cap Fill Pass

Fullscreen cap fill samples `removed intervals` directly.

It should not call `nwbCsgFindReceiverSurfaceSpanForInterval()` because spans have already been built explicitly.

Cap fill writes:

- G-buffer base color.
- G-buffer normal.
- G-buffer world position.
- Depth.

The current hardcoded cap color can remain temporarily, but the final design should derive cap material policy explicitly.

## Migration Plan

### Phase 1: Debug Infrastructure

1. Add debug views for:
   - Receiver event count per pixel.
   - Receiver span count per pixel.
   - Cutter interval count per pixel.
   - Removed interval count per pixel.
   - Overflow pixels.
2. Add smoke captures for these debug views if the testbed supports it.
3. Preserve current CSG path behind a switch while developing the new path.

### Phase 2: Receiver Event Buffer

1. Add deferred CSG event textures or structured buffers.
2. Add receiver event writable binding layout and binding set.
3. Replace or supplement `receiver_surface_ps.slang` with an event-writing shader.
4. Support both mesh shader and compute-emulation render paths.

### Phase 3: Receiver Span Build

1. Add compute shader to sort/pair events into spans.
2. Start with simple insertion sort over fixed K events.
3. Store span buffer and overflow/ambiguous flags.
4. Validate on cube first, then skinned smoke.

### Phase 4: Interval Combine

1. Adapt existing cutter interval peel output to linear depth.
2. Add compute pass that intersects receiver spans with cutter intervals.
3. Store removed intervals with cap normal and receiver id.

### Phase 5: Cap Fill Rewrite

1. Rewrite `interval_cap_fill_ps.slang` to sample removed intervals.
2. Remove dependency on receiver opening/back-surface masks.
3. Keep current cap G-buffer output behavior initially.
4. Compare against current cube CSG captures.

### Phase 6: Cleanup Old Mask Path

1. Remove `csgReceiverSurfaceMask` and `csgReceiverBackSurfaceMask` once the new path is stable.
2. Delete `nwbCsgFindReceiverSurfaceSpanForInterval()` and related mask pairing helpers.
3. Keep compatibility only if needed for a temporary build flag.

### Phase 7: Transparent Reuse

After opaque is correct, transparent can use the same receiver/cutter interval data:

1. Transparent discard can consult removed intervals instead of only direct SDF discard.
2. Transparent cap behavior can be designed explicitly.
3. AVBOIT passes should not maintain a separate, weaker CSG implementation.

## Validation Matrix

Required smoke cases:

1. Static cube receiver, sphere cutter.
2. Static cube receiver, capsule cutter.
3. Skinned receiver, sphere cutter, default camera.
4. Skinned receiver, sphere cutter, close camera.
5. Skinned receiver with animation at early/mid/late settle points.
6. Receiver with overlapping limbs or multiple screen-depth layers.
7. Receiver near camera to stress depth precision.
8. Receiver viewed at grazing angle.

Useful assertions:

1. Event overflow count is zero for standard smoke scenes.
2. Ambiguous span count is visible in debug capture and tracked.
3. Cube result matches or improves current output.
4. Skinned cap no longer protrudes outside the clipped receiver surface.

## Risks

1. Fixed K-buffer overflow can still fail on dense layered pixels.
   - Mitigation: debug overflow mask and configurable K.
2. Event ordering depends on consistent face orientation.
   - Mitigation: add ambiguous span diagnostics and fallback rules.
3. Extra passes add GPU cost.
   - Mitigation: scope event/span work to receivers with active cutters and use receiver/cutter scissor bounds.
4. MSAA/sample behavior may need separate handling.
   - Mitigation: initially target current smoke sample count, then extend per sample if needed.
5. Transparent reuse may require different composition rules.
   - Mitigation: solve opaque first and treat transparent as a second design pass.

## Preferred First Implementation

Implement a fixed K-buffer receiver event path for opaque CSG only, behind a build/runtime flag. Keep the old mask path available during development.

The first milestone is not performance. The first milestone is a debug-visible interval model that makes the skinned receiver cap correct and explains any remaining failures with overflow or ambiguous-span pixels.
