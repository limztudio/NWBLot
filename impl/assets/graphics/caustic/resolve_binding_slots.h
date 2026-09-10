// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Heap-only resolve; exposure scales by (1 - decayFactor).
#define NWB_CAUSTIC_RESOLVE_GROUP_SIZE 8

// stage values shared by C++ and shader.
#define NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE 0u
#define NWB_CAUSTIC_RESOLVE_STAGE_WAVELET 1u
#define NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE 2u

// Accumulator layers, one per RGB channel.
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT 3u
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_RED 0u
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_GREEN 1u
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_BLUE 2u

#define NWB_CAUSTIC_RESOLVE_PASS_COUNT 5

#define NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP 4
#define NWB_CAUSTIC_RESOLVE_TILE_SIDE (NWB_CAUSTIC_RESOLVE_GROUP_SIZE + 4 * NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP)
#define NWB_CAUSTIC_RESOLVE_TILE_TEXELS (NWB_CAUSTIC_RESOLVE_TILE_SIDE * NWB_CAUSTIC_RESOLVE_TILE_SIDE)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

