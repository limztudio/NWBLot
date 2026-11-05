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
    Texture::MipLevelVector levels(arena);
    if(!BuildTextureCookMipLevels(
        static_cast<u8>(entry.payloadFormat),
        static_cast<u8>(entry.dimension),
        entry.width,
        entry.height,
        entry.depth,
        static_cast<u8>(entry.alphaMode),
        entry.payloadBytes.size(),
        levels
    ))
        return MakeUnexpected(Failure{});
    return Move(levels);
}

[[nodiscard]] static Expected<Texture> FinalizeTextureAsset(
    const TextureCookEntry& textureEntry,
    Texture::MipLevelVector&& mipLevels,
    Core::Assets::AssetBytes&& payload,
    Core::Assets::AssetArena& arena
){
    Texture asset(arena, textureEntry.virtualPath);
    asset.setPayload(
        textureEntry.width,
        textureEntry.height,
        Move(mipLevels),
        Move(payload),
        textureEntry.dimension,
        textureEntry.depth,
        textureEntry.payloadFormat,
        textureEntry.alphaMode,
        textureEntry.alphaConstantUnorm8
    );
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TextureAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
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

    AppendBytesNoReserveUnchecked(outBinary, texture.payloadBytes().data(), texture.payloadBytes().size());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Texture> BuildTextureAsset(const TextureCookEntry& textureEntry, Core::Assets::AssetArena& arena){
    auto mipLevels = __hidden_texture_cook::BuildMipLevels(textureEntry, arena);
    if(!mipLevels)
        return MakeUnexpected(Failure{});
    Core::Assets::AssetBytes payload(textureEntry.payloadBytes.begin(), textureEntry.payloadBytes.end(), arena);
    return __hidden_texture_cook::FinalizeTextureAsset(textureEntry, Move(*mipLevels), Move(payload), arena);
}

Expected<Texture> BuildTextureAsset(TextureCookEntry&& textureEntry, Core::Assets::AssetArena& arena){
    auto mipLevels = __hidden_texture_cook::BuildMipLevels(textureEntry, arena);
    if(!mipLevels)
        return MakeUnexpected(Failure{});
    Core::Assets::AssetBytes payload = &textureEntry.payloadBytes.get_allocator().arena() == &arena
        ? Move(textureEntry.payloadBytes)
        : Core::Assets::AssetBytes(textureEntry.payloadBytes.begin(), textureEntry.payloadBytes.end(), arena);
    return __hidden_texture_cook::FinalizeTextureAsset(textureEntry, Move(*mipLevels), Move(payload), arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

