// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "format.h"
#include "resource.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_format{
static constexpr int s_UnknownFormatBlockX = 0;
static constexpr int s_UnknownFormatBlockY = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<FormatInfo, Format::Count> s_FormatInfo = {{
    { Format::UNKNOWN              , "UNKNOWN", __hidden_graphics_format::s_UnknownFormatBlockX, __hidden_graphics_format::s_UnknownFormatBlockY, FormatKind::Integer     , false, false, false, false, false, false, false, false },

    { Format::R8_UINT              , "R8_UINT"              ,  1,  1, FormatKind::Integer     , true , false, false, false, false, false, false, false },
    { Format::R8_SINT              , "R8_SINT"              ,  1,  1, FormatKind::Integer     , true , false, false, false, false, false, true , false },
    { Format::R8_UNORM             , "R8_UNORM"             ,  1,  1, FormatKind::Normalized  , true , false, false, false, false, false, false, false },
    { Format::R8_SNORM             , "R8_SNORM"             ,  1,  1, FormatKind::Normalized  , true , false, false, false, false, false, true , false },
    { Format::RG8_UINT             , "RG8_UINT"             ,  2,  1, FormatKind::Integer     , true , true , false, false, false, false, false, false },
    { Format::RG8_SINT             , "RG8_SINT"             ,  2,  1, FormatKind::Integer     , true , true , false, false, false, false, true , false },
    { Format::RG8_UNORM            , "RG8_UNORM"            ,  2,  1, FormatKind::Normalized  , true , true , false, false, false, false, false, false },
    { Format::RG8_SNORM            , "RG8_SNORM"            ,  2,  1, FormatKind::Normalized  , true , true , false, false, false, false, true , false },
    { Format::RGB8_UNORM           , "RGB8_UNORM"           ,  3,  1, FormatKind::Normalized  , true , true , true , false, false, false, false, false },
    { Format::R16_UINT             , "R16_UINT"             ,  2,  1, FormatKind::Integer     , true , false, false, false, false, false, false, false },
    { Format::R16_SINT             , "R16_SINT"             ,  2,  1, FormatKind::Integer     , true , false, false, false, false, false, true , false },
    { Format::R16_UNORM            , "R16_UNORM"            ,  2,  1, FormatKind::Normalized  , true , false, false, false, false, false, false, false },
    { Format::R16_SNORM            , "R16_SNORM"            ,  2,  1, FormatKind::Normalized  , true , false, false, false, false, false, true , false },
    { Format::R16_FLOAT            , "R16_FLOAT"            ,  2,  1, FormatKind::Float       , true , false, false, false, false, false, true , false },
    { Format::BGRA4_UNORM          , "BGRA4_UNORM"          ,  2,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::B5G6R5_UNORM         , "B5G6R5_UNORM"         ,  2,  1, FormatKind::Normalized  , true , true , true , false, false, false, false, false },
    { Format::B5G5R5A1_UNORM       , "B5G5R5A1_UNORM"       ,  2,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::RGBA8_UINT           , "RGBA8_UINT"           ,  4,  1, FormatKind::Integer     , true , true , true , true , false, false, false, false },
    { Format::RGBA8_SINT           , "RGBA8_SINT"           ,  4,  1, FormatKind::Integer     , true , true , true , true , false, false, true , false },
    { Format::RGBA8_UNORM          , "RGBA8_UNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::RGBA8_SNORM          , "RGBA8_SNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, true , false },
    { Format::RGBA8_UNORM_SRGB     , "RGBA8_UNORM_SRGB"     ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BGRA8_UNORM          , "BGRA8_UNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BGRA8_UNORM_SRGB     , "BGRA8_UNORM_SRGB"     ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BGRX8_UNORM          , "BGRX8_UNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , false, false, false, false, false },
    { Format::SRGBA8_UNORM         , "SRGBA8_UNORM"         ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::SBGRA8_UNORM         , "SBGRA8_UNORM"         ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::SBGRX8_UNORM         , "SBGRX8_UNORM"         ,  4,  1, FormatKind::Normalized  , true , true , true , false, false, false, false, true  },
    { Format::R10G10B10A2_UNORM    , "R10G10B10A2_UNORM"    ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::R11G11B10_FLOAT      , "R11G11B10_FLOAT"      ,  4,  1, FormatKind::Float       , true , true , true , false, false, false, false, false },
    { Format::RG16_UINT            , "RG16_UINT"            ,  4,  1, FormatKind::Integer     , true , true , false, false, false, false, false, false },
    { Format::RG16_SINT            , "RG16_SINT"            ,  4,  1, FormatKind::Integer     , true , true , false, false, false, false, true , false },
    { Format::RG16_UNORM           , "RG16_UNORM"           ,  4,  1, FormatKind::Normalized  , true , true , false, false, false, false, false, false },
    { Format::RG16_SNORM           , "RG16_SNORM"           ,  4,  1, FormatKind::Normalized  , true , true , false, false, false, false, true , false },
    { Format::RG16_FLOAT           , "RG16_FLOAT"           ,  4,  1, FormatKind::Float       , true , true , false, false, false, false, true , false },
    { Format::R32_UINT             , "R32_UINT"             ,  4,  1, FormatKind::Integer     , true , false, false, false, false, false, false, false },
    { Format::R32_SINT             , "R32_SINT"             ,  4,  1, FormatKind::Integer     , true , false, false, false, false, false, true , false },
    { Format::R32_FLOAT            , "R32_FLOAT"            ,  4,  1, FormatKind::Float       , true , false, false, false, false, false, true , false },
    { Format::RGBA16_UINT          , "RGBA16_UINT"          ,  8,  1, FormatKind::Integer     , true , true , true , true , false, false, false, false },
    { Format::RGBA16_SINT          , "RGBA16_SINT"          ,  8,  1, FormatKind::Integer     , true , true , true , true , false, false, true , false },
    { Format::RGBA16_FLOAT         , "RGBA16_FLOAT"         ,  8,  1, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::RGBA16_UNORM         , "RGBA16_UNORM"         ,  8,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::RGBA16_SNORM         , "RGBA16_SNORM"         ,  8,  1, FormatKind::Normalized  , true , true , true , true , false, false, true , false },
    { Format::RG32_UINT            , "RG32_UINT"            ,  8,  1, FormatKind::Integer     , true , true , false, false, false, false, false, false },
    { Format::RG32_SINT            , "RG32_SINT"            ,  8,  1, FormatKind::Integer     , true , true , false, false, false, false, true , false },
    { Format::RG32_FLOAT           , "RG32_FLOAT"           ,  8,  1, FormatKind::Float       , true , true , false, false, false, false, true , false },
    { Format::RGB32_UINT           , "RGB32_UINT"           , 12,  1, FormatKind::Integer     , true , true , true , false, false, false, false, false },
    { Format::RGB32_SINT           , "RGB32_SINT"           , 12,  1, FormatKind::Integer     , true , true , true , false, false, false, true , false },
    { Format::RGB32_FLOAT          , "RGB32_FLOAT"          , 12,  1, FormatKind::Float       , true , true , true , false, false, false, true , false },
    { Format::RGBA32_UINT          , "RGBA32_UINT"          , 16,  1, FormatKind::Integer     , true , true , true , true , false, false, false, false },
    { Format::RGBA32_SINT          , "RGBA32_SINT"          , 16,  1, FormatKind::Integer     , true , true , true , true , false, false, true , false },
    { Format::RGBA32_FLOAT         , "RGBA32_FLOAT"         , 16,  1, FormatKind::Float       , true , true , true , true , false, false, true , false },

    { Format::D16                  , "D16"                  ,  2,  1, FormatKind::DepthStencil, false, false, false, false, true , false, false, false },
    { Format::D24S8                , "D24S8"                ,  4,  1, FormatKind::DepthStencil, false, false, false, false, true , true , false, false },
    { Format::X24G8_UINT           , "X24G8_UINT"           ,  4,  1, FormatKind::Integer     , false, false, false, false, false, true , false, false },
    { Format::D32                  , "D32"                  ,  4,  1, FormatKind::DepthStencil, false, false, false, false, true , false, false, false },
    { Format::D32S8                , "D32S8"                ,  8,  1, FormatKind::DepthStencil, false, false, false, false, true , true , false, false },
    { Format::X32G8_UINT           , "X32G8_UINT"           ,  8,  1, FormatKind::Integer     , false, false, false, false, false, true , false, false },

    { Format::BC1_UNORM            , "BC1_UNORM"            ,  8,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC1_UNORM_SRGB       , "BC1_UNORM_SRGB"       ,  8,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BC2_UNORM            , "BC2_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC2_UNORM_SRGB       , "BC2_UNORM_SRGB"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BC3_UNORM            , "BC3_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC3_UNORM_SRGB       , "BC3_UNORM_SRGB"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BC4_UNORM            , "BC4_UNORM"            ,  8,  4, FormatKind::Normalized  , true , false, false, false, false, false, false, false },
    { Format::BC4_SNORM            , "BC4_SNORM"            ,  8,  4, FormatKind::Normalized  , true , false, false, false, false, false, true , false },
    { Format::BC5_UNORM            , "BC5_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , false, false, false, false, false, false },
    { Format::BC5_SNORM            , "BC5_SNORM"            , 16,  4, FormatKind::Normalized  , true , true , false, false, false, false, true , false },
    { Format::BC6H_UFLOAT          , "BC6H_UFLOAT"          , 16,  4, FormatKind::Float       , true , true , true , false, false, false, false, false },
    { Format::BC6H_SFLOAT          , "BC6H_SFLOAT"          , 16,  4, FormatKind::Float       , true , true , true , false, false, false, true , false },
    { Format::BC7_UNORM            , "BC7_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC7_UNORM_SRGB       , "BC7_UNORM_SRGB"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },

    { Format::ASTC_4x4_UNORM       , "ASTC_4x4_UNORM"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_4x4_UNORM_SRGB  , "ASTC_4x4_UNORM_SRGB"  , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_4x4_FLOAT       , "ASTC_4x4_FLOAT"       , 16,  4, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_5x4_UNORM       , "ASTC_5x4_UNORM"       , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_5x4_UNORM_SRGB  , "ASTC_5x4_UNORM_SRGB"  , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_5x4_FLOAT       , "ASTC_5x4_FLOAT"       , 16,  5, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_5x5_UNORM       , "ASTC_5x5_UNORM"       , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_5x5_UNORM_SRGB  , "ASTC_5x5_UNORM_SRGB"  , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_5x5_FLOAT       , "ASTC_5x5_FLOAT"       , 16,  5, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_6x5_UNORM       , "ASTC_6x5_UNORM"       , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_6x5_UNORM_SRGB  , "ASTC_6x5_UNORM_SRGB"  , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_6x5_FLOAT       , "ASTC_6x5_FLOAT"       , 16,  6, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_6x6_UNORM       , "ASTC_6x6_UNORM"       , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_6x6_UNORM_SRGB  , "ASTC_6x6_UNORM_SRGB"  , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_6x6_FLOAT       , "ASTC_6x6_FLOAT"       , 16,  6, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_8x5_UNORM       , "ASTC_8x5_UNORM"       , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_8x5_UNORM_SRGB  , "ASTC_8x5_UNORM_SRGB"  , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_8x5_FLOAT       , "ASTC_8x5_FLOAT"       , 16,  8, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_8x6_UNORM       , "ASTC_8x6_UNORM"       , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_8x6_UNORM_SRGB  , "ASTC_8x6_UNORM_SRGB"  , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_8x6_FLOAT       , "ASTC_8x6_FLOAT"       , 16,  8, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x5_UNORM      , "ASTC_10x5_UNORM"      , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x5_UNORM_SRGB , "ASTC_10x5_UNORM_SRGB" , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x5_FLOAT      , "ASTC_10x5_FLOAT"      , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x6_UNORM      , "ASTC_10x6_UNORM"      , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x6_UNORM_SRGB , "ASTC_10x6_UNORM_SRGB" , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x6_FLOAT      , "ASTC_10x6_FLOAT"      , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_8x8_UNORM       , "ASTC_8x8_UNORM"       , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_8x8_UNORM_SRGB  , "ASTC_8x8_UNORM_SRGB"  , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_8x8_FLOAT       , "ASTC_8x8_FLOAT"       , 16,  8, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x8_UNORM      , "ASTC_10x8_UNORM"      , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x8_UNORM_SRGB , "ASTC_10x8_UNORM_SRGB" , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x8_FLOAT      , "ASTC_10x8_FLOAT"      , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x10_UNORM     , "ASTC_10x10_UNORM"     , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x10_UNORM_SRGB, "ASTC_10x10_UNORM_SRGB", 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x10_FLOAT     , "ASTC_10x10_FLOAT"     , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_12x10_UNORM     , "ASTC_12x10_UNORM"     , 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_12x10_UNORM_SRGB, "ASTC_12x10_UNORM_SRGB", 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_12x10_FLOAT     , "ASTC_12x10_FLOAT"     , 16, 12, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_12x12_UNORM     , "ASTC_12x12_UNORM"     , 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_12x12_UNORM_SRGB, "ASTC_12x12_UNORM_SRGB", 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_12x12_FLOAT     , "ASTC_12x12_FLOAT"     , 16, 12, FormatKind::Float       , true , true , true , true , false, false, true , false },
}};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const FormatInfo& GetFormatInfo(Format::Enum format)noexcept{
    FOUNDATION_ASSERT_MSG(static_cast<usize>(format) < static_cast<usize>(Format::Count), FOUNDATION_TEXT("Format::Enum out of range"));
    if(static_cast<usize>(format) >= static_cast<usize>(Format::Count))
        return s_FormatInfo[static_cast<u32>(Format::UNKNOWN)];

    return s_FormatInfo[static_cast<u32>(format)];
}

Expected<TextureUploadAspect::Enum> ResolveTextureUploadAspect(const FormatInfo& formatInfo, const TextureUploadAspect::Enum requestedAspect)noexcept{
    if(requestedAspect >= TextureUploadAspect::Count)
        return MakeUnexpected(Failure{});

    if(formatInfo.hasDepth && formatInfo.hasStencil){
        if(requestedAspect != TextureUploadAspect::Depth && requestedAspect != TextureUploadAspect::Stencil)
            return MakeUnexpected(Failure{});
        return requestedAspect;
    }

    if(formatInfo.hasDepth){
        if(requestedAspect != TextureUploadAspect::Automatic && requestedAspect != TextureUploadAspect::Depth)
            return MakeUnexpected(Failure{});
        return TextureUploadAspect::Depth;
    }

    if(formatInfo.hasStencil){
        if(requestedAspect != TextureUploadAspect::Automatic && requestedAspect != TextureUploadAspect::Stencil)
            return MakeUnexpected(Failure{});
        return TextureUploadAspect::Stencil;
    }

    if(requestedAspect != TextureUploadAspect::Automatic && requestedAspect != TextureUploadAspect::Color)
        return MakeUnexpected(Failure{});
    return TextureUploadAspect::Color;
}

Expected<TextureUploadAspectLayout> GetTextureUploadAspectLayout(const FormatInfo& formatInfo, const TextureUploadAspect::Enum requestedAspect)noexcept{
    TextureUploadAspectLayout layout;

    const auto resolvedAspect = ResolveTextureUploadAspect(formatInfo, requestedAspect);
    if(!resolvedAspect)
        return MakeUnexpected(Failure{});

    layout.blockWidth = GetFormatBlockWidth(formatInfo);
    layout.blockHeight = GetFormatBlockHeight(formatInfo);
    layout.bytesPerBlock = formatInfo.bytesPerBlock;
    // Depth/stencil image aspects are copied from independent CPU planes.
    // D24S8/D32S8 both use a 32-bit depth plane and an 8-bit stencil plane even though their opaque whole-image allocation sizes differ.
    if(*resolvedAspect == TextureUploadAspect::Depth && formatInfo.hasStencil)
        layout.bytesPerBlock = sizeof(u32);
    else if(*resolvedAspect == TextureUploadAspect::Stencil)
        layout.bytesPerBlock = sizeof(u8);

    if(layout.blockWidth == 0u || layout.blockHeight == 0u || layout.bytesPerBlock == 0u)
        return MakeUnexpected(Failure{});
    return layout;
}

Expected<usize> ComputeTextureUploadByteSize(const TextureDesc& textureDesc, const u32 arraySlice, const u32 mipLevel, const usize rowPitch, const usize depthPitch, const TextureUploadAspect::Enum aspect)noexcept{
    if(
        textureDesc.width == 0u
        || textureDesc.height == 0u
        || textureDesc.depth == 0u
        || textureDesc.mipLevels == 0u
        || textureDesc.arraySize == 0u
        || textureDesc.sampleCount != 1u
        || mipLevel >= textureDesc.mipLevels
        || mipLevel >= sizeof(u32) * s_BitsPerByte
        || arraySlice >= textureDesc.arraySize
        || static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::Count)
    )
        return MakeUnexpected(Failure{});

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const auto aspectLayout = GetTextureUploadAspectLayout(formatInfo, aspect);
    if(!aspectLayout)
        return MakeUnexpected(Failure{});

    const u32 width = Max<u32>(1u, textureDesc.width >> mipLevel);
    const u32 height = Max<u32>(1u, textureDesc.height >> mipLevel);
    const u32 depth = textureDesc.dimension == TextureDimension::Texture3D ? Max<u32>(1u, textureDesc.depth >> mipLevel) : 1u;
    const u64 blockCountX = DivideUp(static_cast<u64>(width), static_cast<u64>(aspectLayout->blockWidth));
    const u64 blockCountY = DivideUp(static_cast<u64>(height), static_cast<u64>(aspectLayout->blockHeight));
    if(blockCountX > Limit<u64>::s_Max / aspectLayout->bytesPerBlock)
        return MakeUnexpected(Failure{});

    const u64 naturalRowPitch = blockCountX * aspectLayout->bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0u ? static_cast<u64>(rowPitch) : naturalRowPitch;
    if(effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % aspectLayout->bytesPerBlock) != 0u || blockCountY > Limit<u64>::s_Max / effectiveRowPitch)
        return MakeUnexpected(Failure{});

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0u ? static_cast<u64>(depthPitch) : packedSlicePitch;
    if(effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0u)
        return MakeUnexpected(Failure{});

    // The native texture-copy contract uses 32-bit texel pitch fields in CommandList::writeTexture.
    // Validate them at declaration time so an accepted graph upload cannot lower to a native no-op after the command list rejects it.
    const u64 bufferRowBlocks = effectiveRowPitch / aspectLayout->bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(
        bufferRowBlocks > Limit<u64>::s_Max / aspectLayout->blockWidth
        || bufferImageBlocks > Limit<u64>::s_Max / aspectLayout->blockHeight
        || bufferRowBlocks * aspectLayout->blockWidth > Limit<u32>::s_Max
        || bufferImageBlocks * aspectLayout->blockHeight > Limit<u32>::s_Max
    )
        return MakeUnexpected(Failure{});

    if(depth > 1u && static_cast<u64>(depth - 1u) > (Limit<u64>::s_Max - packedSlicePitch) / effectiveDepthPitch)
        return MakeUnexpected(Failure{});

    const u64 requiredBytes = depth > 1u ? effectiveDepthPitch * static_cast<u64>(depth - 1u) + packedSlicePitch : packedSlicePitch;
    if(requiredBytes > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<usize>(requiredBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

