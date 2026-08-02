// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Soft resolve denoises half-resolution transmittance and upsamples slot ranges.
#define NWB_SHADOW_RESOLVE_SET 0

// Shared C++/shader resolve-stage ABI.
#define NWB_SHADOW_RESOLVE_STAGE_PREPARE 0u
#define NWB_SHADOW_RESOLVE_STAGE_WAVELET 1u
#define NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE 2u

// Sparse local slots are ABI gaps; reads are selected through global heap slots.

// Half-resolution visibility/transmittance input.
#define NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF 0
// Packed normal, camera distance, and receiver-validity edge-stop cache.
#define NWB_SHADOW_RESOLVE_BINDING_GEOMETRY 1
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH 2
// Half-resolution ping-pong output.
#define NWB_SHADOW_RESOLVE_BINDING_OUTPUT 3
#define NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR 4
// Full-resolution visibility output sampled by deferred lighting.
#define NWB_SHADOW_RESOLVE_BINDING_VISIBILITY 5

// Temporal moments input; ignored when momentsValid is false.
#define NWB_SHADOW_RESOLVE_BINDING_MOMENTS 6
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS 7
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL 8
#define NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING 9

// Geometry downsample writes the packed half-resolution edge-stop cache.
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_SET 0
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION 0
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_NORMAL 1
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH 2
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING 3
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_BINDLESS_RESOURCES NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT 4

#define NWB_SHADOW_RESOLVE_GROUP_SIZE 8

// Scalar opaque and RGB transparent wavelet variants share the resolve source.
#define NWB_SHADOW_RESOLVE_CHANNELS_SCALAR 1
#define NWB_SHADOW_RESOLVE_CHANNELS_RGB    3

// Must remain odd so the selected upsample input is final.
#define NWB_SHADOW_RESOLVE_PASS_COUNT 1

#define NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT 1

// LDS tile covers the largest cooperatively loaded wavelet halo.
#define NWB_SHADOW_RESOLVE_LDS_MAX_STEP 4
#define NWB_SHADOW_RESOLVE_TILE_SIDE (NWB_SHADOW_RESOLVE_GROUP_SIZE + 4 * NWB_SHADOW_RESOLVE_LDS_MAX_STEP)
#define NWB_SHADOW_RESOLVE_TILE_TEXELS (NWB_SHADOW_RESOLVE_TILE_SIDE * NWB_SHADOW_RESOLVE_TILE_SIDE)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

