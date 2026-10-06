// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binding.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PrimitiveType{
    enum Enum : u8{
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList,
    };
};

struct SinglePassStereoState{
    u8 renderTargetIndexOffset = 0;
    bool enabled = false;
    bool independentViewportMask = false;

    constexpr SinglePassStereoState& setEnabled(bool value)noexcept{ enabled = value; return *this; }
    constexpr SinglePassStereoState& setIndependentViewportMask(bool value)noexcept{ independentViewportMask = value; return *this; }
    constexpr SinglePassStereoState& setRenderTargetIndexOffset(u16 value)noexcept{ renderTargetIndexOffset = static_cast<u8>(value); return *this; }
};
inline bool operator==(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs)noexcept{
    return
        lhs.enabled == rhs.enabled
        && lhs.independentViewportMask == rhs.independentViewportMask
        && lhs.renderTargetIndexOffset == rhs.renderTargetIndexOffset
    ;
}
inline bool operator!=(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs)noexcept{ return !(lhs == rhs); }

struct RenderState{
    RasterState rasterState;
    BlendState blendState;
    DepthStencilState depthStencilState;
    SinglePassStereoState singlePassStereo;

    constexpr RenderState& setBlendState(const BlendState& value)noexcept{ blendState = value; return *this; }
    constexpr RenderState& setDepthStencilState(const DepthStencilState& value)noexcept{ depthStencilState = value; return *this; }
    constexpr RenderState& setRasterState(const RasterState& value)noexcept{ rasterState = value; return *this; }
    constexpr RenderState& setSinglePassStereoState(const SinglePassStereoState& value)noexcept{ singlePassStereo = value; return *this; }
};

namespace VariableShadingRate{
    enum Enum : u8{
        e1x1,
        e1x2,
        e2x1,
        e2x2,
        e2x4,
        e4x2,
        e4x4,
    };
};

namespace ShadingRateCombiner{
    enum Enum : u8{
        Passthrough,
        Override,
        Min,
        Max,
        ApplyRelative,
    };
};

struct VariableRateShadingState{
    VariableShadingRate::Enum shadingRate = VariableShadingRate::e1x1;
    ShadingRateCombiner::Enum pipelinePrimitiveCombiner = ShadingRateCombiner::Passthrough;
    ShadingRateCombiner::Enum imageCombiner = ShadingRateCombiner::Passthrough;
    bool enabled = false;

    constexpr VariableRateShadingState& setEnabled(bool value)noexcept{ enabled = value; return *this; }
    constexpr VariableRateShadingState& setShadingRate(VariableShadingRate::Enum value)noexcept{ shadingRate = value; return *this; }
    constexpr VariableRateShadingState& setPipelinePrimitiveCombiner(ShadingRateCombiner::Enum value)noexcept{ pipelinePrimitiveCombiner = value; return *this; }
    constexpr VariableRateShadingState& setImageCombiner(ShadingRateCombiner::Enum value)noexcept{ imageCombiner = value; return *this; }
};
inline bool operator==(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs)noexcept{
    return
        lhs.enabled == rhs.enabled
        && lhs.shadingRate == rhs.shadingRate
        && lhs.pipelinePrimitiveCombiner == rhs.pipelinePrimitiveCombiner
        && lhs.imageCombiner == rhs.imageCombiner
    ;
}
inline bool operator!=(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs)noexcept{ return !(lhs == rhs); }

typedef FixedVector<BindingLayoutHandle, s_MaxBindingLayouts> BindingLayoutVector;

struct GraphicsPipelineDesc{
    PrimitiveType::Enum primType = PrimitiveType::TriangleList;
    u32 patchControlPoints = 0;
    InputLayoutHandle inputLayout;

    ShaderHandle vertexShader;
    ShaderHandle hullShader;
    ShaderHandle domainShader;
    ShaderHandle geometryShader;
    ShaderHandle pixelShader;

    RenderState renderState;
    VariableRateShadingState shadingRateState;

    BindingLayoutVector bindingLayouts;

    ~GraphicsPipelineDesc();

    constexpr GraphicsPipelineDesc& setPrimType(PrimitiveType::Enum value)noexcept{ primType = value; return *this; }
    constexpr GraphicsPipelineDesc& setPatchControlPoints(u32 value)noexcept{ patchControlPoints = value; return *this; }
    GraphicsPipelineDesc& setInputLayout(const InputLayoutHandle& value)noexcept;
    GraphicsPipelineDesc& setVertexShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setHullShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setTessellationControlShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setDomainShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setTessellationEvaluationShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setGeometryShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setPixelShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setFragmentShader(const ShaderHandle& value)noexcept;
    constexpr GraphicsPipelineDesc& setRenderState(const RenderState& value)noexcept{ renderState = value; return *this; }
    constexpr GraphicsPipelineDesc& setVariableRateShadingState(const VariableRateShadingState& value)noexcept{ shadingRateState = value; return *this; }
    GraphicsPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<GraphicsPipeline> GraphicsPipelineHandle;

struct ComputePipelineDesc{
    ShaderHandle computeShader;

    BindingLayoutVector bindingLayouts;

    ~ComputePipelineDesc();

    ComputePipelineDesc& setComputeShader(const ShaderHandle& value)noexcept;
    ComputePipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<ComputePipeline> ComputePipelineHandle;

struct MeshletPipelineDesc{
    BindingLayoutVector bindingLayouts;

    ShaderHandle amplificationShader;
    ShaderHandle meshShader;
    ShaderHandle pixelShader;

    RenderState renderState;

    PrimitiveType::Enum primType = PrimitiveType::TriangleList;

    ~MeshletPipelineDesc();

    constexpr MeshletPipelineDesc& setPrimType(PrimitiveType::Enum value)noexcept{ primType = value; return *this; }
    MeshletPipelineDesc& setTaskShader(const ShaderHandle& value)noexcept;
    MeshletPipelineDesc& setAmplificationShader(const ShaderHandle& value)noexcept;
    MeshletPipelineDesc& setMeshShader(const ShaderHandle& value)noexcept;
    MeshletPipelineDesc& setPixelShader(const ShaderHandle& value)noexcept;
    MeshletPipelineDesc& setFragmentShader(const ShaderHandle& value)noexcept;
    constexpr MeshletPipelineDesc& setRenderState(const RenderState& value)noexcept{ renderState = value; return *this; }
    MeshletPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<MeshletPipeline> MeshletPipelineHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

