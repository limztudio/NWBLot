// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComputeShader;
class RayGenerationShader;
class ClosestHitShader;
class MissShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

using RefractiveSubsystemAssetCodec = TypedSubsystemAssetCodec<RefractiveSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
