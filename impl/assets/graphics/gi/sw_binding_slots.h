// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// SW trace bindings; slot 0 selects scene/light entries, slot 11 selects scene/material slots.

#define NWB_GI_SW_SET 0

// Bindings 0/1 preserve ABI positions; binding 1 stays a gap.
#define NWB_GI_SW_BINDING_SCENE_SHADING 0
#define NWB_GI_SW_BINDING_LIGHT_LIST 1
#define NWB_GI_SW_BINDING_BINDLESS_RESOURCES NWB_GI_SW_BINDING_SCENE_SHADING
// Slots 2-10 are gaps; do not renumber.
#define NWB_GI_SW_BINDING_MATERIAL_CONTEXT_SLOTS 11 // ConstantBuffer<NwbRayTraceMaterialContextSlots>

// Reuses the software per-mesh buffers filled by the SW shadow/caustic build.
#include "../shadow/sw_binding_slots.h"

// Hit shadow rays per bounce hit (1 = on).
#ifndef NWB_GI_HIT_SHADOW_RAYS
#define NWB_GI_HIT_SHADOW_RAYS 1
#endif

// Traversal stack sizes.
#define NWB_GI_SW_MESH_STACK_SIZE 32
#define NWB_GI_SW_SCENE_STACK_SIZE 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

