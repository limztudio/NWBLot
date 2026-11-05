// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_mip_decoder.h"

#include <core/common/log.h>

#include <global/span.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_mip_decoder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureFormat::s_UastcBlockHeight;
using TextureFormat::s_UastcBlockWidth;
using TextureFormat::s_UastcBytesPerBlock;
static_assert(sizeof(basist::half_float) == sizeof(u16), "Basis HDR output must use 16-bit half components");
static_assert(s_TextureRgba16FloatBytesPerTexel == static_cast<u32>(sizeof(basist::half_float) * s_TextureRgba16FloatComponentCount));
static_assert(s_TextureRgba16FloatAlphaByteOffset == static_cast<u32>(sizeof(basist::half_float) * (s_TextureRgba16FloatComponentCount - 1u)));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 DecodeTextureSliceAsAstc(const Texture& textureAsset, const TextureMipLevel& mip, const u32 sliceIndex, const Span<u8> outUploadBytes){
    const auto source = TryGetPrimaryUastcSlice(textureAsset.payloadBytes(), textureAsset.primaryPayloadByteCount(), mip, sliceIndex);
    if(!source || source->bytes.size() != outUploadBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: ASTC UASTC slice size is invalid"));
        return false;
    }

    for(u64 blockOffset = 0u; blockOffset < source->bytes.size(); blockOffset += s_UastcBytesPerBlock){
        basist::uastc_block sourceBlock;
        NWB_MEMCPY(&sourceBlock, sizeof(sourceBlock), source->bytes.data() + static_cast<usize>(blockOffset), sizeof(sourceBlock));
        if(!basist::transcode_uastc_to_astc(sourceBlock, outUploadBytes.data() + static_cast<usize>(blockOffset))){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC-to-ASTC transcoding failed"));
            return false;
        }
    }
    return true;
}

