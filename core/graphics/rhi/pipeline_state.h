// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "shader.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BlendFactor{
    static constexpr u8 s_BlendFactorZeroBase = 1;
    static constexpr u8 s_BlendFactorConstantColorBase = 14;
    enum Enum : u8{
        Zero = s_BlendFactorZeroBase,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstAlpha,
        InvDstAlpha,
        DstColor,
        InvDstColor,
        SrcAlphaSaturate,
        ConstantColor = s_BlendFactorConstantColorBase,
        InvConstantColor,
        Src1Color,
        InvSrc1Color,
        Src1Alpha,
        InvSrc1Alpha,
    };
};

namespace BlendOp{
    static constexpr u8 s_BlendOpAddBase = 1;
    enum Enum : u8{
        Add = s_BlendOpAddBase,
       Subtract,
       ReverseSubtract,
       Min,
       Max,
    };
};

namespace ColorMask{
    static constexpr u8 s_ColorMaskNoneBase = 0;
    enum Mask : u8{
        None = s_ColorMaskNoneBase,

        Red = 1 << 0,
        Green = 1 << 1,
        Blue = 1 << 2,
        Alpha = 1 << 3,

        All = 0xF,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct BlendState{
    struct RenderTarget{
        BlendFactor::Enum srcBlend = BlendFactor::One;
        BlendFactor::Enum destBlend = BlendFactor::Zero;
        BlendOp::Enum blendOp = BlendOp::Add;
        BlendFactor::Enum srcBlendAlpha = BlendFactor::One;
        BlendFactor::Enum destBlendAlpha = BlendFactor::Zero;
        BlendOp::Enum blendOpAlpha = BlendOp::Add;
        ColorMask::Mask colorWriteMask = ColorMask::All;
        bool blendEnable = false;

        constexpr RenderTarget& setBlendEnable(bool enable)noexcept{ blendEnable = enable; return *this; }
        constexpr RenderTarget& enableBlend()noexcept{ blendEnable = true; return *this; }
        constexpr RenderTarget& disableBlend()noexcept{ blendEnable = false; return *this; }
        constexpr RenderTarget& setSrcBlend(BlendFactor::Enum value)noexcept{ srcBlend = value; return *this; }
        constexpr RenderTarget& setDestBlend(BlendFactor::Enum value)noexcept{ destBlend = value; return *this; }
        constexpr RenderTarget& setBlendOp(BlendOp::Enum value)noexcept{ blendOp = value; return *this; }
        constexpr RenderTarget& setSrcBlendAlpha(BlendFactor::Enum value)noexcept{ srcBlendAlpha = value; return *this; }
        constexpr RenderTarget& setDestBlendAlpha(BlendFactor::Enum value)noexcept{ destBlendAlpha = value; return *this; }
        constexpr RenderTarget& setBlendOpAlpha(BlendOp::Enum value)noexcept{ blendOpAlpha = value; return *this; }
        constexpr RenderTarget& setColorWriteMask(ColorMask::Mask value)noexcept{ colorWriteMask = value; return *this; }

        [[nodiscard]] bool usesConstantColor()const noexcept;
    };

    RenderTarget targets[s_MaxRenderTargets];
    bool alphaToCoverageEnable = false;

    constexpr BlendState& setRenderTarget(u32 index, const RenderTarget& target)noexcept{ targets[index] = target; return *this; }
    constexpr BlendState& setAlphaToCoverageEnable(bool enable)noexcept{ alphaToCoverageEnable = enable; return *this; }
    constexpr BlendState& enableAlphaToCoverage()noexcept{ alphaToCoverageEnable = true; return *this; }
    constexpr BlendState& disableAlphaToCoverage()noexcept{ alphaToCoverageEnable = false; return *this; }

    [[nodiscard]] bool usesConstantColor(u32 numTargets)const;
};
constexpr bool operator==(const BlendState::RenderTarget& lhs, const BlendState::RenderTarget& rhs)noexcept{
    return
        lhs.blendEnable == rhs.blendEnable
        && lhs.srcBlend == rhs.srcBlend
        && lhs.destBlend == rhs.destBlend
        && lhs.blendOp == rhs.blendOp
        && lhs.srcBlendAlpha == rhs.srcBlendAlpha
        && lhs.destBlendAlpha == rhs.destBlendAlpha
        && lhs.blendOpAlpha == rhs.blendOpAlpha
        && lhs.colorWriteMask == rhs.colorWriteMask
    ;
}
constexpr bool operator!=(const BlendState::RenderTarget& lhs, const BlendState::RenderTarget& rhs)noexcept{ return !(lhs == rhs); }
constexpr bool operator==(const BlendState& lhs, const BlendState& rhs)noexcept{
    if(lhs.alphaToCoverageEnable != rhs.alphaToCoverageEnable)
        return false;

    for(u32 i = 0u; i < s_MaxRenderTargets; ++i){
        if(lhs.targets[i] != rhs.targets[i])
            return false;
    }

    return true;
}
constexpr bool operator!=(const BlendState& lhs, const BlendState& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RasterFillMode{
    enum Enum : u8{
        Solid,
        Wireframe,
    };
};

namespace RasterCullMode{
    enum Enum : u8{
        Back,
        Front,
        None,
    };
};

struct RasterState{
    RasterFillMode::Enum fillMode = RasterFillMode::Solid;
    RasterCullMode::Enum cullMode = RasterCullMode::Back;
    bool frontCounterClockwise = false;
    bool depthClipEnable = false;
    i32 depthBias = 0;
    f32 depthBiasClamp = 0.f;
    f32 slopeScaledDepthBias = 0.f;

    constexpr RasterState& setFillMode(RasterFillMode::Enum value)noexcept{ fillMode = value; return *this; }
    constexpr RasterState& setFillSolid()noexcept{ fillMode = RasterFillMode::Solid; return *this; }
    constexpr RasterState& setFillWireframe()noexcept{ fillMode = RasterFillMode::Wireframe; return *this; }
    constexpr RasterState& setCullMode(RasterCullMode::Enum value)noexcept{ cullMode = value; return *this; }
    constexpr RasterState& setCullBack()noexcept{ cullMode = RasterCullMode::Back; return *this; }
    constexpr RasterState& setCullFront()noexcept{ cullMode = RasterCullMode::Front; return *this; }
    constexpr RasterState& setCullNone()noexcept{ cullMode = RasterCullMode::None; return *this; }
    constexpr RasterState& setFrontCounterClockwise(bool value)noexcept{ frontCounterClockwise = value; return *this; }
    constexpr RasterState& setDepthClipEnable(bool value)noexcept{ depthClipEnable = value; return *this; }
    constexpr RasterState& enableDepthClip()noexcept{ depthClipEnable = true; return *this; }
    constexpr RasterState& disableDepthClip()noexcept{ depthClipEnable = false; return *this; }
    constexpr RasterState& setDepthBias(i32 value)noexcept{ depthBias = value; return *this; }
    constexpr RasterState& setDepthBiasClamp(f32 value)noexcept{ depthBiasClamp = value; return *this; }
    constexpr RasterState& setSlopeScaleDepthBias(f32 value)noexcept{ slopeScaledDepthBias = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace StencilOp{
    static constexpr u8 s_StencilOpKeepBase = 1;
    enum Enum : u8{
        Keep = s_StencilOpKeepBase,
        Zero,
        Replace,
        IncrementAndClamp,
        DecrementAndClamp,
        Invert,
        IncrementAndWrap,
        DecrementAndWrap,
    };
};

namespace ComparisonFunc{
    static constexpr u8 s_ComparisonFuncNeverBase = 1;
    enum Enum : u8{
        Never = s_ComparisonFuncNeverBase,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };
};

struct DepthStencilState{
    static constexpr u8 s_AllStencilBits = Limit<u8>::s_Max;

    struct StencilOpDesc{
        StencilOp::Enum failOp = StencilOp::Keep;
        StencilOp::Enum depthFailOp = StencilOp::Keep;
        StencilOp::Enum passOp = StencilOp::Keep;
        ComparisonFunc::Enum stencilFunc = ComparisonFunc::Always;

        constexpr StencilOpDesc& setFailOp(StencilOp::Enum value)noexcept{ failOp = value; return *this; }
        constexpr StencilOpDesc& setDepthFailOp(StencilOp::Enum value)noexcept{ depthFailOp = value; return *this; }
        constexpr StencilOpDesc& setPassOp(StencilOp::Enum value)noexcept{ passOp = value; return *this; }
        constexpr StencilOpDesc& setStencilFunc(ComparisonFunc::Enum value)noexcept{ stencilFunc = value; return *this; }
    };

    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    ComparisonFunc::Enum depthFunc = ComparisonFunc::Less;
    bool stencilEnable = false;
    u8 stencilReadMask = s_AllStencilBits;
    u8 stencilWriteMask = s_AllStencilBits;
    u8 stencilRefValue = 0;
    bool dynamicStencilRef = false;
    StencilOpDesc frontFaceStencil;
    StencilOpDesc backFaceStencil;

    constexpr DepthStencilState& setDepthTestEnable(bool value)noexcept{ depthTestEnable = value; return *this; }
    constexpr DepthStencilState& enableDepthTest()noexcept{ depthTestEnable = true; return *this; }
    constexpr DepthStencilState& disableDepthTest()noexcept{ depthTestEnable = false; return *this; }
    constexpr DepthStencilState& setDepthWriteEnable(bool value)noexcept{ depthWriteEnable = value; return *this; }
    constexpr DepthStencilState& enableDepthWrite()noexcept{ depthWriteEnable = true; return *this; }
    constexpr DepthStencilState& disableDepthWrite()noexcept{ depthWriteEnable = false; return *this; }
    constexpr DepthStencilState& setDepthFunc(ComparisonFunc::Enum value)noexcept{ depthFunc = value; return *this; }
    constexpr DepthStencilState& setStencilEnable(bool value)noexcept{ stencilEnable = value; return *this; }
    constexpr DepthStencilState& enableStencil()noexcept{ stencilEnable = true; return *this; }
    constexpr DepthStencilState& disableStencil()noexcept{ stencilEnable = false; return *this; }
    constexpr DepthStencilState& setStencilReadMask(u8 value)noexcept{ stencilReadMask = value; return *this; }
    constexpr DepthStencilState& setStencilWriteMask(u8 value)noexcept{ stencilWriteMask = value; return *this; }
    constexpr DepthStencilState& setStencilRefValue(u8 value)noexcept{ stencilRefValue = value; return *this; }
    constexpr DepthStencilState& setFrontFaceStencil(const StencilOpDesc& value)noexcept{ frontFaceStencil = value; return *this; }
    constexpr DepthStencilState& setBackFaceStencil(const StencilOpDesc& value)noexcept{ backFaceStencil = value; return *this; }
    constexpr DepthStencilState& setDynamicStencilRef(bool value)noexcept{ dynamicStencilRef = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ViewportState{
    // These are in pixels.
    // Note: you can only set each of these either in the PSO or per draw call in DrawArguments.
    // It is not legal to have the same state set in both the PSO and DrawArguments.
    // Leaving these vectors empty means no state is set.
    FixedVector<Viewport, s_MaxViewports> viewports;
    FixedVector<Rect, s_MaxViewports> scissorRects;

    constexpr ViewportState& addViewport(const Viewport& v){ viewports.push_back(v); return *this; }
    constexpr ViewportState& addScissorRect(const Rect& r){ scissorRects.push_back(r); return *this; }
    constexpr ViewportState& addViewportAndScissorRect(const Viewport& v){ return addViewport(v).addScissorRect(Rect(v)); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SamplerAddressMode{
    enum Enum : u8{
        Clamp,
        Wrap,
        Border,
        Mirror,
        MirrorOnce,
    };
};

namespace SamplerReductionType{
    enum Enum : u8{
        Standard,
        Comparison,
        Minimum,
        Maximum,
    };
};

struct SamplerDesc{
    Color borderColor = 1.f;
    f32 maxAnisotropy = 1.f;
    f32 mipBias = 0.f;

    bool minFilter = true;
    bool magFilter = true;
    bool mipFilter = true;
    SamplerAddressMode::Enum addressU = SamplerAddressMode::Clamp;
    SamplerAddressMode::Enum addressV = SamplerAddressMode::Clamp;
    SamplerAddressMode::Enum addressW = SamplerAddressMode::Clamp;
    SamplerReductionType::Enum reductionType = SamplerReductionType::Standard;

    constexpr SamplerDesc& setBorderColor(const Color& color)noexcept{ borderColor = color; return *this; }
    constexpr SamplerDesc& setMaxAnisotropy(f32 value)noexcept{ maxAnisotropy = value; return *this; }
    constexpr SamplerDesc& setMipBias(f32 value)noexcept{ mipBias = value; return *this; }
    constexpr SamplerDesc& setMinFilter(bool enable)noexcept{ minFilter = enable; return *this; }
    constexpr SamplerDesc& setMagFilter(bool enable)noexcept{ magFilter = enable; return *this; }
    constexpr SamplerDesc& setMipFilter(bool enable)noexcept{ mipFilter = enable; return *this; }
    constexpr SamplerDesc& setAllFilters(bool enable)noexcept{ minFilter = magFilter = mipFilter = enable; return *this; }
    constexpr SamplerDesc& setAddressU(SamplerAddressMode::Enum mode)noexcept{ addressU = mode; return *this; }
    constexpr SamplerDesc& setAddressV(SamplerAddressMode::Enum mode)noexcept{ addressV = mode; return *this; }
    constexpr SamplerDesc& setAddressW(SamplerAddressMode::Enum mode)noexcept{ addressW = mode; return *this; }
    constexpr SamplerDesc& setAllAddressModes(SamplerAddressMode::Enum mode)noexcept{ addressU = addressV = addressW = mode; return *this; }
    constexpr SamplerDesc& setReductionType(SamplerReductionType::Enum type)noexcept{ reductionType = type; return *this; }
};

typedef GraphicsBackend::Handle<Sampler> SamplerHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

