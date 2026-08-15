// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "loader.h"

#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <global/sync.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureFormat::s_UastcBlockHeight;
using TextureFormat::s_UastcBlockWidth;
using TextureFormat::s_UastcBytesPerBlock;
static constexpr u32 s_RgbaBytesPerTexel = 4u;
static constexpr u32 s_Rgba16FloatComponentCount = 4u;
static constexpr u32 s_Rgba16FloatBytesPerTexel = static_cast<u32>(sizeof(basist::half_float) * s_Rgba16FloatComponentCount);
static constexpr u32 s_Rgba16FloatAlphaByteOffset = static_cast<u32>(sizeof(basist::half_float) * (s_Rgba16FloatComponentCount - 1u));
static_assert(sizeof(basist::half_float) == sizeof(u16), "Basis HDR output must use 16-bit half components");
static constexpr Core::FormatSupport::Mask s_RequiredTextureFormatSupport =
    Core::FormatSupport::Texture
    | Core::FormatSupport::ShaderSample
;

Futex s_BasisTranscoderInitializationMutex;
Atomic<bool> s_BasisTranscoderInitialized = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void InitializeBasisTranscoder(){
    if(s_BasisTranscoderInitialized.load(MemoryOrder::acquire))
        return;

    ScopedLock lock(s_BasisTranscoderInitializationMutex);
    if(s_BasisTranscoderInitialized.load(MemoryOrder::acquire))
        return;

    basist::basisu_transcoder_init();
    s_BasisTranscoderInitialized.store(true, MemoryOrder::release);
}

[[nodiscard]] static bool SupportsTextureFormat(Core::Device& device, const Core::Format::Enum format){
    return (device.queryFormatSupport(format) & s_RequiredTextureFormatSupport) == s_RequiredTextureFormatSupport;
}

