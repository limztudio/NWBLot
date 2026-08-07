// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureDimension{
    enum Enum : u8{
        Texture2D = 0u,
        TextureCube,
        Texture3D,
    };
};

[[nodiscard]] inline bool IsValidTextureDimension(const TextureDimension::Enum dimension){
    return dimension == TextureDimension::Texture2D
        || dimension == TextureDimension::TextureCube
        || dimension == TextureDimension::Texture3D
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TexturePayloadFormat{
    enum Enum : u8{
        UastcLdr4x4 = 0u,
        // UASTC HDR uses the standard ASTC HDR 4x4 block bitstream. It is RGB-only in the
        // current Basis encoder; a non-opaque alpha mask, when present, is stored as a
        // companion UASTC LDR stream with the same mip/slice layout.
        UastcHdr4x4,
    };
};

[[nodiscard]] inline bool IsValidTexturePayloadFormat(const TexturePayloadFormat::Enum format){
    return format == TexturePayloadFormat::UastcLdr4x4
        || format == TexturePayloadFormat::UastcHdr4x4
    ;
}

[[nodiscard]] inline bool IsHdrTexturePayloadFormat(const TexturePayloadFormat::Enum format){
    return format == TexturePayloadFormat::UastcHdr4x4;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureAlphaMode{
    enum Enum : u8{
        // The primary texture stream has no meaningful alpha and samples as one.
        Opaque = 0u,
        // LDR UASTC stores alpha in its ordinary RGBA blocks.
        EmbeddedLdr,
        // HDR color remains RGB-only while a single normalized alpha value is supplied at load time.
        ConstantUnorm8,
        // HDR color is followed by a same-layout UASTC LDR alpha stream.
        SeparateUastcLdr4x4,
    };
};

[[nodiscard]] inline bool IsValidTextureAlphaMode(const TextureAlphaMode::Enum mode){
    return mode == TextureAlphaMode::Opaque
        || mode == TextureAlphaMode::EmbeddedLdr
        || mode == TextureAlphaMode::ConstantUnorm8
        || mode == TextureAlphaMode::SeparateUastcLdr4x4
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This is the single source of truth for texture-asset payload contracts. The converter emits these layouts while the
// cooker, runtime codec, and loader validate or consume them, so format constants and mip arithmetic cannot drift.
namespace TextureFormat{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_UastcLdr4x4Format = "uastc_ldr_4x4";
inline constexpr AStringView s_UastcHdr4x4Format = "uastc_hdr_4x4";
inline constexpr AStringView s_UastcSpecificationRevision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";
inline constexpr AStringView s_AlphaOpaqueMode = "opaque";
inline constexpr AStringView s_AlphaConstantUnorm8Mode = "constant_unorm8";
inline constexpr AStringView s_AlphaUastcLdr4x4Mode = "uastc_ldr_4x4";
inline constexpr AStringView s_MipMajorSliceMajorBlocksPayloadLayout = "mip_major_slice_major_blocks";
inline constexpr AStringView s_ClampMipAddressMode = "clamp";
inline constexpr AStringView s_LinearColorSpace = "linear";
inline constexpr AStringView s_SrgbColorSpace = "srgb";
inline constexpr AStringView s_Texture2DDimension = "2d";
inline constexpr AStringView s_TextureCubeDimension = "cube";
inline constexpr AStringView s_Texture3DDimension = "volume";
inline constexpr AStringView s_TextureDataExtension = ".tex";
inline constexpr u32 s_UastcBlockWidth = 4u;
inline constexpr u32 s_UastcBlockHeight = 4u;
inline constexpr u32 s_UastcBytesPerBlock = 16u;
inline constexpr u32 s_UastcLdrTextureMetadataVersion = 1u;
inline constexpr u32 s_UastcHdrTextureMetadataVersion = 2u;
inline constexpr u32 s_TextureCubeFaceCount = 6u;
// HDR alpha transport reserves the fully opaque UNORM8 value for the explicit opaque mode. Constant-alpha payloads
// therefore carry the inclusive [0, 254] range and never alias the opaque-mode sentinel.
inline constexpr u32 s_OpaqueAlphaUnorm8 = 255u;
inline constexpr u32 s_MaxConstantAlphaUnorm8 = s_OpaqueAlphaUnorm8 - 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ComputeCompleteMipCount(
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    u32& outMipCount
){
    outMipCount = 0u;
    if(!IsValidTextureDimension(dimension) || width == 0u || height == 0u || depth == 0u)
        return false;

    u32 mipWidth = width;
    u32 mipHeight = height;
    u32 mipDepth = depth;
    for(;;){
        if(outMipCount == Limit<u32>::s_Max)
            return false;
        ++outMipCount;

        if(mipWidth == 1u && mipHeight == 1u && (dimension != TextureDimension::Texture3D || mipDepth == 1u))
            return true;

        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
        if(dimension == TextureDimension::Texture3D)
            mipDepth = mipDepth > 1u ? mipDepth >> 1u : 1u;
    }
}

[[nodiscard]] inline bool ComputeMipSliceCount(
    const TextureDimension::Enum dimension,
    const u32 mipDepth,
    u32& outSliceCount
){
    switch(dimension){
    case TextureDimension::Texture2D:
        outSliceCount = 1u;
        return true;
    case TextureDimension::TextureCube:
        outSliceCount = s_TextureCubeFaceCount;
        return true;
    case TextureDimension::Texture3D:
        outSliceCount = mipDepth;
        return mipDepth > 0u;
    default:
        outSliceCount = 0u;
        return false;
    }
}

[[nodiscard]] inline bool GetTexturePayloadBlockLayout(
    const TexturePayloadFormat::Enum format,
    u32& outBlockWidth,
    u32& outBlockHeight,
    u32& outBytesPerBlock
){
    switch(format){
    case TexturePayloadFormat::UastcLdr4x4:
    case TexturePayloadFormat::UastcHdr4x4:
        outBlockWidth = s_UastcBlockWidth;
        outBlockHeight = s_UastcBlockHeight;
        outBytesPerBlock = s_UastcBytesPerBlock;
        return true;
    default:
        outBlockWidth = 0u;
        outBlockHeight = 0u;
        outBytesPerBlock = 0u;
        return false;
    }
}

[[nodiscard]] inline bool ComputeMipPlaneBlockLayout(
    const TexturePayloadFormat::Enum format,
    const u32 width,
    const u32 height,
    u32& outBlocksX,
    u32& outBlocksY,
    u64& outPlaneByteCount
){
    outBlocksX = 0u;
    outBlocksY = 0u;
    outPlaneByteCount = 0u;

    u32 blockWidth = 0u;
    u32 blockHeight = 0u;
    u32 bytesPerBlock = 0u;
    if(
        width == 0u
        || height == 0u
        || !GetTexturePayloadBlockLayout(format, blockWidth, blockHeight, bytesPerBlock)
    )
        return false;

    const u64 blocksX64 = DivideUp(static_cast<u64>(width), static_cast<u64>(blockWidth));
    const u64 blocksY64 = DivideUp(static_cast<u64>(height), static_cast<u64>(blockHeight));
    if(blocksX64 > Limit<u32>::s_Max || blocksY64 > Limit<u32>::s_Max || blocksX64 > Limit<u64>::s_Max / blocksY64)
        return false;

    const u64 blockCount = blocksX64 * blocksY64;
    if(blockCount > Limit<u64>::s_Max / bytesPerBlock)
        return false;

    outBlocksX = static_cast<u32>(blocksX64);
    outBlocksY = static_cast<u32>(blocksY64);
    outPlaneByteCount = blockCount * bytesPerBlock;
    return true;
}

[[nodiscard]] inline bool ComputePlaneBlockLayout(
    const u32 width,
    const u32 height,
    u32& outBlocksX,
    u32& outBlocksY,
    u64& outPlaneByteCount
){
    return ComputeMipPlaneBlockLayout(
        TexturePayloadFormat::UastcLdr4x4,
        width,
        height,
        outBlocksX,
        outBlocksY,
        outPlaneByteCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
