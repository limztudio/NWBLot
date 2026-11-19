// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CAUSTIC_RESOLVE_GROUP_SIZE 8

#define NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE 0u
#define NWB_CAUSTIC_RESOLVE_STAGE_WAVELET 1u
#define NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE 2u

#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_DYNAMIC 3u
#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_DIRECT 4u
#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_ONE 5u
#define NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_TWO 6u

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

