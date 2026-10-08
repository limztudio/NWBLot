// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/binary.h>
#include <global/filesystem.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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

    Core::Assets::AssetVector<TextureBinaryPayload::MipLevelBinary> mipBinaries(outBinary.get_allocator().arena());
    mipBinaries.reserve(texture.mipLevels().size());
    for(const TextureMipLevel& mip : texture.mipLevels()){
        TextureBinaryPayload::MipLevelBinary binaryMip;
        binaryMip.width = mip.width;
        binaryMip.height = mip.height;
        binaryMip.sliceCount = mip.sliceCount;
        binaryMip.blockCountX = mip.blockCountX;
        binaryMip.blockCountY = mip.blockCountY;
        binaryMip.offsetBytes = mip.offsetBytes;
        binaryMip.sizeBytes = mip.sizeBytes;
        mipBinaries.push_back(binaryMip);
    }

    usize reserveBytes = sizeof(TextureBinaryPayload::HeaderBinary);
    if(
        !AddBinaryRepeatedReserveBytes(reserveBytes, mipBinaries.size(), sizeof(TextureBinaryPayload::MipLevelBinary))
        || !AddBinaryReserveBytes(reserveBytes, texture.payloadBytes().size())
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: cooked payload size overflows"));
        return false;
    }

    outBinary.clear();
    outBinary.reserve(reserveBytes);

    TextureBinaryPayload::HeaderBinary header;
    header.colorSpace = static_cast<u32>(texture.colorSpace());
    header.dimension = static_cast<u32>(texture.dimension());
    header.width = texture.width();
    header.height = texture.height();
    header.depth = texture.depth();
    header.mipCount = static_cast<u32>(mipBinaries.size());
    header.alphaInfo = static_cast<u32>(texture.alphaMode())
        | (static_cast<u32>(texture.alphaConstantUnorm8()) << TextureBinaryPayload::s_AlphaInfoConstantShift)
    ;
    header.payloadFormat = static_cast<u32>(texture.payloadFormat());
    header.payloadByteCount = static_cast<u64>(texture.payloadBytes().size());
    AppendPOD(outBinary, header);
    if(!Core::Assets::AppendVectorPayload(
        outBinary,
        mipBinaries,
        NWB_TEXT("TextureAssetCodec::serialize"),
        NWB_TEXT("mip levels")
    ))
        return false;

    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, texture.payloadBytes().data(), texture.payloadBytes().size());
    return true;
}


Expected<Texture> BuildTextureAsset(TextureCookEntry& textureEntry, Core::Assets::AssetArena& arena){
    Texture asset(arena, textureEntry.virtualPath);
    asset.setPayload(
        textureEntry.colorSpace,
        textureEntry.hasAlpha,
        textureEntry.width,
        textureEntry.height,
        Move(textureEntry.mipLevels),
        Move(textureEntry.payloadBytes),
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

