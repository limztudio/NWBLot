// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool CreateHeapComputePipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& bindingLayout
){
    if(pipeline)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !bindingLayout
        || !heap.getResourceLayout()
        || !heap.getSamplerLayout()
    )
        return false;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(bindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::createAvboitResources(){
    auto& device = m_graphics.getDevice();

    if(!ECSRenderDetail::CreateClampSampler(device, m_avboitState.m_linearSampler, true)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT"));
        return false;
    }

    auto loadAvboitComputeShader = [&](
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        const Name& debugName
    ) -> bool{
        return m_shaderSystem.loadShader<ComputeShader>(
            outShader,
            shaderName,
            Core::ShaderArchive::s_DefaultVariant,
            debugName
        );
    };

    if(
        !loadAvboitComputeShader(m_avboitState.m_depthWarpComputeShader, AssetsGraphicsAvboit::s_DepthWarpComputeShaderName, "ECSRender_AvboitDepthWarpCS")
        || !loadAvboitComputeShader(m_avboitState.m_integrateComputeShader, AssetsGraphicsAvboit::s_IntegrateComputeShaderName, "ECSRender_AvboitIntegrateCS")
    )
        return false;

    return true;
}

bool RendererAvboitSystem::createAvboitPipelines(){
    if(!createAvboitResources())
        return false;

    auto& device = m_graphics.getDevice();
    const auto materialPassBindingLayout = m_materialSystem.prepareMaterialPassBindingLayout();
    if(!materialPassBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT requires the shared material-pass push-constant layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateHeapComputePipeline(
        device,
        m_avboitState.m_depthWarpPipeline,
        m_avboitState.m_depthWarpComputeShader,
        *materialPassBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateHeapComputePipeline(
        device,
        m_avboitState.m_integratePipeline,
        m_avboitState.m_integrateComputeShader,
        *materialPassBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