[[nodiscard]] static Core::Format::Enum SelectUploadFormat(
    Core::Device& device,
    const Texture& textureAsset
){
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4){
        if(textureAsset.alphaMode() == TextureAlphaMode::Opaque){
            if(SupportsTextureFormat(device, Core::Format::ASTC_4x4_FLOAT))
                return Core::Format::ASTC_4x4_FLOAT;
            if(SupportsTextureFormat(device, Core::Format::BC6H_UFLOAT))
                return Core::Format::BC6H_UFLOAT;
        }
        return SupportsTextureFormat(device, Core::Format::RGBA16_FLOAT) ? Core::Format::RGBA16_FLOAT : Core::Format::UNKNOWN;
    }
    if(textureAsset.payloadFormat() != TexturePayloadFormat::UastcLdr4x4)
        return Core::Format::UNKNOWN;

    const Core::Format::Enum astcFormat = textureAsset.colorSpace() == TextureColorSpace::Srgb
        ? Core::Format::ASTC_4x4_UNORM_SRGB
        : Core::Format::ASTC_4x4_UNORM
    ;
    if(SupportsTextureFormat(device, astcFormat))
        return astcFormat;

    const Core::Format::Enum bcFormat = textureAsset.colorSpace() == TextureColorSpace::Srgb
        ? Core::Format::BC7_UNORM_SRGB
        : Core::Format::BC7_UNORM
    ;
    if(SupportsTextureFormat(device, bcFormat))
        return bcFormat;

    const Core::Format::Enum rgbaFormat = textureAsset.colorSpace() == TextureColorSpace::Srgb
        ? Core::Format::RGBA8_UNORM_SRGB
        : Core::Format::RGBA8_UNORM
    ;
    return SupportsTextureFormat(device, rgbaFormat) ? rgbaFormat : Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectRgbaUploadFormat(Core::Device& device, const TextureColorSpace::Enum colorSpace){
    const Core::Format::Enum rgbaFormat = colorSpace == TextureColorSpace::Srgb
        ? Core::Format::RGBA8_UNORM_SRGB
        : Core::Format::RGBA8_UNORM
    ;
    return SupportsTextureFormat(device, rgbaFormat) ? rgbaFormat : Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectHdrOpaqueUploadFallback(
    Core::Device& device,
    const Core::Format::Enum failedFormat
){
    if(failedFormat == Core::Format::ASTC_4x4_FLOAT && SupportsTextureFormat(device, Core::Format::BC6H_UFLOAT))
        return Core::Format::BC6H_UFLOAT;
    if(failedFormat != Core::Format::RGBA16_FLOAT && SupportsTextureFormat(device, Core::Format::RGBA16_FLOAT))
        return Core::Format::RGBA16_FLOAT;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static bool IsAstc4x4LdrFormat(const Core::Format::Enum format){
    return format == Core::Format::ASTC_4x4_UNORM || format == Core::Format::ASTC_4x4_UNORM_SRGB;
}

[[nodiscard]] static bool IsBc7LdrFormat(const Core::Format::Enum format){
    return format == Core::Format::BC7_UNORM || format == Core::Format::BC7_UNORM_SRGB;
}

[[nodiscard]] static bool IsLdrCompressedFormat(const Core::Format::Enum format){
    return IsAstc4x4LdrFormat(format) || IsBc7LdrFormat(format);
}

[[nodiscard]] static Core::Format::Enum SelectLdrUploadFallback(
    Core::Device& device,
    const Core::Format::Enum failedFormat,
    const TextureColorSpace::Enum colorSpace
){
    const Core::Format::Enum bcFormat = colorSpace == TextureColorSpace::Srgb
        ? Core::Format::BC7_UNORM_SRGB
        : Core::Format::BC7_UNORM
    ;
    if(IsAstc4x4LdrFormat(failedFormat) && SupportsTextureFormat(device, bcFormat))
        return bcFormat;

    const Core::Format::Enum rgbaFormat = SelectRgbaUploadFormat(device, colorSpace);
    if(failedFormat != rgbaFormat)
        return rgbaFormat;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static bool IsHdrCompressedFormat(const Core::Format::Enum format){
    return format == Core::Format::ASTC_4x4_FLOAT || format == Core::Format::BC6H_UFLOAT;
}

[[nodiscard]] static bool GetUastcSliceLayout(
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    u64& outSliceOffsetBytes,
    u64& outSliceByteCount,
    u64& outBlockCount
){
    outSliceOffsetBytes = 0u;
    outSliceByteCount = 0u;
    outBlockCount = 0u;
    if(
        mip.sliceCount == 0u
        || sliceIndex >= mip.sliceCount
        || (mip.sizeBytes % mip.sliceCount) != 0u
        || mip.blockCountX == 0u
        || mip.blockCountY == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: invalid UASTC slice layout"));
        return false;
    }

    const u64 blockCountX = mip.blockCountX;
    const u64 blockCountY = mip.blockCountY;
    if(blockCountX > Limit<u64>::s_Max / blockCountY){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC block count exceeds addressable memory"));
        return false;
    }
    const u64 blockCount = blockCountX * blockCountY;
    if(blockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC block byte count exceeds addressable memory"));
        return false;
    }

    const u64 sliceByteCount = mip.sizeBytes / mip.sliceCount;
    if(sliceByteCount != blockCount * s_UastcBytesPerBlock){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC slice does not match its block layout"));
        return false;
    }
    if(sliceIndex != 0u && sliceByteCount > Limit<u64>::s_Max / sliceIndex){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC slice offset exceeds addressable memory"));
        return false;
    }

    outSliceOffsetBytes = sliceByteCount * sliceIndex;
    outSliceByteCount = sliceByteCount;
    outBlockCount = blockCount;
    return true;
}

[[nodiscard]] static bool GetPrimaryUastcSlice(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    const u8*& outSourceData,
    u64& outSourceByteCount,
    u64& outBlockCount
){
    outSourceData = nullptr;
    outSourceByteCount = 0u;
    outBlockCount = 0u;

    u64 sliceOffsetBytes = 0u;
    if(!GetUastcSliceLayout(mip, sliceIndex, sliceOffsetBytes, outSourceByteCount, outBlockCount))
        return false;
    if(mip.offsetBytes > Limit<u64>::s_Max - sliceOffsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC primary slice offset exceeds addressable memory"));
        return false;
    }

    const u64 sourceOffsetBytes = mip.offsetBytes + sliceOffsetBytes;
    const u64 primaryPayloadByteCount = textureAsset.primaryPayloadByteCount();
    if(
        primaryPayloadByteCount > textureAsset.payloadBytes().size()
        || sourceOffsetBytes > primaryPayloadByteCount
        || outSourceByteCount > primaryPayloadByteCount - sourceOffsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC primary slice is outside the texture payload"));
        return false;
    }

    outSourceData = textureAsset.payloadBytes().data() + static_cast<usize>(sourceOffsetBytes);
    return true;
}

[[nodiscard]] static bool GetSeparateAlphaUastcSlice(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    const u8*& outSourceData,
    u64& outSourceByteCount
){
    outSourceData = nullptr;
    outSourceByteCount = 0u;
    if(textureAsset.alphaMode() != TextureAlphaMode::SeparateUastcLdr4x4){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: texture does not have a separate UASTC alpha stream"));
        return false;
    }

    u64 sliceOffsetBytes = 0u;
    u64 blockCount = 0u;
    if(!GetUastcSliceLayout(mip, sliceIndex, sliceOffsetBytes, outSourceByteCount, blockCount))
        return false;
    const u64 primaryPayloadByteCount = textureAsset.primaryPayloadByteCount();
    const u8* const alphaBlocks = textureAsset.alphaUastcBlocks();
    if(mip.offsetBytes > Limit<u64>::s_Max - sliceOffsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha slice offset exceeds addressable memory"));
        return false;
    }
    const u64 alphaSliceOffsetBytes = mip.offsetBytes + sliceOffsetBytes;
    if(
        !alphaBlocks
        || primaryPayloadByteCount > textureAsset.payloadBytes().size()
        || primaryPayloadByteCount > textureAsset.payloadBytes().size() - primaryPayloadByteCount
        || alphaSliceOffsetBytes > primaryPayloadByteCount
        || outSourceByteCount > primaryPayloadByteCount - alphaSliceOffsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha slice is outside the texture payload"));
        return false;
    }

    outSourceData = alphaBlocks + static_cast<usize>(alphaSliceOffsetBytes);
    return true;
}

[[nodiscard]] static bool DecodeTextureSliceAsAstc(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    u8* const outUploadBytes,
    const usize uploadByteCount
){
    const u8* sourceData = nullptr;
    u64 sliceSizeBytes = 0u;
    u64 blockCount = 0u;
    if(
        !outUploadBytes
        || !GetPrimaryUastcSlice(textureAsset, mip, sliceIndex, sourceData, sliceSizeBytes, blockCount)
        || sliceSizeBytes != uploadByteCount
        || blockCount * s_UastcBytesPerBlock != uploadByteCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: ASTC UASTC slice size is invalid"));
        return false;
    }

    for(u64 blockOffset = 0u; blockOffset < sliceSizeBytes; blockOffset += s_UastcBytesPerBlock){
        basist::uastc_block sourceBlock;
        NWB_MEMCPY(
            &sourceBlock,
            sizeof(sourceBlock),
            sourceData + static_cast<usize>(blockOffset),
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
    const u8* sourceData = nullptr;
    u64 sourceByteCount = 0u;
    u64 blockCount = 0u;
    if(
        !outUploadBytes
        || !GetPrimaryUastcSlice(textureAsset, mip, sliceIndex, sourceData, sourceByteCount, blockCount)
        || sourceByteCount > Limit<u32>::s_Max
        || blockCount > Limit<u32>::s_Max
        || blockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock
        || blockCount * s_UastcBytesPerBlock != uploadByteCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: BC7 UASTC slice size is invalid"));
        return false;
    }

    basist::basisu_lowlevel_uastc_ldr_4x4_transcoder transcoder;
    if(!transcoder.transcode_image(
        basist::transcoder_texture_format::cTFBC7_RGBA,
        outUploadBytes,
        static_cast<u32>(blockCount),
        sourceData,
        static_cast<u32>(sourceByteCount),
        mip.blockCountX,
        mip.blockCountY,
        mip.width,
        mip.height,
        mipLevel,
        0u,
        static_cast<u32>(sourceByteCount),
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
    const u8* sourceData = nullptr;
    u64 sourceSliceBytes = 0u;
    u64 blockCount = 0u;
    if(
        !outUploadBytes
        || expectedUploadByteCount != uploadByteCount
        || !GetPrimaryUastcSlice(textureAsset, mip, sliceIndex, sourceData, sourceSliceBytes, blockCount)
        || sourceSliceBytes != blockCount * s_UastcBytesPerBlock
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: invalid RGBA8 UASTC slice layout"));
        return false;
    }

    const bool srgb = textureAsset.colorSpace() == TextureColorSpace::Srgb;
    if(!VisitDecodedUastcTexels(mip, sourceData, srgb, [outUploadBytes](const basist::color32& sourceTexel, const usize destinationTexelIndex){
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
    const u8* sourceData = nullptr;
    u64 sourceByteCount = 0u;
    u64 blockCount = 0u;
    if(!outUploadBytes || !GetPrimaryUastcSlice(textureAsset, mip, sliceIndex, sourceData, sourceByteCount, blockCount))
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
    const u64 outputElementCount = compressedOutput ? blockCount : texelCount;
    const u64 bytesPerElement = compressedOutput ? s_UastcBytesPerBlock : s_Rgba16FloatBytesPerTexel;
    if(
        sourceByteCount > Limit<u32>::s_Max
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
        sourceData,
        static_cast<u32>(sourceByteCount),
        mip.blockCountX,
        mip.blockCountY,
        mip.width,
        mip.height,
        mipLevel,
        0u,
        static_cast<u32>(sourceByteCount),
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

    const u8* alphaSourceData = nullptr;
    u64 alphaSourceByteCount = 0u;
    if(!GetSeparateAlphaUastcSlice(textureAsset, mip, sliceIndex, alphaSourceData, alphaSourceByteCount))
        return false;
    const u64 expectedAlphaSourceByteCount = static_cast<u64>(mip.blockCountX) * mip.blockCountY * s_UastcBytesPerBlock;
    if(alphaSourceByteCount != expectedAlphaSourceByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha slice does not match its block layout"));
        return false;
    }

    if(!VisitDecodedUastcTexels(mip, alphaSourceData, false, [outRgba16FloatBytes](const basist::color32& sourceTexel, const usize destinationTexelIndex){
        // The companion stream is a grayscale LDR mask: (a, a, a, 255).
        StoreHdrAlpha(outRgba16FloatBytes, destinationTexelIndex, sourceTexel.r);
    })){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC alpha decoding failed"));
        return false;
    }
    return true;
}

struct DecodedTextureMipUpload{
    explicit DecodedTextureMipUpload(Core::Alloc::ScratchArena& arena)
        : bytes(arena)
    {}

    Vector<u8, Core::Alloc::ScratchArena> bytes;
    usize rowPitch = 0u;
    usize sliceByteCount = 0u;
};

[[nodiscard]] static bool DecodeLdrTextureMip(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const Core::Format::Enum format,
    DecodedTextureMipUpload& outUpload
){
    usize rowPitch = 0u;
    usize sliceUploadByteCount = 0u;
    if(IsLdrCompressedFormat(format)){
        const u64 rowPitch64 = static_cast<u64>(mip.blockCountX) * s_UastcBytesPerBlock;
        if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / mip.blockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: compressed LDR mip row pitch exceeds addressable memory"));
            return false;
        }
        rowPitch = static_cast<usize>(rowPitch64);
        sliceUploadByteCount = static_cast<usize>(rowPitch64 * mip.blockCountY);
    }
    else{
        const u64 rowPitch64 = static_cast<u64>(mip.width) * s_RgbaBytesPerTexel;
        if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / mip.height){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: RGBA8 mip row pitch exceeds addressable memory"));
            return false;
        }
        rowPitch = static_cast<usize>(rowPitch64);
        sliceUploadByteCount = static_cast<usize>(rowPitch64 * mip.height);
    }

    if(mip.sliceCount == 0u || sliceUploadByteCount > Limit<usize>::s_Max / mip.sliceCount){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: texture mip upload size exceeds addressable memory"));
        return false;
    }
    outUpload.bytes.resize(sliceUploadByteCount * mip.sliceCount);
    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        u8* const destination = outUpload.bytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount;
        bool decoded = false;
        if(IsAstc4x4LdrFormat(format))
            decoded = DecodeTextureSliceAsAstc(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount);
        else if(IsBc7LdrFormat(format))
            decoded = DecodeTextureSliceAsBc7(textureAsset, mip, mipLevel, sliceIndex, destination, sliceUploadByteCount);
        else
            decoded = DecodeTextureSliceAsRgba(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount);
        if(!decoded)
            return false;
    }
    outUpload.rowPitch = rowPitch;
    outUpload.sliceByteCount = sliceUploadByteCount;
    return true;
}

[[nodiscard]] static bool DecodeHdrTextureMip(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const Core::Format::Enum format,
    DecodedTextureMipUpload& outUpload
){
    const bool compressedOutput = IsHdrCompressedFormat(format);
    if(!compressedOutput && format != Core::Format::RGBA16_FLOAT){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported HDR texture upload format"));
        return false;
    }

    const u64 rowPitch64 = compressedOutput
        ? static_cast<u64>(mip.blockCountX) * s_UastcBytesPerBlock
        : static_cast<u64>(mip.width) * s_Rgba16FloatBytesPerTexel
    ;
    const u32 rowCount = compressedOutput ? mip.blockCountY : mip.height;
    if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / rowCount){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR mip row pitch exceeds addressable memory"));
        return false;
    }
    const u64 sliceUploadByteCount64 = rowPitch64 * rowCount;
    if(
        mip.sliceCount == 0u
        || sliceUploadByteCount64 > Limit<usize>::s_Max
        || sliceUploadByteCount64 > Limit<usize>::s_Max / mip.sliceCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: HDR mip upload size exceeds addressable memory"));
        return false;
    }

    const usize rowPitch = static_cast<usize>(rowPitch64);
    const usize sliceUploadByteCount = static_cast<usize>(sliceUploadByteCount64);
    outUpload.bytes.resize(sliceUploadByteCount * mip.sliceCount);
    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        u8* const destination = outUpload.bytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount;
        if(!DecodeHdrTextureSlice(textureAsset, mip, mipLevel, sliceIndex, format, destination, sliceUploadByteCount))
            return false;
        if(
            format == Core::Format::RGBA16_FLOAT
            && !MergeHdrAlpha(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount)
        )
            return false;
    }
    outUpload.rowPitch = rowPitch;
    outUpload.sliceByteCount = sliceUploadByteCount;
    return true;
}

[[nodiscard]] static bool DecodeTextureMip(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const Core::Format::Enum format,
    DecodedTextureMipUpload& outUpload
){
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4)
        return DecodeLdrTextureMip(textureAsset, mip, mipLevel, format, outUpload);
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4)
        return DecodeHdrTextureMip(textureAsset, mip, mipLevel, format, outUpload);

    NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: unsupported texture payload format"));
    return false;
}

[[nodiscard]] static Core::TextureDimension::Enum ToCoreTextureDimension(const TextureDimension::Enum dimension){
    switch(dimension){
    case TextureDimension::Texture2D: return Core::TextureDimension::Texture2D;
    case TextureDimension::TextureCube: return Core::TextureDimension::TextureCube;
    case TextureDimension::Texture3D: return Core::TextureDimension::Texture3D;
    default: return Core::TextureDimension::Unknown;
    }
}

[[nodiscard]] static Core::GpuDescriptorClass::Enum ToSampledImageDescriptorClass(const TextureDimension::Enum dimension){
    switch(dimension){
    case TextureDimension::Texture2D: return Core::GpuDescriptorClass::SampledImage;
    case TextureDimension::TextureCube: return Core::GpuDescriptorClass::SampledImageCube;
    case TextureDimension::Texture3D: return Core::GpuDescriptorClass::SampledImage3D;
    default: return Core::GpuDescriptorClass::kCount;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TextureAssetLoader::Create(
    TextureGpuResource& outResource,
    const Texture& textureAsset,
    const Name& debugName,
    Core::Graphics& graphics,
    const tchar* const ownerName
){
    const tchar* const owner = ownerName ? ownerName : NWB_TEXT("TextureAssetLoader");
    if(outResource.valid())
        return true;
    if(outResource.texture || outResource.sampledImageHeapHandle.valid() || outResource.format != Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture resource is partially initialized; release it before recreating"), owner);
        return false;
    }
    if(!textureAsset.validatePayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture '{}' has invalid cooked texture data")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
        return false;
    }

    Core::Device& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot load texture '{}' without an initialized descriptor heap")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
        return false;
    }

    Core::Format::Enum format = __hidden_texture_loader::SelectUploadFormat(device, textureAsset);
    if(format == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: device cannot sample the required texture format for '{}'")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
        return false;
    }

    const Name imageName = debugName ? debugName : textureAsset.virtualPath();
    const Core::TextureDimension::Enum textureDimension = __hidden_texture_loader::ToCoreTextureDimension(textureAsset.dimension());
    const Core::GpuDescriptorClass::Enum descriptorClass = __hidden_texture_loader::ToSampledImageDescriptorClass(textureAsset.dimension());
    if(textureDimension == Core::TextureDimension::Unknown || descriptorClass == Core::GpuDescriptorClass::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture '{}' has an unsupported dimension")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
        return false;
    }

    Core::TextureDesc textureDesc;
    textureDesc
        .setWidth(textureAsset.width())
        .setHeight(textureAsset.height())
        .setMipLevels(static_cast<u32>(textureAsset.mipLevels().size()))
        .setFormat(format)
        .setDimension(textureDimension)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        // Static decoded assets are immediately sampled by Graphics/Compute, while sizeable batches may use a
        // dedicated Transfer producer.  Declare all three consumer/producer transports before creation so the
        // graph-owned upload can choose the automatic Transfer -> Compute -> Graphics route safely.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName(imageName)
    ;
    if(textureAsset.dimension() == TextureDimension::TextureCube)
        textureDesc.setArraySize(TextureFormat::s_TextureCubeFaceCount);
    else if(textureAsset.dimension() == TextureDimension::Texture3D)
        textureDesc.setDepth(textureAsset.depth());

    Core::TextureHandle texture = graphics.createTexture(textureDesc);
    if(
        !texture
        && textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4
        && __hidden_texture_loader::IsLdrCompressedFormat(format)
    ){
        Core::Format::Enum fallbackFormat = __hidden_texture_loader::SelectLdrUploadFallback(
            device,
            format,
            textureAsset.colorSpace()
        );
        while(fallbackFormat != Core::Format::UNKNOWN){
            textureDesc.setFormat(fallbackFormat);
            format = fallbackFormat;
            texture = graphics.createTexture(textureDesc);
            if(texture)
                break;
            fallbackFormat = __hidden_texture_loader::SelectLdrUploadFallback(device, format, textureAsset.colorSpace());
        }
    }
    if(
        !texture
        && textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4
        && textureAsset.alphaMode() == TextureAlphaMode::Opaque
    ){
        Core::Format::Enum fallbackFormat = __hidden_texture_loader::SelectHdrOpaqueUploadFallback(device, format);
        while(fallbackFormat != Core::Format::UNKNOWN){
            textureDesc.setFormat(fallbackFormat);
            format = fallbackFormat;
            texture = graphics.createTexture(textureDesc);
            if(texture)
                break;
            fallbackFormat = __hidden_texture_loader::SelectHdrOpaqueUploadFallback(device, format);
        }
    }
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    __hidden_texture_loader::InitializeBasisTranscoder();

    Core::Alloc::ScratchArena scratchArena(AssetsTextureArenaScope::s_UploadScratchArena);
    Vector<__hidden_texture_loader::DecodedTextureMipUpload, Core::Alloc::ScratchArena> decodedMips{scratchArena};
    decodedMips.reserve(textureAsset.mipLevels().size());
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex){
        decodedMips.emplace_back(scratchArena);
        if(!__hidden_texture_loader::DecodeTextureMip(
            textureAsset,
            textureAsset.mipLevels()[mipIndex],
            static_cast<u32>(mipIndex),
            format,
            decodedMips.back()
        )){
            return false;
        }
    }

    Vector<Core::Graphics::TextureUploadRegion, Core::Alloc::ScratchArena> uploadRegions{scratchArena};
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex){
        const TextureMipLevel& mip = textureAsset.mipLevels()[mipIndex];
        const __hidden_texture_loader::DecodedTextureMipUpload& decoded = decodedMips[mipIndex];
        if(
            decoded.bytes.empty()
            || decoded.rowPitch == 0u
            || decoded.sliceByteCount == 0u
            || mip.sliceCount == 0u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: decoded texture '{}' mip {} has an empty upload region")
                , owner
                , StringConvert(imageName.c_str())
                , static_cast<u32>(mipIndex)
            );
            return false;
        }

        if(textureAsset.dimension() == TextureDimension::Texture3D){
            uploadRegions.emplace_back(Core::Graphics::TextureUploadRegion{
                .data = decoded.bytes.data(),
                .dataSize = decoded.bytes.size(),
                .rowPitch = decoded.rowPitch,
                .depthPitch = decoded.sliceByteCount,
                .arraySlice = 0u,
                .mipLevel = static_cast<u32>(mipIndex),
            });
            continue;
        }

        for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
            uploadRegions.emplace_back(Core::Graphics::TextureUploadRegion{
                .data = decoded.bytes.data() + static_cast<usize>(sliceIndex) * decoded.sliceByteCount,
                .dataSize = decoded.sliceByteCount,
                .rowPitch = decoded.rowPitch,
                .depthPitch = decoded.sliceByteCount,
                .arraySlice = sliceIndex,
                .mipLevel = static_cast<u32>(mipIndex),
            });
        }
    }

    Core::QueueSubmissionToken uploadToken;
    if(!graphics.uploadTextureBatch(Core::Graphics::TextureUploadBatchDesc{
        .destination = texture,
        .regions = uploadRegions.data(),
        .regionCount = uploadRegions.size(),
        .finalState = Core::ResourceStates::ShaderResource,
        .acceptedToken = &uploadToken,
    })){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to submit graph-owned texture upload for '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    const Core::GpuDescriptorHandle sampledImageHandle = heap.allocate(descriptorClass);
    if(!sampledImageHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to allocate a bindless sampled-image slot for texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }
    if(!heap.write(sampledImageHandle, Core::DescriptorWriteItem::Texture_SRV(
        0u,
        texture.get(),
        format,
        Core::s_AllSubresources,
        textureDimension
    ))){
        heap.free(sampledImageHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to write the bindless sampled-image slot for texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    outResource.texture = Move(texture);
    outResource.sampledImageHeapHandle = sampledImageHandle;
    outResource.format = format;
    return true;
}

bool TextureAssetLoader::Load(
    TextureGpuResource& outResource,
    const Core::Assets::AssetRef<Texture>& textureAsset,
    const Name& debugName,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    const tchar* const ownerName
){
    const tchar* const owner = ownerName ? ownerName : NWB_TEXT("TextureAssetLoader");
    if(!textureAsset.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture asset reference is empty"), owner);
        return false;
    }
    if(outResource.valid())
        return true;

    const Name& textureVirtualPath = textureAsset.name();

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager.loadSync(Texture::AssetTypeName(), textureVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load texture asset '{}'"), owner, StringConvert(textureVirtualPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Texture::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: asset '{}' is not a texture"), owner, StringConvert(textureVirtualPath.c_str()));
        return false;
    }

    return Create(outResource, static_cast<const Texture&>(*loadedAsset), debugName, graphics, owner);
}

void TextureAssetLoader::Release(TextureGpuResource& inOutResource, Core::Graphics& graphics){
    if(inOutResource.sampledImageHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
        if(heap.isInitialized())
            heap.free(inOutResource.sampledImageHeapHandle);
        inOutResource.sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid();
    }

    inOutResource.texture.reset();
    inOutResource.format = Core::Format::UNKNOWN;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

