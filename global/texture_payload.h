// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "algorithm.h"
#include "array.h"
#include "expected.h"
#include "span.h"
#include "string_view.h"
#include "text_utils.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Linear UASTC block layouts and the internal cooked texture binary contract.


namespace TextureDimension{
    static constexpr u8 s_TextureDimensionTexture2DBase = 0u;
    enum Enum : u8{
        Texture2D = s_TextureDimensionTexture2DBase,
        TextureCube,
        Texture3D,
    };
};

[[nodiscard]] constexpr b8 IsValidTextureDimension(const TextureDimension::Enum dimension)noexcept{
    return dimension == TextureDimension::Texture2D || dimension == TextureDimension::TextureCube || dimension == TextureDimension::Texture3D;
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

[[nodiscard]] constexpr b8 IsValidTexturePayloadFormat(const TexturePayloadFormat::Enum format)noexcept{
    return format == TexturePayloadFormat::UastcLdr4x4 || format == TexturePayloadFormat::UastcHdr4x4;
}

[[nodiscard]] constexpr b8 IsHdrTexturePayloadFormat(const TexturePayloadFormat::Enum format)noexcept{
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

[[nodiscard]] constexpr b8 IsValidTextureAlphaMode(const TextureAlphaMode::Enum mode)noexcept{
    return mode == TextureAlphaMode::Opaque || mode == TextureAlphaMode::EmbeddedLdr || mode == TextureAlphaMode::ConstantUnorm8 || mode == TextureAlphaMode::SeparateUastcLdr4x4;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureFormat{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_UastcLdr4x4Format = "uastc_ldr_4x4";
inline constexpr AStringView s_UastcHdr4x4Format = "uastc_hdr_4x4";
inline constexpr AStringView s_Texture2DDimension = "2d";
inline constexpr AStringView s_TextureCubeDimension = "cube";
inline constexpr AStringView s_Texture3DDimension = "volume";
inline constexpr AStringView s_OpaqueAlphaMode = "opaque";
inline constexpr AStringView s_EmbeddedLdrAlphaMode = "embedded_ldr";
inline constexpr AStringView s_ConstantUnorm8AlphaMode = "constant_unorm8";
inline constexpr AStringView s_SeparateUastcLdr4x4AlphaMode = "separate_uastc_ldr_4x4";
inline constexpr AStringView s_TextureDataExtension = ".tex";
inline constexpr u32 s_UastcBlockWidth = 4u;
inline constexpr u32 s_UastcBlockHeight = 4u;
inline constexpr u32 s_UastcBytesPerBlock = 16u;
inline constexpr u32 s_TextureCubeFaceCount = 6u;
// UNORM8 255 is reserved for opaque mode; constant-alpha payloads use [0, 254].
inline constexpr u32 s_OpaqueAlphaUnorm8 = 255u;
inline constexpr u32 s_MaxConstantAlphaUnorm8 = s_OpaqueAlphaUnorm8 - 1u;

inline constexpr Array<NamedEnumCase<TexturePayloadFormat::Enum>, 2u> s_PayloadFormatCases = {{
    { s_UastcLdr4x4Format, TexturePayloadFormat::UastcLdr4x4 },
    { s_UastcHdr4x4Format, TexturePayloadFormat::UastcHdr4x4 },
}};

inline constexpr Array<NamedEnumCase<TextureDimension::Enum>, 3u> s_DimensionCases = {{
    { s_Texture2DDimension, TextureDimension::Texture2D },
    { s_TextureCubeDimension, TextureDimension::TextureCube },
    { s_Texture3DDimension, TextureDimension::Texture3D },
}};

inline constexpr Array<NamedEnumCase<TextureAlphaMode::Enum>, 4u> s_AlphaModeCases = {{
    { s_OpaqueAlphaMode, TextureAlphaMode::Opaque },
    { s_EmbeddedLdrAlphaMode, TextureAlphaMode::EmbeddedLdr },
    { s_ConstantUnorm8AlphaMode, TextureAlphaMode::ConstantUnorm8 },
    { s_SeparateUastcLdr4x4AlphaMode, TextureAlphaMode::SeparateUastcLdr4x4 },
}};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr AStringView PayloadFormatText(const TexturePayloadFormat::Enum format)noexcept{
    for(const auto& entry : s_PayloadFormatCases){
        if(entry.value == format)
            return entry.text;
    }
    return AStringView{};
}

[[nodiscard]] constexpr AStringView DimensionText(const TextureDimension::Enum dimension)noexcept{
    for(const auto& entry : s_DimensionCases){
        if(entry.value == dimension)
            return entry.text;
    }
    return AStringView{};
}

[[nodiscard]] constexpr AStringView AlphaModeText(const TextureAlphaMode::Enum alphaMode)noexcept{
    for(const auto& entry : s_AlphaModeCases){
        if(entry.value == alphaMode)
            return entry.text;
    }
    return AStringView{};
}

[[nodiscard]] constexpr u32 NextMipExtent(const u32 extent)noexcept{
    return extent > 1u ? extent >> 1u : 1u;
}

[[nodiscard]] constexpr b8 IsValidTextureResolution(const TextureDimension::Enum dimension, const u32 width, const u32 height, const u32 depth)noexcept{
    return
        IsValidTextureDimension(dimension)
        && width > 0u
        && height > 0u
        && depth > 0u
        && (dimension == TextureDimension::Texture3D || depth == 1u)
        && (dimension != TextureDimension::TextureCube || width == height)
    ;
}

[[nodiscard]] constexpr b8 IsValidTextureAlphaTransport(const TexturePayloadFormat::Enum format, const TextureAlphaMode::Enum alphaMode)noexcept{
    if(!IsValidTexturePayloadFormat(format) || !IsValidTextureAlphaMode(alphaMode))
        return false;
    if(format == TexturePayloadFormat::UastcLdr4x4)
        return alphaMode == TextureAlphaMode::Opaque || alphaMode == TextureAlphaMode::EmbeddedLdr;
    return alphaMode != TextureAlphaMode::EmbeddedLdr;
}

[[nodiscard]] constexpr b8 IsValidTextureAlphaConstant(const TextureAlphaMode::Enum alphaMode, const u8 alphaConstantUnorm8)noexcept{
    return alphaMode == TextureAlphaMode::ConstantUnorm8
        ? alphaConstantUnorm8 <= s_MaxConstantAlphaUnorm8
        : alphaConstantUnorm8 == s_OpaqueAlphaUnorm8
    ;
}

[[nodiscard]] constexpr b8 IsValidTextureDescriptor(
    const TexturePayloadFormat::Enum format,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    const TextureAlphaMode::Enum alphaMode,
    const u8 alphaConstantUnorm8
)noexcept{
    return
        IsValidTextureResolution(dimension, width, height, depth)
        && IsValidTextureAlphaTransport(format, alphaMode)
        && IsValidTextureAlphaConstant(alphaMode, alphaConstantUnorm8)
    ;
}

[[nodiscard]] inline Expected<u64> ComputeExpectedPayloadByteCount(const u64 primaryPayloadBytes, const TextureAlphaMode::Enum alphaMode)noexcept{
    if(!IsValidTextureAlphaMode(alphaMode))
        return MakeUnexpected(Failure{});
    if(alphaMode != TextureAlphaMode::SeparateUastcLdr4x4)
        return primaryPayloadBytes;
    if(primaryPayloadBytes > Limit<u64>::s_Max - primaryPayloadBytes)
        return MakeUnexpected(Failure{});
    return primaryPayloadBytes + primaryPayloadBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextureMipPlaneBlockLayout{
    u32 blocksX;
    u32 blocksY;
    u64 planeByteCount;
};

struct MipLevelLayout{
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 blockCountX = 0u;
    u32 blockCountY = 0u;
    u32 sliceCount = 1u;
};

namespace MipChainLayoutFailure{
    enum Enum : u8{
        None = 0u,
        InvalidMipChain,
        MipCountMismatch,
        BlockLayoutOverflow,
        SliceCountInvalid,
        SliceCountMismatch,
        ByteSizeOverflow,
        OffsetOverflow,
        ResolutionMismatch,
        BlockGridMismatch,
        ByteRangeMismatch,
        kCount
    };
};

struct MipChainLayoutError{
    MipChainLayoutFailure::Enum failure = MipChainLayoutFailure::None;
    u32 mipIndex = 0u;
};

[[nodiscard]] inline Expected<u32> ComputeCompleteMipCount(const TextureDimension::Enum dimension, const u32 width, const u32 height, const u32 depth)noexcept{
    if(!IsValidTextureDimension(dimension) || width == 0u || height == 0u || depth == 0u)
        return MakeUnexpected(Failure{});

    u32 mipCount = 0u;
    u32 mipWidth = width;
    u32 mipHeight = height;
    u32 mipDepth = depth;
    for(;;){
        ++mipCount;

        if(mipWidth == 1u && mipHeight == 1u && (dimension != TextureDimension::Texture3D || mipDepth == 1u))
            return mipCount;

        mipWidth = NextMipExtent(mipWidth);
        mipHeight = NextMipExtent(mipHeight);
        if(dimension == TextureDimension::Texture3D)
            mipDepth = NextMipExtent(mipDepth);
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

[[nodiscard]] inline Expected<TextureMipPlaneBlockLayout> ComputeMipPlaneBlockLayout(const TexturePayloadFormat::Enum format, const u32 width, const u32 height)noexcept{
    if(!IsValidTexturePayloadFormat(format) || width == 0u || height == 0u)
        return MakeUnexpected(Failure{});

    const u64 blocksX64 = DivideUp(static_cast<u64>(width), static_cast<u64>(s_UastcBlockWidth));
    const u64 blocksY64 = DivideUp(static_cast<u64>(height), static_cast<u64>(s_UastcBlockHeight));
    // u32 extents keep each block axis within u32, so the block count fits u64.
    const u64 blockCount = blocksX64 * blocksY64;
    if(blockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock)
        return MakeUnexpected(Failure{});

    return TextureMipPlaneBlockLayout{ .blocksX = static_cast<u32>(blocksX64), .blocksY = static_cast<u32>(blocksY64), .planeByteCount = blockCount * s_UastcBytesPerBlock, };
}

template<typename MipVectorT, typename MakeLevelFn>
[[nodiscard]] inline Expected<MipVectorT, MipChainLayoutError> BuildMipChainLayout(
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    MipVectorT&& emptyLevels,
    MakeLevelFn&& makeLevel
){
    if(!IsValidTexturePayloadFormat(payloadFormat) || !IsValidTextureResolution(dimension, width, height, depth))
        return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::InvalidMipChain });

    const auto completeMipCount = ComputeCompleteMipCount(dimension, width, height, depth);
    if(!completeMipCount)
        return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::InvalidMipChain });

    MipVectorT levels(Move(emptyLevels));
    levels.clear();
    levels.reserve(*completeMipCount);
    u32 mipWidth = width;
    u32 mipHeight = height;
    u32 mipDepth = depth;
    u64 offsetBytes = 0u;
    for(u32 mipIndex = 0u; mipIndex < *completeMipCount; ++mipIndex){
        const auto planeLayout = ComputeMipPlaneBlockLayout(payloadFormat, mipWidth, mipHeight);
        if(!planeLayout)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::BlockLayoutOverflow, .mipIndex = mipIndex });
        const auto sliceCount = ComputeMipSliceCount(dimension, mipDepth);
        if(!sliceCount)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::SliceCountInvalid, .mipIndex = mipIndex });
        if(planeLayout->planeByteCount > Limit<u64>::s_Max / *sliceCount)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::ByteSizeOverflow, .mipIndex = mipIndex });
        const u64 sizeBytes = planeLayout->planeByteCount * *sliceCount;
        if(sizeBytes > Limit<u64>::s_Max - offsetBytes)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::OffsetOverflow, .mipIndex = mipIndex });

        const MipLevelLayout level{
            .offsetBytes = offsetBytes,
            .sizeBytes = sizeBytes,
            .width = mipWidth,
            .height = mipHeight,
            .blockCountX = planeLayout->blocksX,
            .blockCountY = planeLayout->blocksY,
            .sliceCount = *sliceCount,
        };
        levels.push_back(makeLevel(level));

        offsetBytes += sizeBytes;
        mipWidth = NextMipExtent(mipWidth);
        mipHeight = NextMipExtent(mipHeight);
        if(dimension == TextureDimension::Texture3D)
            mipDepth = NextMipExtent(mipDepth);
    }
    return levels;
}

