// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_MESH_BINDING_SLOTS_H
#define NWB_GRAPHICS_MESH_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_MESH_SET
#define NWB_MESH_SET 0
#endif

#define NWB_MESH_BINDING_POSITION 0
#define NWB_MESH_BINDING_NORMAL 1
#define NWB_MESH_BINDING_TANGENT 2
#define NWB_MESH_BINDING_UV0 3
#define NWB_MESH_BINDING_COLOR 4
#define NWB_MESH_BINDING_MESHLET_DESC 5
// The typed material-constant words + the per-instance mutable-storage records (material_typed_bindings.slangi).
// Raster and CSG consumers use these direct mesh-set bindings. Trace consumers enable their heap-selected material
// context before including the shared surface-hook code, so they do not override these binding numbers.
#ifndef NWB_MESH_BINDING_MATERIAL_TYPED
#define NWB_MESH_BINDING_MATERIAL_TYPED 6
#endif
#define NWB_MESH_BINDING_MESHLET_BOUNDS 7
#define NWB_MESH_BINDING_MESHLET_POSITION_REFS 8
#define NWB_MESH_BINDING_MESHLET_ATTRIBUTE_REFS 9
#define NWB_MESH_BINDING_MESHLET_LOCAL_VERTEX_REFS 10
#define NWB_MESH_BINDING_MESHLET_PRIMITIVE_INDICES 11
#ifndef NWB_MESH_BINDING_INSTANCE
#define NWB_MESH_BINDING_INSTANCE 12
#endif
#define NWB_MESH_BINDING_VIEW 13
#define NWB_MESH_BINDING_GENERATED_VERTEX 14


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

