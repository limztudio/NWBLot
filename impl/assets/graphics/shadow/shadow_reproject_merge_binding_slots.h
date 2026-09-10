// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_REPROJECT_MERGE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_REPROJECT_MERGE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Temporal reproject-merge between the half-res soft trace and the a-trous resolve.
// Accumulates the noisy trace over time so static receivers converge at ~1 SPP without
// ghosting on moving occluders. No motion vectors: the current world position is
// reprojected through the stashed previous worldToClip, then gated (gates A/B/C).
#define NWB_SHADOW_REPROJECT_MERGE_SET 0

// Pass ABI: push-constant heap slots select these reads without local descriptor entries.
// Heap SRV: raw half-res trace this frame (Texture2DArray, one layer per shadow slot).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE       0
// Heap SRV: previous accumulated visibility (Texture2DArray).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN       1
// Heap SRV: previous moments (Texture2DArray: .x = m1, .y = m2, .z = n).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN       2
// Heap SRV: current half-res geometry cache (.xy = normal, .z = camera distance, .w = validity).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR    3
// Heap SRV: previous geometry cache at the reprojected texel (disocclusion gate).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV    4
// Heap SRV: full-res world-position G-buffer; projected through prevWorldToClip.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS 5
// UAV: accumulated visibility (a-trous input; next frame's history).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT      6
// UAV: the next-frame moments (Texture2DArray: .x = m1, .y = m2, .z = n).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT      7

// Shared with the resolve: 8x8 threads per group, one per half-res pixel.
#define NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE NWB_SHADOW_RESOLVE_GROUP_SIZE

// Tuning knobs; see the shader's per-gate comments for the math.
//  - GAMMA: gate B variance-clamp half-width in std-devs of the 3x3 visibility.
#define NWB_SHADOW_TEMPORAL_GAMMA          5.0
//  - ANTILAG: gate C antilag strength; supported gradients snap to the current sample.
#define NWB_SHADOW_TEMPORAL_ANTILAG        4.0
// History resets only with corroborated 3x3 support (centre + two neighbours).
#define NWB_SHADOW_TEMPORAL_ANTILAG_SUPPORT_FRACTION 0.5
#define NWB_SHADOW_TEMPORAL_ANTILAG_MIN_SUPPORT      3u
//  - NOISE_SLACK: gate C noise floor in std-devs of the 3x3 luma.
#define NWB_SHADOW_TEMPORAL_NOISE_SLACK    2.0
//  - ALPHA_MIN: minimum blend alpha; caps lag at ~1/ALPHA_MIN frames.
#define NWB_SHADOW_TEMPORAL_ALPHA_MIN      0.1
//  - PLANE_K: gate A plane-distance tolerance in world-spacing units; excess resets.
#define NWB_SHADOW_TEMPORAL_PLANE_K        1.0
//  - NORMAL_MIN: gate A minimum dot(currNormal, prevNormal).
#define NWB_SHADOW_TEMPORAL_NORMAL_MIN     0.9
//  - MAX_HISTORY: n clamp; keeps slight responsiveness to slow changes.
#define NWB_SHADOW_TEMPORAL_MAX_HISTORY    32.0
//  - World spacing for the plane gate; matches the resolve's edge-stop metric.
#define NWB_SHADOW_TEMPORAL_WORLD_SPACING_SCALE 0.02
#define NWB_SHADOW_TEMPORAL_WORLD_SPACING_MIN   1e-3


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

