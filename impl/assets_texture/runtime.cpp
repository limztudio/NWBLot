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


static constexpr u32 s_UastcBlockWidth = 4u;
static constexpr u32 s_UastcBlockHeight = 4u;
static constexpr u32 s_UastcBytesPerBlock = 16u;

Core::Assets::AssetCodecAutoRegistrar s_TextureAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<TextureAssetCodec>);


[[nodiscard]] static bool ComputeCompleteMipCount(const u32 width, const u32 height, u32& outMipCount){
    outMipCount = 0u;
    if(width == 0u || height == 0u)
        return false;

    u32 mipWidth = width;
    u32 mipHeight = height;
    for(;;){
        if(outMipCount == Limit<u32>::s_Max)
            return false;
        ++outMipCount;

        if(mipWidth == 1u && mipHeight == 1u)
            return true;

        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
    }
}

[[nodiscard]] static bool ValidateTextureMipLevels(
    const Texture::MipLevelVector& mipLevels,
    const Core::Assets::AssetBytes& uastcBlocks,
    const u32 width,
    const u32 height,
    const tchar* failureContext
){
    u32 expectedMipCount = 0u;
    if(!ComputeCompleteMipCount(width, height, expectedMipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: base resolution is invalid"), failureContext);
        return false;
    }
    if(mipLevels.size() != expectedMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip count does not describe a complete chain"), failureContext);
        return false;
    }

    u32 expectedWidth = width;
    u32 expectedHeight = height;
    u64 expectedOffsetBytes = 0u;
    for(usize mipIndex = 0u; mipIndex < mipLevels.size(); ++mipIndex){
        const TextureMipLevel& mip = mipLevels[mipIndex];
        const u64 expectedBlockCountX = DivideUp(static_cast<u64>(expectedWidth), static_cast<u64>(s_UastcBlockWidth));
        const u64 expectedBlockCountY = DivideUp(static_cast<u64>(expectedHeight), static_cast<u64>(s_UastcBlockHeight));
        if(expectedBlockCountX > Limit<u32>::s_Max || expectedBlockCountY > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} block layout exceeds runtime limits"), failureContext, mipIndex);
            return false;
        }
        if(expectedBlockCountX > Limit<u64>::s_Max / expectedBlockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} block count overflows"), failureContext, mipIndex);
            return false;
        }

        const u64 expectedBlockCount = expectedBlockCountX * expectedBlockCountY;
        if(expectedBlockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} byte size overflows"), failureContext, mipIndex);
            return false;
        }
        const u64 expectedSizeBytes = expectedBlockCount * s_UastcBytesPerBlock;

        if(mip.width != expectedWidth || mip.height != expectedHeight){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} resolution is not a complete chain"), failureContext, mipIndex);
            return false;
        }
        if(mip.blockCountX != expectedBlockCountX || mip.blockCountY != expectedBlockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} block grid is invalid"), failureContext, mipIndex);
            return false;
        }
        if(mip.offsetBytes != expectedOffsetBytes || mip.sizeBytes != expectedSizeBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} is not contiguous UASTC data"), failureContext, mipIndex);
            return false;
        }
        if(expectedSizeBytes > Limit<u64>::s_Max - expectedOffsetBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mip {} byte range overflows"), failureContext, mipIndex);
            return false;
        }

        expectedOffsetBytes += expectedSizeBytes;
        expectedWidth = expectedWidth > 1u ? expectedWidth >> 1u : 1u;
        expectedHeight = expectedHeight > 1u ? expectedHeight >> 1u : 1u;
    }

    if(expectedOffsetBytes != static_cast<u64>(uastcBlocks.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: UASTC byte count does not match mip payload"), failureContext);
        return false;
    }
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
    if(m_width == 0u || m_height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an empty resolution")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }
    if(m_mipLevels.empty() || m_uastcBlocks.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::validatePayload failed: texture '{}' has an incomplete UASTC payload")
            , StringConvert(virtualPath().c_str())
        );
        return false;
    }

    return __hidden_texture_runtime::ValidateTextureMipLevels(
        m_mipLevels,
        m_uastcBlocks,
        m_width,
        m_height,
        NWB_TEXT("Texture::validatePayload")
    );
}


bool Texture::loadBinary(const Core::Assets::AssetBytes& binary){
    m_colorSpace = TextureColorSpace::Linear;
    m_hasAlpha = false;
    m_width = 0u;
    m_height = 0u;
    m_mipLevels.clear();
    m_uastcBlocks.clear();

    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: virtual path is empty"));
        return false;
    }

    usize cursor = 0u;
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

    if(header.version != TextureBinaryPayload::s_TextureVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: unsupported texture payload version"));
        return false;
    }
    if(header.reserved != 0u || header.hasAlpha > 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: invalid texture payload flags"));
        return false;
    }
    if(header.mipCount == 0u || header.uastcByteCount > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: texture payload counts exceed runtime limits"));
        return false;
    }

    Core::Assets::AssetVector<TextureBinaryPayload::MipLevelBinary> mipBinaries(m_mipLevels.get_allocator().arena());
    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        header.mipCount,
        mipBinaries,
        NWB_TEXT("Texture::loadBinary"),
        NWB_TEXT("mip levels")
    ))
        return false;

    const usize uastcByteCount = static_cast<usize>(header.uastcByteCount);
    if(!BinaryDetail::CanReadBytes(binary, cursor, uastcByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: UASTC payload is truncated"));
        return false;
    }

    MipLevelVector mipLevels(m_mipLevels.get_allocator().arena());
    mipLevels.reserve(mipBinaries.size());
    for(const TextureBinaryPayload::MipLevelBinary& mipBinary : mipBinaries){
        TextureMipLevel mip;
        mip.width = mipBinary.width;
        mip.height = mipBinary.height;
        mip.blockCountX = mipBinary.blockCountX;
        mip.blockCountY = mipBinary.blockCountY;
        mip.offsetBytes = mipBinary.offsetBytes;
        mip.sizeBytes = mipBinary.sizeBytes;
        mipLevels.push_back(mip);
    }

    Core::Assets::AssetBytes uastcBlocks(m_uastcBlocks.get_allocator().arena());
    uastcBlocks.resize(uastcByteCount);
    if(!BinaryDetail::ReadBytes(binary, cursor, uastcBlocks.data(), uastcByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture::loadBinary failed: UASTC payload is malformed"));
        return false;
    }
    if(!Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Texture::loadBinary")))
        return false;

    m_colorSpace = static_cast<TextureColorSpace::Enum>(header.colorSpace);
    m_hasAlpha = header.hasAlpha != 0u;
    m_width = header.width;
    m_height = header.height;
    m_mipLevels = Move(mipLevels);
    m_uastcBlocks = Move(uastcBlocks);
    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

