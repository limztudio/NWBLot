// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_sampler/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSubsystem final : public TypedSubsystem<RendererSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("renderer_subsystem")


public:
    explicit RendererSubsystem(Core::Assets::AssetArena& arena)noexcept
        : TypedSubsystem(arena)
        , basicSamplers(arena)
    {}
    RendererSubsystem(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : TypedSubsystem(arena, virtualPath)
        , basicSamplers(arena)
    {}


public:
    SubsystemAssetRefVector<Sampler> basicSamplers;

    static constexpr SubsystemFieldDescriptor<RendererSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<RendererSubsystem, Sampler, &RendererSubsystem::basicSamplers>("basic_samplers"),
    };
};

using RendererSubsystemAssetCodec = TypedSubsystemAssetCodec<RendererSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
