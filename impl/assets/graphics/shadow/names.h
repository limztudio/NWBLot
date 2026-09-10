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


// Opaque RayQuery trace; software transparent shadows multiply onto this mask.
inline constexpr Name s_RayQueryShaderName("engine/graphics/shadow/shadow_rayquery_cs");
// Half-res soft opaque HW trace feeding the shared denoise chain.
inline constexpr Name s_RayQuerySoftShaderName("engine/graphics/shadow/shadow_rayquery_soft_cs");
// One kernel per pass with minimal bindings.
inline constexpr Name s_SwOpaquePrepassShaderName("engine/graphics/shadow/sw_shadow_opaque_prepass_cs");
inline constexpr Name s_SwSoftOpaqueShaderName("engine/graphics/shadow/sw_shadow_soft_opaque_cs");
inline constexpr Name s_SwTransparentCoarseShaderName("engine/graphics/shadow/sw_shadow_transparent_coarse_cs");
inline constexpr Name s_SwTransparentResolveShaderName("engine/graphics/shadow/sw_shadow_transparent_resolve_cs");
inline constexpr Name s_SwTransparentClassifyShaderName("engine/graphics/shadow/sw_shadow_transparent_classify_cs");
inline constexpr Name s_SwTransparentBuildArgsShaderName("engine/graphics/shadow/sw_shadow_transparent_buildargs_cs");
inline constexpr Name s_SwTransparentIndirectShaderName("engine/graphics/shadow/sw_shadow_transparent_indirect_cs");
inline constexpr Name s_SwTransparentUniformShaderName("engine/graphics/shadow/sw_shadow_transparent_uniform_cs");
// Colored analog of the soft trace, folded at the final upsample.
inline constexpr Name s_SwTransparentSoftShaderName("engine/graphics/shadow/sw_shadow_transparent_soft_cs");
// Denoises half-res visibility into full-res visibility.
inline constexpr Name s_GeometryDownsampleShaderName("engine/graphics/shadow/shadow_geometry_downsample_cs");
inline constexpr Name s_SoftResolveShaderName("engine/graphics/shadow/shadow_resolve_cs");
// RGB resolve variant (NWB_SHADOW_RESOLVE_CHANNELS=3).
inline constexpr Name s_SoftResolveRgbShaderName("engine/graphics/shadow/shadow_resolve_rgb_cs");
// Temporal merge between the half-res trace and the resolve.
inline constexpr Name s_SoftReprojectMergeShaderName("engine/graphics/shadow/shadow_reproject_merge_cs");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

