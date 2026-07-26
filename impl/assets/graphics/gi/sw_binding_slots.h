// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared SOFTWARE (SW BVH) trace bindings declared by gi_sw_trace.slangi. Slot 0 is the target-generation
// DeferredBindlessResourceSlots cbuffer; its avboitSlots.z/.w select the shared scene-shading + light-list heap
// entries. Slot 11 is the trace-owned material-context slot cbuffer, which selects the scene BVH / instance and
// shadow material buffers from the global storage-buffer heap. The surfel-specific tail starts at 12.

#define NWB_GI_SW_SET 0

// Keep the historical scene/light binding numbers stable as logical ABI positions. Binding 0 now carries the resource
// slot cbuffer instead of the scene-shading CB; binding 1 remains an intentional gap because the light list is heap
// fetched too.
#define NWB_GI_SW_BINDING_SCENE_SHADING 0
#define NWB_GI_SW_BINDING_LIGHT_LIST 1
#define NWB_GI_SW_BINDING_BINDLESS_RESOURCES NWB_GI_SW_BINDING_SCENE_SHADING
// Slots 2-10 are intentional ABI gaps. SW GI reads per-mesh nodes, positions, indices, and attributes from the global
// descriptor heap through slots carried by the material record; b11 supplies the global-heap scene/material slots.
// Do not repurpose or renumber these holes.
#define NWB_GI_SW_BINDING_MATERIAL_CONTEXT_SLOTS 11 // ConstantBuffer<NwbRayTraceMaterialContextSlots>

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

