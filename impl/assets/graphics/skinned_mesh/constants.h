// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SKINNED_MESH_CONSTANTS_H
#define NWB_GRAPHICS_SKINNED_MESH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SKINNED_MESH_SKINNING_MODE_LINEAR_BLEND 0u
#define NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION 1u

#define NWB_SKINNED_MESH_GROUP_SIZE_X 64
#define NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X 128

// 0 keeps the groupshared baseline; variants may opt into the wave path.
#ifndef NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
#define NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE 0u
#endif
#define NWB_SKINNED_MESH_EPSILON 0.000001
// Joint/weight pairs per skin record.
#define NWB_SKINNED_MESH_MAX_INFLUENCE_COUNT 4u
#define NWB_SKINNED_MESH_SKIN_JOINT_WORD_COUNT 2u
#define NWB_SKINNED_MESH_SKIN_JOINT_BITS 16u
#define NWB_SKINNED_MESH_SKIN_JOINT_MASK 0xffffu
#define NWB_SKINNED_MESH_SKIN_WEIGHT_BYTE_OFFSET 8u
#define NWB_SKINNED_MESH_SKIN_INFLUENCE_BYTE_SIZE 24u
#define NWB_SKINNED_MESH_SKIN_INFLUENCE_ALIGNMENT 8u

#define NWB_SKINNED_MESH_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_PUSH_SKIN_COUNT 1u
#define NWB_SKINNED_MESH_PUSH_JOINT_COUNT 2u
#define NWB_SKINNED_MESH_PUSH_SKINNING_MODE 3u
#define NWB_SKINNED_MESH_PUSH_BINDLESS_RESOURCES_SLOT 4u
#define NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE 20u

#define NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_BOUNDS_PUSH_BINDLESS_RESOURCES_SLOT 1u
#define NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE 8u

#define NWB_SKINNED_MESH_REPACK_GROUP_SIZE_X 64
#define NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_REPACK_PUSH_BINDLESS_RESOURCES_SLOT 1u
#define NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE 8u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

