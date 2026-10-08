// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_mip_decoder.h"

#include <core/common/log.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_mip_decoder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureFormat::s_UastcBlockHeight;
using TextureFormat::s_UastcBlockWidth;
using TextureFormat::s_UastcBytesPerBlock;
static constexpr u32 s_RgbaBytesPerTexel = 4u;
static constexpr u32 s_Rgba16FloatComponentCount = 4u;
static constexpr u32 s_Rgba16FloatBytesPerTexel = static_cast<u32>(sizeof(basist::half_float) * s_Rgba16FloatComponentCount);
static constexpr u32 s_Rgba16FloatAlphaByteOffset = static_cast<u32>(sizeof(basist::half_float) * (s_Rgba16FloatComponentCount - 1u));
static_assert(sizeof(basist::half_float) == sizeof(u16), "Basis HDR output must use 16-bit half components");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UastcSliceLayout{
    u64 offsetBytes = 0u;
    u64 byteCount = 0u;
    u64 blockCount = 0u;
};

struct PrimaryUastcSlice{
    BinaryByteView bytes;
    u64 blockCount = 0u;
};

[[nodiscard]] static Expected<UastcSliceLayout> GetUastcSliceLayout(
    const TextureMipLevel& mip,
    const u32 sliceIndex
){
    if(
        mip.sliceCount == 0u
        || sliceIndex >= mip.sliceCount
        || (mip.sizeBytes % mip.sliceCount) != 0u
        || mip.blockCountX == 0u
        || mip.blockCountY == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: invalid UASTC slice layout"));
        return MakeUnexpected(Failure{});
    }

    const u64 blockCountX = mip.blockCountX;
    const u64 blockCountY = mip.blockCountY;
    if(blockCountX > Limit<u64>::s_Max / blockCountY){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC block count exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }
    const u64 blockCount = blockCountX * blockCountY;
    if(blockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC block byte count exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }

    const u64 sliceByteCount = mip.sizeBytes / mip.sliceCount;
    if(sliceByteCount != blockCount * s_UastcBytesPerBlock){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC slice does not match its block layout"));
        return MakeUnexpected(Failure{});
    }
    if(sliceIndex != 0u && sliceByteCount > Limit<u64>::s_Max / sliceIndex){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC slice offset exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }

    return UastcSliceLayout{ sliceByteCount * sliceIndex, sliceByteCount, blockCount };
}

[[nodiscard]] static Expected<PrimaryUastcSlice> GetPrimaryUastcSlice(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex
){
    const auto layout = GetUastcSliceLayout(mip, sliceIndex);
    if(!layout)
        return MakeUnexpected(layout.error());
    if(mip.offsetBytes > Limit<u64>::s_Max - layout->offsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC primary slice offset exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }

    const u64 sourceOffsetBytes = mip.offsetBytes + layout->offsetBytes;
    const u64 primaryPayloadByteCount = textureAsset.primaryPayloadByteCount();
    if(
        primaryPayloadByteCount > textureAsset.payloadBytes().size()
        || sourceOffsetBytes > primaryPayloadByteCount
        || layout->byteCount > primaryPayloadByteCount - sourceOffsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC primary slice is outside the texture payload"));
        return MakeUnexpected(Failure{});
    }

    return PrimaryUastcSlice{
        { textureAsset.payloadBytes().data() + static_cast<usize>(sourceOffsetBytes), static_cast<usize>(layout->byteCount) },
        layout->blockCount
    };
}

[[nodiscard]] static Expected<BinaryByteView> GetSeparateAlphaUastcSlice(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex
){
    if(textureAsset.alphaMode() != TextureAlphaMode::SeparateUastcLdr4x4){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: texture does not have a separate UASTC alpha stream"));
        return MakeUnexpected(Failure{});
    }

    const auto layout = GetUastcSliceLayout(mip, sliceIndex);
    if(!layout)
        return MakeUnexpected(layout.error());
    const u64 primaryPayloadByteCount = textureAsset.primaryPayloadByteCount();
    const u8* const alphaBlocks = textureAsset.alphaUastcBlocks();
    if(mip.offsetBytes > Limit<u64>::s_Max - layout->offsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha slice offset exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }
    const u64 alphaSliceOffsetBytes = mip.offsetBytes + layout->offsetBytes;
    if(
        !alphaBlocks
        || primaryPayloadByteCount > textureAsset.payloadBytes().size()
        || primaryPayloadByteCount > textureAsset.payloadBytes().size() - primaryPayloadByteCount
        || alphaSliceOffsetBytes > primaryPayloadByteCount
        || layout->byteCount > primaryPayloadByteCount - alphaSliceOffsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha slice is outside the texture payload"));
        return MakeUnexpected(Failure{});
    }

    return BinaryByteView{ alphaBlocks + static_cast<usize>(alphaSliceOffsetBytes), static_cast<usize>(layout->byteCount) };
}

