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
// Software (compute) shadow traversal, decomposed into one named kernel per pass. Each pass composes only the concern
// .slangi files it needs, defines its occluder class at compile time, and declares its own minimal binding subset + push
// struct. The renderer creates one pipeline per pass and dispatches the full-res opaque prepass, the soft opaque half-res
// trace, then the transparent coarse path with compacted/adaptive/uniform resolve variants.
inline constexpr Name s_SwOpaquePrepassShaderName("engine/graphics/shadow/sw_shadow_opaque_prepass_cs");
inline constexpr Name s_SwSoftOpaqueShaderName("engine/graphics/shadow/sw_shadow_soft_opaque_cs");
inline constexpr Name s_SwTransparentCoarseShaderName("engine/graphics/shadow/sw_shadow_transparent_coarse_cs");
inline constexpr Name s_SwTransparentResolveShaderName("engine/graphics/shadow/sw_shadow_transparent_resolve_cs");
inline constexpr Name s_SwTransparentClassifyShaderName("engine/graphics/shadow/sw_shadow_transparent_classify_cs");
inline constexpr Name s_SwTransparentBuildArgsShaderName("engine/graphics/shadow/sw_shadow_transparent_buildargs_cs");
inline constexpr Name s_SwTransparentIndirectShaderName("engine/graphics/shadow/sw_shadow_transparent_indirect_cs");
inline constexpr Name s_SwTransparentUniformShaderName("engine/graphics/shadow/sw_shadow_transparent_uniform_cs");
// Soft COLORED TRANSPARENT shadow: the half-res cone-jittered TRANSPARENT
// trace -- the colored (Beer-Lambert/Fresnel) analog of the soft opaque trace, kept a PARALLEL signal (independent noise,
// separately denoised) and fold-multiplied onto the soft opaque visibility only at the final full-res upsample.
inline constexpr Name s_SwTransparentSoftShaderName("engine/graphics/shadow/sw_shadow_transparent_soft_cs");
// Soft opaque shadow (soft-ray-traced-shadow feature, all light types): the half-res geometry downsample pre-pass
// (octahedral normal + camera distance + validity, for the resolve's edge-stop) + the a-trous wavelet resolve +
// bilateral upsample that denoises the jittered half-res opaque visibility into the full-res visibility.
inline constexpr Name s_GeometryDownsampleShaderName("engine/graphics/shadow/shadow_geometry_downsample_cs");
inline constexpr Name s_SoftResolveShaderName("engine/graphics/shadow/shadow_resolve_cs");
// RGB variant of the soft-shadow a-trous resolve: the SAME shadow_resolve source compiled with
// NWB_SHADOW_RESOLVE_CHANNELS=3 (via the shadow_resolve_rgb_cs wrapper .slang), a SECOND cooked pipeline that denoises the
// COLORED soft transparent transmittance while the scalar pipeline above keeps the grayscale opaque path at 1x ALU/LDS.
inline constexpr Name s_SoftResolveRgbShaderName("engine/graphics/shadow/shadow_resolve_rgb_cs");
// Soft opaque shadow TEMPORAL reproject-merge: inserted per slot between
// the half-res soft trace and the a-trous resolve, it reprojects the current world position through a stashed previous-
// frame worldToClip and accumulates the noisy per-frame trace into a variance-clamped/antilag temporal history (so the
// per-frame SPP can drop and static receivers converge smooth, while a moving occluder leaves no ghost trail).
inline constexpr Name s_SoftReprojectMergeShaderName("engine/graphics/shadow/shadow_reproject_merge_cs");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

