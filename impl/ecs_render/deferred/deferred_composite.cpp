// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/assets/graphics/deferred/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredCompositeResources(){
    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred compositing requires the global descriptor heap"));
        return false;
    }

    if(!deferredState().m_compositeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Pixel)
            .setUseDescriptorBuffer(heap.usesDescriptorBuffer())
        ;
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_DEFERRED_COMPOSITE_BINDING_BINDLESS_RESOURCES, 1));

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

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(deferredState().m_compositeVertexShader)
        .setPixelShader(deferredState().m_compositePixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(deferredState().m_compositeBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

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

    if(!uploadDeferredBindlessFrameResources(commandList, targets))
        return false;

    commandList.setResourceStatesForBindingSet(targets.compositeBindingSet.get());
    // The three compositor inputs are global-heap descriptors, so move them explicitly to SRV state before the
    // fullscreen draw. The local slot cbuffer above remains covered by the ordinary binding-set transition.
    commandList.setTextureState(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.avboit.accumColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.avboit.accumExtinction.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_DeferredComposite, graphics().getDevice(), commandList);

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(deferredState().m_compositePipeline.get());
    graphicsState.setFramebuffer(presentationFramebuffer);
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(targets.compositeBindingSet.get());

    commandList.setGraphicsState(graphicsState);
    graphics().getDevice()->getDescriptorHeap().bindGraphics(commandList, *deferredState().m_compositePipeline);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(ECSRenderDetail::s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

