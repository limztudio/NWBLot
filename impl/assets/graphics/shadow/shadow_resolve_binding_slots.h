// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Denoises half-resolution transmittance, then upsamples.
#define NWB_SHADOW_RESOLVE_SET 0

// Shared C++/shader resolve-stage ABI.
#define NWB_SHADOW_RESOLVE_STAGE_PREPARE 0u
#define NWB_SHADOW_RESOLVE_STAGE_WAVELET 1u
#define NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE 2u

// Sparse locals are gaps; reads use global heap slots.

// Half-resolution visibility/transmittance input.
#define NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF 0
// Packed normal/distance/validity cache.
#define NWB_SHADOW_RESOLVE_BINDING_GEOMETRY 1
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH 2
// Half-resolution ping-pong output.
#define NWB_SHADOW_RESOLVE_BINDING_OUTPUT 3
#define NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR 4
// Sampled by deferred lighting.
#define NWB_SHADOW_RESOLVE_BINDING_VISIBILITY 5

// Ignored when momentsValid is false.
#define NWB_SHADOW_RESOLVE_BINDING_MOMENTS 6
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS 7
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL 8
#define NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING 9

// Writes the packed edge-stop cache.
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_SET 0
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION 0
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_NORMAL 1
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH 2
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING 3
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_BINDLESS_RESOURCES NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT 4

#define NWB_SHADOW_RESOLVE_GROUP_SIZE 8

// Opaque and transparent variants share the source.
#define NWB_SHADOW_RESOLVE_CHANNELS_SCALAR 1
#define NWB_SHADOW_RESOLVE_CHANNELS_RGB    3

// Keep odd so the upsample input is final.
#define NWB_SHADOW_RESOLVE_PASS_COUNT 1

#define NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT 1

// Covers the largest wavelet halo.
#define NWB_SHADOW_RESOLVE_LDS_MAX_STEP 4
#define NWB_SHADOW_RESOLVE_TILE_SIDE (NWB_SHADOW_RESOLVE_GROUP_SIZE + 4 * NWB_SHADOW_RESOLVE_LDS_MAX_STEP)
#define NWB_SHADOW_RESOLVE_TILE_TEXELS (NWB_SHADOW_RESOLVE_TILE_SIDE * NWB_SHADOW_RESOLVE_TILE_SIDE)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

