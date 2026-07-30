// limztudio@gmail.com


#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/assets/graphics/deferred/names.h>


NWB_IMPL_BEGIN


namespace __hidden_deferred_composite{


struct PushConstants{
    u32 resourceSlots = 0u;
};
static_assert(sizeof(PushConstants) == sizeof(u32));


};


bool RendererDeferredSystem::createDeferredCompositeResources(){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred compositing requires the global descriptor heap"));
        return false;
    }

    if(!deferredState().m_compositeComputeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Compute)
        ;
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(__hidden_deferred_composite::PushConstants)));

        deferredState().m_compositeComputeBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!deferredState().m_compositeComputeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute binding layout"));
            return false;
        }
    }

    if(!deferredState().m_presentBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Pixel)
        ;
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(__hidden_deferred_composite::PushConstants)));

        deferredState().m_presentBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!deferredState().m_presentBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreateClampSampler(device, deferredState().m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite sampler"));
        return false;
    }

    if(!m_renderer.shaderSystem().loadShader(
        deferredState().m_compositeComputeShader,
        AssetsGraphicsDeferred::s_CompositeComputeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_DeferredCompositeCS"
    ))
        return false;

    if(!m_renderer.shaderSystem().loadDeferredCompositeVertexShader())
        return false;

    if(!m_renderer.shaderSystem().loadShader(
        deferredState().m_presentPixelShader,
        AssetsGraphicsDeferred::s_PresentPixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredPresentPS"
    ))
        return false;

    return true;
}

bool RendererDeferredSystem::createDeferredCompositePipeline(){
    if(!createDeferredCompositeResources())
        return false;

    if(deferredState().m_compositeComputePipeline)
        return true;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(deferredState().m_compositeComputeShader)
        .addBindingLayout(deferredState().m_compositeComputeBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    deferredState().m_compositeComputePipeline = device.createComputePipeline(pipelineDesc);
    if(!deferredState().m_compositeComputePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute pipeline"));
        return false;
    }

    return true;
}

bool RendererDeferredSystem::createDeferredPresentPipeline(Core::Framebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;

    if(!createDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer->getFramebufferInfo();
    if(deferredState().m_presentPipeline && deferredState().m_presentPipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(deferredState().m_compositeVertexShader)
        .setPixelShader(deferredState().m_presentPixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(deferredState().m_presentBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    deferredState().m_presentPipeline = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!deferredState().m_presentPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation pipeline"));
        return false;
    }

    return true;
}

bool RendererDeferredSystem::renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets){
    NWB_ASSERT(deferredState().m_compositeComputePipeline);

    if(!uploadDeferredBindlessFrameResources(commandList, targets))
        return false;

    // Heap-selected inputs are not automatically visible to state tracking. Composite runs directly after lighting on
    // the same Compute lane and writes a linear intermediate; the Graphics-only present pass consumes only that output.
    commandList.setTextureState(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.avboit.accumColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.avboit.accumExtinction.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.compositeColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_DeferredComposite, graphics().getDevice(), commandList);

    Core::ComputeState computeState;
    computeState.setPipeline(deferredState().m_compositeComputePipeline.get());
    commandList.setComputeState(computeState);
    graphics().getDevice().getDescriptorHeap().bindCompute(commandList, *deferredState().m_compositeComputePipeline);
    const __hidden_deferred_composite::PushConstants pushConstants{
        targets.bindless.slotsBufferDescriptor.slot()
    };
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    const u32 groupCountX = (targets.width + NWB_DEFERRED_COMPOSITE_GROUP_SIZE - 1u) / NWB_DEFERRED_COMPOSITE_GROUP_SIZE;
    const u32 groupCountY = (targets.height + NWB_DEFERRED_COMPOSITE_GROUP_SIZE - 1u) / NWB_DEFERRED_COMPOSITE_GROUP_SIZE;
    commandList.dispatch(groupCountX, groupCountY, 1u);
    return true;
}

bool RendererDeferredSystem::renderDeferredPresent(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer){
    NWB_ASSERT(presentationFramebuffer);
    NWB_ASSERT(deferredState().m_presentPipeline);
    NWB_ASSERT(
        presentationFramebuffer
        && deferredState().m_presentPipeline
        && deferredState().m_presentPipeline->getFramebufferInfo() == presentationFramebuffer->getFramebufferInfo()
    );

    if(!uploadDeferredBindlessFrameResources(commandList, targets))
        return false;

    // The presentation shader samples the Compute-composited linear image and writes the surface framebuffer, which
    // keeps format conversion and presentation ownership on Graphics without requiring a storage-capable swapchain.
    commandList.setTextureState(targets.compositeColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_DeferredPresent, graphics().getDevice(), commandList);

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(deferredState().m_presentPipeline.get());
    graphicsState.setFramebuffer(presentationFramebuffer);
    graphicsState.setViewport(viewportState);

    commandList.setGraphicsState(graphicsState);
    graphics().getDevice().getDescriptorHeap().bindGraphics(commandList, *deferredState().m_presentPipeline);
    const __hidden_deferred_composite::PushConstants pushConstants{
        targets.bindless.slotsBufferDescriptor.slot()
    };
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(ECSRenderDetail::s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
    return true;
}


NWB_IMPL_END


