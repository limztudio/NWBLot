// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sampler_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_SamplerValidatePayloadContext = NWB_TEXT("Sampler::validatePayload");

NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SamplerAssetCodecAutoRegistrar, SamplerAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Sampler::validatePayload()const{
    if(!checkVirtualPath(__hidden_sampler_runtime::s_SamplerValidatePayloadContext))
        return false;
    return ValidateSamplerDescriptionPayload(
        virtualPath(),
        m_description.borderColor.r,
        m_description.borderColor.g,
        m_description.borderColor.b,
        m_description.borderColor.a,
        m_description.maxAnisotropy,
        m_description.mipBias,
        static_cast<u32>(m_description.addressU),
        static_cast<u32>(m_description.addressV),
        static_cast<u32>(m_description.addressW),
        static_cast<u32>(m_description.reductionType)
    );
}

b8 Sampler::loadBinary(const Span<const u8> binary){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SamplerBinaryHeader>(binary, s_SamplerBinaryMagic, SamplerBinaryPayload::s_SamplerLoadBinaryContext, SamplerBinaryPayload::s_SamplerAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SamplerBinaryHeader& header = *headerResult;
    if(!ValidateSamplerBinaryHeaderAndDescription(header))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, SamplerBinaryPayload::s_SamplerLoadBinaryContext))
        return false;
    if(!checkVirtualPath(__hidden_sampler_runtime::s_SamplerValidatePayloadContext))
        return false;

    m_description.borderColor = Core::Color(header.borderColorR, header.borderColorG, header.borderColorB, header.borderColorA);
    m_description.maxAnisotropy = header.maxAnisotropy;
    m_description.mipBias = header.mipBias;
    m_description.minFilter = header.minFilter != 0u;
    m_description.magFilter = header.magFilter != 0u;
    m_description.mipFilter = header.mipFilter != 0u;
    m_description.addressU = static_cast<Core::SamplerAddressMode::Enum>(header.addressU);
    m_description.addressV = static_cast<Core::SamplerAddressMode::Enum>(header.addressV);
    m_description.addressW = static_cast<Core::SamplerAddressMode::Enum>(header.addressW);
    m_description.reductionType = static_cast<Core::SamplerReductionType::Enum>(header.reductionType);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

