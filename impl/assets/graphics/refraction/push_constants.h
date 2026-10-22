// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_REFRACTION_PUSH_CONSTANTS_H
#define NWB_GRAPHICS_REFRACTION_PUSH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_REFRACTION_GROUP_SIZE 8
#define NWB_REFRACTION_MAX_GROUP_COUNT_X 65535u
#define NWB_REFRACTION_TILE_COUNTER_OFFSET 12u
#define NWB_REFRACTION_DENSE_ARGUMENT_OFFSET 16u
#define NWB_REFRACTION_TILE_ARGUMENT_BYTES 32u

// Shared CPU/Slang ABI; the graph freezes each heap selector before recording.
#define NWB_REFRACTION_PUSH_CONSTANTS_FIELDS(FIELD) \
    FIELD(width, 0u) \
    FIELD(height, 0u) \
    FIELD(deferredResourcesHeapSlot, 0u) \
    FIELD(materialContextSlotsHeapSlot, 0u) \
    FIELD(viewHeapSlot, 0u) \
    FIELD(opaqueReflectionSlot, 0xffffffffu) \
    FIELD(refractionEnabled, 1u) \
    FIELD(tileListSlot, 0u) \
    FIELD(tileArgumentsSlot, 0u)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

