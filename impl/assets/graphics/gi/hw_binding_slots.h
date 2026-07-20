#ifndef NWB_GRAPHICS_GI_HW_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_HW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Binding slots for the HARDWARE surfel-GI trace (surfel_trace_hw_cs / gi_hw_trace.slangi). Dual-consumed by the Slang
// shader AND the C++ pipeline/binding-set builder so both agree. Slots 0/1 (scene shading + light list) match the SW
// trace layout, and the surfel tail (constants 12 / pool 13 / snapshot 20/21 -- surfel_binding_slots.h) is shared
// verbatim; the HW path swaps the SW-BVH node bindings (SW slots 2-10) for the TLAS + the InstanceID-indexed material
// record + the per-mesh position/index/attribute buffers and typed material context it uses to evaluate the authored
// surface at the hit.


#define NWB_GI_HW_SET 0

#define NWB_GI_HW_BINDING_SCENE_SHADING 0      // ConstantBuffer (scene shading) -- same slot as the SW trace
#define NWB_GI_HW_BINDING_LIGHT_LIST 1         // StructuredBuffer<NwbSceneLight> (SRV)
#define NWB_GI_HW_BINDING_TLAS 2               // RaytracingAccelerationStructure (the scene TLAS)
#define NWB_GI_HW_BINDING_INSTANCE_MATERIAL 3  // StructuredBuffer<NwbRtInstanceMaterial> (SRV) -- InstanceID-indexed
#define NWB_GI_HW_BINDING_MESH_POSITIONS 4     // ByteAddressBuffer[NWB_GI_HW_MAX_MESHES] -- geometric face normal
#define NWB_GI_HW_BINDING_MESH_INDICES 5       // ByteAddressBuffer[NWB_GI_HW_MAX_MESHES]
#define NWB_GI_HW_BINDING_MESH_ATTRIBUTES 6    // ByteAddressBuffer[NWB_GI_HW_MAX_MESHES] -- shading normal + uv0
#define NWB_GI_HW_BINDING_MATERIAL_TYPED 7     // StructuredBuffer<uint> -- authored typed material constants
#define NWB_GI_HW_BINDING_MESH_INSTANCES 8     // StructuredBuffer<NwbMeshInstanceData> -- mutable material offsets

#include "../shadow/binding_slots.h"           // NWB_SHADOW_RT_MAX_MESHES (the HW-resident per-mesh array size)
#ifndef NWB_GI_HW_MAX_MESHES
#define NWB_GI_HW_MAX_MESHES NWB_SHADOW_RT_MAX_MESHES
#endif

// The shared shade (gi_trace_common.slangi) casts a dominant-light occlusion ray; keep it on the HW path too so the HW
// and SW shades are identical (the occlusion ray re-enters the HW RayQuery via the seam).
#ifndef NWB_GI_HIT_SHADOW_RAYS
#define NWB_GI_HIT_SHADOW_RAYS 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