[[nodiscard]] static b8 DecodeTextureSliceAsBc7(const Texture& textureAsset, const TextureMipLevel& mip, const u32 mipLevel, const u32 sliceIndex, const Span<u8> outUploadBytes){
    const auto source = TryGetPrimaryUastcSlice(textureAsset.payloadBytes(), textureAsset.primaryPayloadByteCount(), mip, sliceIndex);
    if(!source){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: BC7 UASTC slice size is invalid"));
        return false;
    }
    if(!ValidateBc7UastcTranscodeLayout(*source, outUploadBytes.size()))
        return false;

    basist::basisu_lowlevel_uastc_ldr_4x4_transcoder transcoder;
    if(!transcoder.transcode_image(
        basist::transcoder_texture_format::cTFBC7_RGBA,
        outUploadBytes.data(),
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
[[nodiscard]] static b8 VisitDecodedUastcTexels(const TextureMipLevel& mip, const BinaryByteView sourceBytes, StoreTexelT&& storeTexel){
    for(u32 blockY = 0u; blockY < mip.blockCountY; ++blockY){
        for(u32 blockX = 0u; blockX < mip.blockCountX; ++blockX){
            const u64 blockIndex = static_cast<u64>(blockY) * static_cast<u64>(mip.blockCountX) + blockX;
            const u64 blockOffset = blockIndex * s_UastcBytesPerBlock;
            basist::uastc_block sourceBlock;
            NWB_MEMCPY(&sourceBlock, sizeof(sourceBlock), sourceBytes.data() + static_cast<usize>(blockOffset), sizeof(sourceBlock));

            Array<basist::color32, s_UastcBlockWidth * s_UastcBlockHeight> decodedTexels{};
            if(!basist::unpack_uastc(sourceBlock, decodedTexels.data(), false))
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

[[nodiscard]] static b8 DecodeTextureSliceAsRgba(const Texture& textureAsset, const TextureMipLevel& mip, const u32 sliceIndex, const Span<u8> outUploadBytes){
    const u64 texelCount = static_cast<u64>(mip.width) * static_cast<u64>(mip.height);
    if(texelCount > Limit<usize>::s_Max / s_TextureRgbaBytesPerTexel){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: RGBA8 fallback mip size exceeds addressable memory"));
        return false;
    }
    const u64 expectedUploadByteCount = texelCount * s_TextureRgbaBytesPerTexel;
    const auto source = expectedUploadByteCount == outUploadBytes.size()
        ? TryGetPrimaryUastcSlice(textureAsset.payloadBytes(), textureAsset.primaryPayloadByteCount(), mip, sliceIndex)
        : Expected<PrimaryUastcSlice>{ MakeUnexpected(Failure{}) };
    if(expectedUploadByteCount != outUploadBytes.size() || !source){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: invalid RGBA8 UASTC slice layout"));
        return false;
    }
    NWB_ASSERT(source->bytes.size() == source->blockCount * s_UastcBytesPerBlock);

    if(!VisitDecodedUastcTexels(mip, BinaryByteView{ source->bytes.data(), source->bytes.size() }, [outUploadBytes](const basist::color32& sourceTexel, const usize destinationTexelIndex)noexcept{
        const usize destinationByteOffset = destinationTexelIndex * s_TextureRgbaBytesPerTexel;
        outUploadBytes[destinationByteOffset + 0u] = sourceTexel.r;
        outUploadBytes[destinationByteOffset + 1u] = sourceTexel.g;
        outUploadBytes[destinationByteOffset + 2u] = sourceTexel.b;
        outUploadBytes[destinationByteOffset + 3u] = sourceTexel.a;
    }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC-to-RGBA8 decoding failed"));
        return false;
    }
    return true;
}

[[nodiscard]] static b8 DecodeHdrTextureSlice(const Texture& textureAsset, const TextureMipLevel& mip, const u32 mipLevel, const u32 sliceIndex, const Core::Format::Enum format, const Span<u8> outUploadBytes){
    const auto source = TryGetPrimaryUastcSlice(textureAsset.payloadBytes(), textureAsset.primaryPayloadByteCount(), mip, sliceIndex);
    if(!source)
        return false;

    basist::transcoder_texture_format targetFormat = basist::transcoder_texture_format::cTFRGBA_HALF;
    b8 compressedOutput = false;
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

    u32 outputElementCount = 0u;
    u32 outputRowBlocksOrPixels = 0u;
    if(!ValidateHdrUastcTranscodeLayout(mip, *source, compressedOutput, outUploadBytes.size(), outputElementCount, outputRowBlocksOrPixels))
        return false;

    basist::basisu_lowlevel_uastc_hdr_4x4_transcoder transcoder;
    if(!transcoder.transcode_image(
        targetFormat,
        outUploadBytes.data(),
        outputElementCount,
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
        outputRowBlocksOrPixels,
        nullptr,
        mip.height
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR UASTC transcoding failed"));
        return false;
    }
    return true;
}

static void StoreHdrAlpha(const usize texelIndex, const u8 alphaUnorm8, const Span<u8> outRgba16FloatBytes)noexcept{
    const basist::half_float alphaHalf = basist::float_to_half(static_cast<f32>(alphaUnorm8) / static_cast<f32>(Limit<u8>::s_Max));
    u8* const destination = outRgba16FloatBytes.data() + texelIndex * s_TextureRgba16FloatBytesPerTexel + s_TextureRgba16FloatAlphaByteOffset;
    NWB_MEMCPY(destination, sizeof(alphaHalf), &alphaHalf, sizeof(alphaHalf));
}

[[nodiscard]] static b8 MergeHdrAlpha(const Texture& textureAsset, const TextureMipLevel& mip, const u32 sliceIndex, const Span<u8> outRgba16FloatBytes){
    usize texelCount = 0u;
    if(!ValidateHdrRgba16FloatAlphaMergeLayout(mip, outRgba16FloatBytes.size(), texelCount))
        return false;

    if(textureAsset.alphaMode() != TextureAlphaMode::SeparateUastcLdr4x4){
        const u8 alphaUnorm8 = textureAsset.alphaMode() == TextureAlphaMode::ConstantUnorm8 ? textureAsset.alphaConstantUnorm8() : static_cast<u8>(Limit<u8>::s_Max);
        for(usize texelIndex = 0u; texelIndex < texelCount; ++texelIndex)
            StoreHdrAlpha(texelIndex, alphaUnorm8, outRgba16FloatBytes);
        return true;
    }

    const auto alphaSource = TryGetSeparateAlphaUastcSlice(
        textureAsset.payloadBytes(),
        textureAsset.primaryPayloadByteCount(),
        textureAsset.alphaMode() == TextureAlphaMode::SeparateUastcLdr4x4 && textureAsset.alphaUastcBlocks() != nullptr,
        mip,
        sliceIndex
    );
    if(!alphaSource)
        return false;
    NWB_ASSERT(alphaSource->size() == static_cast<u64>(mip.blockCountX) * mip.blockCountY * s_UastcBytesPerBlock);

    if(!VisitDecodedUastcTexels(mip, BinaryByteView{ alphaSource->data(), alphaSource->size() }, [outRgba16FloatBytes](const basist::color32& sourceTexel, const usize destinationTexelIndex)noexcept{
        // The companion stream is a grayscale LDR mask: (a, a, a, 255).
        StoreHdrAlpha(destinationTexelIndex, sourceTexel.r, outRgba16FloatBytes);
    }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha decoding failed"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureDecodedMipUpload> TextureMipDecoder::decode(const Texture& textureAsset, const TextureMipLevel& mip, const u32 mipLevel, const Core::Format::Enum format, Core::Alloc::ScratchArena& arena){
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4)
        return decodeLdr(textureAsset, mip, mipLevel, format, arena);
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4)
        return decodeHdr(textureAsset, mip, mipLevel, format, arena);

    NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported texture payload format"));
    return MakeUnexpected(Failure{});
}


Expected<TextureDecodedMipUpload> TextureMipDecoder::decodeLdr(const Texture& textureAsset, const TextureMipLevel& mip, const u32 mipLevel, const Core::Format::Enum format, Core::Alloc::ScratchArena& arena){
    if(format != Core::Format::ASTC_4x4_UNORM && format != Core::Format::BC7_UNORM && format != Core::Format::RGBA8_UNORM){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported linear LDR texture upload format"));
        return MakeUnexpected(Failure{});
    }

    usize rowPitch = 0u;
    usize sliceUploadByteCount = 0u;
    usize totalUploadByteCount = 0u;
    if(!ValidateAndComputeLdrMipUploadLayout(mip, format == Core::Format::RGBA8_UNORM, rowPitch, sliceUploadByteCount, totalUploadByteCount))
        return MakeUnexpected(Failure{});
    TextureDecodedMipUpload upload(arena);
    upload.bytes.resize(totalUploadByteCount);
    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        const Span<u8> destination(upload.bytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount, sliceUploadByteCount);
        b8 decoded = false;
        if(format == Core::Format::ASTC_4x4_UNORM)
            decoded = __hidden_texture_mip_decoder::DecodeTextureSliceAsAstc(textureAsset, mip, sliceIndex, destination);
        else if(format == Core::Format::BC7_UNORM)
            decoded = __hidden_texture_mip_decoder::DecodeTextureSliceAsBc7(textureAsset, mip, mipLevel, sliceIndex, destination);
        else
            decoded = __hidden_texture_mip_decoder::DecodeTextureSliceAsRgba(textureAsset, mip, sliceIndex, destination);
        if(!decoded)
            return MakeUnexpected(Failure{});
    }
    upload.rowPitch = rowPitch;
    upload.sliceByteCount = sliceUploadByteCount;
    return upload;
}

Expected<TextureDecodedMipUpload> TextureMipDecoder::decodeHdr(const Texture& textureAsset, const TextureMipLevel& mip, const u32 mipLevel, const Core::Format::Enum format, Core::Alloc::ScratchArena& arena){
    const b8 compressedOutput = Core::Format::IsHdrCompressedFormat(format);
    if(!compressedOutput && format != Core::Format::RGBA16_FLOAT){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported HDR texture upload format"));
        return MakeUnexpected(Failure{});
    }

    usize rowPitch = 0u;
    usize sliceUploadByteCount = 0u;
    usize totalUploadByteCount = 0u;
    if(!ValidateAndComputeHdrMipUploadLayout(mip, compressedOutput, rowPitch, sliceUploadByteCount, totalUploadByteCount))
        return MakeUnexpected(Failure{});

    TextureDecodedMipUpload upload(arena);
    upload.bytes.resize(totalUploadByteCount);
    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        const Span<u8> destination(upload.bytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount, sliceUploadByteCount);
        if(!__hidden_texture_mip_decoder::DecodeHdrTextureSlice(textureAsset, mip, mipLevel, sliceIndex, format, destination))
            return MakeUnexpected(Failure{});
        if(format == Core::Format::RGBA16_FLOAT && !__hidden_texture_mip_decoder::MergeHdrAlpha(textureAsset, mip, sliceIndex, destination))
            return MakeUnexpected(Failure{});
    }
    upload.rowPitch = rowPitch;
    upload.sliceByteCount = sliceUploadByteCount;
    return upload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

