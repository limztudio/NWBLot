// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createDeferredLightingResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_sceneShadingBuffer){
        Core::BufferDesc sceneShadingBufferDesc;
        sceneShadingBufferDesc
            .setByteSize(sizeof(ECSRenderDetail::SceneShadingGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(ECSRenderDetail::s_SceneShadingBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_sceneShadingBuffer = m_graphics.createBuffer(sceneShadingBufferDesc);
        if(!m_sceneShadingBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene shading buffer"));
            return false;
        }
    }

    if(!m_deferredLightingBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(5, 1));

        m_deferredLightingBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_deferredLightingBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreatePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create deferred lighting sampler")))
        return false;

    if(!loadDeferredCompositeVertexShader())
        return false;

    if(!loadShader(
        m_deferredLightingPixelShader,
        ECSRenderDetail::s_DeferredLightingPixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredLightingPS"
    ))
        return false;

    return true;
}

bool RendererSystem::createDeferredLightingPipeline(DeferredFrameTargets& targets){
    if(!targets.opaqueLightingFramebuffer)
        return false;
    if(!createDeferredLightingResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = targets.opaqueLightingFramebuffer->getFramebufferInfo();
    if(m_deferredLightingPipeline && m_deferredLightingPipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(m_deferredCompositeVertexShader)
        .setPixelShader(m_deferredLightingPixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(m_deferredLightingBindingLayout)
    ;

    Core::IDevice* device = m_graphics.getDevice();
    m_deferredLightingPipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredLightingPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting pipeline"));
        return false;
    }

    return true;
}

bool RendererSystem::updateSceneShadingBuffer(Core::ICommandList& commandList, const f32 fallbackAspectRatio){
    if(!m_sceneShadingBuffer)
        return false;

    const ECSRenderDetail::SceneShadingGpuData sceneShadingState = ECSRenderDetail::ResolveSceneShadingState(m_world, fallbackAspectRatio);

    commandList.setBufferState(m_sceneShadingBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_sceneShadingBuffer.get(), &sceneShadingState, sizeof(sceneShadingState));
    commandList.setBufferState(m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::renderDeferredLighting(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.lightingBindingSet || !targets.opaqueLightingFramebuffer)
        return false;
    if(!m_deferredLightingPipeline)
        return false;

    commandList.setResourceStatesForBindingSet(targets.lightingBindingSet.get());
    commandList.commitBarriers();

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(targets.opaqueLightingFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredLightingPipeline.get());
    graphicsState.setFramebuffer(targets.opaqueLightingFramebuffer.get());
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(targets.lightingBindingSet.get());

    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

