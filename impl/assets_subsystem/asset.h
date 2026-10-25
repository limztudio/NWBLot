// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/alloc/scratch.h>
#include <core/assets/module.h>
#include <core/assets/ref.h>

#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VertexShader;
class PixelShader;
class ComputeShader;
class MeshShader;
class RayGenerationShader;
class ClosestHitShader;
class MissShader;
class Sampler;
class Texture;
class UiSkin;
class Font;
class FontAtlas;
class CsgShape;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SubsystemFieldValueDescriptor{
    AStringView fieldName;
    AStringView expectedAssetTypeText;
};


interface ISubsystem : Core::Assets::IAsset{
public:
    NWB_DEFINE_ASSET_TYPE("subsystem")

    using SubsystemRef = Core::Assets::AssetRef<ISubsystem>;
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using SubsystemRefVector = Core::Assets::AssetVector<SubsystemRef>;
    using AssetRefVector = Core::Assets::AssetVector<AssetRef>;


public:
    virtual ~ISubsystem()noexcept override = 0;
protected:
    ISubsystem(Core::Assets::AssetArena& arena, const Name& assetType, const Name& virtualPath = s_NameNone)noexcept
        : Core::Assets::IAsset(assetType, virtualPath)
        , m_subsystems(arena)
    {}
    ISubsystem(const ISubsystem&) = default;
    ISubsystem(ISubsystem&&)noexcept = default;

protected:
    ISubsystem& operator=(const ISubsystem&) = default;

    ISubsystem& operator=(ISubsystem&&)noexcept = default;


public:
    [[nodiscard]] virtual bool loadBinary(Span<const u8> binary) = 0;
    [[nodiscard]] virtual bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const = 0;
    [[nodiscard]] virtual usize fieldCount()const noexcept = 0;
    [[nodiscard]] virtual SubsystemFieldValueDescriptor fieldDescriptor(usize index)const noexcept = 0;
    [[nodiscard]] virtual Name fieldValue(usize index)const noexcept = 0;
    virtual void setFieldValue(usize index, const Name& value)noexcept = 0;
    virtual void collectAssets(Vector<AssetRef, Core::Alloc::GlobalArena>& outAssets)const = 0;


public:
    [[nodiscard]] SubsystemRefVector& subsystems()noexcept{ return m_subsystems; }

    [[nodiscard]] const SubsystemRefVector& subsystems()const noexcept{ return m_subsystems; }


private:
    SubsystemRefVector m_subsystems;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
struct SubsystemFieldDescriptor{
    AStringView fieldName;
    AStringView expectedAssetTypeText;
    Name (*get)(const SubsystemT& subsystem)noexcept = nullptr;
    void (*set)(SubsystemT& subsystem, const Name& value)noexcept = nullptr;
};

template<typename SubsystemT, typename TAsset, Core::Assets::AssetRef<TAsset> SubsystemT::* Member>
[[nodiscard]] constexpr SubsystemFieldDescriptor<SubsystemT> MakeSubsystemFieldDescriptor(
    const AStringView fieldName,
    const AStringView expectedAssetTypeText
)noexcept{
    return SubsystemFieldDescriptor<SubsystemT>{
        fieldName,
        expectedAssetTypeText,
        [](const SubsystemT& subsystem)noexcept -> Name{
            return (subsystem.*Member).name();
        },
        [](SubsystemT& subsystem, const Name& value)noexcept{
            subsystem.*Member = Core::Assets::AssetRef<TAsset>(value);
        }
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
class TypedSubsystem : public ISubsystem{
public:
    static constexpr bool s_SupportsChildSubsystems = false;


public:
    explicit TypedSubsystem(Core::Assets::AssetArena& arena)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(SubsystemT::AssetTypeName())))
        : ISubsystem(arena, SubsystemT::AssetTypeName())
    {}
    TypedSubsystem(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(SubsystemT::AssetTypeName())))
        : ISubsystem(arena, SubsystemT::AssetTypeName(), virtualPath)
    {}
    TypedSubsystem(const TypedSubsystem&) = default;
    TypedSubsystem(TypedSubsystem&&)noexcept = default;
    virtual ~TypedSubsystem()noexcept override = 0;


public:
    TypedSubsystem& operator=(const TypedSubsystem&) = default;

