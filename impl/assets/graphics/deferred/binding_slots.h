// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
// Surfel GI bindings the deferred lighting pass consumes: the surfel pool (StructuredBuffer<NwbSurfel>), the
// spatial-hash cell-head buffer (StructuredBuffer<uint>), and the surfel params CB. nwbSurfelGather walks the
// hash at the shaded point; when no surfel covers it (or GI is off) nwbBxdfIndirectIrradiance falls back to
// hemiAmbient, so lighting is correct WITHOUT these bound (the pool is zero-init, cell heads are 0xFFFFFFFF).
#define NWB_DEFERRED_LIGHTING_BINDING_GI_SURFEL_POOL 9
#define NWB_DEFERRED_LIGHTING_BINDING_GI_SURFEL_HASH 10
#define NWB_DEFERRED_LIGHTING_BINDING_GI_SURFEL_PARAMS 11

#define NWB_DEFERRED_COMPOSITE_BINDING_OPAQUE_COLOR 0
#define NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_COLOR 1
#define NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_EXTINCTION 2
#define NWB_DEFERRED_COMPOSITE_BINDING_SAMPLER 3

#define NWB_DEFERRED_FULLSCREEN_UV_LOCATION 0
#define NWB_DEFERRED_COLOR_TARGET_LOCATION 0


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

