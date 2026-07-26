// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_HW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_HW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Binding slots for the HARDWARE surfel-GI trace (surfel_trace_hw_cs / gi_hw_trace.slangi). Dual-consumed by the Slang
// shader AND the C++ pipeline-layout builder so both agree. Slot 0 carries the target-generation
// DeferredBindlessResourceSlots cbuffer; its avboitSlots.z/.w select the shared scene-shading + light-list heap entries,
// leaving historical slot 1 as a gap. Slot 11 is the trace-owned material-context slot cbuffer, selecting the shared
// InstanceID material and surface-evaluator buffers from the global heap. The surfel tail (constants 12 / pool 13 /
// snapshot 20/21 -- surfel_binding_slots.h) is shared verbatim.


#define NWB_GI_HW_SET 0

// Historical local scene/light positions: binding 0 is now the bindless slot cbuffer, while 1 remains intentionally
// unbound. Do not renumber the subsequent ABI slots.
#define NWB_GI_HW_BINDING_SCENE_SHADING 0
#define NWB_GI_HW_BINDING_LIGHT_LIST 1
#define NWB_GI_HW_BINDING_BINDLESS_RESOURCES NWB_GI_HW_BINDING_SCENE_SHADING
#define NWB_GI_HW_BINDING_TLAS 2               // RaytracingAccelerationStructure (the scene TLAS)
// Slots 3-10 are intentional ABI gaps. HW GI reads positions, indices, and attributes from the global descriptor heap
// through the material record's {position,index,attribute}Slot; b11 selects its InstanceID/material-surface context.
// Do not repurpose or renumber these holes.
#define NWB_GI_HW_BINDING_MATERIAL_CONTEXT_SLOTS 11 // ConstantBuffer<NwbRayTraceMaterialContextSlots>

// The shared shade (gi_trace_common.slangi) casts a dominant-light occlusion ray; keep it on the HW path too so the HW
// and SW shades are identical (the occlusion ray re-enters the HW RayQuery via the seam).
#ifndef NWB_GI_HIT_SHADOW_RAYS
#define NWB_GI_HIT_SHADOW_RAYS 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