    TypedSubsystem& operator=(TypedSubsystem&&)noexcept = default;


public:
    [[nodiscard]] static Span<const SubsystemFieldDescriptor<SubsystemT>> FieldDescriptors()noexcept{
        if constexpr(SubsystemT::s_FieldCount == 0u)
            return {};
        else
            return Span<const SubsystemFieldDescriptor<SubsystemT>>(SubsystemT::s_Fields, SubsystemT::s_FieldCount);
    }


public:
    [[nodiscard]] virtual bool loadBinary(Span<const u8> binary)override;
    [[nodiscard]] virtual bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const override;
    [[nodiscard]] virtual usize fieldCount()const noexcept override{ return SubsystemT::s_FieldCount; }
    [[nodiscard]] virtual SubsystemFieldValueDescriptor fieldDescriptor(usize index)const noexcept override;
    [[nodiscard]] virtual Name fieldValue(usize index)const noexcept override;
    virtual void setFieldValue(usize index, const Name& value)noexcept override;
    virtual void collectAssets(Vector<AssetRef, Core::Alloc::GlobalArena>& outAssets)const override;
};

template<typename SubsystemT>
TypedSubsystem<SubsystemT>::~TypedSubsystem()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DefaultSubsystem final : public TypedSubsystem<DefaultSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("default_subsystem")

    static constexpr bool s_SupportsChildSubsystems = true;
    static constexpr usize s_FieldCount = 0u;


public:
    using TypedSubsystem<DefaultSubsystem>::TypedSubsystem;
};


class RendererSubsystem final : public TypedSubsystem<RendererSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("renderer_subsystem")

    static constexpr bool s_SupportsChildSubsystems = true;
    static constexpr usize s_FieldCount = 6u;


public:
    using TypedSubsystem<RendererSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<Sampler> linearClamp;
    Core::Assets::AssetRef<Sampler> linearMirror;
    Core::Assets::AssetRef<Sampler> linearWrap;
    Core::Assets::AssetRef<Sampler> nearestClamp;
    Core::Assets::AssetRef<Sampler> nearestMirror;
    Core::Assets::AssetRef<Sampler> nearestWrap;

    static constexpr SubsystemFieldDescriptor<RendererSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<RendererSubsystem, Sampler, &RendererSubsystem::linearClamp>("linear_clamp", "sampler"),
        MakeSubsystemFieldDescriptor<RendererSubsystem, Sampler, &RendererSubsystem::linearMirror>("linear_mirror", "sampler"),
        MakeSubsystemFieldDescriptor<RendererSubsystem, Sampler, &RendererSubsystem::linearWrap>("linear_wrap", "sampler"),
        MakeSubsystemFieldDescriptor<RendererSubsystem, Sampler, &RendererSubsystem::nearestClamp>("nearest_clamp", "sampler"),
        MakeSubsystemFieldDescriptor<RendererSubsystem, Sampler, &RendererSubsystem::nearestMirror>("nearest_mirror", "sampler"),
        MakeSubsystemFieldDescriptor<RendererSubsystem, Sampler, &RendererSubsystem::nearestWrap>("nearest_wrap", "sampler"),
    };
};


class MeshSubsystem final : public TypedSubsystem<MeshSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("mesh_subsystem")

    static constexpr usize s_FieldCount = 3u;


public:
    using TypedSubsystem<MeshSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<VertexShader> emulationVs;
    Core::Assets::AssetRef<ComputeShader> objectDecodeCs;
    Core::Assets::AssetRef<MeshShader> sharedMs;

    static constexpr SubsystemFieldDescriptor<MeshSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<MeshSubsystem, VertexShader, &MeshSubsystem::emulationVs>("emulation_vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, ComputeShader, &MeshSubsystem::objectDecodeCs>("object_decode_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, MeshShader, &MeshSubsystem::sharedMs>("shared_ms", "mesh_shader"),
    };
};


class DeferredSubsystem final : public TypedSubsystem<DeferredSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("deferred_subsystem")

    static constexpr usize s_FieldCount = 4u;


