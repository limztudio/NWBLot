// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SCENE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SCENE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Fallback slots for shaders that include scene/buffer.slangi without selecting their own. Each real
// consumer owns the complete slot map of its binding set in its own header (deferred/binding_slots.h,
// avboit/binding_slots.h, shadow/binding_slots.h, ...) and points NWB_SCENE_SHADING_* /
// NWB_SCENE_LIGHT_LIST_* at the matching slot there, so a set's layout is never split across headers.
#define NWB_SCENE_SHADING_DEFAULT_SET 0
#define NWB_SCENE_SHADING_DEFAULT_BINDING 4

#define NWB_SCENE_LIGHT_LIST_DEFAULT_SET 0
#define NWB_SCENE_LIGHT_LIST_DEFAULT_BINDING 6

#define NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT 4u
#define NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT 16u
#define NWB_SCENE_MAX_LIGHTS 64u

// Hard shadows pack one visibility bit per light into a single R32_UINT, so the first 32 lights can be
// individually shadowed and any beyond that stay fully lit. This is the width of the visibility mask
// word: the shadow producer (raygen / software fallback) and the lighting consumer MUST share it.
#define NWB_SCENE_SHADOW_VISIBILITY_LIGHT_COUNT 32u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

