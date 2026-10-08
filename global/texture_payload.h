// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "algorithm.h"
#include "basic_string.h"
#include "expected.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture payload ABI shared by the asset cooker, runtime loader, and tex_conv.


namespace TextureDimension{
    static constexpr u8 s_TextureDimensionTexture2DBase = 0u;
    enum Enum : u8{
        Texture2D = s_TextureDimensionTexture2DBase,
        TextureCube,
        Texture3D,
    };
};

[[nodiscard]] constexpr bool IsValidTextureDimension(const TextureDimension::Enum dimension)noexcept{
    return dimension == TextureDimension::Texture2D
        || dimension == TextureDimension::TextureCube
        || dimension == TextureDimension::Texture3D
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TexturePayloadFormat{
    static constexpr u8 s_TexturePayloadFormatUastcLdr4x4Base = 0u;
    enum Enum : u8{
        UastcLdr4x4 = s_TexturePayloadFormatUastcLdr4x4Base,
        // RGB-only ASTC HDR 4x4 bitstream in the Basis encoder.
        UastcHdr4x4,
    };
};

[[nodiscard]] constexpr bool IsValidTexturePayloadFormat(const TexturePayloadFormat::Enum format)noexcept{
    return format == TexturePayloadFormat::UastcLdr4x4
        || format == TexturePayloadFormat::UastcHdr4x4
    ;
}

[[nodiscard]] constexpr bool IsHdrTexturePayloadFormat(const TexturePayloadFormat::Enum format)noexcept{
    return format == TexturePayloadFormat::UastcHdr4x4;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureAlphaMode{
    static constexpr u8 s_TextureAlphaModeOpaqueBase = 0u;
    enum Enum : u8{
        // The primary texture stream has no meaningful alpha and samples as one.
        Opaque = s_TextureAlphaModeOpaqueBase,
        // LDR UASTC stores alpha in its ordinary RGBA blocks.
        EmbeddedLdr,
        // HDR color remains RGB-only while a single normalized alpha value is supplied at load time.
        ConstantUnorm8,
        // HDR color is followed by a same-layout UASTC LDR alpha stream.
        SeparateUastcLdr4x4,
    };
};

[[nodiscard]] constexpr bool IsValidTextureAlphaMode(const TextureAlphaMode::Enum mode)noexcept{
    return mode == TextureAlphaMode::Opaque
        || mode == TextureAlphaMode::EmbeddedLdr
        || mode == TextureAlphaMode::ConstantUnorm8
        || mode == TextureAlphaMode::SeparateUastcLdr4x4
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureFormat{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_UastcLdr4x4Format = "uastc_ldr_4x4";
inline constexpr AStringView s_UastcHdr4x4Format = "uastc_hdr_4x4";
inline constexpr AStringView s_AlphaOpaqueMode = "opaque";
inline constexpr AStringView s_AlphaConstantUnorm8Mode = "constant_unorm8";
inline constexpr AStringView s_AlphaUastcLdr4x4Mode = "uastc_ldr_4x4";
inline constexpr AStringView s_LinearColorSpace = "linear";
inline constexpr AStringView s_SrgbColorSpace = "srgb";
inline constexpr AStringView s_Texture2DDimension = "2d";
inline constexpr AStringView s_TextureCubeDimension = "cube";
inline constexpr AStringView s_Texture3DDimension = "volume";
inline constexpr AStringView s_TextureDataExtension = ".tex";
inline constexpr u32 s_UastcBlockWidth = 4u;
inline constexpr u32 s_UastcBlockHeight = 4u;
inline constexpr u32 s_UastcBytesPerBlock = 16u;
inline constexpr u32 s_TextureCubeFaceCount = 6u;
// UNORM8 255 is reserved for opaque mode; constant-alpha payloads use [0, 254].
inline constexpr u32 s_OpaqueAlphaUnorm8 = 255u;
inline constexpr u32 s_MaxConstantAlphaUnorm8 = s_OpaqueAlphaUnorm8 - 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TexturePayloadBlockLayout{
    u32 blockWidth;
    u32 blockHeight;
    u32 bytesPerBlock;
};

struct TextureMipPlaneBlockLayout{
    u32 blocksX;
    u32 blocksY;
    u64 planeByteCount;
};

[[nodiscard]] inline Expected<u32> ComputeCompleteMipCount(
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth
)noexcept{
    if(!IsValidTextureDimension(dimension) || width == 0u || height == 0u || depth == 0u)
        return MakeUnexpected(Failure{});

    u32 mipCount = 0u;
    u32 mipWidth = width;
    u32 mipHeight = height;
    u32 mipDepth = depth;
    for(;;){
        if(mipCount == Limit<u32>::s_Max)
            return MakeUnexpected(Failure{});
        ++mipCount;

        if(mipWidth == 1u && mipHeight == 1u && (dimension != TextureDimension::Texture3D || mipDepth == 1u))
            return mipCount;

        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
        if(dimension == TextureDimension::Texture3D)
            mipDepth = mipDepth > 1u ? mipDepth >> 1u : 1u;
    }
}

[[nodiscard]] inline Expected<u32> ComputeMipSliceCount(const TextureDimension::Enum dimension, const u32 mipDepth)noexcept{
    switch(dimension){
    case TextureDimension::Texture2D:
        return 1u;
    case TextureDimension::TextureCube:
        return s_TextureCubeFaceCount;
    case TextureDimension::Texture3D:
        if(mipDepth > 0u)
            return mipDepth;
        break;
    default:
        break;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] inline Expected<TexturePayloadBlockLayout> GetTexturePayloadBlockLayout(const TexturePayloadFormat::Enum format)noexcept{
    switch(format){
    case TexturePayloadFormat::UastcLdr4x4:
    case TexturePayloadFormat::UastcHdr4x4:
        return TexturePayloadBlockLayout{ s_UastcBlockWidth, s_UastcBlockHeight, s_UastcBytesPerBlock };
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] inline Expected<TextureMipPlaneBlockLayout> ComputeMipPlaneBlockLayout(
    const TexturePayloadFormat::Enum format,
    const u32 width,
    const u32 height
)noexcept{
    if(width == 0u || height == 0u)
        return MakeUnexpected(Failure{});
    const auto layout = GetTexturePayloadBlockLayout(format);
    if(!layout)
        return MakeUnexpected(layout.error());

    const u64 blocksX64 = DivideUp(static_cast<u64>(width), static_cast<u64>(layout->blockWidth));
    const u64 blocksY64 = DivideUp(static_cast<u64>(height), static_cast<u64>(layout->blockHeight));
    if(blocksX64 > Limit<u32>::s_Max || blocksY64 > Limit<u32>::s_Max || blocksX64 > Limit<u64>::s_Max / blocksY64)
        return MakeUnexpected(Failure{});

    const u64 blockCount = blocksX64 * blocksY64;
    if(blockCount > Limit<u64>::s_Max / layout->bytesPerBlock)
        return MakeUnexpected(Failure{});

    return TextureMipPlaneBlockLayout{
        .blocksX = static_cast<u32>(blocksX64),
        .blocksY = static_cast<u32>(blocksY64),
        .planeByteCount = blockCount * layout->bytesPerBlock,
    };
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

