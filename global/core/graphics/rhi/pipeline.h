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

    constexpr SinglePassStereoState& setEnabled(bool value){ enabled = value; return *this; }
    constexpr SinglePassStereoState& setIndependentViewportMask(bool value){ independentViewportMask = value; return *this; }
    constexpr SinglePassStereoState& setRenderTargetIndexOffset(u16 value){ renderTargetIndexOffset = static_cast<u8>(value); return *this; }
};
inline bool operator==(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs){
    return
        lhs.enabled == rhs.enabled
        && lhs.independentViewportMask == rhs.independentViewportMask
        && lhs.renderTargetIndexOffset == rhs.renderTargetIndexOffset
    ;
}
inline bool operator!=(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs){ return !(lhs == rhs); }

struct RenderState{
    RasterState rasterState;
    BlendState blendState;
    DepthStencilState depthStencilState;
    SinglePassStereoState singlePassStereo;

    constexpr RenderState& setBlendState(const BlendState& value){ blendState = value; return *this; }
    constexpr RenderState& setDepthStencilState(const DepthStencilState& value){ depthStencilState = value; return *this; }
    constexpr RenderState& setRasterState(const RasterState& value){ rasterState = value; return *this; }
    constexpr RenderState& setSinglePassStereoState(const SinglePassStereoState& value){ singlePassStereo = value; return *this; }
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

    constexpr VariableRateShadingState& setEnabled(bool value){ enabled = value; return *this; }
    constexpr VariableRateShadingState& setShadingRate(VariableShadingRate::Enum value){ shadingRate = value; return *this; }
    constexpr VariableRateShadingState& setPipelinePrimitiveCombiner(ShadingRateCombiner::Enum value){ pipelinePrimitiveCombiner = value; return *this; }
    constexpr VariableRateShadingState& setImageCombiner(ShadingRateCombiner::Enum value){ imageCombiner = value; return *this; }
};
inline bool operator==(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs){
    return
        lhs.enabled == rhs.enabled
        && lhs.shadingRate == rhs.shadingRate
        && lhs.pipelinePrimitiveCombiner == rhs.pipelinePrimitiveCombiner
        && lhs.imageCombiner == rhs.imageCombiner
    ;
}
inline bool operator!=(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs){ return !(lhs == rhs); }

typedef FixedVector<BindingLayoutHandle, s_MaxBindingLayouts> BindingLayoutVector;

struct GraphicsPipelineDesc{
    PrimitiveType::Enum primType = PrimitiveType::TriangleList;
    u32 patchControlPoints = 0;
    InputLayoutHandle inputLayout;

    ShaderHandle VS;
    ShaderHandle HS;
    ShaderHandle DS;
    ShaderHandle GS;
    ShaderHandle PS;

    RenderState renderState;
    VariableRateShadingState shadingRateState;

    BindingLayoutVector bindingLayouts;

    ~GraphicsPipelineDesc();

    constexpr GraphicsPipelineDesc& setPrimType(PrimitiveType::Enum value){ primType = value; return *this; }
    constexpr GraphicsPipelineDesc& setPatchControlPoints(u32 value){ patchControlPoints = value; return *this; }
    GraphicsPipelineDesc& setInputLayout(const InputLayoutHandle& value);
    GraphicsPipelineDesc& setVertexShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setHullShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setTessellationControlShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setDomainShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setTessellationEvaluationShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setGeometryShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setPixelShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setFragmentShader(const ShaderHandle& value);
    constexpr GraphicsPipelineDesc& setRenderState(const RenderState& value){ renderState = value; return *this; }
    constexpr GraphicsPipelineDesc& setVariableRateShadingState(const VariableRateShadingState& value){ shadingRateState = value; return *this; }
    GraphicsPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<GraphicsPipeline> GraphicsPipelineHandle;

struct ComputePipelineDesc{
    ShaderHandle CS;

    BindingLayoutVector bindingLayouts;

    ~ComputePipelineDesc();

    ComputePipelineDesc& setComputeShader(const ShaderHandle& value);
    ComputePipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<ComputePipeline> ComputePipelineHandle;

struct MeshletPipelineDesc{
    PrimitiveType::Enum primType = PrimitiveType::TriangleList;

    ShaderHandle AS;
    ShaderHandle MS;
    ShaderHandle PS;

    RenderState renderState;

    BindingLayoutVector bindingLayouts;

    ~MeshletPipelineDesc();

    constexpr MeshletPipelineDesc& setPrimType(PrimitiveType::Enum value){ primType = value; return *this; }
    MeshletPipelineDesc& setTaskShader(const ShaderHandle& value);
    MeshletPipelineDesc& setAmplificationShader(const ShaderHandle& value);
    MeshletPipelineDesc& setMeshShader(const ShaderHandle& value);
    MeshletPipelineDesc& setPixelShader(const ShaderHandle& value);
    MeshletPipelineDesc& setFragmentShader(const ShaderHandle& value);
    constexpr MeshletPipelineDesc& setRenderState(const RenderState& value){ renderState = value; return *this; }
    MeshletPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<MeshletPipeline> MeshletPipelineHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

