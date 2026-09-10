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


// Hardware OPAQUE trace: inline RayQuery compute (any opaque hit -> shadowed). The colored TRANSPARENT shadow
// comes from software traversal below and multiplies onto this mask (the hybrid split).
inline constexpr Name s_RayQueryShaderName("engine/graphics/shadow/shadow_rayquery_cs");
// Hardware SOFT OPAQUE half-res trace: the HW analog of s_SwSoftOpaqueShaderName. Each half-res pixel casts SPP
// cone-jittered rays into the shared soft buffer, so the common denoise chain softens HW shadows as on the SW path.
inline constexpr Name s_RayQuerySoftShaderName("engine/graphics/shadow/shadow_rayquery_soft_cs");
// Software shadow traversal: one named kernel per pass, each composing only its needed .slangi files with a
// compile-time occluder class and minimal bindings.
inline constexpr Name s_SwOpaquePrepassShaderName("engine/graphics/shadow/sw_shadow_opaque_prepass_cs");
inline constexpr Name s_SwSoftOpaqueShaderName("engine/graphics/shadow/sw_shadow_soft_opaque_cs");
inline constexpr Name s_SwTransparentCoarseShaderName("engine/graphics/shadow/sw_shadow_transparent_coarse_cs");
inline constexpr Name s_SwTransparentResolveShaderName("engine/graphics/shadow/sw_shadow_transparent_resolve_cs");
inline constexpr Name s_SwTransparentClassifyShaderName("engine/graphics/shadow/sw_shadow_transparent_classify_cs");
inline constexpr Name s_SwTransparentBuildArgsShaderName("engine/graphics/shadow/sw_shadow_transparent_buildargs_cs");
inline constexpr Name s_SwTransparentIndirectShaderName("engine/graphics/shadow/sw_shadow_transparent_indirect_cs");
inline constexpr Name s_SwTransparentUniformShaderName("engine/graphics/shadow/sw_shadow_transparent_uniform_cs");
// Soft COLORED TRANSPARENT shadow: the colored (Beer-Lambert/Fresnel) analog of the soft opaque trace, kept as a
// parallel signal and folded onto opaque visibility only at the final upsample.
inline constexpr Name s_SwTransparentSoftShaderName("engine/graphics/shadow/sw_shadow_transparent_soft_cs");
// Soft opaque shadow: half-res geometry downsample + a-trous wavelet resolve + bilateral upsample denoises the
// jittered half-res visibility into full-res visibility.
inline constexpr Name s_GeometryDownsampleShaderName("engine/graphics/shadow/shadow_geometry_downsample_cs");
inline constexpr Name s_SoftResolveShaderName("engine/graphics/shadow/shadow_resolve_cs");
// RGB variant of the soft-shadow resolve: same source compiled with NWB_SHADOW_RESOLVE_CHANNELS=3, denoising
// colored transparent transmittance while the scalar pipeline keeps the opaque path cheap.
inline constexpr Name s_SoftResolveRgbShaderName("engine/graphics/shadow/shadow_resolve_rgb_cs");
// Soft opaque TEMPORAL reproject-merge: sits between the half-res trace and the resolve, reprojecting through the
// stashed previous worldToClip into a variance-clamped history so static receivers converge while moving occluders
// leave no ghost trail.
inline constexpr Name s_SoftReprojectMergeShaderName("engine/graphics/shadow/shadow_reproject_merge_cs");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

