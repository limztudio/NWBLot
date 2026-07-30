// limztudio@gmail.com


#ifndef NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H


#define NWB_SHADOW_RT_SET 0

#define NWB_SHADOW_RT_BINDING_TLAS 0
// Local G-buffer binding numbers are retained as ABI gaps. The hardware trace selects the target-generation Texture2D
// descriptors through heap-slot push constants.
#define NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION 1
#define NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL 2
#define NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH 3
// Bindings 4 and 5 preserve the scene/light ABI positions: binding 4 carries the target-generation resource-slot
// cbuffer, whose avboitSlots.z/.w select the shared scene-shading + light-list heap entries; binding 5 remains an
// intentional gap because the light list is fetched from that heap too. Do not renumber the subsequent slots.
#define NWB_SHADOW_RT_BINDING_SCENE_SHADING 4
#define NWB_SHADOW_RT_BINDING_LIGHT_LIST 5
#define NWB_SHADOW_RT_BINDING_BINDLESS_RESOURCES NWB_SHADOW_RT_BINDING_SCENE_SHADING
#define NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT 6
// Slots 7-12 are intentional ABI gaps. The hardware opaque trace commits its first FORCE_OPAQUE hit without reading
// per-instance material or geometry; software shadow, GI, and caustics select their material context through the
// global descriptor heap instead. Do not repurpose or renumber these holes: existing archive variants and
// descriptor-layout tests rely on the stable slot map.

// Workgroup size of the hardware RayQuery opaque shadow trace (shadow_rayquery_cs): one thread per FULL-res pixel.
#define NWB_SHADOW_RT_GROUP_SIZE 8


#endif


