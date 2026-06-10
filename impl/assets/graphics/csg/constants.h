// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#ifndef NWB_GRAPHICS_CSG_CONSTANTS_H
#define NWB_GRAPHICS_CSG_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_MESHLET_CLASS_UNAFFECTED 0u
#define NWB_CSG_MESHLET_CLASS_INTERSECTING 1u
#define NWB_CSG_MESHLET_CLASS_FULLY_REMOVED 2u

#define NWB_CSG_BOUNDS_VALID_FLAG 1
#define NWB_CSG_BOUNDS_FINITE_FLAG 2

#define NWB_CSG_PEEL_LAYER_COUNT 4u
#define NWB_CSG_RECEIVER_SURFACE_LAYER_COUNT 8u
#define NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X 8u
#define NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y 8u
#define NWB_CSG_INTERVAL_PEEL_STEP_COUNT 128u
#define NWB_CSG_INTERVAL_PEEL_REFINE_STEP_COUNT 6u

#define NWB_CSG_CAP_NORMAL_IMAGE_FORMAT "rgba16f"
#define NWB_CSG_INTERVAL_DEPTH_IMAGE_FORMAT "rg32f"
#define NWB_CSG_INTERVAL_ID_IMAGE_FORMAT "r32ui"
#define NWB_CSG_RECEIVER_SURFACE_MASK_IMAGE_FORMAT "r32ui"
#define NWB_CSG_RECEIVER_BACK_SURFACE_MASK_IMAGE_FORMAT "r32ui"

#define NWB_CSG_RECEIVER_SURFACE_MASK_RECEIVER_BITS 10u
#define NWB_CSG_RECEIVER_SURFACE_MASK_RECEIVER_MASK 0x3ffu
#define NWB_CSG_RECEIVER_SURFACE_MASK_PAYLOAD_BITS NWB_CSG_RECEIVER_SURFACE_MASK_RECEIVER_BITS
#define NWB_CSG_RECEIVER_SURFACE_MASK_DEPTH_EPSILON_BITS 64u
#define NWB_CSG_RECEIVER_SURFACE_MASK_INVALID 0xffffffffu
#define NWB_CSG_RECEIVER_BACK_SURFACE_MASK_INVALID 0xffffffffu

#if defined(__cplusplus)
#define NWB_CSG_CAP_NORMAL_CORE_FORMAT NWB::Core::Format::RGBA16_FLOAT
#define NWB_CSG_INTERVAL_DEPTH_CORE_FORMAT NWB::Core::Format::RG32_FLOAT
#define NWB_CSG_INTERVAL_ID_CORE_FORMAT NWB::Core::Format::R32_UINT
#define NWB_CSG_RECEIVER_SURFACE_MASK_CORE_FORMAT NWB::Core::Format::R32_UINT
#define NWB_CSG_RECEIVER_BACK_SURFACE_MASK_CORE_FORMAT NWB::Core::Format::R32_UINT
#endif

#define NWB_CSG_INTERVAL_PEEL_PUSH_FRAME_WIDTH 0u
#define NWB_CSG_INTERVAL_PEEL_PUSH_FRAME_HEIGHT 1u
#define NWB_CSG_INTERVAL_PEEL_PUSH_RECEIVER_COUNT 2u
#define NWB_CSG_INTERVAL_PEEL_PUSH_LAYER_COUNT 3u
#define NWB_CSG_INTERVAL_PEEL_PUSH_CONSTANT_BYTE_SIZE 16u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

