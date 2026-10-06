// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/graphics/rhi/pipeline_state.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool IsValidSamplerAddressMode(const Core::SamplerAddressMode::Enum addressMode)noexcept{
    switch(addressMode){
    case Core::SamplerAddressMode::Clamp:
    case Core::SamplerAddressMode::Wrap:
    case Core::SamplerAddressMode::Border:
    case Core::SamplerAddressMode::Mirror:
    case Core::SamplerAddressMode::MirrorOnce:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool IsValidSamplerReductionType(const Core::SamplerReductionType::Enum reductionType)noexcept{
    switch(reductionType){
    case Core::SamplerReductionType::Standard:
    case Core::SamplerReductionType::Comparison:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool IsValidSamplerDescription(const Core::SamplerDesc& description)noexcept{
    return
        description.borderColor.r == 0.0f
        && description.borderColor.g == 0.0f
        && description.borderColor.b == 0.0f
        && description.borderColor.a == 0.0f
        && IsFinite(description.maxAnisotropy)
        && description.maxAnisotropy >= 1.0f
        && IsFinite(description.mipBias)
        && IsValidSamplerAddressMode(description.addressU)
        && IsValidSamplerAddressMode(description.addressV)
        && IsValidSamplerAddressMode(description.addressW)
        && IsValidSamplerReductionType(description.reductionType)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Sampler final : public Core::Assets::TypedAsset<Sampler>{
public:
    NWB_DEFINE_ASSET_TYPE("sampler")


public:
    explicit Sampler(Core::Assets::AssetArena&)noexcept
    {}
    Sampler(Core::Assets::AssetArena&, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<Sampler>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

    void setDescription(const Core::SamplerDesc& description)noexcept{ m_description = description; }


public:
    [[nodiscard]] const Core::SamplerDesc& description()const noexcept{ return m_description; }


private:
    Core::SamplerDesc m_description;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SamplerAssetCodec, Sampler);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

