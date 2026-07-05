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
#define NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT 20u
#define NWB_SCENE_MAX_LIGHTS 64u

// Colored shadows store per-light float3 transmittance in a Texture2DArray with one layer per shadow slot.
// A bounded pool of slots is assigned per frame to the most important lights (the shadow-slot allocator);
// lights without a slot stay fully lit. The shadow producers (raygen / software fallback) and the lighting
// consumer MUST share this slot count (the array depth).
#define NWB_SCENE_SHADOW_SLOT_COUNT 8u

// Caustic-light slots: a bounded pool assigned per frame to the most important directional/spot lights that illuminate
// a scene containing at least one refractive instance (the caustic-slot allocator). The chosen slot index rides
// NwbSceneLight.params.w (negative = no slot). The caustic producer and any caustic consumer MUST share this count.
// Point lights are excluded because omnidirectional emission would spread the photon budget too thin.
#define NWB_SCENE_CAUSTIC_SLOT_COUNT 4u

// Fallback set/binding for the shadow-visibility SRV when a consumer includes scene/lighting.slangi without
// selecting its own (the real consumer -- deferred lighting -- points these at its own slot map).
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_SET 0
#define NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_BINDING 7

// Fallback set/binding for the additive caustic-irradiance SRV (RGBA16F) when a consumer includes
// scene/lighting.slangi without selecting its own (the real consumer -- deferred lighting -- points these at its
// own slot map). Unlike the multiplicative shadow visibility, this is additive scene-referred irradiance the
// caustic producer focuses onto receivers; an unwritten / black buffer is the additive identity (no-op).
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_SET 0
#define NWB_SCENE_CAUSTIC_IRRADIANCE_DEFAULT_BINDING 8

// Surfel GI bindings the deferred lighting pass consumes. The surfel pool (StructuredBuffer<NwbSurfel>) holds the
// screen-spawned, world-hashed surfels' integrated irradiance; the cell-head buffer (StructuredBuffer<uint>) is the
// spatial-hash linked-list heads; the params CB carries the runtime hash/coverage constants. All live on
// RendererRayTracingState. nwbSurfelGather walks the 3x3x3 neighbour cells at the shaded point; when no surfel covers
// it (or GI is disabled) nwbBxdfIndirectIrradiance falls back to the hemiAmbient ambient term.
#define NWB_SCENE_GI_SURFEL_POOL_DEFAULT_SET 0
#define NWB_SCENE_GI_SURFEL_POOL_DEFAULT_BINDING 9
#define NWB_SCENE_GI_SURFEL_HASH_DEFAULT_SET 0
#define NWB_SCENE_GI_SURFEL_HASH_DEFAULT_BINDING 10
#define NWB_SCENE_GI_SURFEL_PARAMS_DEFAULT_SET 0
#define NWB_SCENE_GI_SURFEL_PARAMS_DEFAULT_BINDING 11


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

