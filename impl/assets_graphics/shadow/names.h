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


// Opaque RayQuery trace; colored transparent shadows multiply onto this mask.
inline constexpr Name s_RayQueryShaderName("shadow_rayquery_cs");
// Half-res soft opaque HW trace feeding the shared denoise chain.
inline constexpr Name s_RayQuerySoftShaderName("shadow_rayquery_soft_cs");
// One kernel per pass with minimal bindings.
inline constexpr Name s_SwOpaquePrepassShaderName("sw_shadow_opaque_prepass_cs");
inline constexpr Name s_SwSoftOpaqueShaderName("sw_shadow_soft_opaque_cs");
inline constexpr Name s_SwTransparentCoarseShaderName("sw_shadow_transparent_coarse_cs");
inline constexpr Name s_SwTransparentResolveShaderName("sw_shadow_transparent_resolve_cs");
inline constexpr Name s_SwTransparentClassifyShaderName("sw_shadow_transparent_classify_cs");
inline constexpr Name s_SwTransparentBuildArgsShaderName("sw_shadow_transparent_buildargs_cs");
inline constexpr Name s_SwTransparentIndirectShaderName("sw_shadow_transparent_indirect_cs");
// Colored analog of the soft trace, folded at the final upsample.
inline constexpr Name s_SwTransparentSoftShaderName("sw_shadow_transparent_soft_cs");
inline constexpr Name s_HardwareTransparentGatherShaderName("hardware_transparent_gather_cs");
inline constexpr Name s_HardwareTransparentEvaluateShaderName("hardware_transparent_evaluate_cs");
inline constexpr Name s_HardwareTransparentOverflowShaderName("hardware_transparent_overflow_cs");
inline constexpr Name s_LightSpaceViewShaderName("light_space_view_cs");
inline constexpr Name s_LightSpaceCullShaderName("light_space_cull_cs");
inline constexpr Name s_LightSpaceCaptureVertexShaderName("light_space_capture_vs");
inline constexpr Name s_LightSpaceCapturePixelShaderName("light_space_capture_ps");
inline constexpr Name s_LightSpaceResolveShaderName("light_space_resolve_cs");
inline constexpr Name s_LightSpaceResolveHwShaderName("light_space_resolve_hw_cs");
inline constexpr Name s_LightSpaceFallbackShaderName("light_space_fallback_cs");
inline constexpr Name s_LightSpaceShadeShaderName("light_space_shade_cs");
// Denoises half-res visibility into full-res visibility.
inline constexpr Name s_GeometryDownsampleShaderName("shadow_geometry_downsample_cs");
inline constexpr Name s_SoftResolveShaderName("shadow_resolve_cs");
// RGB resolve variant (NWB_SHADOW_RESOLVE_CHANNELS=3).
inline constexpr Name s_SoftResolveRgbShaderName("shadow_resolve_rgb_cs");
inline constexpr Name s_SoftResolveCombinedShaderName("shadow_resolve_combined_cs");
inline constexpr Name s_SoftResolveCombinedWaveletShaderName("shadow_resolve_wavelet_combined_cs");
// Temporal merge between the half-res trace and the resolve.
inline constexpr Name s_SoftReprojectMergeShaderName("shadow_reproject_merge_cs");
inline constexpr Name s_SoftReprojectMergeCombinedShaderName("shadow_reproject_merge_combined_cs");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

