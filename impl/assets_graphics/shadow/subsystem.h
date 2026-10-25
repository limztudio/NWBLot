// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VertexShader;
class PixelShader;
class ComputeShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowSubsystem final : public TypedSubsystem<ShadowSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("shadow_subsystem")

    static constexpr usize s_FieldCount = 28u;


public:
    using TypedSubsystem<ShadowSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> hardwareTransparentEvaluateCs;
    Core::Assets::AssetRef<ComputeShader> hardwareTransparentGatherCs;
    Core::Assets::AssetRef<ComputeShader> hardwareTransparentOverflowCs;
    Core::Assets::AssetRef<PixelShader> lightSpaceCapturePs;
    Core::Assets::AssetRef<VertexShader> lightSpaceCaptureVs;
    Core::Assets::AssetRef<ComputeShader> lightSpaceCullCs;
    Core::Assets::AssetRef<ComputeShader> lightSpaceFallbackCs;
    Core::Assets::AssetRef<ComputeShader> lightSpaceResolveCs;
    Core::Assets::AssetRef<ComputeShader> lightSpaceResolveHwCs;
    Core::Assets::AssetRef<ComputeShader> lightSpaceShadeCs;
    Core::Assets::AssetRef<ComputeShader> lightSpaceViewCs;
    Core::Assets::AssetRef<ComputeShader> shadowGeometryDownsampleCs;
    Core::Assets::AssetRef<ComputeShader> shadowRayqueryCs;
    Core::Assets::AssetRef<ComputeShader> shadowRayquerySoftCs;
    Core::Assets::AssetRef<ComputeShader> shadowReprojectMergeCombinedCs;
    Core::Assets::AssetRef<ComputeShader> shadowReprojectMergeCs;
    Core::Assets::AssetRef<ComputeShader> shadowResolveCombinedCs;
    Core::Assets::AssetRef<ComputeShader> shadowResolveCs;
    Core::Assets::AssetRef<ComputeShader> shadowResolveRgbCs;
    Core::Assets::AssetRef<ComputeShader> shadowResolveWaveletCombinedCs;
    Core::Assets::AssetRef<ComputeShader> swShadowOpaquePrepassCs;
    Core::Assets::AssetRef<ComputeShader> swShadowSoftOpaqueCs;
    Core::Assets::AssetRef<ComputeShader> swShadowTransparentBuildargsCs;
    Core::Assets::AssetRef<ComputeShader> swShadowTransparentClassifyCs;
    Core::Assets::AssetRef<ComputeShader> swShadowTransparentCoarseCs;
    Core::Assets::AssetRef<ComputeShader> swShadowTransparentIndirectCs;
    Core::Assets::AssetRef<ComputeShader> swShadowTransparentResolveCs;
    Core::Assets::AssetRef<ComputeShader> swShadowTransparentSoftCs;

    static constexpr SubsystemFieldDescriptor<ShadowSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::hardwareTransparentEvaluateCs>("hardware_transparent_evaluate_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::hardwareTransparentGatherCs>("hardware_transparent_gather_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::hardwareTransparentOverflowCs>("hardware_transparent_overflow_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, PixelShader, &ShadowSubsystem::lightSpaceCapturePs>("light_space_capture_ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, VertexShader, &ShadowSubsystem::lightSpaceCaptureVs>("light_space_capture_vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::lightSpaceCullCs>("light_space_cull_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::lightSpaceFallbackCs>("light_space_fallback_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::lightSpaceResolveCs>("light_space_resolve_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::lightSpaceResolveHwCs>("light_space_resolve_hw_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::lightSpaceShadeCs>("light_space_shade_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::lightSpaceViewCs>("light_space_view_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowGeometryDownsampleCs>("shadow_geometry_downsample_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowRayqueryCs>("shadow_rayquery_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowRayquerySoftCs>("shadow_rayquery_soft_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowReprojectMergeCombinedCs>("shadow_reproject_merge_combined_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowReprojectMergeCs>("shadow_reproject_merge_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowResolveCombinedCs>("shadow_resolve_combined_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowResolveCs>("shadow_resolve_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowResolveRgbCs>("shadow_resolve_rgb_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::shadowResolveWaveletCombinedCs>("shadow_resolve_wavelet_combined_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowOpaquePrepassCs>("sw_shadow_opaque_prepass_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowSoftOpaqueCs>("sw_shadow_soft_opaque_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowTransparentBuildargsCs>("sw_shadow_transparent_buildargs_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowTransparentClassifyCs>("sw_shadow_transparent_classify_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowTransparentCoarseCs>("sw_shadow_transparent_coarse_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowTransparentIndirectCs>("sw_shadow_transparent_indirect_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowTransparentResolveCs>("sw_shadow_transparent_resolve_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ShadowSubsystem, ComputeShader, &ShadowSubsystem::swShadowTransparentSoftCs>("sw_shadow_transparent_soft_cs", "compute_shader"),
    };
};

using ShadowSubsystemAssetCodec = TypedSubsystemAssetCodec<ShadowSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
