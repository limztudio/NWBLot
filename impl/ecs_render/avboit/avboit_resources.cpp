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

    if(!ECSRenderDetail::CreateClampSampler(device, m_deferredState.m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT"));
        return false;
    }
    if(!ECSRenderDetail::CreateClampSampler(device, m_avboitState.m_linearSampler, true)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT"));
        return false;
    }

    if(!m_deferredState.m_sceneShadingBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene shading buffer"));
        return false;
    }

    if(!m_deferredState.m_lightBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene light buffer"));
        return false;
    }

    auto loadAvboitShader = [&](
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        const Core::ShaderType::Mask shaderType,
        const Name& debugName
    ) -> bool{
        return m_shaderSystem.loadShader(
            outShader,
            shaderName,
            Core::ShaderArchive::s_DefaultVariant,
            shaderType,
            debugName
        );
    };

    if(
        !loadAvboitShader(m_avboitState.m_depthWarpComputeShader, AssetsGraphicsAvboit::s_DepthWarpComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitDepthWarpCS")
        || !loadAvboitShader(m_avboitState.m_integrateComputeShader, AssetsGraphicsAvboit::s_IntegrateComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitIntegrateCS")
    )
        return false;

    return true;
}

bool RendererAvboitSystem::createAvboitPipelines(){
    if(!createAvboitResources())
        return false;

    auto& device = m_graphics.getDevice();
    Core::BindingLayoutHandle materialPassBindingLayout;
    if(!m_materialSystem.prepareMaterialPassBindingLayout(materialPassBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT requires the shared material-pass push-constant layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateHeapComputePipeline(
        device,
        m_avboitState.m_depthWarpPipeline,
        m_avboitState.m_depthWarpComputeShader,
        materialPassBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateHeapComputePipeline(
        device,
        m_avboitState.m_integratePipeline,
        m_avboitState.m_integrateComputeShader,
        materialPassBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

