// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsShadow{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware OPAQUE shadow trace: inline RayQuery compute (binary "any opaque hit -> shadowed"). The colored TRANSPARENT
// shadow is cast by the software traversal below and multiplied onto this opaque mask (the hybrid split).
inline constexpr Name s_RayQueryShaderName("engine/graphics/shadow/shadow_rayquery_cs");
// Software (compute) shadow traversal, decomposed into one NAMED kernel per pass (the old numeric multiplyMode monolith
// is retired). Each pass composes only the concern .slangi files it needs, #defines its occluder class at compile time,
// and declares its own minimal binding subset + push struct. The renderer creates one pipeline per pass and dispatches
// the same sequence the monolith did (opaque prepass OR adaptive-opaque coarse+resolve; the soft opaque half-res trace
// -- all light types; then the transparent coarse + either the Stage-3 compacted classify/build-args/indirect chain, the Stage-2
// adaptive resolve, or the uniform half-res multiply -- env-gated exactly as before).
inline constexpr Name s_SwOpaquePrepassShaderName("engine/graphics/shadow/sw_shadow_opaque_prepass_cs");
inline constexpr Name s_SwOpaqueCoarseShaderName("engine/graphics/shadow/sw_shadow_opaque_coarse_cs");
inline constexpr Name s_SwOpaqueResolveShaderName("engine/graphics/shadow/sw_shadow_opaque_resolve_cs");
inline constexpr Name s_SwSoftOpaqueShaderName("engine/graphics/shadow/sw_shadow_soft_opaque_cs");
inline constexpr Name s_SwTransparentCoarseShaderName("engine/graphics/shadow/sw_shadow_transparent_coarse_cs");
inline constexpr Name s_SwTransparentResolveShaderName("engine/graphics/shadow/sw_shadow_transparent_resolve_cs");
inline constexpr Name s_SwTransparentClassifyShaderName("engine/graphics/shadow/sw_shadow_transparent_classify_cs");
inline constexpr Name s_SwTransparentBuildArgsShaderName("engine/graphics/shadow/sw_shadow_transparent_buildargs_cs");
inline constexpr Name s_SwTransparentIndirectShaderName("engine/graphics/shadow/sw_shadow_transparent_indirect_cs");
inline constexpr Name s_SwTransparentUniformShaderName("engine/graphics/shadow/sw_shadow_transparent_uniform_cs");
// Soft opaque shadow (soft-ray-traced-shadow feature, all light types): the half-res geometry downsample pre-pass
// (octahedral normal + camera distance + validity, for the resolve's edge-stop) + the a-trous wavelet resolve +
// bilateral upsample that denoises the jittered half-res opaque visibility into the full-res visibility.
inline constexpr Name s_GeometryDownsampleShaderName("engine/graphics/shadow/shadow_geometry_downsample_cs");
inline constexpr Name s_SoftResolveShaderName("engine/graphics/shadow/shadow_resolve_cs");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

