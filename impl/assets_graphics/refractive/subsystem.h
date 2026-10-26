// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RefractiveSubsystem final : public TypedSubsystem<RefractiveSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("refractive_subsystem")


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

    static constexpr SubsystemFieldDescriptor<RefractiveSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticAccumulatorDecayCs>("caustic_accumulator_decay_cs"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticGeometryDownsampleCs>("caustic_geometry_downsample_cs"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ClosestHitShader, &RefractiveSubsystem::causticPhotonHwChit>("caustic_photon_hw_chit"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, MissShader, &RefractiveSubsystem::causticPhotonHwMiss>("caustic_photon_hw_miss"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, RayGenerationShader, &RefractiveSubsystem::causticPhotonHwRaygen>("caustic_photon_hw_raygen"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticPhotonSwCs>("caustic_photon_sw_cs"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::causticResolveCs>("caustic_resolve_cs"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionClassifyTilesCs>("refraction_classify_tiles_cs"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionFinalizeTilesCs>("refraction_finalize_tiles_cs"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionResolveHwCs>("refraction_resolve_hw_cs"),
        MakeSubsystemFieldDescriptor<RefractiveSubsystem, ComputeShader, &RefractiveSubsystem::refractionResolveScreenCs>("refraction_resolve_screen_cs"),
    };
};

using RefractiveSubsystemAssetCodec = TypedSubsystemAssetCodec<RefractiveSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
