// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binding.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RenderState{
    RasterState rasterState;
    BlendState blendState;
    DepthStencilState depthStencilState;

    constexpr RenderState& setBlendState(const BlendState& value){ blendState = value; return *this; }
    constexpr RenderState& setDepthStencilState(const DepthStencilState& value){ depthStencilState = value; return *this; }
    constexpr RenderState& setRasterState(const RasterState& value){ rasterState = value; return *this; }
};

typedef FixedVector<BindingLayoutHandle, s_MaxBindingLayouts> BindingLayoutVector;

struct GraphicsPipelineDesc{
    InputLayoutHandle inputLayout;

    ShaderHandle VS;
    ShaderHandle PS;

    RenderState renderState;

    BindingLayoutVector bindingLayouts;

    ~GraphicsPipelineDesc();

    GraphicsPipelineDesc& setInputLayout(const InputLayoutHandle& value);
    GraphicsPipelineDesc& setVertexShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setPixelShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setFragmentShader(const ShaderHandle& value);
    constexpr GraphicsPipelineDesc& setRenderState(const RenderState& value){ renderState = value; return *this; }
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
    BindingLayoutVector bindingLayouts;

    ShaderHandle MS;
    ShaderHandle PS;

    RenderState renderState;

    ~MeshletPipelineDesc();

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

