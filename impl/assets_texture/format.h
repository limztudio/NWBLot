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


// This is the single source of truth for the UASTC texture-asset contract. The converter emits this layout while the
// cooker, runtime codec, and loader validate or consume it, so format constants and mip arithmetic must not drift.
namespace TextureFormat{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_UastcLdr4x4Format = "uastc_ldr_4x4";
inline constexpr AStringView s_UastcSpecificationRevision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";
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
inline constexpr u32 s_TextureMetadataVersion = 1u;


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
        outSliceCount = 6u;
        return true;
    case TextureDimension::Texture3D:
        outSliceCount = mipDepth;
        return mipDepth > 0u;
    default:
        outSliceCount = 0u;
        return false;
    }
}

[[nodiscard]] inline bool ComputePlaneBlockLayout(
    const u32 width,
    const u32 height,
    u32& outBlocksX,
    u32& outBlocksY,
    u64& outPlaneByteCount
){
    outBlocksX = 0u;
    outBlocksY = 0u;
    outPlaneByteCount = 0u;
    if(width == 0u || height == 0u)
        return false;

    const u64 blocksX64 = DivideUp(static_cast<u64>(width), static_cast<u64>(s_UastcBlockWidth));
    const u64 blocksY64 = DivideUp(static_cast<u64>(height), static_cast<u64>(s_UastcBlockHeight));
    if(blocksX64 > Limit<u32>::s_Max || blocksY64 > Limit<u32>::s_Max || blocksX64 > Limit<u64>::s_Max / blocksY64)
        return false;

    const u64 blockCount = blocksX64 * blocksY64;
    if(blockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock)
        return false;

    outBlocksX = static_cast<u32>(blocksX64);
    outBlocksY = static_cast<u32>(blocksY64);
    outPlaneByteCount = blockCount * s_UastcBytesPerBlock;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
