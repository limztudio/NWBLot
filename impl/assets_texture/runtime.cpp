// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_LoadBinaryContext = NWB_TEXT("Texture::loadBinary");
static constexpr TStringView s_ValidatePayloadContext = NWB_TEXT("Texture::validatePayload");

NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_TextureAssetCodecAutoRegistrar, TextureAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Texture::validatePayload()const{
    if(!checkVirtualPath(__hidden_texture_runtime::s_ValidatePayloadContext))
        return false;
    return ValidateTextureAssetPayload(
        virtualPath(),
        static_cast<u8>(m_payloadFormat),
        static_cast<u8>(m_dimension),
        m_width,
        m_height,
        m_depth,
        static_cast<u8>(m_alphaMode),
        m_alphaConstantUnorm8,
        m_mipLevels,
        m_payloadBytes
    );
}

b8 Texture::loadBinary(const Span<const u8> binary){
    if(!checkVirtualPath(__hidden_texture_runtime::s_LoadBinaryContext))
        return false;

    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<TextureBinaryPayload::HeaderBinary>(binary, TextureBinaryPayload::s_TextureMagic, __hidden_texture_runtime::s_LoadBinaryContext, s_TextureAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const TextureBinaryPayload::HeaderBinary& header = *headerResult;
    if(header.version != TextureBinaryPayload::s_TextureVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: unsupported texture payload version; recook required"));
        return false;
    }
    if((header.alphaInfo & TextureBinaryPayload::s_AlphaInfoReservedMask) != 0u || header.payloadFormat > static_cast<u32>(Limit<u8>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid texture payload flags"));
        return false;
    }

    const u32 dimensionValue = header.dimension;
    const u32 width = header.width;
    const u32 height = header.height;
    const u32 depth = header.depth;
    const u32 mipCount = header.mipCount;
    const TexturePayloadFormat::Enum payloadFormat = static_cast<TexturePayloadFormat::Enum>(header.payloadFormat);
    const TextureAlphaMode::Enum alphaMode = static_cast<TextureAlphaMode::Enum>(header.alphaInfo & TextureBinaryPayload::s_AlphaInfoModeMask);
    const u8 alphaConstantUnorm8 = static_cast<u8>((header.alphaInfo & TextureBinaryPayload::s_AlphaInfoConstantMask) >> TextureBinaryPayload::s_AlphaInfoConstantShift);
    const u64 payloadByteCount64 = header.payloadByteCount;

    const TextureDimension::Enum dimension = static_cast<TextureDimension::Enum>(dimensionValue);
    if(dimensionValue > static_cast<u32>(Limit<u8>::s_Max) || !IsValidTextureDimension(dimension) || !IsValidTexturePayloadFormat(payloadFormat) || !IsValidTextureAlphaMode(alphaMode) || depth == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid texture payload dimensions"));
        return false;
    }
    if(mipCount == 0u || payloadByteCount64 > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: texture payload counts exceed runtime limits"));
        return false;
    }

    const auto mipByteCount = ComputeBinaryVectorPayloadBytes<TextureBinaryPayload::MipLevelBinary>(mipCount);
    if(!mipByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: mip payload byte size overflows"));
        return false;
    }
    if(!CanReadBytes(binary, cursor, *mipByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: malformed mip levels payload"));
        return false;
    }

    const usize payloadByteCount = static_cast<usize>(payloadByteCount64);
    if(!CanReadBytes(binary, cursor + *mipByteCount, payloadByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: texture payload is truncated"));
        return false;
    }

    MipLevelVector mipLevels(m_mipLevels.get_allocator().arena());
    if(!CanStoreValueCount(mipLevels, mipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: mip count exceeds runtime container limits"));
        return false;
    }
    mipLevels.reserve(mipCount);
    for(u32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        const auto mipBinaryResult = ReadPOD<TextureBinaryPayload::MipLevelBinary>(binary, cursor);
        if(!mipBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: malformed mip level {}"), mipIndex);
            return false;
        }
        const TextureBinaryPayload::MipLevelBinary& mipBinary = *mipBinaryResult;
        if(mipBinary.reserved != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid texture mip payload flags"));
            return false;
        }

        mipLevels.push_back(TextureMipLevel(
            mipBinary.offsetBytes,
            mipBinary.sizeBytes,
            mipBinary.width,
            mipBinary.height,
            mipBinary.blockCountX,
            mipBinary.blockCountY,
            mipBinary.sliceCount
        ));
    }

    Core::Assets::AssetBytes payloadBytes(m_payloadBytes.get_allocator().arena());
    payloadBytes.resize(payloadByteCount);
    if(!ReadBytes(binary, payloadByteCount, cursor, payloadBytes.data())){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: texture payload is malformed"));
        return false;
    }
    if(!Core::Assets::ReadCompletePayload(binary, cursor, __hidden_texture_runtime::s_LoadBinaryContext))
        return false;

    Texture candidate(m_mipLevels.get_allocator().arena(), virtualPath());
    candidate.setPayload(width, height, Move(mipLevels), Move(payloadBytes), dimension, depth, payloadFormat, alphaMode, alphaConstantUnorm8);
    if(!candidate.validatePayload())
        return false;
    *this = Move(candidate);
    return true;
}

void Texture::setPayload(
    const u32 width,
    const u32 height,
    MipLevelVector&& mipLevels,
    Core::Assets::AssetBytes&& payloadBytes,
    const TextureDimension::Enum dimension,
    const u32 depth,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureAlphaMode::Enum alphaMode,
    const u8 alphaConstantUnorm8
)noexcept{
    m_width = width;
    m_height = height;
    m_dimension = dimension;
    m_depth = depth;
    m_payloadFormat = payloadFormat;
    m_alphaMode = alphaMode;
    m_alphaConstantUnorm8 = alphaConstantUnorm8;
    m_mipLevels = Move(mipLevels);
    m_payloadBytes = Move(payloadBytes);
}

u64 Texture::primaryPayloadByteCount()const noexcept{
    return ComputeTexturePrimaryPayloadByteCount(m_mipLevels);
}

const u8* Texture::alphaUastcBlocks()const noexcept{
    if(m_alphaMode != TextureAlphaMode::SeparateUastcLdr4x4)
        return nullptr;
    return m_payloadBytes.data() + static_cast<usize>(primaryPayloadByteCount());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

