// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "primitives.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Format{
    enum Enum : u8{
        UNKNOWN,

        R8_UINT,
        R8_SINT,
        R8_UNORM,
        R8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        R16_UINT,
        R16_SINT,
        R16_UNORM,
        R16_SNORM,
        R16_FLOAT,
        BGRA4_UNORM,
        B5G6R5_UNORM,
        B5G5R5A1_UNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        RGBA8_UNORM_SRGB,
        BGRA8_UNORM,
        BGRA8_UNORM_SRGB,
        BGRX8_UNORM,
        SRGBA8_UNORM,
        SBGRA8_UNORM,
        SBGRX8_UNORM,
        R10G10B10A2_UNORM,
        R11G11B10_FLOAT,
        RG16_UINT,
        RG16_SINT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_FLOAT,
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,

        D16,
        D24S8,
        X24G8_UINT,
        D32,
        D32S8,
        X32G8_UINT,

        BC1_UNORM,
        BC1_UNORM_SRGB,
        BC2_UNORM,
        BC2_UNORM_SRGB,
        BC3_UNORM,
        BC3_UNORM_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_UNORM_SRGB,

        ASTC_4x4_UNORM,
        ASTC_4x4_UNORM_SRGB,
        ASTC_4x4_FLOAT,
        ASTC_5x4_UNORM,
        ASTC_5x4_UNORM_SRGB,
        ASTC_5x4_FLOAT,
        ASTC_5x5_UNORM,
        ASTC_5x5_UNORM_SRGB,
        ASTC_5x5_FLOAT,
        ASTC_6x5_UNORM,
        ASTC_6x5_UNORM_SRGB,
        ASTC_6x5_FLOAT,
        ASTC_6x6_UNORM,
        ASTC_6x6_UNORM_SRGB,
        ASTC_6x6_FLOAT,
        ASTC_8x5_UNORM,
        ASTC_8x5_UNORM_SRGB,
        ASTC_8x5_FLOAT,
        ASTC_8x6_UNORM,
        ASTC_8x6_UNORM_SRGB,
        ASTC_8x6_FLOAT,
        ASTC_10x5_UNORM,
        ASTC_10x5_UNORM_SRGB,
        ASTC_10x5_FLOAT,
        ASTC_10x6_UNORM,
        ASTC_10x6_UNORM_SRGB,
        ASTC_10x6_FLOAT,
        ASTC_8x8_UNORM,
        ASTC_8x8_UNORM_SRGB,
        ASTC_8x8_FLOAT,
        ASTC_10x8_UNORM,
        ASTC_10x8_UNORM_SRGB,
        ASTC_10x8_FLOAT,
        ASTC_10x10_UNORM,
        ASTC_10x10_UNORM_SRGB,
        ASTC_10x10_FLOAT,
        ASTC_12x10_UNORM,
        ASTC_12x10_UNORM_SRGB,
        ASTC_12x10_FLOAT,
        ASTC_12x12_UNORM,
        ASTC_12x12_UNORM_SRGB,
        ASTC_12x12_FLOAT,

        kCount
    };
};

namespace FormatKind{
    enum Enum : u8{
        Integer,
        Normalized,
        Float,
        DepthStencil,

        kCount
    };
};

struct FormatInfo{
    const char* name;
    Format::Enum format;
    u8 bytesPerBlock;
    u8 blockSize;
    FormatKind::Enum kind;
    bool hasRed : 1;
    bool hasGreen : 1;
    bool hasBlue : 1;
    bool hasAlpha : 1;
    bool hasDepth : 1;
    bool hasStencil : 1;
    bool isSigned : 1;
    bool isSRGB : 1;

    constexpr FormatInfo(
        const Format::Enum formatValue,
        const char* const nameValue,
        const u8 bytesPerBlockValue,
        const u8 blockSizeValue,
        const FormatKind::Enum kindValue,
        const bool hasRedValue,
        const bool hasGreenValue,
        const bool hasBlueValue,
        const bool hasAlphaValue,
        const bool hasDepthValue,
        const bool hasStencilValue,
        const bool isSignedValue,
        const bool isSrgbValue
    )
        : name(nameValue)
        , format(formatValue)
        , bytesPerBlock(bytesPerBlockValue)
        , blockSize(blockSizeValue)
        , kind(kindValue)
        , hasRed(hasRedValue)
        , hasGreen(hasGreenValue)
        , hasBlue(hasBlueValue)
        , hasAlpha(hasAlphaValue)
        , hasDepth(hasDepthValue)
        , hasStencil(hasStencilValue)
        , isSigned(isSignedValue)
        , isSRGB(isSrgbValue)
    {}
};
static_assert(sizeof(FormatInfo) == 16u, "FormatInfo should remain tightly packed");

const FormatInfo& GetFormatInfo(Format::Enum format)noexcept;
[[nodiscard]] u32 GetFormatBlockWidth(const FormatInfo& formatInfo)noexcept;
[[nodiscard]] u32 GetFormatBlockHeight(const FormatInfo& formatInfo)noexcept;

namespace FormatSupport{
    enum Mask : u32{
        None = 0,

        Buffer = 1 << 0,
        IndexBuffer = 1 << 1,
        VertexBuffer = 1 << 2,

        Texture = 1 << 3,
        DepthStencil = 1 << 4,
        RenderTarget = 1 << 5,
        Blendable = 1 << 6,

        ShaderLoad = 1 << 7,
        ShaderSample = 1 << 8,
        ShaderUavLoad = 1 << 9,
        ShaderUavStore = 1 << 10,
        ShaderAtomicCounter = 1 << 11,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

