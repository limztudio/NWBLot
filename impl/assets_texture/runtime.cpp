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


[[nodiscard]] static Expected<u64> ValidateTextureMipLevels(
    const Span<const TextureMipLevel> mipLevels,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    const TStringView failureContext
){
    const auto result = TextureFormat::ValidateMipChainLayout(mipLevels, payloadFormat, dimension, width, height, depth);
    if(!result){
        switch(result.error().failure){
        case TextureFormat::MipChainLayoutFailure::InvalidMipChain:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: base resolution is invalid"), failureContext);
            break;
        case TextureFormat::MipChainLayoutFailure::MipCountMismatch:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip count does not describe a complete chain"), failureContext);
            break;
        case TextureFormat::MipChainLayoutFailure::BlockLayoutOverflow:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} block layout exceeds runtime limits"), failureContext, result.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::SliceCountInvalid:
        case TextureFormat::MipChainLayoutFailure::SliceCountMismatch:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} slice count is invalid"), failureContext, result.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::ByteSizeOverflow:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} byte size overflows"), failureContext, result.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::ResolutionMismatch:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} resolution is not a complete chain"), failureContext, result.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::BlockGridMismatch:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} block grid is invalid"), failureContext, result.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::ByteRangeMismatch:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} is not a contiguous texture payload"), failureContext, result.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::OffsetOverflow:
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} byte range overflows"), failureContext, result.error().mipIndex);
            break;
        }
        return MakeUnexpected(Failure{});
    }
    return *result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Texture::validatePayload()const{
    if(!checkVirtualPath(__hidden_texture_runtime::s_ValidatePayloadContext))
        return false;
    if(!IsValidTexturePayloadFormat(m_payloadFormat)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid payload format"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(!IsValidTextureDimension(m_dimension)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid dimension"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(m_width == 0u || m_height == 0u || m_depth == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an empty resolution"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(m_dimension != TextureDimension::Texture3D && m_depth != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: non-volume texture '{}' has an invalid depth"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(m_dimension == TextureDimension::TextureCube && m_width != m_height){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: cubemap texture '{}' must have square faces"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(m_mipLevels.empty() || m_payloadBytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an incomplete payload"), ConvertString(virtualPath().resolvedText()));
        return false;
    }

    if(!IsValidTextureAlphaMode(m_alphaMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid alpha mode"), ConvertString(virtualPath().resolvedText()));
        return false;
    }

    if(m_alphaMode == TextureAlphaMode::ConstantUnorm8 && !TextureFormat::IsValidTextureAlphaConstant(m_alphaMode, m_alphaConstantUnorm8)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' constant alpha must be below opaque alpha"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(m_alphaMode != TextureAlphaMode::ConstantUnorm8 && !TextureFormat::IsValidTextureAlphaConstant(m_alphaMode, m_alphaConstantUnorm8)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an unexpected alpha constant"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(!TextureFormat::IsValidTextureAlphaTransport(m_payloadFormat, m_alphaMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid alpha transport for its payload format"), ConvertString(virtualPath().resolvedText()));
        return false;
    }

    const auto primaryPayloadByteCountResult = __hidden_texture_runtime::ValidateTextureMipLevels(m_mipLevels, m_payloadFormat, m_dimension, m_width, m_height, m_depth, __hidden_texture_runtime::s_ValidatePayloadContext);
    if(!primaryPayloadByteCountResult)
        return false;

    const auto expectedPayloadByteCount = TextureFormat::ComputeExpectedPayloadByteCount(*primaryPayloadByteCountResult, m_alphaMode);
    if(!expectedPayloadByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' alpha payload size overflows"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(*expectedPayloadByteCount != static_cast<u64>(m_payloadBytes.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' byte count does not match its payload transport"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    return true;
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

        mipLevels.push_back(BuildTextureMipLevel(
            mipBinary.width,
            mipBinary.height,
            mipBinary.blockCountX,
            mipBinary.blockCountY,
            mipBinary.offsetBytes,
            mipBinary.sizeBytes,
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
    if(m_mipLevels.empty())
        return 0u;
    const TextureMipLevel& lastMip = m_mipLevels.back();
    return lastMip.offsetBytes + lastMip.sizeBytes;
}

const u8* Texture::alphaUastcBlocks()const noexcept{
    if(m_alphaMode != TextureAlphaMode::SeparateUastcLdr4x4)
        return nullptr;
    return m_payloadBytes.data() + static_cast<usize>(primaryPayloadByteCount());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

