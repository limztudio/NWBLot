
#pragma once


#include <impl/global.h>

#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsGi{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel GI shader virtual paths. Screen-spawn -> world-hash-build -> SW trace; the trace reuses gi_sw_trace.slangi
// (the extracted closest-hit + Lambert shade). The gather is folded into the deferred-lighting pass (surfel_gather).
inline constexpr Name s_SurfelSpawnShaderName("engine/graphics/gi/surfel/surfel_spawn_cs");
inline constexpr Name s_SurfelAgeFreeShaderName("engine/graphics/gi/surfel/surfel_age_free_cs");
inline constexpr Name s_SurfelHashBuildShaderName("engine/graphics/gi/surfel/surfel_hash_build_cs");
inline constexpr Name s_SurfelTraceShaderName("engine/graphics/gi/surfel/surfel_trace_cs");
inline constexpr Name s_SurfelTraceHwShaderName("engine/graphics/gi/surfel/surfel_trace_hw_cs");
inline constexpr Name s_SurfelResolveShaderName("engine/graphics/gi/surfel/surfel_resolve_cs");
inline constexpr Name s_SurfelUpsampleShaderName("engine/graphics/gi/surfel/surfel_upsample_cs");
inline constexpr Name s_SurfelTraceBuildArgsShaderName("engine/graphics/gi/surfel/surfel_trace_buildargs_cs");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

