// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SCENE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SCENE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene bindings 4 and 6 remain ABI positions for archived shader variants; active renderer consumers select scene
// resources through the global descriptor heap.
#define NWB_SCENE_SHADING_DEFAULT_SET 0
#define NWB_SCENE_SHADING_DEFAULT_BINDING 4

#define NWB_SCENE_LIGHT_LIST_DEFAULT_SET 0
#define NWB_SCENE_LIGHT_LIST_DEFAULT_BINDING 6

#define NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT 4u
#define NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT 20u
#define NWB_SCENE_MAX_LIGHTS 64u

// NwbSceneLight.params.y packs the LightType enum into a float; these midpoints decode directional/point/spot behavior.
#define NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX 0.5
#define NWB_SCENE_LIGHT_TYPE_POINT_MAX 1.5

// Cleared G-buffer depth at or above this threshold has no scene receiver.
#define NWB_SCENE_BACKGROUND_DEPTH 0.999999

// Colored shadows store per-light float3 transmittance in a Texture2DArray with one layer per shadow slot.
// A bounded per-frame pool goes to the most important lights; lights without a slot stay fully lit.
// Producers and the lighting consumer share this slot count (the array depth).
#define NWB_SCENE_SHADOW_SLOT_COUNT 8u

// Caustic-light slots: a bounded per-frame pool for the most important directional/spot lights illuminating
// a scene with at least one refractive instance. The slot index rides NwbSceneLight.params.w (negative = no slot).
// Point lights are excluded: omnidirectional emission would spread the photon budget too thin.
#define NWB_SCENE_CAUSTIC_SLOT_COUNT 4u

// Shadow-visibility binding 7 remains an ABI position; active consumers use a heap-selected resource alias.
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_SET 0
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_BINDING 7

// Caustic-irradiance binding 8 remains an ABI position. Unlike multiplicative shadow visibility, this additive
// scene-referred irradiance is a no-op when unwritten/black.
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_SET 0
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_BINDING 8

// Surfel GI: deferred lighting samples a single resolved screen-space irradiance texture written by the
// dedicated surfel resolve compute pass, so the lighting shader never touches the read-write surfel pool.
#define NWB_SCENE_GI_SURFEL_IRRADIANCE_DEFAULT_SET 0
#define NWB_SCENE_GI_SURFEL_IRRADIANCE_DEFAULT_BINDING 9


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

