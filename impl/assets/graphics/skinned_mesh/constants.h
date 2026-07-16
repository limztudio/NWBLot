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
// reduces from 8 group-wide barriers to ~2. Leave ON (1) for the wave-intrinsic path; set to
// 0 to fall back to the groupshared-tree baseline. Shader-only define -> each arm is a fresh
// .vol recook of the same namesym-domain binary (no per-arm C++ rebuild), mirroring
// NWB_SURFEL_USE_WAVE_REDUCE.
#define NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE 1u
#define NWB_SKINNED_MESH_EPSILON 0.000001
#define NWB_SKINNED_MESH_SKIN_INFLUENCE_FLOAT_COUNT 8u
#define NWB_SKINNED_MESH_JOINT_MATRIX_FLOAT_COUNT 12u

#define NWB_SKINNED_MESH_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_PUSH_SKIN_COUNT 1u
#define NWB_SKINNED_MESH_PUSH_JOINT_COUNT 2u
#define NWB_SKINNED_MESH_PUSH_SKINNING_MODE 3u
#define NWB_SKINNED_MESH_PUSH_ATTRIBUTE_COUNT 4u
#define NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE 32u

#define NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE 16u

#define NWB_SKINNED_MESH_REPACK_GROUP_SIZE_X 64
#define NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT 0u
#define NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE 16u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

