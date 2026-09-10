// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADOW_RT_SET 0

#define NWB_SHADOW_RT_BINDING_TLAS 0
// Local G-buffer bindings are gaps; the trace selects descriptors via heap slots.
#define NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION 1
#define NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL 2
#define NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH 3
// Bindings 4/5 preserve ABI positions; do not renumber.
#define NWB_SHADOW_RT_BINDING_SCENE_SHADING 4
#define NWB_SHADOW_RT_BINDING_LIGHT_LIST 5
#define NWB_SHADOW_RT_BINDING_BINDLESS_RESOURCES NWB_SHADOW_RT_BINDING_SCENE_SHADING
#define NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT 6
// Slots 7-12 are gaps; do not renumber.

// One thread per full-res pixel.
#define NWB_SHADOW_RT_GROUP_SIZE 8


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

