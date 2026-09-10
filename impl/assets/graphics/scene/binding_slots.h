// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SCENE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SCENE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Bindings 4 and 6 stay for archived variants; active consumers use the global heap.
#define NWB_SCENE_SHADING_DEFAULT_SET 0
#define NWB_SCENE_SHADING_DEFAULT_BINDING 4

#define NWB_SCENE_LIGHT_LIST_DEFAULT_SET 0
#define NWB_SCENE_LIGHT_LIST_DEFAULT_BINDING 6

#define NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT 4u
#define NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT 20u
#define NWB_SCENE_MAX_LIGHTS 64u

// params.y packs LightType; these midpoints decode directional/point/spot.
#define NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX 0.5
#define NWB_SCENE_LIGHT_TYPE_POINT_MAX 1.5

// Cleared depth at or above this has no scene receiver.
#define NWB_SCENE_BACKGROUND_DEPTH 0.999999

// Per-light transmittance layers; lights without a slot stay fully lit.
#define NWB_SCENE_SHADOW_SLOT_COUNT 8u

// Caustic slots ride params.w (negative = none); point lights are excluded.
#define NWB_SCENE_CAUSTIC_SLOT_COUNT 4u

// Binding 7 stays as an ABI position; consumers use a heap alias.
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_SET 0
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_BINDING 7

// Binding 8 stays as an ABI position; unwritten irradiance is a no-op.
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_SET 0
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_BINDING 8

// Lighting samples the resolved surfel texture, never the read-write pool.
#define NWB_SCENE_GI_SURFEL_IRRADIANCE_DEFAULT_SET 0
#define NWB_SCENE_GI_SURFEL_IRRADIANCE_DEFAULT_BINDING 9


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

