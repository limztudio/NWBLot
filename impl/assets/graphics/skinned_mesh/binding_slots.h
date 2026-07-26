// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SKINNED_MESH_BINDING_SLOTS_H
#define NWB_SKINNED_MESH_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SKINNED_MESH_SET 0
#define NWB_SKINNED_MESH_BOUNDS_SET 0

// The 12 former skinning-stream bindings (0..11) are intentional ABI gaps. Their persistent buffers are selected
// through a heap UniformBuffer slot payload and fetched from the global StorageBuffer heap. Do not repurpose or
// renumber the holes.
// Retired local selector-CBV ABI gap. The stream-slot payload is now a global UniformBuffer heap entry selected by
// push constants; preserve this number rather than reusing it.
#define NWB_SKINNED_MESH_BINDING_BINDLESS_RESOURCES 12

// The six former meshlet-bounds bindings (0..5) are intentional ABI gaps. The heap UniformBuffer slot payload
// selects every source and output buffer through the global StorageBuffer heap. Do not repurpose or renumber the
// holes.
// Retired local selector-CBV ABI gap; resources are selected from the global heap.
#define NWB_SKINNED_MESH_BOUNDS_BINDING_BINDLESS_RESOURCES 6

// Per-frame skinned-normal repack into the RT attribute buffer: re-derives the triangle-corner shading normals from
// the current-frame deformed (attribute-stream) skinned normals so the RT shadow + caustic traces bend on the live
// pose, not the bind pose. Reproduces BuildMeshletTriangleAttributes (meshlet_vertex_attributes.h) on the GPU.
#define NWB_SKINNED_MESH_REPACK_SET 0

// The six former repack bindings (0..5) are intentional ABI gaps. The heap UniformBuffer slot payload selects every
// source and output buffer through the global StorageBuffer heap. Do not repurpose or renumber the holes.
// Retired local selector-CBV ABI gap; resources are selected from the global heap.
#define NWB_SKINNED_MESH_REPACK_BINDING_BINDLESS_RESOURCES 6


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

