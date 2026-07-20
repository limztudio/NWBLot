#pragma once


#include "shader.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BlendFactor{
    enum Enum : u8{
        Zero = 1,
        One = 2,
        SrcColor = 3,
        InvSrcColor = 4,
        SrcAlpha = 5,
        InvSrcAlpha = 6,
        DstAlpha  = 7,
        InvDstAlpha = 8,
        DstColor = 9,
        InvDstColor = 10,
        SrcAlphaSaturate = 11,
        ConstantColor = 14,
        InvConstantColor = 15,
        Src1Color = 16,
        InvSrc1Color = 17,
        Src1Alpha = 18,
        InvSrc1Alpha = 19,
    };
};

namespace BlendOp{
    enum Enum : u8{
        Add = 1,
       Subtract = 2,
       ReverseSubtract = 3,
       Min = 4,
       Max = 5,
    };
};

namespace ColorMask{
    enum Mask : u8{
        None = 0,

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

            constexpr RenderTarget& setBlendEnable(bool enable){ blendEnable = enable; return *this; }
            constexpr RenderTarget& enableBlend(){ blendEnable = true; return *this; }
            constexpr RenderTarget& disableBlend(){ blendEnable = false; return *this; }
            constexpr RenderTarget& setSrcBlend(BlendFactor::Enum value){ srcBlend = value; return *this; }
            constexpr RenderTarget& setDestBlend(BlendFactor::Enum value){ destBlend = value; return *this; }
            constexpr RenderTarget& setBlendOp(BlendOp::Enum value){ blendOp = value; return *this; }
            constexpr RenderTarget& setSrcBlendAlpha(BlendFactor::Enum value){ srcBlendAlpha = value; return *this; }
            constexpr RenderTarget& setDestBlendAlpha(BlendFactor::Enum value){ destBlendAlpha = value; return *this; }
            constexpr RenderTarget& setBlendOpAlpha(BlendOp::Enum value){ blendOpAlpha = value; return *this; }
            constexpr RenderTarget& setColorWriteMask(ColorMask::Mask value){ colorWriteMask = value; return *this; }

            [[nodiscard]] bool usesConstantColor()const;
        };

        RenderTarget targets[s_MaxRenderTargets];
        bool alphaToCoverageEnable = false;

        constexpr BlendState& setRenderTarget(u32 index, const RenderTarget& target){ targets[index] = target; return *this; }
        constexpr BlendState& setAlphaToCoverageEnable(bool enable){ alphaToCoverageEnable = enable; return *this; }
        constexpr BlendState& enableAlphaToCoverage(){ alphaToCoverageEnable = true; return *this; }
        constexpr BlendState& disableAlphaToCoverage(){ alphaToCoverageEnable = false; return *this; }

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

        for(u32 i = 0; i < s_MaxRenderTargets; ++i){
            if(lhs.targets[i] != rhs.targets[i])
                return false;
        }

