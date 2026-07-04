// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI probe trace -- SOFTWARE (SW BVH) bindings. The trace reuses the SAME software scene/instance + per-mesh BVH
// buffers the SW shadow/caustic trace builds (NWB_GI_SW_MAX_MESHES == NWB_SW_SHADOW_MAX_MESHES), so the
// prepareShadowVisibilityResources -> buildSceneSwBvh pipeline already populates them. Adds the GI-specific I/O:
// the grid CB, the ray-data output UAV, the prev-front atlas SRVs (bounce), and the hit-albedo buffer. See
// .helper/ddgi_plan.md §2 (Trace / Ray data).

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

// GI-specific bindings.
#define NWB_GI_SW_BINDING_GRID_CONSTANTS 11  // ConstantBuffer<NwbGiGridConstants>
#define NWB_GI_SW_BINDING_RAY_DATA 12        // RWTexture2D<float4> (RGBA16F: rgb=irradiance, a=hitT)
#define NWB_GI_SW_BINDING_PREV_IRRADIANCE 13 // Texture2D<float4> (prev-front irradiance atlas SRV)
#define NWB_GI_SW_BINDING_PREV_DISTANCE 14   // Texture2D<float2> (prev-front distance atlas SRV)
#define NWB_GI_SW_BINDING_HIT_ALBEDO 15      // StructuredBuffer<float3> (per-instance flat albedo)

#define NWB_GI_SW_BINDING_PUSH_CONSTANTS 16  // NwbGiTracePushConstants

// Reuse the SAME per-mesh cap as the SW shadow/caustic (they share the buildSceneSwBvh output). The shadow SW
// binding slots header defines NWB_SW_SHADOW_MAX_MESHES.
#include "../shadow/sw_binding_slots.h"
#ifndef NWB_GI_SW_MAX_MESHES
#define NWB_GI_SW_MAX_MESHES NWB_SW_SHADOW_MAX_MESHES
#endif

// Hit shadow rays (D4 = 1). Included here so the .slang does not need a separate define; U3 measures both 1 and 0.
#ifndef NWB_GI_HIT_SHADOW_RAYS
#define NWB_GI_HIT_SHADOW_RAYS 1
#endif

// BVH traversal stack sizes (same as caustic: 32 for mesh, 64 for scene).
#define NWB_GI_SW_MESH_STACK_SIZE 32
#define NWB_GI_SW_SCENE_STACK_SIZE 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