public:
    using TypedSubsystem<DeferredSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> compositeCs;
    Core::Assets::AssetRef<PixelShader> compositePs;
    Core::Assets::AssetRef<VertexShader> compositeVs;
    Core::Assets::AssetRef<ComputeShader> lightingCs;

    static constexpr SubsystemFieldDescriptor<DeferredSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<DeferredSubsystem, ComputeShader, &DeferredSubsystem::compositeCs>("composite_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<DeferredSubsystem, PixelShader, &DeferredSubsystem::compositePs>("composite_ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<DeferredSubsystem, VertexShader, &DeferredSubsystem::compositeVs>("composite_vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<DeferredSubsystem, ComputeShader, &DeferredSubsystem::lightingCs>("lighting_cs", "compute_shader"),
    };
};


class BvhSubsystem final : public TypedSubsystem<BvhSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("bvh_subsystem")

    static constexpr usize s_FieldCount = 5u;


public:
    using TypedSubsystem<BvhSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> bitonicSortCs;
    Core::Assets::AssetRef<ComputeShader> fitCs;
    Core::Assets::AssetRef<ComputeShader> mortonCs;
    Core::Assets::AssetRef<ComputeShader> topologyCs;
    Core::Assets::AssetRef<ComputeShader> sceneRefitCs;

    static constexpr SubsystemFieldDescriptor<BvhSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::bitonicSortCs>("bitonic_sort_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::fitCs>("fit_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::mortonCs>("morton_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::topologyCs>("topology_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::sceneRefitCs>("scene_refit_cs", "compute_shader"),
    };
};


class GiSubsystem final : public TypedSubsystem<GiSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("gi_subsystem")

    static constexpr usize s_FieldCount = 8u;