template<typename MipT>
[[nodiscard]] inline Expected<u64, MipChainLayoutError> ValidateMipChainLayout(
    const Span<const MipT> mipLevels,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth
)noexcept{
    const auto mipCountResult = ComputeCompleteMipCount(dimension, width, height, depth);
    if(!mipCountResult)
        return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::InvalidMipChain });
    if(mipLevels.size() != *mipCountResult)
        return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::MipCountMismatch });

    u32 expectedWidth = width;
    u32 expectedHeight = height;
    u32 expectedDepth = depth;
    u64 expectedOffsetBytes = 0u;
    for(usize mipIndex = 0u; mipIndex < mipLevels.size(); ++mipIndex){
        const u32 mipIndexU32 = static_cast<u32>(mipIndex);
        const MipT& mip = mipLevels[mipIndex];
        const auto planeLayoutResult = ComputeMipPlaneBlockLayout(payloadFormat, expectedWidth, expectedHeight);
        if(!planeLayoutResult)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::BlockLayoutOverflow, .mipIndex = mipIndexU32 });
        const auto sliceCountResult = ComputeMipSliceCount(dimension, expectedDepth);
        if(!sliceCountResult)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::SliceCountInvalid, .mipIndex = mipIndexU32 });
        if(planeLayoutResult->planeByteCount > Limit<u64>::s_Max / *sliceCountResult)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::ByteSizeOverflow, .mipIndex = mipIndexU32 });
        const u64 expectedSizeBytes = planeLayoutResult->planeByteCount * *sliceCountResult;

        if(mip.width != expectedWidth || mip.height != expectedHeight)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::ResolutionMismatch, .mipIndex = mipIndexU32 });
        if(mip.blockCountX != planeLayoutResult->blocksX || mip.blockCountY != planeLayoutResult->blocksY)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::BlockGridMismatch, .mipIndex = mipIndexU32 });
        if(mip.sliceCount != *sliceCountResult)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::SliceCountMismatch, .mipIndex = mipIndexU32 });
        if(mip.offsetBytes != expectedOffsetBytes || mip.sizeBytes != expectedSizeBytes)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::ByteRangeMismatch, .mipIndex = mipIndexU32 });
        if(expectedSizeBytes > Limit<u64>::s_Max - expectedOffsetBytes)
            return MakeUnexpected(MipChainLayoutError{ .failure = MipChainLayoutFailure::OffsetOverflow, .mipIndex = mipIndexU32 });

        expectedOffsetBytes += expectedSizeBytes;
        expectedWidth = NextMipExtent(expectedWidth);
        expectedHeight = NextMipExtent(expectedHeight);
        if(dimension == TextureDimension::Texture3D)
            expectedDepth = NextMipExtent(expectedDepth);
    }

    return expectedOffsetBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_TextureMagic = 0x54455831u; // TEX1
