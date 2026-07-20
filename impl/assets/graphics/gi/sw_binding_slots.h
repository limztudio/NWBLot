#ifndef NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared SOFTWARE (SW BVH) trace bindings -- slots 0-10 the GI trace body (gi_sw_trace.slangi) declares. It reuses the
// SAME software scene/instance + per-mesh BVH buffers the SW shadow/caustic trace builds (NWB_GI_SW_MAX_MESHES ==
// NWB_SW_SHADOW_MAX_MESHES), so prepareShadowVisibilityResources -> buildSceneSwBvh already populates them. Producers
// (the surfel trace) add their OWN output bindings at the tail (>= 11); nothing GI-grid-specific lives here anymore.

#define NWB_GI_SW_SET 0

// Scene BVH + instance + light list + per-mesh descriptor arrays (mirrors caustic/sw_binding_slots.h exactly).
#define NWB_GI_SW_BINDING_SCENE_SHADING 0
#define NWB_GI_SW_BINDING_LIGHT_LIST 1
#define NWB_GI_SW_BINDING_SCENE_NODES 2
#define NWB_GI_SW_BINDING_SCENE_INSTANCES 3
#define NWB_GI_SW_BINDING_INSTANCE_MATERIAL 4
#define NWB_GI_SW_BINDING_MESH_NODES 5
#define NWB_GI_SW_BINDING_MESH_POSITIONS 6
#define NWB_GI_SW_BINDING_MESH_INDICES 7
#define NWB_GI_SW_BINDING_MESH_ATTRIBUTES 8
#define NWB_GI_SW_BINDING_MATERIAL_TYPED 9
#define NWB_GI_SW_BINDING_MESH_INSTANCES 10

// Reuse the SAME per-mesh cap as the SW shadow/caustic (they share the buildSceneSwBvh output). The shadow SW
// binding slots header defines NWB_SW_SHADOW_MAX_MESHES.
#include "../shadow/sw_binding_slots.h"
#ifndef NWB_GI_SW_MAX_MESHES
#define NWB_GI_SW_MAX_MESHES NWB_SW_SHADOW_MAX_MESHES
#endif

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

