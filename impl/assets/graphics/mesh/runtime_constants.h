// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
#pragma once
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_MESH_RUNTIME_CONSTANTS_H
#define NWB_GRAPHICS_MESH_RUNTIME_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_MESH_DISPATCH_FLAG_SCISSOR_CULL (1u << 0u)
#define NWB_MESH_DISPATCH_FLAG_MESHLET_FRUSTUM_CULL (1u << 1u)
#define NWB_MESH_DISPATCH_FLAG_MESHLET_CONE_CULL (1u << 2u)

#define NWB_MESH_PUSH_DISPATCH_MESHLET_COUNT 0u
#define NWB_MESH_PUSH_DISPATCH_INSTANCE_INDEX 1u
#define NWB_MESH_PUSH_DISPATCH_MATERIAL_CONSTANT_BYTE_OFFSET 2u
#define NWB_MESH_PUSH_DISPATCH_FLAGS 3u
#define NWB_MESH_PUSH_CONSTANT_BYTE_SIZE 48u

#define NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT 6

#define NWB_MESH_RASTER_COLOR_LOCATION 0
#define NWB_MESH_RASTER_NORMAL_LOCATION 1
#define NWB_MESH_RASTER_TANGENT_LOCATION 2
#define NWB_MESH_RASTER_UV0_LOCATION 3
#define NWB_MESH_RASTER_WORLD_POSITION_LOCATION 4

#define NWB_MESH_COLOR_TARGET_LOCATION 0

#define NWB_MESH_GBUFFER_BASE_COLOR_LOCATION 0
#define NWB_MESH_GBUFFER_NORMAL_LOCATION 1
#define NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION 2
#define NWB_MESH_GBUFFER_TARGET_COUNT 3

#define NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX 0
#define NWB_MESH_EMULATION_VERTEX_ATTRIBUTE_COUNT 6
#define NWB_MESH_EMULATION_VERTEX_FLOAT_COUNT 24u
#define NWB_MESH_EMULATION_VERTEX_POSITION_LOCATION 0
#define NWB_MESH_EMULATION_VERTEX_NORMAL_LOCATION 1
#define NWB_MESH_EMULATION_VERTEX_TANGENT_LOCATION 2
#define NWB_MESH_EMULATION_VERTEX_UV0_LOCATION 3
#define NWB_MESH_EMULATION_VERTEX_COLOR_LOCATION 4
#define NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_LOCATION 5
#define NWB_MESH_EMULATION_VERTEX_POSITION_FLOAT_OFFSET 0u
#define NWB_MESH_EMULATION_VERTEX_NORMAL_FLOAT_OFFSET 4u
#define NWB_MESH_EMULATION_VERTEX_TANGENT_FLOAT_OFFSET 8u
#define NWB_MESH_EMULATION_VERTEX_UV0_FLOAT_OFFSET 12u
#define NWB_MESH_EMULATION_VERTEX_COLOR_FLOAT_OFFSET 16u
#define NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_FLOAT_OFFSET 20u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