inline constexpr u32 s_TextureVersion = 4u;

static constexpr usize s_PackedAlignBytes = 1u;

// alphaInfo stores its mode in bits 0..7 and the UNORM8 constant in bits 8..15; the high half must remain zero.
#pragma pack(push, 1)
struct HeaderBinary{
    u32 magic = s_TextureMagic;
    u32 version = s_TextureVersion;
    u32 dimension = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 0u;
    u32 mipCount = 0u;
    u32 alphaInfo = 0u;
    u32 payloadFormat = 0u;
    u64 payloadByteCount = 0u;
};
#pragma pack(pop)
static constexpr usize s_HeaderBinaryByteSize = 44u;
static_assert(sizeof(HeaderBinary) == s_HeaderBinaryByteSize, "Texture V4 header layout drifted");
static_assert(alignof(HeaderBinary) == s_PackedAlignBytes, "Texture V4 header must stay packed");
static_assert(IsStandardLayout_V<HeaderBinary>, "Texture V4 header must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<HeaderBinary>, "Texture V4 header must stay binary-serializable");

inline constexpr u32 s_AlphaInfoModeMask = 0x000000FFu;
inline constexpr u32 s_AlphaInfoConstantShift = 8u;
inline constexpr u32 s_AlphaInfoConstantMask = 0x0000FF00u;
inline constexpr u32 s_AlphaInfoReservedMask = 0xFFFF0000u;

#pragma pack(push, 1)
struct MipLevelBinary{
    u32 width = 0u;
    u32 height = 0u;
    u32 sliceCount = 0u;
    u32 blockCountX = 0u;
    u32 blockCountY = 0u;
    u32 reserved = 0u;
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;
};
#pragma pack(pop)
static constexpr usize s_MipLevelBinaryByteSize = 40u;
static_assert(sizeof(MipLevelBinary) == s_MipLevelBinaryByteSize, "Texture mip level layout drifted");
static_assert(alignof(MipLevelBinary) == s_PackedAlignBytes, "Texture mip level must stay packed");
static_assert(IsStandardLayout_V<MipLevelBinary>, "Texture mip level must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MipLevelBinary>, "Texture mip level must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

