#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/assets/graphics/deferred/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredCompositeResources(){
    auto* device = graphics().getDevice();

    if(!deferredState().m_compositeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_COMPOSITE_BINDING_OPAQUE_COLOR, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_COLOR, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_EXTINCTION, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_DEFERRED_COMPOSITE_BINDING_SAMPLER, 1));

        deferredState().m_compositeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!deferredState().m_compositeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreateClampSampler(*device, deferredState().m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite sampler"));
        return false;
    }

    if(!m_renderer.shaderSystem().loadDeferredCompositeVertexShader())
        return false;

    if(!m_renderer.shaderSystem().loadShader(
        deferredState().m_compositePixelShader,
        AssetsGraphicsDeferred::s_CompositePixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredCompositePS"
    ))
        return false;

    return true;
}

bool RendererDeferredSystem::createDeferredCompositePipeline(Core::Framebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;

    if(!createDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer->getFramebufferInfo();
    if(deferredState().m_compositePipeline && deferredState().m_compositePipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(deferredState().m_compositeVertexShader)
        .setPixelShader(deferredState().m_compositePixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(deferredState().m_compositeBindingLayout)
    ;

    auto* device = graphics().getDevice();
    deferredState().m_compositePipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!deferredState().m_compositePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite pipeline"));
        return false;
    }

    return true;
}

bool RendererDeferredSystem::renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer){
    NWB_ASSERT(presentationFramebuffer);
    NWB_ASSERT(targets.compositeBindingSet);
    NWB_ASSERT(deferredState().m_compositePipeline);
    NWB_ASSERT(
        presentationFramebuffer
        && deferredState().m_compositePipeline
        && deferredState().m_compositePipeline->getFramebufferInfo() == presentationFramebuffer->getFramebufferInfo()
    );

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_DeferredComposite, graphics().getDevice(), commandList);

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(deferredState().m_compositePipeline.get());
    graphicsState.setFramebuffer(presentationFramebuffer);
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(targets.compositeBindingSet.get());

    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

