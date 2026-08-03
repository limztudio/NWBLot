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


[[nodiscard]] inline bool IsValidSamplerAddressMode(const Core::SamplerAddressMode::Enum addressMode){
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

[[nodiscard]] inline bool IsValidSamplerReductionType(const Core::SamplerReductionType::Enum reductionType){
    switch(reductionType){
    case Core::SamplerReductionType::Standard:
    case Core::SamplerReductionType::Comparison:
    case Core::SamplerReductionType::Minimum:
    case Core::SamplerReductionType::Maximum:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool IsValidSamplerDescription(const Core::SamplerDesc& description){
    return
        IsFinite(description.borderColor.r)
        && IsFinite(description.borderColor.g)
        && IsFinite(description.borderColor.b)
        && IsFinite(description.borderColor.a)
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
    explicit Sampler(Core::Assets::AssetArena&)
    {}
    Sampler(Core::Assets::AssetArena&, const Name& virtualPath)
        : Core::Assets::TypedAsset<Sampler>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

    void setDescription(const Core::SamplerDesc& description){ m_description = description; }


public:
    [[nodiscard]] const Core::SamplerDesc& description()const{ return m_description; }


private:
    Core::SamplerDesc m_description;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SamplerAssetCodec final : public Core::Assets::AssetCodec<Sampler>{
public:
    SamplerAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
