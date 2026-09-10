// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_REPROJECT_MERGE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_REPROJECT_MERGE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Temporal merge of the soft trace into the resolve; reprojection is gated.
#define NWB_SHADOW_REPROJECT_MERGE_SET 0

// Raw half-res trace this frame.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE       0
// Previous accumulated visibility.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN       1
// Previous moments (.x = m1, .y = m2, .z = n).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN       2
// Current geometry cache.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR    3
// Previous geometry cache (disocclusion gate).
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV    4
// Full-res world positions, projected through prevWorldToClip.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS 5
// Accumulated visibility output.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT      6
// Next-frame moments output.
#define NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT      7

// 8x8 threads per group, one per half-res pixel.
#define NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE NWB_SHADOW_RESOLVE_GROUP_SIZE

//  - GAMMA: gate B half-width in std-devs.
#define NWB_SHADOW_TEMPORAL_GAMMA          5.0
//  - ANTILAG: gate C strength.
#define NWB_SHADOW_TEMPORAL_ANTILAG        4.0
// History resets need 3x3 corroboration.
#define NWB_SHADOW_TEMPORAL_ANTILAG_SUPPORT_FRACTION 0.5
#define NWB_SHADOW_TEMPORAL_ANTILAG_MIN_SUPPORT      3u
//  - NOISE_SLACK: gate C noise floor.
#define NWB_SHADOW_TEMPORAL_NOISE_SLACK    2.0
//  - ALPHA_MIN: minimum blend alpha.
#define NWB_SHADOW_TEMPORAL_ALPHA_MIN      0.1
//  - PLANE_K: gate A plane tolerance.
#define NWB_SHADOW_TEMPORAL_PLANE_K        1.0
//  - NORMAL_MIN: gate A normal threshold.
#define NWB_SHADOW_TEMPORAL_NORMAL_MIN     0.9
//  - MAX_HISTORY: n clamp.
#define NWB_SHADOW_TEMPORAL_MAX_HISTORY    32.0
//  - World spacing for the plane gate.
#define NWB_SHADOW_TEMPORAL_WORLD_SPACING_SCALE 0.02
#define NWB_SHADOW_TEMPORAL_WORLD_SPACING_MIN   1e-3


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

