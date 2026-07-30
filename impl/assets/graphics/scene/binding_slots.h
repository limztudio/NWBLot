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

// Colored shadows store per-light float3 transmittance in a Texture2DArray with one layer per shadow slot.
// A bounded pool of slots is assigned per frame to the most important lights (the shadow-slot allocator);
// lights without a slot stay fully lit. The shadow producers (hardware RayQuery / software fallback) and the lighting
// consumer MUST share this slot count (the array depth).
#define NWB_SCENE_SHADOW_SLOT_COUNT 8u

// Caustic-light slots: a bounded pool assigned per frame to the most important directional/spot lights that illuminate
// a scene containing at least one refractive instance (the caustic-slot allocator). The chosen slot index rides
// NwbSceneLight.params.w (negative = no slot). The caustic producer and any caustic consumer MUST share this count.
// Point lights are excluded because omnidirectional emission would spread the photon budget too thin.
#define NWB_SCENE_CAUSTIC_SLOT_COUNT 4u

// Shadow-visibility binding 7 remains an ABI position; active consumers use a heap-selected resource alias.
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_SET 0
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_BINDING 7

// Caustic-irradiance binding 8 remains an ABI position. Unlike multiplicative shadow visibility, this is additive
// scene-referred irradiance the caustic producer focuses onto receivers; an unwritten / black buffer is a no-op.
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_SET 0
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_BINDING 8

// Surfel GI: the deferred lighting pass samples a single RESOLVED screen-space irradiance texture. The per-pixel
// surfel gather runs in a dedicated COMPUTE pass (surfel_resolve_cs) that writes this texture, so the lighting
// compute shader never touches the read-write surfel pool (that decoupling eliminates the frames-in-flight pool race). The
// pool / hash / params are bound only in the resolve set (surfel_binding_slots.h), not the lighting set.
#define NWB_SCENE_GI_SURFEL_IRRADIANCE_DEFAULT_SET 0
#define NWB_SCENE_GI_SURFEL_IRRADIANCE_DEFAULT_BINDING 9


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

