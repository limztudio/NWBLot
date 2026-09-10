// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_HW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_HW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware surfel-GI trace slots, shared by Slang and the C++ pipeline-layout builder.
// Slot 0 is the bindless slot cbuffer; slot 1 is an ABI gap. Slot 11 selects the
// InstanceID/material-surface context. Surfel tail (12/13/20/21) matches surfel_binding_slots.h.


#define NWB_GI_HW_SET 0

// Bindings 0-1 keep the scene/light ABI positions; 1 stays unbound. Do not renumber.
#define NWB_GI_HW_BINDING_SCENE_SHADING 0
#define NWB_GI_HW_BINDING_LIGHT_LIST 1
#define NWB_GI_HW_BINDING_BINDLESS_RESOURCES NWB_GI_HW_BINDING_SCENE_SHADING
#define NWB_GI_HW_BINDING_TLAS 2               // RaytracingAccelerationStructure (the scene TLAS)
// Slots 3-10 are ABI gaps. Positions/indices/attributes come from the global heap
// via the material record; b11 selects the material-surface context. Do not reuse.
#define NWB_GI_HW_BINDING_MATERIAL_CONTEXT_SLOTS 11 // ConstantBuffer<NwbRayTraceMaterialContextSlots>

// Keep the dominant-light occlusion ray on the HW path so HW and SW shading match.
#ifndef NWB_GI_HIT_SHADOW_RAYS
#define NWB_GI_HIT_SHADOW_RAYS 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