[[nodiscard]] static bool DecodeTextureSliceAsAstc(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    u8* const outUploadBytes,
    const usize uploadByteCount
){
    const auto source = outUploadBytes
        ? GetPrimaryUastcSlice(textureAsset, mip, sliceIndex)
        : Expected<PrimaryUastcSlice>{ MakeUnexpected(Failure{}) }
    ;
    if(
        !outUploadBytes
        || !source
        || source->bytes.size() != uploadByteCount
        || source->blockCount * s_UastcBytesPerBlock != uploadByteCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: ASTC UASTC slice size is invalid"));
        return false;
    }

    for(u64 blockOffset = 0u; blockOffset < source->bytes.size(); blockOffset += s_UastcBytesPerBlock){
        basist::uastc_block sourceBlock;
        NWB_MEMCPY(
            &sourceBlock,
            sizeof(sourceBlock),
            source->bytes.data() + static_cast<usize>(blockOffset),
            sizeof(sourceBlock)
        );
        if(!basist::transcode_uastc_to_astc(sourceBlock, outUploadBytes + static_cast<usize>(blockOffset))){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC-to-ASTC transcoding failed"));
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool DecodeTextureSliceAsBc7(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const u32 sliceIndex,
    u8* const outUploadBytes,
    const usize uploadByteCount
){
    const auto source = outUploadBytes
        ? GetPrimaryUastcSlice(textureAsset, mip, sliceIndex)
        : Expected<PrimaryUastcSlice>{ MakeUnexpected(Failure{}) }
    ;
    if(
        !outUploadBytes
        || !source
        || source->bytes.size() > Limit<u32>::s_Max
        || source->blockCount > Limit<u32>::s_Max
        || source->blockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock
        || source->blockCount * s_UastcBytesPerBlock != uploadByteCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: BC7 UASTC slice size is invalid"));
        return false;
    }

    basist::basisu_lowlevel_uastc_ldr_4x4_transcoder transcoder;
    if(!transcoder.transcode_image(
        basist::transcoder_texture_format::cTFBC7_RGBA,
        outUploadBytes,
        static_cast<u32>(source->blockCount),
        source->bytes.data(),
        static_cast<u32>(source->bytes.size()),
        mip.blockCountX,
        mip.blockCountY,
        mip.width,
        mip.height,
        mipLevel,
        0u,
        static_cast<u32>(source->bytes.size()),
        0u,
        true,
        false,
        mip.blockCountX,
        nullptr,
        mip.height
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC-to-BC7 transcoding failed"));
        return false;
    }
    return true;
}

template<typename StoreTexelT>
[[nodiscard]] static bool VisitDecodedUastcTexels(
    const TextureMipLevel& mip,
    const u8* const sourceData,
    const bool srgb,
    StoreTexelT&& storeTexel
){
    for(u32 blockY = 0u; blockY < mip.blockCountY; ++blockY){
        for(u32 blockX = 0u; blockX < mip.blockCountX; ++blockX){
            const u64 blockIndex = static_cast<u64>(blockY) * static_cast<u64>(mip.blockCountX) + blockX;
            const u64 blockOffset = blockIndex * s_UastcBytesPerBlock;
            basist::uastc_block sourceBlock;
            NWB_MEMCPY(
                &sourceBlock,
                sizeof(sourceBlock),
                sourceData + static_cast<usize>(blockOffset),
                sizeof(sourceBlock)
            );

            basist::color32 decodedTexels[s_UastcBlockWidth * s_UastcBlockHeight];
            if(!basist::unpack_uastc(sourceBlock, decodedTexels, srgb))
                return false;

            for(u32 localY = 0u; localY < s_UastcBlockHeight; ++localY){
                const u64 destinationY = static_cast<u64>(blockY) * s_UastcBlockHeight + localY;
                if(destinationY >= mip.height)
                    break;

                for(u32 localX = 0u; localX < s_UastcBlockWidth; ++localX){
                    const u64 destinationX = static_cast<u64>(blockX) * s_UastcBlockWidth + localX;
                    if(destinationX >= mip.width)
                        break;

                    const usize sourceTexelIndex = static_cast<usize>(localY * s_UastcBlockWidth + localX);
                    const usize destinationTexelIndex = static_cast<usize>(destinationY * static_cast<u64>(mip.width) + destinationX);
                    storeTexel(decodedTexels[sourceTexelIndex], destinationTexelIndex);
                }
            }
        }
    }
    return true;
}

[[nodiscard]] static bool DecodeTextureSliceAsRgba(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    u8* const outUploadBytes,
    const usize uploadByteCount
){
    const u64 texelCount = static_cast<u64>(mip.width) * static_cast<u64>(mip.height);
    if(texelCount > Limit<usize>::s_Max / s_RgbaBytesPerTexel){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: RGBA8 fallback mip size exceeds addressable memory"));
        return false;
    }
    const u64 expectedUploadByteCount = texelCount * s_RgbaBytesPerTexel;
    const auto source = outUploadBytes && expectedUploadByteCount == uploadByteCount
        ? GetPrimaryUastcSlice(textureAsset, mip, sliceIndex)
        : Expected<PrimaryUastcSlice>{ MakeUnexpected(Failure{}) }
    ;
    if(
        !outUploadBytes
        || expectedUploadByteCount != uploadByteCount
        || !source
        || source->bytes.size() != source->blockCount * s_UastcBytesPerBlock
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: invalid RGBA8 UASTC slice layout"));
        return false;
    }

    const bool srgb = textureAsset.colorSpace() == TextureColorSpace::Srgb;
    if(!VisitDecodedUastcTexels(mip, source->bytes.data(), srgb, [outUploadBytes](const basist::color32& sourceTexel, const usize destinationTexelIndex){
        const usize destinationByteOffset = destinationTexelIndex * s_RgbaBytesPerTexel;
        outUploadBytes[destinationByteOffset + 0u] = sourceTexel.r;
        outUploadBytes[destinationByteOffset + 1u] = sourceTexel.g;
        outUploadBytes[destinationByteOffset + 2u] = sourceTexel.b;
        outUploadBytes[destinationByteOffset + 3u] = sourceTexel.a;
    })){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC-to-RGBA8 decoding failed"));
        return false;
    }
    return true;
}

[[nodiscard]] static bool DecodeHdrTextureSlice(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const u32 sliceIndex,
    const Core::Format::Enum format,
    u8* const outUploadBytes,
    const usize uploadByteCount
){
    const auto source = outUploadBytes
        ? GetPrimaryUastcSlice(textureAsset, mip, sliceIndex)
        : Expected<PrimaryUastcSlice>{ MakeUnexpected(Failure{}) }
    ;
    if(!outUploadBytes || !source)
        return false;

    basist::transcoder_texture_format targetFormat = basist::transcoder_texture_format::cTFRGBA_HALF;
    bool compressedOutput = false;
    switch(format){
    case Core::Format::ASTC_4x4_FLOAT:
        targetFormat = basist::transcoder_texture_format::cTFASTC_HDR_4x4_RGBA;
        compressedOutput = true;
        break;
    case Core::Format::BC6H_UFLOAT:
        targetFormat = basist::transcoder_texture_format::cTFBC6H;
        compressedOutput = true;
        break;
    case Core::Format::RGBA16_FLOAT:
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported HDR UASTC upload format"));
        return false;
    }

    const u64 texelCount = static_cast<u64>(mip.width) * static_cast<u64>(mip.height);
    const u64 outputElementCount = compressedOutput ? source->blockCount : texelCount;
    const u64 bytesPerElement = compressedOutput ? s_UastcBytesPerBlock : s_Rgba16FloatBytesPerTexel;
    if(
        source->bytes.size() > Limit<u32>::s_Max
        || outputElementCount > Limit<u32>::s_Max
        || outputElementCount > Limit<u64>::s_Max / bytesPerElement
        || outputElementCount * bytesPerElement != uploadByteCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR UASTC slice exceeds Basis transcoder limits"));
        return false;
    }

    basist::basisu_lowlevel_uastc_hdr_4x4_transcoder transcoder;
    if(!transcoder.transcode_image(
        targetFormat,
        outUploadBytes,
        static_cast<u32>(outputElementCount),
        source->bytes.data(),
        static_cast<u32>(source->bytes.size()),
        mip.blockCountX,
        mip.blockCountY,
        mip.width,
        mip.height,
        mipLevel,
        0u,
        static_cast<u32>(source->bytes.size()),
        0u,
        false,
        false,
        compressedOutput ? mip.blockCountX : mip.width,
        nullptr,
        mip.height
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR UASTC transcoding failed"));
        return false;
    }
    return true;
}

static void StoreHdrAlpha(
    u8* const outRgba16FloatBytes,
    const usize texelIndex,
    const u8 alphaUnorm8
){
    const basist::half_float alphaHalf = basist::float_to_half(
        static_cast<float>(alphaUnorm8) / static_cast<float>(Limit<u8>::s_Max)
    );
    u8* const destination = outRgba16FloatBytes
        + texelIndex * s_Rgba16FloatBytesPerTexel
        + s_Rgba16FloatAlphaByteOffset
    ;
    NWB_MEMCPY(destination, sizeof(alphaHalf), &alphaHalf, sizeof(alphaHalf));
}

[[nodiscard]] static bool MergeHdrAlpha(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    u8* const outRgba16FloatBytes,
    const usize uploadByteCount
){
    const u64 texelCount = static_cast<u64>(mip.width) * static_cast<u64>(mip.height);
    if(
        !outRgba16FloatBytes
        || texelCount > Limit<usize>::s_Max / s_Rgba16FloatBytesPerTexel
        || texelCount * s_Rgba16FloatBytesPerTexel != uploadByteCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR RGBA16_FLOAT alpha merge layout is invalid"));
        return false;
    }

    if(textureAsset.alphaMode() != TextureAlphaMode::SeparateUastcLdr4x4){
        const u8 alphaUnorm8 = textureAsset.alphaMode() == TextureAlphaMode::ConstantUnorm8
            ? textureAsset.alphaConstantUnorm8()
            : static_cast<u8>(Limit<u8>::s_Max)
        ;
        for(usize texelIndex = 0u; texelIndex < static_cast<usize>(texelCount); ++texelIndex)
            StoreHdrAlpha(outRgba16FloatBytes, texelIndex, alphaUnorm8);
        return true;
    }

    const auto alphaSource = GetSeparateAlphaUastcSlice(textureAsset, mip, sliceIndex);
    if(!alphaSource)
        return false;
    const u64 expectedAlphaSourceByteCount = static_cast<u64>(mip.blockCountX) * mip.blockCountY * s_UastcBytesPerBlock;
    if(alphaSource->size() != expectedAlphaSourceByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha slice does not match its block layout"));
        return false;
    }

    if(!VisitDecodedUastcTexels(mip, alphaSource->data(), false, [outRgba16FloatBytes](const basist::color32& sourceTexel, const usize destinationTexelIndex){
        // The companion stream is a grayscale LDR mask: (a, a, a, 255).
        StoreHdrAlpha(outRgba16FloatBytes, destinationTexelIndex, sourceTexel.r);
    })){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha decoding failed"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextureMipDecoder::TextureMipDecoder()noexcept
{}


Expected<TextureDecodedMipUpload> TextureMipDecoder::decode(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const Core::Format::Enum format,
    Core::Alloc::ScratchArena& arena
){
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4)
        return decodeLdr(textureAsset, mip, mipLevel, format, arena);
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4)
        return decodeHdr(textureAsset, mip, mipLevel, format, arena);

    NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported texture payload format"));
    return MakeUnexpected(Failure{});
}


Expected<TextureDecodedMipUpload> TextureMipDecoder::decodeLdr(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const Core::Format::Enum format,
    Core::Alloc::ScratchArena& arena
){
    usize rowPitch = 0u;
    usize sliceUploadByteCount = 0u;
    if(Core::Format::IsLdrCompressedFormat(format)){
        const u64 rowPitch64 = static_cast<u64>(mip.blockCountX) * TextureFormat::s_UastcBytesPerBlock;
        if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / mip.blockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: compressed LDR mip row pitch exceeds addressable memory"));
            return MakeUnexpected(Failure{});
        }
        rowPitch = static_cast<usize>(rowPitch64);
        sliceUploadByteCount = static_cast<usize>(rowPitch64 * mip.blockCountY);
    }
    else{
        const u64 rowPitch64 = static_cast<u64>(mip.width) * __hidden_texture_mip_decoder::s_RgbaBytesPerTexel;
        if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / mip.height){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: RGBA8 mip row pitch exceeds addressable memory"));
            return MakeUnexpected(Failure{});
        }
        rowPitch = static_cast<usize>(rowPitch64);
        sliceUploadByteCount = static_cast<usize>(rowPitch64 * mip.height);
    }

    if(mip.sliceCount == 0u || sliceUploadByteCount > Limit<usize>::s_Max / mip.sliceCount){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: texture mip upload size exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }
    TextureDecodedMipUpload upload(arena);
    upload.bytes.resize(sliceUploadByteCount * mip.sliceCount);
    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        u8* const destination = upload.bytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount;
        bool decoded = false;
        if(Core::Format::IsAstc4x4LdrFormat(format))
            decoded = __hidden_texture_mip_decoder::DecodeTextureSliceAsAstc(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount);
        else if(Core::Format::IsBc7LdrFormat(format))
            decoded = __hidden_texture_mip_decoder::DecodeTextureSliceAsBc7(textureAsset, mip, mipLevel, sliceIndex, destination, sliceUploadByteCount);
        else
            decoded = __hidden_texture_mip_decoder::DecodeTextureSliceAsRgba(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount);
        if(!decoded)
            return MakeUnexpected(Failure{});
    }
    upload.rowPitch = rowPitch;
    upload.sliceByteCount = sliceUploadByteCount;
    return upload;
}

Expected<TextureDecodedMipUpload> TextureMipDecoder::decodeHdr(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const Core::Format::Enum format,
    Core::Alloc::ScratchArena& arena
){
    const bool compressedOutput = Core::Format::IsHdrCompressedFormat(format);
    if(!compressedOutput && format != Core::Format::RGBA16_FLOAT){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported HDR texture upload format"));
        return MakeUnexpected(Failure{});
    }

    const u64 rowPitch64 = compressedOutput
        ? static_cast<u64>(mip.blockCountX) * TextureFormat::s_UastcBytesPerBlock
        : static_cast<u64>(mip.width) * __hidden_texture_mip_decoder::s_Rgba16FloatBytesPerTexel
    ;
    const u32 rowCount = compressedOutput ? mip.blockCountY : mip.height;
    if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / rowCount){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR mip row pitch exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }
    const u64 sliceUploadByteCount64 = rowPitch64 * rowCount;
    if(
        mip.sliceCount == 0u
        || sliceUploadByteCount64 > Limit<usize>::s_Max
        || sliceUploadByteCount64 > Limit<usize>::s_Max / mip.sliceCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR mip upload size exceeds addressable memory"));
        return MakeUnexpected(Failure{});
    }

    const usize rowPitch = static_cast<usize>(rowPitch64);
    const usize sliceUploadByteCount = static_cast<usize>(sliceUploadByteCount64);
    TextureDecodedMipUpload upload(arena);
    upload.bytes.resize(sliceUploadByteCount * mip.sliceCount);
    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        u8* const destination = upload.bytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount;
        if(!__hidden_texture_mip_decoder::DecodeHdrTextureSlice(textureAsset, mip, mipLevel, sliceIndex, format, destination, sliceUploadByteCount))
            return MakeUnexpected(Failure{});
        if(
            format == Core::Format::RGBA16_FLOAT
            && !__hidden_texture_mip_decoder::MergeHdrAlpha(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount)
        )
            return MakeUnexpected(Failure{});
    }
    upload.rowPitch = rowPitch;
    upload.sliceByteCount = sliceUploadByteCount;
    return upload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

