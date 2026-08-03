// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sampler_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCodecAutoRegistrar s_SamplerAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<SamplerAssetCodec>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Sampler::validatePayload()const{
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Sampler::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(!IsValidSamplerDescription(m_description)){
        NWB_LOGGER_ERROR(NWB_TEXT("Sampler::validatePayload failed: sampler '{}' has an invalid description")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    return true;
}

bool Sampler::loadBinary(const Core::Assets::AssetBytes& binary){
    m_description = {};

    usize cursor = 0u;
    SamplerBinaryPayload::HeaderBinary header;
    if(!Core::Assets::ReadMagicHeaderPayload(
        binary,
        cursor,
        header,
        SamplerBinaryPayload::s_SamplerMagic,
        NWB_TEXT("Sampler::loadBinary"),
        NWB_TEXT("sampler")
    ))
        return false;
    if(header.version != SamplerBinaryPayload::s_SamplerVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Sampler::loadBinary failed: unsupported sampler version {}; recook required"), header.version);
        return false;
    }
    if(
        header.reserved != 0u
        || header.minFilter > 1u
        || header.magFilter > 1u
        || header.mipFilter > 1u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Sampler::loadBinary failed: sampler header contains invalid flags"));
        return false;
    }

    Core::SamplerDesc description;
    description.borderColor = Core::Color(
        header.borderColorR,
        header.borderColorG,
        header.borderColorB,
        header.borderColorA
    );
    description.maxAnisotropy = header.maxAnisotropy;
    description.mipBias = header.mipBias;
    description.minFilter = header.minFilter != 0u;
    description.magFilter = header.magFilter != 0u;
    description.mipFilter = header.mipFilter != 0u;
    description.addressU = static_cast<Core::SamplerAddressMode::Enum>(header.addressU);
    description.addressV = static_cast<Core::SamplerAddressMode::Enum>(header.addressV);
    description.addressW = static_cast<Core::SamplerAddressMode::Enum>(header.addressW);
    description.reductionType = static_cast<Core::SamplerReductionType::Enum>(header.reductionType);
    if(!IsValidSamplerDescription(description)){
        NWB_LOGGER_ERROR(NWB_TEXT("Sampler::loadBinary failed: sampler description is invalid"));
        return false;
    }
    if(!Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Sampler::loadBinary")))
        return false;

    m_description = description;
    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
