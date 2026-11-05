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

    return SerializeTextureBinaryPayload(
        static_cast<u8>(texture.dimension()),
        texture.width(),
        texture.height(),
        texture.depth(),
        static_cast<u8>(texture.alphaMode()),
        texture.alphaConstantUnorm8(),
        static_cast<u8>(texture.payloadFormat()),
        texture.mipLevels(),
        texture.payloadBytes(),
        outBinary
    );
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

