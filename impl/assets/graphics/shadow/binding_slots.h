// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADOW_RT_SET 0

#define NWB_SHADOW_RT_BINDING_TLAS 0
// Legacy local G-buffer binding numbers retained for source compatibility. The hardware trace now selects the
// target-generation Texture2D descriptors through heap-slot push constants, leaving local bindings 1..3 as gaps.
#define NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION 1
#define NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL 2
#define NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH 3
// Preserve the historical scene/light ABI positions: binding 4 now carries the target-generation resource-slot
// cbuffer, whose avboitSlots.z/.w select the shared scene-shading + light-list heap entries; binding 5 stays an
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

