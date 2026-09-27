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

// Compilation selector only; the dynamic program still receives one of the stage values above.
#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_DYNAMIC 3u
// Direct wavelet compilation excludes the small-dilation shared-memory path; the logical stage remains wavelet.
#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_DIRECT 4u
// Fixed small dilations reserve only the shared-memory footprint used by that pass.
#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_ONE 5u
#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_TWO 6u

// Accumulator layers, one per RGB channel.
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT 3u
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_RED 0u
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_GREEN 1u
#define NWB_CAUSTIC_ACCUMULATOR_CHANNEL_BLUE 2u

#define NWB_CAUSTIC_RESOLVE_PASS_COUNT 5
#define NWB_CAUSTIC_RESOLVE_ACTIVITY_INVALID_SLOT 0xffffffffu

#define NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP 4


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

