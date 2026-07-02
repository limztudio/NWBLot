// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_REPROJECT_MERGE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_REPROJECT_MERGE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Soft OPAQUE shadow TEMPORAL reproject-merge: the pass inserted BETWEEN
// the half-res soft trace + the a-trous resolve, per shadow slot, that accumulates the noisy per-frame trace over time so
// the per-frame samples-per-pixel can drop toward 1 while STATIC receivers still converge smooth -- and a MOVING/spinning
// occluder leaves NO ghost trail. There are NO motion vectors and NO prev-G-buffer in this engine (the view is rebuilt
// fresh each frame with no history), so instead of chasing per-pixel motion the merge REPROJECTS the CURRENT world position
// through a STASHED previous-frame worldToClip: for a STATIC receiver that reprojection is EXACT no matter how the occluder
// moved, which collapses the whole moving-occluder problem to a VALUE-agreement test (gates B/C below) rather than a motion
// test. The pass reads the raw trace (this frame's "current" sample), the previous accumulated visibility + its moments,
// the current + previous half-res geometry caches, and the full-res world-position G-buffer; it writes the accumulated
// visibility (the a-trous then reads THIS instead of the raw trace, and it becomes next frame's history) + the next moments.
#define NWB_SHADOW_REPROJECT_MERGE_SET 0

// SRV: the raw half-res trace this frame -- the "current" sample the blend leans on. Texture2DArray, one layer per
// shadow slot.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE       0
// SRV: the PREVIOUS frame's accumulated visibility (the history buffer), Texture2DArray. Reprojected + gated + blended.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN       1
// SRV: the PREVIOUS frame's moments (Texture2DArray: .x = m1 (luma), .y = m2 (luma^2), .z = n (history length in frames)).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN       2
// SRV: the CURRENT half-res geometry cache (shadowSoftGeometry: .xy = octahedral receiver normal, .z = camera distance,
// .w = validity) -- the receiver geometry the reprojected pixel is compared AGAINST for the disocclusion gate.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR    3
// SRV: the PREVIOUS half-res geometry cache (shadowSoftGeometryPrev) sampled at the reprojected history texel -- the
// disocclusion gate compares curr-vs-prev camera distance (plane test) + normal agreement to detect a moving RECEIVER.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV    4
// SRV: the FULL-res world-position G-buffer (Texture2D). The merge reads the CURRENT world position at the half pixel's
// 2x texel (the geometry cache stores camera-distance, not world-pos, so the reprojection reads world-pos directly, exactly
// like the geometry downsample does) and projects it through prevWorldToClip to find the history texel.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS 5
// UAV: the accumulated visibility this frame (Texture2DArray) -- the a-trous resolve reads it as its "raw trace" input,
// and it becomes NEXT frame's history via the frame-end ping-pong swap.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT      6
// UAV: the next-frame moments (Texture2DArray: .x = m1, .y = m2, .z = n).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT      7

// Group size shared with the resolve (8x8 = 64 threads per group, one thread per HALF-res pixel).
#define NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE NWB_SHADOW_RESOLVE_GROUP_SIZE

// Named tuning constants (NO magic numbers -- each is one gate's knob; see the shader's per-gate comments for the math):
//  - GAMMA: gate B variance-clamp band half-width, in standard deviations of the current 3x3 spatial visibility. The
//    make-or-break anti-ghost: when a large occluder sweeps in, the newly-shadowed texel's local mean drops, stale-bright
//    history lands OUTSIDE mu +/- GAMMA*sigma and is clamped hard toward the new dark value -> the ghost collapses in 1-2
//    frames. 5 sigmas keeps same-region temporal averaging intact (noise stays inside the band) but rejects the umbra<->lit
//    disagreement (~1.0, many sigmas out).
#define NWB_SHADOW_TEMPORAL_GAMMA          5.0
//  - ANTILAG: gate C temporal-gradient antilag strength. A large per-frame luma gradient (a hard swept shadow edge) scales
//    the effective history length toward 0 so the blend snaps to the current sample instead of lagging.
#define NWB_SHADOW_TEMPORAL_ANTILAG        4.0
//  - ALPHA_MIN: the minimum blend alpha (weight of the current sample), which caps the temporal lag at ~1/ALPHA_MIN frames
//    (~10) as a global anti-ghost backstop even if a gate mis-fires -- history can never dominate indefinitely.
#define NWB_SHADOW_TEMPORAL_ALPHA_MIN      0.1
//  - PLANE_K: gate A plane-distance tolerance, in multiples of the distance-proportional world spacing (see
//    nwbShadowTemporalWorldSpacing). curr-vs-prev camera distance beyond this is a moving receiver / camera cut -> reset.
#define NWB_SHADOW_TEMPORAL_PLANE_K        1.0
//  - NORMAL_MIN: gate A minimum dot(currNormal, prevNormal) for the reprojected history to be accepted (an orientation
//    crease or a receiver that rotated away fails this).
#define NWB_SHADOW_TEMPORAL_NORMAL_MIN     0.9
//  - MAX_HISTORY: n clamp. Caps the convergence floor (alpha never falls below 1/(MAX_HISTORY+1)) so the accumulator keeps
//    a small responsiveness to slow lighting changes and never fully freezes.
#define NWB_SHADOW_TEMPORAL_MAX_HISTORY    32.0
//  - The camera-distance-proportional world spacing the plane gate scales by, matching the resolve's edge-stop metric
//    (camDist * 0.02, clamped away from zero). Shared so gate A's tolerance tracks camera distance like the a-trous does.
#define NWB_SHADOW_TEMPORAL_WORLD_SPACING_SCALE 0.02
#define NWB_SHADOW_TEMPORAL_WORLD_SPACING_MIN   1e-3


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