public:
    using TypedSubsystem<GiSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> surfelAgeFreeCs;
    Core::Assets::AssetRef<ComputeShader> surfelHashBuildCs;
    Core::Assets::AssetRef<ComputeShader> surfelResolveCs;
    Core::Assets::AssetRef<ComputeShader> surfelSpawnCs;
    Core::Assets::AssetRef<ComputeShader> surfelTraceBuildArgsCs;
    Core::Assets::AssetRef<ComputeShader> surfelTraceCs;
    Core::Assets::AssetRef<ComputeShader> surfelTraceHwCs;
    Core::Assets::AssetRef<ComputeShader> surfelUpsampleCs;

    static constexpr SubsystemFieldDescriptor<GiSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelAgeFreeCs>("surfel_age_free_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelHashBuildCs>("surfel_hash_build_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelResolveCs>("surfel_resolve_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelSpawnCs>("surfel_spawn_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceBuildArgsCs>("surfel_trace_build_args_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceCs>("surfel_trace_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceHwCs>("surfel_trace_hw_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelUpsampleCs>("surfel_upsample_cs", "compute_shader"),
    };
};


class ReflectionSubsystem final : public TypedSubsystem<ReflectionSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("reflection_subsystem")

    static constexpr usize s_FieldCount = 6u;


public:
    using TypedSubsystem<ReflectionSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> buildArgsCs;
    Core::Assets::AssetRef<ComputeShader> classifyCs;
    Core::Assets::AssetRef<ComputeShader> depthReduceCs;
    Core::Assets::AssetRef<ComputeShader> resolveHwCs;
    Core::Assets::AssetRef<ComputeShader> spatialCs;
    Core::Assets::AssetRef<ComputeShader> temporalCs;

    static constexpr SubsystemFieldDescriptor<ReflectionSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::buildArgsCs>("build_args_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::classifyCs>("classify_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::depthReduceCs>("depth_reduce_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::resolveHwCs>("resolve_hw_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::spatialCs>("spatial_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::temporalCs>("temporal_cs", "compute_shader"),
    };
};


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


class SkinningSubsystem final : public TypedSubsystem<SkinningSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("skinning_subsystem")

    static constexpr usize s_FieldCount = 4u;


public:
    using TypedSubsystem<SkinningSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> localBoundsCs;
    Core::Assets::AssetRef<ComputeShader> meshletBoundsCs;
    Core::Assets::AssetRef<ComputeShader> repackNormalsCs;
    Core::Assets::AssetRef<ComputeShader> skinningCs;

    static constexpr SubsystemFieldDescriptor<SkinningSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::localBoundsCs>("local_bounds_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::meshletBoundsCs>("meshlet_bounds_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::repackNormalsCs>("repack_normals_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::skinningCs>("skinning_cs", "compute_shader"),
    };
};


class TransparentSubsystem final : public TypedSubsystem<TransparentSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("transparent_subsystem")

    static constexpr usize s_FieldCount = 7u;


public:
    using TypedSubsystem<TransparentSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> avboitDepthWarpCs;
    Core::Assets::AssetRef<ComputeShader> avboitIntegrateCs;
    Core::Assets::AssetRef<ComputeShader> opticalBoundsFinalizeCs;
    Core::Assets::AssetRef<ComputeShader> refractionClassifyTilesCs;
    Core::Assets::AssetRef<ComputeShader> refractionFinalizeTilesCs;
    Core::Assets::AssetRef<ComputeShader> refractionResolveHwCs;
    Core::Assets::AssetRef<ComputeShader> refractionResolveScreenCs;

    static constexpr SubsystemFieldDescriptor<TransparentSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::avboitDepthWarpCs>("avboit_depth_warp_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::avboitIntegrateCs>("avboit_integrate_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::opticalBoundsFinalizeCs>("optical_bounds_finalize_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::refractionClassifyTilesCs>("refraction_classify_tiles_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::refractionFinalizeTilesCs>("refraction_finalize_tiles_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::refractionResolveHwCs>("refraction_resolve_hw_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::refractionResolveScreenCs>("refraction_resolve_screen_cs", "compute_shader"),
    };
};


class RefractiveSubsystem final : public TypedSubsystem<RefractiveSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("refractive_subsystem")

    static constexpr usize s_FieldCount = 11u;


public:
    using TypedSubsystem<RefractiveSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> causticAccumulatorDecayCs;
    Core::Assets::AssetRef<ComputeShader> causticGeometryDownsampleCs;
    Core::Assets::AssetRef<ClosestHitShader> causticPhotonHwChit;
    Core::Assets::AssetRef<MissShader> causticPhotonHwMiss;
    Core::Assets::AssetRef<RayGenerationShader> causticPhotonHwRaygen;
    Core::Assets::AssetRef<ComputeShader> causticPhotonSwCs;
    Core::Assets::AssetRef<ComputeShader> causticResolveCs;
    Core::Assets::AssetRef<ComputeShader> refractionClassifyTilesCs;
    Core::Assets::AssetRef<ComputeShader> refractionFinalizeTilesCs;
    Core::Assets::AssetRef<ComputeShader> refractionResolveHwCs;
    Core::Assets::AssetRef<ComputeShader> refractionResolveScreenCs;

    static constexpr SubsystemFieldDescriptor<RefractiveSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticAccumulatorDecayCs>("caustic_accumulator_decay_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticGeometryDownsampleCs>("caustic_geometry_downsample_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ClosestHitShader, &RefractiveSubsystem::causticPhotonHwChit>("caustic_photon_hw_chit", "closest_hit_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, MissShader, &RefractiveSubsystem::causticPhotonHwMiss>("caustic_photon_hw_miss", "miss_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, RayGenerationShader, &RefractiveSubsystem::causticPhotonHwRaygen>("caustic_photon_hw_raygen", "ray_generation_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticPhotonSwCs>("caustic_photon_sw_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticResolveCs>("caustic_resolve_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionClassifyTilesCs>("refraction_classify_tiles_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionFinalizeTilesCs>("refraction_finalize_tiles_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionResolveHwCs>("refraction_resolve_hw_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionResolveScreenCs>("refraction_resolve_screen_cs", "compute_shader"),
    };
};


class CsgSubsystem final : public TypedSubsystem<CsgSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("csg_subsystem")

    static constexpr usize s_FieldCount = 10u;


public:
    using TypedSubsystem<CsgSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<CsgShape> boxShape;
    Core::Assets::AssetRef<CsgShape> capsuleShape;
    Core::Assets::AssetRef<CsgShape> planeShape;
    Core::Assets::AssetRef<CsgShape> sphereShape;
    Core::Assets::AssetRef<PixelShader> intervalCapFillPs;
    Core::Assets::AssetRef<ComputeShader> intervalCombineCs;
    Core::Assets::AssetRef<ComputeShader> intervalPeelCs;
    Core::Assets::AssetRef<ComputeShader> receiverSpanBuildCs;
    Core::Assets::AssetRef<PixelShader> receiverSurfacePs;
    Core::Assets::AssetRef<PixelShader> refractionCapFillPs;

    static constexpr SubsystemFieldDescriptor<CsgSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<CsgSubsystem, CsgShape, &CsgSubsystem::boxShape>("box_shape", "csg_shape"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, CsgShape, &CsgSubsystem::capsuleShape>("capsule_shape", "csg_shape"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, CsgShape, &CsgSubsystem::planeShape>("plane_shape", "csg_shape"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, CsgShape, &CsgSubsystem::sphereShape>("sphere_shape", "csg_shape"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, PixelShader, &CsgSubsystem::intervalCapFillPs>("interval_cap_fill_ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, ComputeShader, &CsgSubsystem::intervalCombineCs>("interval_combine_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, ComputeShader, &CsgSubsystem::intervalPeelCs>("interval_peel_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, ComputeShader, &CsgSubsystem::receiverSpanBuildCs>("receiver_span_build_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, PixelShader, &CsgSubsystem::receiverSurfacePs>("receiver_surface_ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, PixelShader, &CsgSubsystem::refractionCapFillPs>("refraction_cap_fill_ps", "pixel_shader"),
    };
};


class UiSubsystem final : public TypedSubsystem<UiSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("ui_subsystem")

    static constexpr usize s_FieldCount = 10u;


public:
    using TypedSubsystem<UiSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<PixelShader> outputPs;
    Core::Assets::AssetRef<VertexShader> outputVs;
    Core::Assets::AssetRef<PixelShader> ps;
    Core::Assets::AssetRef<VertexShader> vs;
    Core::Assets::AssetRef<FontAtlas> defaultKoreanAtlas;
    Core::Assets::AssetRef<Font> defaultKoreanFace;
    Core::Assets::AssetRef<FontAtlas> defaultLatinAtlas;
    Core::Assets::AssetRef<Font> defaultLatinFace;
    Core::Assets::AssetRef<UiSkin> defaultSkin;
    Core::Assets::AssetRef<Texture> defaultSkinTexture;

    static constexpr SubsystemFieldDescriptor<UiSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::outputPs>("output_ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::outputVs>("output_vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::ps>("ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::vs>("vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, FontAtlas, &UiSubsystem::defaultKoreanAtlas>("default_korean_atlas", "font_atlas"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Font, &UiSubsystem::defaultKoreanFace>("default_korean_face", "font"),
        MakeSubsystemFieldDescriptor<UiSubsystem, FontAtlas, &UiSubsystem::defaultLatinAtlas>("default_latin_atlas", "font_atlas"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Font, &UiSubsystem::defaultLatinFace>("default_latin_face", "font"),
        MakeSubsystemFieldDescriptor<UiSubsystem, UiSkin, &UiSubsystem::defaultSkin>("default_skin", "ui_skin"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Texture, &UiSubsystem::defaultSkinTexture>("default_skin_texture", "texture"),
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(macro) \
    macro(DefaultSubsystem) \
    macro(RendererSubsystem) \
    macro(MeshSubsystem) \
    macro(DeferredSubsystem) \
    macro(BvhSubsystem) \
    macro(GiSubsystem) \
    macro(ReflectionSubsystem) \
    macro(ShadowSubsystem) \
    macro(SkinningSubsystem) \
    macro(TransparentSubsystem) \
    macro(RefractiveSubsystem) \
    macro(CsgSubsystem) \
    macro(UiSubsystem)


[[nodiscard]] bool IsSubsystemAssetType(const Name& assetType)noexcept;
[[nodiscard]] const ISubsystem* CastSubsystem(const Core::Assets::IAsset* asset)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BuiltInSubsystems{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_DefaultText = "engine/subsystems/default";
inline constexpr AStringView s_RendererText = "engine/subsystems/renderer";
inline constexpr AStringView s_MeshText = "engine/subsystems/mesh";
inline constexpr AStringView s_DeferredText = "engine/subsystems/deferred";
inline constexpr AStringView s_BvhText = "engine/subsystems/bvh";
inline constexpr AStringView s_GiText = "engine/subsystems/gi";
inline constexpr AStringView s_ReflectionText = "engine/subsystems/reflection";
inline constexpr AStringView s_ShadowText = "engine/subsystems/shadow";
inline constexpr AStringView s_SkinningText = "engine/subsystems/skinning";
inline constexpr AStringView s_TransparentText = "engine/subsystems/transparent";
inline constexpr AStringView s_RefractiveText = "engine/subsystems/refractive";
inline constexpr AStringView s_CsgText = "engine/subsystems/csg";
inline constexpr AStringView s_UiText = "engine/subsystems/ui";

inline constexpr ISubsystem::SubsystemRef s_Default("engine/subsystems/default");
inline constexpr ISubsystem::SubsystemRef s_Renderer("engine/subsystems/renderer");
inline constexpr ISubsystem::SubsystemRef s_Mesh("engine/subsystems/mesh");
inline constexpr ISubsystem::SubsystemRef s_Deferred("engine/subsystems/deferred");
inline constexpr ISubsystem::SubsystemRef s_Bvh("engine/subsystems/bvh");
inline constexpr ISubsystem::SubsystemRef s_Gi("engine/subsystems/gi");
inline constexpr ISubsystem::SubsystemRef s_Reflection("engine/subsystems/reflection");
inline constexpr ISubsystem::SubsystemRef s_Shadow("engine/subsystems/shadow");
inline constexpr ISubsystem::SubsystemRef s_Skinning("engine/subsystems/skinning");
inline constexpr ISubsystem::SubsystemRef s_Transparent("engine/subsystems/transparent");
inline constexpr ISubsystem::SubsystemRef s_Refractive("engine/subsystems/refractive");
inline constexpr ISubsystem::SubsystemRef s_Csg("engine/subsystems/csg");
inline constexpr ISubsystem::SubsystemRef s_Ui("engine/subsystems/ui");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SubsystemAssetSerialization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool Serialize(const ISubsystem& subsystem, Core::Assets::AssetBytes& outBinary);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SubsystemAssetCodec final : public Core::Assets::IAssetCodec{
public:
    SubsystemAssetCodec()noexcept
        : Core::Assets::IAssetCodec(ISubsystem::AssetTypeName())
    {}


public:
    [[nodiscard]] virtual Expected<UniquePtr<Core::Assets::IAsset>> deserialize(Core::Assets::AssetArena& arena, const Name& virtualPath, Span<const u8> binary)const override;

#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


template<typename SubsystemT>
class TypedSubsystemAssetCodec final : public Core::Assets::AssetCodec<SubsystemT>{
public:
    TypedSubsystemAssetCodec()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(SubsystemT::AssetTypeName()))) = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override{
        if(!this->checkSerializeAssetType(asset, NWB_TEXT("TypedSubsystemAssetCodec::serialize")))
            return false;

        return SubsystemAssetSerialization::Serialize(*MakeNotNull(checked_cast<const SubsystemT*>(&asset)), outBinary);
    }
#endif
};

using DefaultSubsystemAssetCodec = TypedSubsystemAssetCodec<DefaultSubsystem>;
using RendererSubsystemAssetCodec = TypedSubsystemAssetCodec<RendererSubsystem>;
using MeshSubsystemAssetCodec = TypedSubsystemAssetCodec<MeshSubsystem>;
using DeferredSubsystemAssetCodec = TypedSubsystemAssetCodec<DeferredSubsystem>;
using BvhSubsystemAssetCodec = TypedSubsystemAssetCodec<BvhSubsystem>;
using GiSubsystemAssetCodec = TypedSubsystemAssetCodec<GiSubsystem>;
using ReflectionSubsystemAssetCodec = TypedSubsystemAssetCodec<ReflectionSubsystem>;
using ShadowSubsystemAssetCodec = TypedSubsystemAssetCodec<ShadowSubsystem>;
using SkinningSubsystemAssetCodec = TypedSubsystemAssetCodec<SkinningSubsystem>;
using TransparentSubsystemAssetCodec = TypedSubsystemAssetCodec<TransparentSubsystem>;
using RefractiveSubsystemAssetCodec = TypedSubsystemAssetCodec<RefractiveSubsystem>;
using CsgSubsystemAssetCodec = TypedSubsystemAssetCodec<CsgSubsystem>;
using UiSubsystemAssetCodec = TypedSubsystemAssetCodec<UiSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

