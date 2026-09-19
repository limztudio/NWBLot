// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_HARDWARE_TRANSPARENT_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_HARDWARE_TRANSPARENT_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_HW_TRANSPARENT_CROSSING_CAPACITY 12u
#define NWB_HW_TRANSPARENT_CROSSING_WORDS 5u
#define NWB_HW_TRANSPARENT_WORDS_PER_RAY (1u + NWB_HW_TRANSPARENT_CROSSING_CAPACITY * NWB_HW_TRANSPARENT_CROSSING_WORDS)
#define NWB_HW_TRANSPARENT_OVERFLOW_COUNT (NWB_HW_TRANSPARENT_CROSSING_CAPACITY + 1u)
#define NWB_HW_TRANSPARENT_GROUP_SIZE 8u
#define NWB_HW_TRANSPARENT_OVERFLOW_GROUP_SIZE 64u
#define NWB_HW_TRANSPARENT_PUSH_CONSTANT_BYTES 48u
#define NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS 4u
#define NWB_HW_TRANSPARENT_OVERFLOW_ARGS_GROUPS_X 0u
#define NWB_HW_TRANSPARENT_OVERFLOW_ARGS_COUNT 3u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

