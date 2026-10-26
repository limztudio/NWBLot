// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredSubsystem final : public TypedSubsystem<DeferredSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("deferred_subsystem")


public:
    using TypedSubsystem<DeferredSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> compositeCs;
    Core::Assets::AssetRef<PixelShader> compositePs;
    Core::Assets::AssetRef<VertexShader> compositeVs;
    Core::Assets::AssetRef<ComputeShader> lightingCs;

    static constexpr SubsystemFieldDescriptor<DeferredSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<DeferredSubsystem, ComputeShader, &DeferredSubsystem::compositeCs>("composite_cs"),
        MakeSubsystemFieldDescriptor<DeferredSubsystem, PixelShader, &DeferredSubsystem::compositePs>("composite_ps"),
        MakeSubsystemFieldDescriptor<DeferredSubsystem, VertexShader, &DeferredSubsystem::compositeVs>("composite_vs"),
        MakeSubsystemFieldDescriptor<DeferredSubsystem, ComputeShader, &DeferredSubsystem::lightingCs>("lighting_cs"),
    };
};

using DeferredSubsystemAssetCodec = TypedSubsystemAssetCodec<DeferredSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
