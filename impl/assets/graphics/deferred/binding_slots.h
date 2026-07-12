
#ifndef NWB_DEFERRED_BINDING_SLOTS_H
#define NWB_DEFERRED_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFERRED_LIGHTING_SET 0
#define NWB_DEFERRED_COMPOSITE_SET 0

#define NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_BASE_COLOR 0
#define NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_NORMAL 1
#define NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_WORLD_POSITION 2
#define NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_DEPTH 3
#define NWB_DEFERRED_LIGHTING_BINDING_SAMPLER 4
#define NWB_DEFERRED_LIGHTING_BINDING_SCENE_SHADING 5
#define NWB_DEFERRED_LIGHTING_BINDING_LIGHT_LIST 6
#define NWB_DEFERRED_LIGHTING_BINDING_SHADOW_VISIBILITY 7
#define NWB_DEFERRED_LIGHTING_BINDING_CAUSTIC_IRRADIANCE 8
// Surfel GI: the deferred lighting pass samples a single RESOLVED screen-space irradiance texture (RGBA16F). The
// surfel_resolve_cs COMPUTE pass gathered the surfel field once per pixel into it (a = coverage flag). The lighting
// reads THIS texture, never the read-write surfel pool -- keeping the pool off the pixel shader (compute-only) is what
// eliminates the frames-in-flight pool race. Cleared to 0 (a = 0 -> hemiAmbient) so an unbound/disabled GI is a no-op.
#define NWB_DEFERRED_LIGHTING_BINDING_GI_SURFEL_IRRADIANCE 9

#define NWB_DEFERRED_COMPOSITE_BINDING_OPAQUE_COLOR 0
#define NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_COLOR 1
#define NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_EXTINCTION 2
#define NWB_DEFERRED_COMPOSITE_BINDING_SAMPLER 3

#define NWB_DEFERRED_FULLSCREEN_UV_LOCATION 0
#define NWB_DEFERRED_COLOR_TARGET_LOCATION 0


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

