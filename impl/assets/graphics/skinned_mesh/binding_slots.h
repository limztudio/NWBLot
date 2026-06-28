// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SKINNED_MESH_BINDING_SLOTS_H
#define NWB_SKINNED_MESH_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SKINNED_MESH_SET 0
#define NWB_SKINNED_MESH_BOUNDS_SET 0

#define NWB_SKINNED_MESH_BINDING_REST_POSITION 0
#define NWB_SKINNED_MESH_BINDING_SKINNED_POSITION 1
#define NWB_SKINNED_MESH_BINDING_REST_NORMAL 2
#define NWB_SKINNED_MESH_BINDING_SKINNED_NORMAL 3
#define NWB_SKINNED_MESH_BINDING_REST_TANGENT 4
#define NWB_SKINNED_MESH_BINDING_SKINNED_TANGENT 5
#define NWB_SKINNED_MESH_BINDING_MESHLET_DESC 6
#define NWB_SKINNED_MESH_BINDING_POSITION_REF_DELTAS 7
#define NWB_SKINNED_MESH_BINDING_ATTRIBUTE_REF_DELTAS 8
#define NWB_SKINNED_MESH_BINDING_ATTRIBUTE_SKINS 9
#define NWB_SKINNED_MESH_BINDING_SKIN_INFLUENCES 10
#define NWB_SKINNED_MESH_BINDING_JOINT_PALETTE 11

#define NWB_SKINNED_MESH_BOUNDS_BINDING_POSITIONS 0
#define NWB_SKINNED_MESH_BOUNDS_BINDING_MESHLET_DESC 1
#define NWB_SKINNED_MESH_BOUNDS_BINDING_POSITION_REF_DELTAS 2
#define NWB_SKINNED_MESH_BOUNDS_BINDING_LOCAL_VERTEX_REFS 3
#define NWB_SKINNED_MESH_BOUNDS_BINDING_PRIMITIVE_INDICES 4
#define NWB_SKINNED_MESH_BOUNDS_BINDING_DYNAMIC_BOUNDS 5

// Per-frame skinned-normal repack into the RT attribute buffer: re-derives the triangle-corner shading normals from
// the current-frame deformed (attribute-stream) skinned normals so the RT shadow + caustic traces bend on the live
// pose, not the bind pose. Reproduces BuildMeshletTriangleAttributes (meshlet_vertex_attributes.h) on the GPU.
#define NWB_SKINNED_MESH_REPACK_SET 0

#define NWB_SKINNED_MESH_REPACK_BINDING_MESHLET_DESC 0
#define NWB_SKINNED_MESH_REPACK_BINDING_PRIMITIVE_INDICES 1
#define NWB_SKINNED_MESH_REPACK_BINDING_ATTRIBUTE_REF_DELTAS 2
#define NWB_SKINNED_MESH_REPACK_BINDING_LOCAL_VERTEX_REFS 3
#define NWB_SKINNED_MESH_REPACK_BINDING_SKINNED_NORMALS 4
#define NWB_SKINNED_MESH_REPACK_BINDING_ATTRIBUTE_BUFFER 5


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

