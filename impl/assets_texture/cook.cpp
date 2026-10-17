// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Texture::MipLevelVector> BuildMipLevels(const TextureCookEntry& entry, Core::Assets::AssetArena& arena){
    if(
        !IsValidTexturePayloadFormat(entry.payloadFormat) || !IsValidTextureDimension(entry.dimension) || entry.width == 0u || entry.height == 0u || entry.depth == 0u
        || (entry.dimension != TextureDimension::Texture3D && entry.depth != 1u) || (entry.dimension == TextureDimension::TextureCube && entry.width != entry.height)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture cook: invalid texture format or resolution."));
        return MakeUnexpected(Failure{});
    }
    const auto completeMipCount = TextureFormat::ComputeCompleteMipCount(entry.dimension, entry.width, entry.height, entry.depth);
    if(!completeMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture cook: resolution cannot describe a complete mip chain."));
        return MakeUnexpected(Failure{});
    }

    Texture::MipLevelVector levels(arena);
    levels.reserve(*completeMipCount);
    u32 width = entry.width;
    u32 height = entry.height;
    u32 depth = entry.depth;
    u64 offset = 0u;
    for(u32 level = 0u; level < *completeMipCount; ++level){
        const auto layout = TextureFormat::ComputeMipPlaneBlockLayout(entry.payloadFormat, width, height);
        const auto slices = TextureFormat::ComputeMipSliceCount(entry.dimension, depth);
        if(!layout || !slices || layout->planeByteCount > Limit<u64>::s_Max / *slices){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture cook: mip {} block layout exceeds supported limits."), level);
            return MakeUnexpected(Failure{});
        }
        const u64 byteCount = layout->planeByteCount * *slices;
        if(byteCount > Limit<u64>::s_Max - offset){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture cook: mip byte offsets overflow."));
            return MakeUnexpected(Failure{});
        }
        TextureMipLevel mip;
        mip.width = width;
        mip.height = height;
        mip.blockCountX = layout->blocksX;
        mip.blockCountY = layout->blocksY;
        mip.offsetBytes = offset;
        mip.sizeBytes = byteCount;
        mip.sliceCount = *slices;
        levels.push_back(mip);
        offset += byteCount;
        width = width > 1u ? width >> 1u : 1u;
        height = height > 1u ? height >> 1u : 1u;
        if(entry.dimension == TextureDimension::Texture3D)
            depth = depth > 1u ? depth >> 1u : 1u;
    }
    u64 expectedBytes = offset;
    if(entry.alphaMode == TextureAlphaMode::SeparateUastcLdr4x4){
        if(offset > Limit<u64>::s_Max - expectedBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("Texture cook: separate alpha byte count overflows."));
            return MakeUnexpected(Failure{});
        }
        expectedBytes += offset;
    }
    if(expectedBytes != static_cast<u64>(entry.payloadBytes.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("Texture cook: raw texture byte count does not match the declared mip and alpha layout."));
        return MakeUnexpected(Failure{});
    }
    return levels;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TextureAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("TextureAssetCodec::serialize")))
        return false;

    const Texture& texture = static_cast<const Texture&>(asset);
    if(!texture.validatePayload())
        return false;
    if(texture.mipLevels().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: mip count exceeds cooked payload limits"));
        return false;
    }

    usize reserveBytes = sizeof(TextureBinaryPayload::HeaderBinary);
    if(!AddBinaryRepeatedReserveBytes(reserveBytes, texture.mipLevels().size(), sizeof(TextureBinaryPayload::MipLevelBinary)) || !AddBinaryReserveBytes(reserveBytes, texture.payloadBytes().size())){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: cooked payload size overflows"));
        return false;
    }

    outBinary.clear();
    outBinary.reserve(reserveBytes);

    TextureBinaryPayload::HeaderBinary header;
    header.dimension = static_cast<u32>(texture.dimension());
    header.width = texture.width();
    header.height = texture.height();
    header.depth = texture.depth();
    header.mipCount = static_cast<u32>(texture.mipLevels().size());
    header.alphaInfo = static_cast<u32>(texture.alphaMode()) | (static_cast<u32>(texture.alphaConstantUnorm8()) << TextureBinaryPayload::s_AlphaInfoConstantShift);
    header.payloadFormat = static_cast<u32>(texture.payloadFormat());
    header.payloadByteCount = static_cast<u64>(texture.payloadBytes().size());
    AppendPOD(outBinary, header);
    for(const TextureMipLevel& mip : texture.mipLevels()){
        TextureBinaryPayload::MipLevelBinary binaryMip;
        binaryMip.width = mip.width;
        binaryMip.height = mip.height;
        binaryMip.sliceCount = mip.sliceCount;
        binaryMip.blockCountX = mip.blockCountX;
        binaryMip.blockCountY = mip.blockCountY;
        binaryMip.offsetBytes = mip.offsetBytes;
        binaryMip.sizeBytes = mip.sizeBytes;
        AppendPOD(outBinary, binaryMip);
    }

    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, texture.payloadBytes().data(), texture.payloadBytes().size());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Texture> BuildTextureAsset(const TextureCookEntry& textureEntry, Core::Assets::AssetArena& arena){
    auto mipLevels = __hidden_texture_cook::BuildMipLevels(textureEntry, arena);
    if(!mipLevels)
        return MakeUnexpected(Failure{});
    Core::Assets::AssetBytes payload(textureEntry.payloadBytes.begin(), textureEntry.payloadBytes.end(), arena);
    Texture asset(arena, textureEntry.virtualPath);
    asset.setPayload(textureEntry.width, textureEntry.height, Move(*mipLevels), Move(payload), textureEntry.dimension, textureEntry.depth, textureEntry.payloadFormat, textureEntry.alphaMode, textureEntry.alphaConstantUnorm8);
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