        return true;
    }
    constexpr bool operator!=(const BlendState& lhs, const BlendState& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raster State


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
    bool scissorEnable = false;
    bool multisampleEnable = false;
    bool antialiasedLineEnable = false;
    i32 depthBias = 0;
    f32 depthBiasClamp = 0.f;
    f32 slopeScaledDepthBias = 0.f;

    // Extended rasterizer state supported by Maxwell
    u8 forcedSampleCount = 0;
    bool programmableSamplePositionsEnable = false;
    bool conservativeRasterEnable = false;
    bool quadFillEnable = false;
    char samplePositionsX[s_MaxProgrammableSamplePositions]{};
    char samplePositionsY[s_MaxProgrammableSamplePositions]{};

    constexpr RasterState& setFillMode(RasterFillMode::Enum value){ fillMode = value; return *this; }
    constexpr RasterState& setFillSolid(){ fillMode = RasterFillMode::Solid; return *this; }
    constexpr RasterState& setFillWireframe(){ fillMode = RasterFillMode::Wireframe; return *this; }
    constexpr RasterState& setCullMode(RasterCullMode::Enum value){ cullMode = value; return *this; }
    constexpr RasterState& setCullBack(){ cullMode = RasterCullMode::Back; return *this; }
    constexpr RasterState& setCullFront(){ cullMode = RasterCullMode::Front; return *this; }
    constexpr RasterState& setCullNone(){ cullMode = RasterCullMode::None; return *this; }
    constexpr RasterState& setFrontCounterClockwise(bool value){ frontCounterClockwise = value; return *this; }
    constexpr RasterState& setDepthClipEnable(bool value){ depthClipEnable = value; return *this; }
    constexpr RasterState& enableDepthClip(){ depthClipEnable = true; return *this; }
    constexpr RasterState& disableDepthClip(){ depthClipEnable = false; return *this; }
    constexpr RasterState& setScissorEnable(bool value){ scissorEnable = value; return *this; }
    constexpr RasterState& enableScissor(){ scissorEnable = true; return *this; }
    constexpr RasterState& disableScissor(){ scissorEnable = false; return *this; }
    constexpr RasterState& setMultisampleEnable(bool value){ multisampleEnable = value; return *this; }
    constexpr RasterState& enableMultisample(){ multisampleEnable = true; return *this; }
    constexpr RasterState& disableMultisample(){ multisampleEnable = false; return *this; }
    constexpr RasterState& setAntialiasedLineEnable(bool value){ antialiasedLineEnable = value; return *this; }
    constexpr RasterState& enableAntialiasedLine(){ antialiasedLineEnable = true; return *this; }
    constexpr RasterState& disableAntialiasedLine(){ antialiasedLineEnable = false; return *this; }
    constexpr RasterState& setDepthBias(i32 value){ depthBias = value; return *this; }
    constexpr RasterState& setDepthBiasClamp(f32 value){ depthBiasClamp = value; return *this; }
    constexpr RasterState& setSlopeScaleDepthBias(f32 value){ slopeScaledDepthBias = value; return *this; }
    constexpr RasterState& setForcedSampleCount(u8 value){ forcedSampleCount = value; return *this; }
    constexpr RasterState& setProgrammableSamplePositionsEnable(bool value){ programmableSamplePositionsEnable = value; return *this; }
    constexpr RasterState& enableProgrammableSamplePositions(){ programmableSamplePositionsEnable = true; return *this; }
    constexpr RasterState& disableProgrammableSamplePositions(){ programmableSamplePositionsEnable = false; return *this; }
    constexpr RasterState& setConservativeRasterEnable(bool value){ conservativeRasterEnable = value; return *this; }
    constexpr RasterState& enableConservativeRaster(){ conservativeRasterEnable = true; return *this; }
    constexpr RasterState& disableConservativeRaster(){ conservativeRasterEnable = false; return *this; }
    constexpr RasterState& setQuadFillEnable(bool value){ quadFillEnable = value; return *this; }
    constexpr RasterState& enableQuadFill(){ quadFillEnable = true; return *this; }
    constexpr RasterState& disableQuadFill(){ quadFillEnable = false; return *this; }
    constexpr RasterState& setSamplePositions(const i8* x, const i8* y, usize count){
        if(!x || !y)
            return *this;
        const usize samplePositionCount = count < s_MaxProgrammableSamplePositions ? count : s_MaxProgrammableSamplePositions;
        for(usize i = 0; i < samplePositionCount; ++i){
            samplePositionsX[i] = x[i];
            samplePositionsY[i] = y[i];
        }
        return *this;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Depth Stencil State


namespace StencilOp{
    enum Enum : u8{
        Keep = 1,
        Zero = 2,
        Replace = 3,
        IncrementAndClamp = 4,
        DecrementAndClamp = 5,
        Invert = 6,
        IncrementAndWrap = 7,
        DecrementAndWrap = 8,
    };
};

namespace ComparisonFunc{
    enum Enum : u8{
        Never = 1,
        Less = 2,
        Equal = 3,
        LessOrEqual = 4,
        Greater = 5,
        NotEqual = 6,
        GreaterOrEqual = 7,
        Always = 8,
    };
};

struct DepthStencilState{
    struct StencilOpDesc{
        StencilOp::Enum failOp = StencilOp::Keep;
        StencilOp::Enum depthFailOp = StencilOp::Keep;
        StencilOp::Enum passOp = StencilOp::Keep;
        ComparisonFunc::Enum stencilFunc = ComparisonFunc::Always;

        constexpr StencilOpDesc& setFailOp(StencilOp::Enum value){ failOp = value; return *this; }
        constexpr StencilOpDesc& setDepthFailOp(StencilOp::Enum value){ depthFailOp = value; return *this; }
        constexpr StencilOpDesc& setPassOp(StencilOp::Enum value){ passOp = value; return *this; }
        constexpr StencilOpDesc& setStencilFunc(ComparisonFunc::Enum value){ stencilFunc = value; return *this; }
    };

    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    ComparisonFunc::Enum depthFunc = ComparisonFunc::Less;
    bool stencilEnable = false;
    u8 stencilReadMask = 0xff;
    u8 stencilWriteMask = 0xff;
    u8 stencilRefValue = 0;
    bool dynamicStencilRef = false;
    StencilOpDesc frontFaceStencil;
    StencilOpDesc backFaceStencil;

    constexpr DepthStencilState& setDepthTestEnable(bool value){ depthTestEnable = value; return *this; }
    constexpr DepthStencilState& enableDepthTest(){ depthTestEnable = true; return *this; }
    constexpr DepthStencilState& disableDepthTest(){ depthTestEnable = false; return *this; }
    constexpr DepthStencilState& setDepthWriteEnable(bool value){ depthWriteEnable = value; return *this; }
    constexpr DepthStencilState& enableDepthWrite(){ depthWriteEnable = true; return *this; }
    constexpr DepthStencilState& disableDepthWrite(){ depthWriteEnable = false; return *this; }
    constexpr DepthStencilState& setDepthFunc(ComparisonFunc::Enum value){ depthFunc = value; return *this; }
    constexpr DepthStencilState& setStencilEnable(bool value){ stencilEnable = value; return *this; }
    constexpr DepthStencilState& enableStencil(){ stencilEnable = true; return *this; }
    constexpr DepthStencilState& disableStencil(){ stencilEnable = false; return *this; }
    constexpr DepthStencilState& setStencilReadMask(u8 value){ stencilReadMask = value; return *this; }
    constexpr DepthStencilState& setStencilWriteMask(u8 value){ stencilWriteMask = value; return *this; }
    constexpr DepthStencilState& setStencilRefValue(u8 value){ stencilRefValue = value; return *this; }
    constexpr DepthStencilState& setFrontFaceStencil(const StencilOpDesc& value){ frontFaceStencil = value; return *this; }
    constexpr DepthStencilState& setBackFaceStencil(const StencilOpDesc& value){ backFaceStencil = value; return *this; }
    constexpr DepthStencilState& setDynamicStencilRef(bool value){ dynamicStencilRef = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Viewport State


struct ViewportState{
    //These are in pixels
    // note: you can only set each of these either in the PSO or per draw call in DrawArguments
    // it is not legal to have the same state set in both the PSO and DrawArguments
    // leaving these vectors empty means no state is set
    FixedVector<Viewport, s_MaxViewports> viewports;
    FixedVector<Rect, s_MaxViewports> scissorRects;

    constexpr ViewportState& addViewport(const Viewport& v){ viewports.push_back(v); return *this; }
    constexpr ViewportState& addScissorRect(const Rect& r){ scissorRects.push_back(r); return *this; }
    constexpr ViewportState& addViewportAndScissorRect(const Viewport& v){ return addViewport(v).addScissorRect(Rect(v)); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


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

    constexpr SamplerDesc& setBorderColor(const Color& color){ borderColor = color; return *this; }
    constexpr SamplerDesc& setMaxAnisotropy(f32 value){ maxAnisotropy = value; return *this; }
    constexpr SamplerDesc& setMipBias(f32 value){ mipBias = value; return *this; }
    constexpr SamplerDesc& setMinFilter(bool enable){ minFilter = enable; return *this; }
    constexpr SamplerDesc& setMagFilter(bool enable){ magFilter = enable; return *this; }
    constexpr SamplerDesc& setMipFilter(bool enable){ mipFilter = enable; return *this; }
    constexpr SamplerDesc& setAllFilters(bool enable){ minFilter = magFilter = mipFilter = enable; return *this; }
    constexpr SamplerDesc& setAddressU(SamplerAddressMode::Enum mode){ addressU = mode; return *this; }
    constexpr SamplerDesc& setAddressV(SamplerAddressMode::Enum mode){ addressV = mode; return *this; }
    constexpr SamplerDesc& setAddressW(SamplerAddressMode::Enum mode){ addressW = mode; return *this; }
    constexpr SamplerDesc& setAllAddressModes(SamplerAddressMode::Enum mode){ addressU = addressV = addressW = mode; return *this; }
    constexpr SamplerDesc& setReductionType(SamplerReductionType::Enum type){ reductionType = type; return *this; }
};

typedef GraphicsBackend::Handle<Sampler> SamplerHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

