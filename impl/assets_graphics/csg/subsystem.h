// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_csg/asset.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgSubsystem final : public TypedSubsystem<CsgSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("csg_subsystem")


public:
    explicit CsgSubsystem(Core::Assets::AssetArena& arena)noexcept
        : TypedSubsystem(arena)
        , basicShapes(arena)
    {}
    CsgSubsystem(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : TypedSubsystem(arena, virtualPath)
        , basicShapes(arena)
    {}


public:
    SubsystemAssetRefVector<CsgShape> basicShapes;
    Core::Assets::AssetRef<PixelShader> intervalCapFillPs;
    Core::Assets::AssetRef<ComputeShader> intervalCombineCs;
    Core::Assets::AssetRef<ComputeShader> intervalPeelCs;
    Core::Assets::AssetRef<ComputeShader> receiverSpanBuildCs;
    Core::Assets::AssetRef<PixelShader> receiverSurfacePs;
    Core::Assets::AssetRef<PixelShader> refractionCapFillPs;


public:
    static constexpr SubsystemFieldDescriptor<CsgSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<CsgSubsystem, CsgShape, &CsgSubsystem::basicShapes>("basic_shapes"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, PixelShader, &CsgSubsystem::intervalCapFillPs>("interval_cap_fill_ps"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, ComputeShader, &CsgSubsystem::intervalCombineCs>("interval_combine_cs"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, ComputeShader, &CsgSubsystem::intervalPeelCs>("interval_peel_cs"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, ComputeShader, &CsgSubsystem::receiverSpanBuildCs>("receiver_span_build_cs"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, PixelShader, &CsgSubsystem::receiverSurfacePs>("receiver_surface_ps"),
        MakeSubsystemFieldDescriptor<CsgSubsystem, PixelShader, &CsgSubsystem::refractionCapFillPs>("refraction_cap_fill_ps"),
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

