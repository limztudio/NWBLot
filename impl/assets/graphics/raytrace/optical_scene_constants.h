// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_RAYTRACE_OPTICAL_SCENE_CONSTANTS_H
#define NWB_GRAPHICS_RAYTRACE_OPTICAL_SCENE_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED 0u
#define NWB_RT_OPTICAL_BOUNDARY_CLOSED_NESTED 1u
#define NWB_RT_OPTICAL_BOUNDARY_CLOSED_PRIORITY 2u

#define NWB_RT_OPTICAL_BASE_INSTANCE_MASK 0x1u
#define NWB_RT_OPTICAL_TRANSPARENT_INSTANCE_MASK 0x2u

#define NWB_RT_OPTICAL_SCENE_HEADER_BYTES 32u
#define NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET 0u
#define NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET 12u
#define NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET 16u
#define NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET 28u
#define NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID 1u

#define NWB_RT_OPTICAL_INSTANCE_BYTES 16u
#define NWB_RT_OPTICAL_INSTANCE_ENTITY_OFFSET 0u
#define NWB_RT_OPTICAL_INSTANCE_PRIORITY_OFFSET 4u
#define NWB_RT_OPTICAL_INSTANCE_BOUNDARY_OFFSET 8u
#define NWB_RT_OPTICAL_INSTANCE_FLAGS_OFFSET 12u
#define NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT 1u
#define NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID 2u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

