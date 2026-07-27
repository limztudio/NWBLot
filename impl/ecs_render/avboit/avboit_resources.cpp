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
    auto* device = graphics().getDevice();

    if(!ECSRenderDetail::CreateClampSampler(*device, deferredState().m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT"));
        return false;
    }
    if(!ECSRenderDetail::CreateClampSampler(*device, avboitState().m_linearSampler, true)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT"));
        return false;
    }

    if(!avboitState().m_emptyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::All)
            // This is the only AVBOIT pipeline-local layout: a push range. The descriptor-buffer pipeline-layout
            // builder supplies empty gap sets through set 7, while all live resource access starts at heap set 8.
            .addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize))
        ;
        avboitState().m_emptyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
    }
    if(!avboitState().m_emptyBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT shared push-constant layout"));
        return false;
    }

    if(!deferredState().m_sceneShadingBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene shading buffer"));
        return false;
    }

    if(!deferredState().m_lightBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene light buffer"));
        return false;
    }

    auto loadAvboitShader = [&](
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        const Core::ShaderType::Mask shaderType,
        const Name& debugName
    ) -> bool{
        return m_renderer.shaderSystem().loadShader(
            outShader,
            shaderName,
            Core::ShaderArchive::s_DefaultVariant,
            shaderType,
            debugName
        );
    };

    if(
        !loadAvboitShader(avboitState().m_depthWarpComputeShader, AssetsGraphicsAvboit::s_DepthWarpComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitDepthWarpCS")
        || !loadAvboitShader(avboitState().m_integrateComputeShader, AssetsGraphicsAvboit::s_IntegrateComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitIntegrateCS")
    )
        return false;

    return true;
}

bool RendererAvboitSystem::createAvboitPipelines(){
    if(!createAvboitResources())
        return false;

    auto* device = graphics().getDevice();

    if(!__hidden_avboit_resources::CreateHeapComputePipeline(
        *device,
        avboitState().m_depthWarpPipeline,
        avboitState().m_depthWarpComputeShader,
        avboitState().m_emptyBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateHeapComputePipeline(
        *device,
        avboitState().m_integratePipeline,
        avboitState().m_integrateComputeShader,
        avboitState().m_emptyBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

