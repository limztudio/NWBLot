// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared SOFTWARE (SW BVH) trace bindings declared by gi_sw_trace.slangi: slots 0-10, with 5-8 reserved. They reuse
// the scene/instance and per-mesh BVH buffers built for SW shadow and caustics. Producers add output bindings at >= 11.

#define NWB_GI_SW_SET 0

// Scene BVH + instance + light list + material record (mirrors caustic/sw_binding_slots.h exactly).
#define NWB_GI_SW_BINDING_SCENE_SHADING 0
#define NWB_GI_SW_BINDING_LIGHT_LIST 1
#define NWB_GI_SW_BINDING_SCENE_NODES 2
#define NWB_GI_SW_BINDING_SCENE_INSTANCES 3
#define NWB_GI_SW_BINDING_INSTANCE_MATERIAL 4
// Slots 5-8 are intentionally unused. SW GI reads per-mesh nodes, positions, indices, and attributes from the global
// descriptor heap through slots carried by the material record.
#define NWB_GI_SW_BINDING_MATERIAL_TYPED 9
#define NWB_GI_SW_BINDING_MESH_INSTANCES 10

// The GI surfel trace reuses the SAME software per-mesh buffers the SW shadow/caustic build (buildSceneSwBvh fills the
// shared dynamic distinct-mesh table in renderer_state.h). The per-mesh geometry is fetched from the descriptor heap.
#include "../shadow/sw_binding_slots.h"

// Hit shadow rays toward the dominant light per bounce hit (1 = on). gi_sw_trace.slangi's shade reads this.
#ifndef NWB_GI_HIT_SHADOW_RAYS
#define NWB_GI_HIT_SHADOW_RAYS 1
#endif

// BVH traversal stack sizes (same as caustic: 32 for mesh, 64 for scene).
#define NWB_GI_SW_MESH_STACK_SIZE 32
#define NWB_GI_SW_SCENE_STACK_SIZE 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

