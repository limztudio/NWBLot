// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SKINNED_MESH_BINDING_SLOTS_H
#define NWB_SKINNED_MESH_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SKINNED_MESH_SET 0
#define NWB_SKINNED_MESH_BOUNDS_SET 0

// Bindings 0..11 are skinning-stream ABI gaps: persistent buffers are heap-selected through a
// UniformBuffer slot payload. Binding 12 is the selector-CBV ABI gap; preserve both numbers.
#define NWB_SKINNED_MESH_BINDING_BINDLESS_RESOURCES 12

// Bindings 0..5 are meshlet-bounds ABI gaps: every source and output buffer is heap-selected through
// the global StorageBuffer heap. Binding 6 is the selector-CBV ABI gap; preserve both numbers.
#define NWB_SKINNED_MESH_BOUNDS_BINDING_BINDLESS_RESOURCES 6

// Per-frame skinned-normal repack into the RT attribute buffer: re-derives triangle-corner shading normals from
// current-frame deformed skinned normals so RT traces bend on the live pose, not the bind pose.
#define NWB_SKINNED_MESH_REPACK_SET 0

// Bindings 0..5 are repack ABI gaps: every source and output buffer is heap-selected. Binding 6 is the
// selector-CBV ABI gap; preserve both numbers.
#define NWB_SKINNED_MESH_REPACK_BINDING_BINDLESS_RESOURCES 6


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

