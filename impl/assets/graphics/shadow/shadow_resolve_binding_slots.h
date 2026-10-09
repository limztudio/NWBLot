// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Denoises half-resolution transmittance, then upsamples.

// Shared C++/shader resolve-stage ABI.
#define NWB_SHADOW_RESOLVE_STAGE_WAVELET 1u
#define NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE 2u

#define NWB_SHADOW_RESOLVE_GROUP_SIZE 8

// Opaque and transparent variants share the source.
#define NWB_SHADOW_RESOLVE_CHANNELS_SCALAR 1
#define NWB_SHADOW_RESOLVE_CHANNELS_RGB    3
#define NWB_SHADOW_RESOLVE_CHANNELS_COMBINED 4

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

