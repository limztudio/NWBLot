// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>
#include <global/algorithm.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCodecAutoRegistrar s_TextureAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<TextureAssetCodec>);

using TextureFormat::ComputeCompleteMipCount;
using TextureFormat::ComputeMipPlaneBlockLayout;
using TextureFormat::ComputeMipSliceCount;

[[nodiscard]] static bool ValidateTextureMipLevels(
    const Texture::MipLevelVector& mipLevels,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    u64& outPrimaryPayloadByteCount,
    const tchar* failureContext
){
    outPrimaryPayloadByteCount = 0u;
    u32 expectedMipCount = 0u;
    if(!ComputeCompleteMipCount(dimension, width, height, depth, expectedMipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: base resolution is invalid"), failureContext);
        return false;
    }
    if(mipLevels.size() != expectedMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip count does not describe a complete chain"), failureContext);
        return false;
    }

    u32 expectedWidth = width;
    u32 expectedHeight = height;
    u32 expectedDepth = depth;
    u64 expectedOffsetBytes = 0u;
    for(usize mipIndex = 0u; mipIndex < mipLevels.size(); ++mipIndex){
        const TextureMipLevel& mip = mipLevels[mipIndex];
        u32 expectedBlockCountX = 0u;
        u32 expectedBlockCountY = 0u;
        u64 expectedSliceSizeBytes = 0u;
        if(!ComputeMipPlaneBlockLayout(
            payloadFormat,
            expectedWidth,
            expectedHeight,
            expectedBlockCountX,
            expectedBlockCountY,
            expectedSliceSizeBytes
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} block layout exceeds runtime limits"), failureContext, mipIndex);
            return false;
        }
        u32 expectedSliceCount = 0u;
        if(!ComputeMipSliceCount(dimension, expectedDepth, expectedSliceCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} slice count is invalid"), failureContext, mipIndex);
            return false;
        }
        if(expectedSliceSizeBytes > Limit<u64>::s_Max / expectedSliceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} byte size overflows"), failureContext, mipIndex);
            return false;
        }
        const u64 expectedSizeBytes = expectedSliceSizeBytes * expectedSliceCount;

        if(mip.width != expectedWidth || mip.height != expectedHeight){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} resolution is not a complete chain"), failureContext, mipIndex);
            return false;
        }
        if(mip.blockCountX != expectedBlockCountX || mip.blockCountY != expectedBlockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} block grid is invalid"), failureContext, mipIndex);
            return false;
        }
        if(mip.sliceCount != expectedSliceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} slice count is invalid"), failureContext, mipIndex);
            return false;
        }
        if(mip.offsetBytes != expectedOffsetBytes || mip.sizeBytes != expectedSizeBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} is not a contiguous texture payload"), failureContext, mipIndex);
            return false;
        }
        if(expectedSizeBytes > Limit<u64>::s_Max - expectedOffsetBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} byte range overflows"), failureContext, mipIndex);
            return false;
        }

        expectedOffsetBytes += expectedSizeBytes;
        expectedWidth = expectedWidth > 1u ? expectedWidth >> 1u : 1u;
        expectedHeight = expectedHeight > 1u ? expectedHeight >> 1u : 1u;
        if(dimension == TextureDimension::Texture3D)
            expectedDepth = expectedDepth > 1u ? expectedDepth >> 1u : 1u;
    }

    outPrimaryPayloadByteCount = expectedOffsetBytes;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Texture::validatePayload()const{
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(!IsValidTextureColorSpace(m_colorSpace)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid color space")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(!IsValidTexturePayloadFormat(m_payloadFormat)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid payload format")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(IsHdrTexturePayloadFormat(m_payloadFormat) && m_colorSpace != TextureColorSpace::Linear){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: HDR texture '{}' must use linear color space")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(!IsValidTextureDimension(m_dimension)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid dimension")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(m_width == 0u || m_height == 0u || m_depth == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an empty resolution")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(m_dimension != TextureDimension::Texture3D && m_depth != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: non-volume texture '{}' has an invalid depth")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(m_dimension == TextureDimension::TextureCube && m_width != m_height){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: cubemap texture '{}' must have square faces")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(m_mipLevels.empty() || m_payloadBytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an incomplete payload")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }

    if(!IsValidTextureAlphaMode(m_alphaMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid alpha mode")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }

    const bool alphaModeHasAlpha = m_alphaMode != TextureAlphaMode::Opaque;
    if(m_hasAlpha != alphaModeHasAlpha){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has inconsistent alpha metadata")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(m_alphaMode != TextureAlphaMode::ConstantUnorm8 && m_alphaConstantUnorm8 != 255u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an unexpected alpha constant")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(
        (m_payloadFormat == TexturePayloadFormat::UastcLdr4x4
            && m_alphaMode != TextureAlphaMode::Opaque
            && m_alphaMode != TextureAlphaMode::EmbeddedLdr)
        || (IsHdrTexturePayloadFormat(m_payloadFormat)
            && m_alphaMode == TextureAlphaMode::EmbeddedLdr)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an invalid alpha transport for its payload format")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }

    u64 primaryPayloadByteCount = 0u;
    if(!__hidden_texture_runtime::ValidateTextureMipLevels(
        m_mipLevels,
        m_payloadFormat,
        m_dimension,
        m_width,
        m_height,
        m_depth,
        primaryPayloadByteCount,
        NWB_TEXT("Texture::validatePayload")
    ))
        return false;

    u64 expectedPayloadByteCount = primaryPayloadByteCount;
    if(m_alphaMode == TextureAlphaMode::SeparateUastcLdr4x4){
        if(primaryPayloadByteCount > Limit<u64>::s_Max - expectedPayloadByteCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' alpha payload size overflows")
                , StringConvert(virtualPath().c_str())
            );
            return false;
        }
        expectedPayloadByteCount += primaryPayloadByteCount;
    }
    if(expectedPayloadByteCount != static_cast<u64>(m_payloadBytes.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' byte count does not match its payload transport")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    return true;
}


bool Texture::loadBinary(const Core::Assets::AssetBytes& binary){
    m_colorSpace = TextureColorSpace::Linear;
    m_hasAlpha = false;
    m_width = 0u;
    m_height = 0u;
    m_dimension = TextureDimension::Texture2D;
    m_depth = 1u;
    m_payloadFormat = TexturePayloadFormat::UastcLdr4x4;
    m_alphaMode = TextureAlphaMode::Opaque;
    m_alphaConstantUnorm8 = 255u;
    m_mipLevels.clear();
    m_payloadBytes.clear();

    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: virtual path is empty"));
        return false;
    }

    usize prefixCursor = 0u;
    TextureBinaryPayload::HeaderPrefix headerPrefix;
    if(!Core::Assets::ReadMagicHeaderPayload(
        binary,
        prefixCursor,
        headerPrefix,
        TextureBinaryPayload::s_TextureMagic,
        NWB_TEXT("Texture::loadBinary"),
        NWB_TEXT("texture")
    ))
        return false;
    if(
        headerPrefix.version != TextureBinaryPayload::s_TextureVersionV2
        && headerPrefix.version != TextureBinaryPayload::s_TextureVersion
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: unsupported texture payload version; recook required"));
        return false;
    }

    usize cursor = 0u;
    u32 colorSpace = 0u;
    u32 dimensionValue = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 0u;
    u32 mipCount = 0u;
    TexturePayloadFormat::Enum payloadFormat = TexturePayloadFormat::UastcLdr4x4;
    TextureAlphaMode::Enum alphaMode = TextureAlphaMode::Opaque;
    u8 alphaConstantUnorm8 = 255u;
    u64 payloadByteCount64 = 0u;
    if(headerPrefix.version == TextureBinaryPayload::s_TextureVersionV2){
        TextureBinaryPayload::HeaderBinaryV2 header;
        if(!Core::Assets::ReadMagicHeaderPayload(
            binary,
            cursor,
            header,
            TextureBinaryPayload::s_TextureMagic,
            NWB_TEXT("Texture::loadBinary"),
            NWB_TEXT("texture")
        ))
            return false;
        if(
            header.version != TextureBinaryPayload::s_TextureVersionV2
            || header.hasAlpha > 1u
            || header.reserved != 0u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid V2 texture payload flags"));
            return false;
        }
        colorSpace = header.colorSpace;
        dimensionValue = header.dimension;
        width = header.width;
        height = header.height;
        depth = header.depth;
        mipCount = header.mipCount;
        payloadFormat = TexturePayloadFormat::UastcLdr4x4;
        alphaMode = header.hasAlpha != 0u ? TextureAlphaMode::EmbeddedLdr : TextureAlphaMode::Opaque;
        payloadByteCount64 = header.uastcByteCount;
    }
    else{
        TextureBinaryPayload::HeaderBinary header;
        if(!Core::Assets::ReadMagicHeaderPayload(
            binary,
            cursor,
            header,
            TextureBinaryPayload::s_TextureMagic,
            NWB_TEXT("Texture::loadBinary"),
            NWB_TEXT("texture")
        ))
            return false;
        if(
            header.version != TextureBinaryPayload::s_TextureVersion
            || (header.alphaInfo & TextureBinaryPayload::s_AlphaInfoReservedMask) != 0u
            || header.payloadFormat > static_cast<u32>(Limit<u8>::s_Max)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid V3 texture payload flags"));
            return false;
        }
        colorSpace = header.colorSpace;
        dimensionValue = header.dimension;
        width = header.width;
        height = header.height;
        depth = header.depth;
        mipCount = header.mipCount;
        payloadFormat = static_cast<TexturePayloadFormat::Enum>(header.payloadFormat);
        alphaMode = static_cast<TextureAlphaMode::Enum>(header.alphaInfo & TextureBinaryPayload::s_AlphaInfoModeMask);
        alphaConstantUnorm8 = static_cast<u8>(
            (header.alphaInfo & TextureBinaryPayload::s_AlphaInfoConstantMask)
            >> TextureBinaryPayload::s_AlphaInfoConstantShift
        );
        payloadByteCount64 = header.payloadByteCount;
    }

    const TextureDimension::Enum dimension = static_cast<TextureDimension::Enum>(dimensionValue);
    if(
        dimensionValue > static_cast<u32>(Limit<u8>::s_Max)
        || !IsValidTextureDimension(dimension)
        || !IsValidTexturePayloadFormat(payloadFormat)
        || !IsValidTextureAlphaMode(alphaMode)
        || depth == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid texture payload dimensions"));
        return false;
    }
    if(mipCount == 0u || payloadByteCount64 > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: texture payload counts exceed runtime limits"));
        return false;
    }

    Core::Assets::AssetVector<TextureBinaryPayload::MipLevelBinary> mipBinaries(m_mipLevels.get_allocator().arena());
    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        mipCount,
        mipBinaries,
        NWB_TEXT("Texture::loadBinary"),
        NWB_TEXT("mip levels")
    ))
        return false;

    const usize payloadByteCount = static_cast<usize>(payloadByteCount64);
    if(!BinaryDetail::CanReadBytes(binary, cursor, payloadByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: texture payload is truncated"));
        return false;
    }

    MipLevelVector mipLevels(m_mipLevels.get_allocator().arena());
    mipLevels.reserve(mipBinaries.size());
    for(const TextureBinaryPayload::MipLevelBinary& mipBinary : mipBinaries){
        if(mipBinary.reserved != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid texture mip payload flags"));
            return false;
        }

        TextureMipLevel mip;
        mip.width = mipBinary.width;
        mip.height = mipBinary.height;
        mip.blockCountX = mipBinary.blockCountX;
        mip.blockCountY = mipBinary.blockCountY;
        mip.offsetBytes = mipBinary.offsetBytes;
        mip.sizeBytes = mipBinary.sizeBytes;
        mip.sliceCount = mipBinary.sliceCount;
        mipLevels.push_back(mip);
    }

    Core::Assets::AssetBytes payloadBytes(m_payloadBytes.get_allocator().arena());
    payloadBytes.resize(payloadByteCount);
    if(!BinaryDetail::ReadBytes(binary, cursor, payloadBytes.data(), payloadByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: texture payload is malformed"));
        return false;
    }
    if(!Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Texture::loadBinary")))
        return false;

    m_colorSpace = static_cast<TextureColorSpace::Enum>(colorSpace);
    m_hasAlpha = alphaMode != TextureAlphaMode::Opaque;
    m_width = width;
    m_height = height;
    m_dimension = dimension;
    m_depth = depth;
    m_payloadFormat = payloadFormat;
    m_alphaMode = alphaMode;
    m_alphaConstantUnorm8 = alphaConstantUnorm8;
    m_mipLevels = Move(mipLevels);
    m_payloadBytes = Move(payloadBytes);
    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

