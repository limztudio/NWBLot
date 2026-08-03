// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SKINNED_MESH_CONSTANTS_H
#define NWB_GRAPHICS_SKINNED_MESH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SKINNED_MESH_SKINNING_MODE_LINEAR_BLEND 0u
#define NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION 1u

#define NWB_SKINNED_MESH_GROUP_SIZE_X 64
#define NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X 128

// A/B switch for the meshlet-bounds group reductions. The group (128) is larger than the
// wave on this device (subgroup 64 on AMD BC-250 / RADV, 32 on NVIDIA), so unlike the
// surfel SH reduce (group == wave) one WaveActive* does NOT span the group: the wave path
// is a two-stage fold -- one WaveActive* per wave (no barrier), then a single barrier plus
// a tiny cross-wave tree of the per-wave results (2 waves here). That drops each of the six
// reduces from 8 group-wide barriers to ~2. The default must also run on Vulkan 1.3 devices
// without subgroup arithmetic support, so leave the groupshared-tree baseline selected (0) unless a
// capability-gated shader variant explicitly opts into the wave path. Shader-only define -> each
// arm is a fresh .vol recook of the same namesym-domain binary (no per-arm C++ rebuild), mirroring
// NWB_SURFEL_USE_WAVE_REDUCE.
#ifndef NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
#define NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE 0u
#endif
#define NWB_SKINNED_MESH_EPSILON 0.000001
// Serialized skin records carry this many joint/weight pairs on both CPU and GPU.
#define NWB_SKINNED_MESH_MAX_INFLUENCE_COUNT 4u
#define NWB_SKINNED_MESH_SKIN_INFLUENCE_FLOAT_COUNT (NWB_SKINNED_MESH_MAX_INFLUENCE_COUNT * 2u)
#define NWB_SKINNED_MESH_JOINT_MATRIX_FLOAT_COUNT 12u

#define NWB_SKINNED_MESH_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_PUSH_SKIN_COUNT 1u
#define NWB_SKINNED_MESH_PUSH_JOINT_COUNT 2u
#define NWB_SKINNED_MESH_PUSH_SKINNING_MODE 3u
#define NWB_SKINNED_MESH_PUSH_ATTRIBUTE_COUNT 4u
// The persistent per-runtime stream-slot payload is a UniformBuffer heap entry. The skinning dispatch uses the first
// free word of payload1 to select it.
#define NWB_SKINNED_MESH_PUSH_BINDLESS_RESOURCES_SLOT 5u
#define NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE 32u

#define NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT 0u
// Bounds and repack have a single uint4 push lane, so their first padding word selects the same heap UniformBuffer.
#define NWB_SKINNED_MESH_BOUNDS_PUSH_BINDLESS_RESOURCES_SLOT 1u
#define NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE 16u

#define NWB_SKINNED_MESH_REPACK_GROUP_SIZE_X 64
#define NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_REPACK_PUSH_BINDLESS_RESOURCES_SLOT 1u
#define NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE 16u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

