// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_UI_PUSH_CONSTANTS_H
#define NWB_GRAPHICS_UI_PUSH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_UI_INVALID_HEAP_SLOT 0xffffffffu
#define NWB_UI_PUSH_CONSTANTS_BYTE_SIZE 48u
#define NWB_UI_PUSH_CONSTANTS_SCALE_BYTE_OFFSET 0u
#define NWB_UI_PUSH_CONSTANTS_TRANSLATE_BYTE_OFFSET 8u
#define NWB_UI_PUSH_CONSTANTS_TEXTURE_SLOT_BYTE_OFFSET 16u
#define NWB_UI_PUSH_CONSTANTS_SAMPLER_SLOT_BYTE_OFFSET 20u
#define NWB_UI_PUSH_CONSTANTS_MATERIAL_BYTE_OFFSET 24u
#define NWB_UI_PUSH_CONSTANTS_SDF_CHANNEL_BYTE_OFFSET 28u
#define NWB_UI_PUSH_CONSTANTS_SDF_SPREAD_BYTE_OFFSET 32u
#define NWB_UI_PUSH_CONSTANTS_SDF_ENCODING_BYTE_OFFSET 36u
#define NWB_UI_PUSH_CONSTANTS_RESERVED_BYTE_OFFSET 40u
#define NWB_UI_PUSH_CONSTANTS_RESERVED1_BYTE_OFFSET 44u

// Shared CPU/Slang field order; heap selectors address SampledImage and Sampler classes respectively.
#define NWB_UI_PUSH_CONSTANTS_FIELDS(FLOAT2_FIELD, UINT_FIELD) \
    FLOAT2_FIELD(scale) \
    FLOAT2_FIELD(translate) \
    UINT_FIELD(textureSlot, NWB_UI_INVALID_HEAP_SLOT) \
    UINT_FIELD(samplerSlot, NWB_UI_INVALID_HEAP_SLOT) \
    UINT_FIELD(material, NWB_UI_MATERIAL_SOLID) \
    UINT_FIELD(sdfChannel, 0u) \
    UINT_FIELD(sdfSpreadPixels, 0u) \
    UINT_FIELD(sdfDistanceEncoding, 0u) \
    UINT_FIELD(reserved, 0u) \
    UINT_FIELD(reserved1, 0u)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

