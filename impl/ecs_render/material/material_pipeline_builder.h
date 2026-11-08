// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "material_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPipelineBuildInputs{
    RendererMaterialSystem& system;
    RendererShaderSystem& shaderSystem;
    RendererMaterialState& materialState;
    MaterialPipelineResources& resources;
    const MaterialSurfaceInfo& materialInfo;
    const MaterialPipelineKey& pipelineKey;
    Core::Framebuffer& framebuffer;
    Core::Device& device;
    Core::GpuDescriptorHeap& heap;
    const Core::RenderState& renderState;
    const Core::BindingLayoutHandle& materialPassBindingLayout;
    const Name& materialKey;
    Name sharedMeshShaderAssetName;
    Name passPixelShaderName;
    Name passPixelShaderDebugName;
    AStringView pixelShaderVariant;
    AStringView meshShaderVariant;
    AStringView avboitCsgShaderVariant;
    MaterialPipelinePass::Enum pass;
    b8 avboitCsgClipPipeline;
};

class MaterialPipelineBuilder final : NoCopy{
public:
    explicit MaterialPipelineBuilder(const MaterialPipelineBuildInputs& inputs)noexcept : m_inputs(inputs){}


public:
    [[nodiscard]] b8 buildMesh();
    [[nodiscard]] b8 buildIndexed();
    [[nodiscard]] b8 buildCompute();


private:
    [[nodiscard]] b8 loadPassPixelShader();


private:
    const MaterialPipelineBuildInputs& m_inputs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

