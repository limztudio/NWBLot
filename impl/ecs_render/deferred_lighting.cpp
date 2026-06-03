// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredLightingResources(){
    auto* device = m_graphics.getDevice();

    if(!m_deferredState.m_sceneShadingBuffer){
        Core::BufferDesc sceneShadingBufferDesc;
        sceneShadingBufferDesc
            .setByteSize(sizeof(ECSRenderDetail::SceneShadingGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(ECSRenderDetail::s_SceneShadingBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_deferredState.m_sceneShadingBuffer = m_graphics.createBuffer(sceneShadingBufferDesc);
        if(!m_deferredState.m_sceneShadingBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene shading buffer"));
            return false;
        }
    }

    if(!m_deferredState.m_lightingBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_BASE_COLOR, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_NORMAL, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_WORLD_POSITION, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_DEPTH, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_DEFERRED_LIGHTING_BINDING_SAMPLER, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SCENE_SHADING_DEFERRED_LIGHTING_BINDING, 1));

        m_deferredState.m_lightingBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_deferredState.m_lightingBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreateClampSampler(*device, m_deferredState.m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting sampler"));
        return false;
    }

    if(!m_renderer.shaderSystem().loadDeferredCompositeVertexShader())
        return false;

    if(!m_renderer.shaderSystem().loadShader(
        m_deferredState.m_lightingPixelShader,
        ECSRenderDetail::s_DeferredLightingPixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredLightingPS"
    ))
        return false;

    return true;
}

bool RendererDeferredSystem::createDeferredLightingPipeline(DeferredFrameTargets& targets){
    if(!targets.opaqueLightingFramebuffer)
        return false;
    if(!createDeferredLightingResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = targets.opaqueLightingFramebuffer->getFramebufferInfo();
    if(m_deferredState.m_lightingPipeline && m_deferredState.m_lightingPipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(m_deferredState.m_compositeVertexShader)
        .setPixelShader(m_deferredState.m_lightingPixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(m_deferredState.m_lightingBindingLayout)
    ;

    auto* device = m_graphics.getDevice();
    m_deferredState.m_lightingPipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredState.m_lightingPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting pipeline"));
        return false;
    }

    return true;
}

bool RendererDeferredSystem::updateSceneShadingBuffer(Core::CommandList& commandList, const f32 fallbackAspectRatio){
    if(!m_deferredState.m_sceneShadingBuffer)
        return false;

    const ECSRenderDetail::SceneShadingGpuData sceneShadingState = ECSRenderDetail::ResolveSceneShadingState(m_world, fallbackAspectRatio);

    commandList.setBufferState(m_deferredState.m_sceneShadingBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_deferredState.m_sceneShadingBuffer.get(), &sceneShadingState, sizeof(sceneShadingState));
    commandList.setBufferState(m_deferredState.m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool RendererDeferredSystem::renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.lightingBindingSet || !targets.opaqueLightingFramebuffer)
        return false;
    if(!m_deferredState.m_lightingPipeline)
        return false;

    commandList.setResourceStatesForBindingSet(targets.lightingBindingSet.get());
    commandList.commitBarriers();

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(targets.opaqueLightingFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredState.m_lightingPipeline.get());
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

