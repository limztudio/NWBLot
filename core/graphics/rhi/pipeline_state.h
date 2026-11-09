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
        b8 blendEnable = false;


    public:

        constexpr RenderTarget& enableBlend()noexcept{ blendEnable = true; return *this; }

        constexpr RenderTarget& setSrcBlend(BlendFactor::Enum value)noexcept{ srcBlend = value; return *this; }

        constexpr RenderTarget& setDestBlend(BlendFactor::Enum value)noexcept{ destBlend = value; return *this; }

        constexpr RenderTarget& setBlendOp(BlendOp::Enum value)noexcept{ blendOp = value; return *this; }

        constexpr RenderTarget& setSrcBlendAlpha(BlendFactor::Enum value)noexcept{ srcBlendAlpha = value; return *this; }

        constexpr RenderTarget& setDestBlendAlpha(BlendFactor::Enum value)noexcept{ destBlendAlpha = value; return *this; }

        constexpr RenderTarget& setBlendOpAlpha(BlendOp::Enum value)noexcept{ blendOpAlpha = value; return *this; }

        constexpr RenderTarget& setColorWriteMask(ColorMask::Mask value)noexcept{ colorWriteMask = value; return *this; }

        [[nodiscard]] b8 usesConstantColor()const noexcept;
    };

    Array<RenderTarget, s_MaxRenderTargets> targets;
    b8 alphaToCoverageEnable = false;


public:
    constexpr BlendState& setRenderTarget(u32 index, const RenderTarget& target)noexcept{ targets[index] = target; return *this; }

    [[nodiscard]] b8 usesConstantColor(u32 numTargets)const noexcept;
};
constexpr b8 operator==(const BlendState::RenderTarget& lhs, const BlendState::RenderTarget& rhs)noexcept{
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

constexpr b8 operator!=(const BlendState::RenderTarget& lhs, const BlendState::RenderTarget& rhs)noexcept{ return !(lhs == rhs); }

constexpr b8 operator==(const BlendState& lhs, const BlendState& rhs)noexcept{
    if(lhs.alphaToCoverageEnable != rhs.alphaToCoverageEnable)
        return false;

    for(u32 i = 0u; i < s_MaxRenderTargets; ++i){
        if(lhs.targets[i] != rhs.targets[i])
            return false;
    }

    return true;
}

constexpr b8 operator!=(const BlendState& lhs, const BlendState& rhs)noexcept{ return !(lhs == rhs); }


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
    b8 frontCounterClockwise = false;
    b8 depthClipEnable = false;
    i32 depthBias = 0;
    f32 depthBiasClamp = 0.f;
    f32 slopeScaledDepthBias = 0.f;


public:

    constexpr RasterState& setCullMode(RasterCullMode::Enum value)noexcept{ cullMode = value; return *this; }

    constexpr RasterState& setCullBack()noexcept{ cullMode = RasterCullMode::Back; return *this; }

    constexpr RasterState& setCullNone()noexcept{ cullMode = RasterCullMode::None; return *this; }

    constexpr RasterState& enableDepthClip()noexcept{ depthClipEnable = true; return *this; }

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
    };

    b8 depthTestEnable = true;
    b8 depthWriteEnable = true;
    ComparisonFunc::Enum depthFunc = ComparisonFunc::Less;
    b8 stencilEnable = false;
    u8 stencilReadMask = s_AllStencilBits;
    u8 stencilWriteMask = s_AllStencilBits;
    u8 stencilRefValue = 0;
    b8 dynamicStencilRef = false;
    StencilOpDesc frontFaceStencil;
    StencilOpDesc backFaceStencil;


public:
    constexpr DepthStencilState& setDepthTestEnable(b8 value)noexcept{ depthTestEnable = value; return *this; }

    constexpr DepthStencilState& enableDepthTest()noexcept{ depthTestEnable = true; return *this; }

    constexpr DepthStencilState& disableDepthTest()noexcept{ depthTestEnable = false; return *this; }

    constexpr DepthStencilState& setDepthWriteEnable(b8 value)noexcept{ depthWriteEnable = value; return *this; }

    constexpr DepthStencilState& enableDepthWrite()noexcept{ depthWriteEnable = true; return *this; }

    constexpr DepthStencilState& disableDepthWrite()noexcept{ depthWriteEnable = false; return *this; }

    constexpr DepthStencilState& setDepthFunc(ComparisonFunc::Enum value)noexcept{ depthFunc = value; return *this; }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ViewportState{
    // These are in pixels.
    // Note: you can only set each of these either in the PSO or per draw call in DrawArguments.
    // It is not legal to have the same state set in both the PSO and DrawArguments.
    // Leaving these vectors empty means no state is set.
    FixedVector<Viewport, s_MaxViewports> viewports;
    FixedVector<Rect, s_MaxViewports> scissorRects;


public:
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

    b8 minFilter = true;
    b8 magFilter = true;
    b8 mipFilter = true;
    SamplerAddressMode::Enum addressU = SamplerAddressMode::Clamp;
    SamplerAddressMode::Enum addressV = SamplerAddressMode::Clamp;
    SamplerAddressMode::Enum addressW = SamplerAddressMode::Clamp;
    SamplerReductionType::Enum reductionType = SamplerReductionType::Standard;


public:

    constexpr SamplerDesc& setAllFilters(b8 enable)noexcept{ minFilter = magFilter = mipFilter = enable; return *this; }

    constexpr SamplerDesc& setAllAddressModes(SamplerAddressMode::Enum mode)noexcept{ addressU = addressV = addressW = mode; return *this; }

};

typedef GraphicsBackend::Handle<Sampler> SamplerHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

