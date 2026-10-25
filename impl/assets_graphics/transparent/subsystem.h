// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComputeShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

using TransparentSubsystemAssetCodec = TypedSubsystemAssetCodec<TransparentSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
