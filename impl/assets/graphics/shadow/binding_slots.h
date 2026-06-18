// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADOW_RT_SET 0

#define NWB_SHADOW_RT_BINDING_TLAS 0
#define NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION 1
#define NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL 2
#define NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH 3
#define NWB_SHADOW_RT_BINDING_SCENE_SHADING 4
#define NWB_SHADOW_RT_BINDING_LIGHT_LIST 5
#define NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT 6

// Software distance-field backend (compute). Slots 1-6 mirror the RT set (same G-buffer / scene-shading /
// light-list / visibility resources) so the C++ binding-set builder is shared; slot 0 is the SDF occluder
// instance buffer (instead of the TLAS) and slot 7 carries the instance count.
#define NWB_SHADOW_SDF_SET 0

#define NWB_SHADOW_SDF_BINDING_INSTANCES 0
#define NWB_SHADOW_SDF_BINDING_GBUFFER_WORLD_POSITION 1
#define NWB_SHADOW_SDF_BINDING_GBUFFER_NORMAL 2
#define NWB_SHADOW_SDF_BINDING_GBUFFER_DEPTH 3
#define NWB_SHADOW_SDF_BINDING_SCENE_SHADING 4
#define NWB_SHADOW_SDF_BINDING_LIGHT_LIST 5
#define NWB_SHADOW_SDF_BINDING_VISIBILITY_OUTPUT 6
#define NWB_SHADOW_SDF_BINDING_PARAMS 7


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

