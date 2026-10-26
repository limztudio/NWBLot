// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TransparentSubsystem final : public TypedSubsystem<TransparentSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("transparent_subsystem")


public:
    using TypedSubsystem<TransparentSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> avboitDepthWarpCs;
    Core::Assets::AssetRef<ComputeShader> avboitIntegrateCs;
    Core::Assets::AssetRef<ComputeShader> opticalBoundsFinalizeCs;


public:
    static constexpr SubsystemFieldDescriptor<TransparentSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::avboitDepthWarpCs>("avboit_depth_warp_cs"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::avboitIntegrateCs>("avboit_integrate_cs"),
        MakeSubsystemFieldDescriptor<TransparentSubsystem, ComputeShader, &TransparentSubsystem::opticalBoundsFinalizeCs>("optical_bounds_finalize_cs"),
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

