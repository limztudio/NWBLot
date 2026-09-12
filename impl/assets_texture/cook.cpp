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
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Texture::s_AssetTypeText)
        );
        return false;
    }

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

    const bool writeLegacyLdrHeader = texture.payloadFormat() == TexturePayloadFormat::UastcLdr4x4;
    const usize headerByteCount = writeLegacyLdrHeader
        ? sizeof(TextureBinaryPayload::HeaderBinaryV2)
        : sizeof(TextureBinaryPayload::HeaderBinary)
    ;
    usize reserveBytes = headerByteCount;
    if(
        !AddBinaryRepeatedReserveBytes(reserveBytes, mipBinaries.size(), sizeof(TextureBinaryPayload::MipLevelBinary))
        || !AddBinaryReserveBytes(reserveBytes, texture.payloadBytes().size())
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: cooked payload size overflows"));
        return false;
    }

    outBinary.clear();
    outBinary.reserve(reserveBytes);

    if(writeLegacyLdrHeader){
        TextureBinaryPayload::HeaderBinaryV2 header;
        header.colorSpace = static_cast<u32>(texture.colorSpace());
        header.dimension = static_cast<u32>(texture.dimension());
        header.width = texture.width();
        header.height = texture.height();
        header.depth = texture.depth();
        header.mipCount = static_cast<u32>(mipBinaries.size());
        header.hasAlpha = texture.hasAlpha() ? 1u : 0u;
        header.uastcByteCount = static_cast<u64>(texture.payloadBytes().size());
        AppendPOD(outBinary, header);
    }
    else{
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
    }
    if(!Core::Assets::AppendVectorPayload(
        outBinary,
        mipBinaries,
        MakeNotNull(NWB_TEXT("TextureAssetCodec::serialize")),
        MakeNotNull(NWB_TEXT("mip levels"))
    ))
        return false;

    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, texture.payloadBytes().data(), texture.payloadBytes().size());
    return true;
}


bool BuildTextureAsset(TextureCookEntry& textureEntry, Texture& outTexture){
    Core::Assets::AssetArena& arena = textureEntry.mipLevels.get_allocator().arena();
    outTexture = Texture(arena, textureEntry.virtualPath);
    outTexture.setPayload(
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
    return outTexture.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

