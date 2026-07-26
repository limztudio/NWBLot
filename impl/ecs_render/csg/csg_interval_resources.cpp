// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_interval_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


[[nodiscard]] static bool CreateCsgIntervalBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    Core::ShaderType::Mask visibility,
    CsgTextureAccess::Enum intervalAccess,
    CsgTextureAccess::Enum receiverEventAccess,
    CsgTextureAccess::Enum removedIntervalAccess
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(visibility);
    bindingLayoutDesc.setUseDescriptorBuffer(true);
    // All CSG interval/peel textures are target-generation StorageImage heap entries. Retain their historical local
    // binding numbers as gaps. The ordinary mesh view now arrives through a heap slot in the dispatch/b18 payload,
    // so its former direct b13 cbuffer is also an intentional gap; local state carries only the shared target slots.
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CSG_INTERVAL_BINDING_BINDLESS_RESOURCES, 1));
    const bool usesSampleState = receiverEventAccess != CsgTextureAccess::None || removedIntervalAccess != CsgTextureAccess::None;
    if(usesSampleState)
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CSG_INTERVAL_BINDING_SAMPLE_STATE, 1));

    (void)intervalAccess;

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateCsgIntervalBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::Buffer* sampleStateBuffer,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet,
    CsgTextureAccess::Enum intervalAccess,
    CsgTextureAccess::Enum receiverEventAccess,
    CsgTextureAccess::Enum removedIntervalAccess
){
    if(bindingSet)
        return true;
    const bool usesSampleState = receiverEventAccess != CsgTextureAccess::None || removedIntervalAccess != CsgTextureAccess::None;
    if(
        (usesSampleState && !sampleStateBuffer)
        || !layout
        || !targets.bindless.valid()
        || !targets.bindless.slotsBuffer
    )
        return false;
    if(
        intervalAccess != CsgTextureAccess::None
        && (
            !targets.csgCapBackNormal
            || !targets.csgIntervalDepth
            || !targets.csgIntervalId
            || targets.csgPeelLayerCount == 0u
        )
    )
        return false;
    if(
        receiverEventAccess != CsgTextureAccess::None
        && (
            !targets.csgReceiverEventData
            || !targets.csgReceiverEventCount
        )
    )
        return false;
    if(
        removedIntervalAccess != CsgTextureAccess::None
        && (
            !targets.csgRemovedIntervalDepth
            || !targets.csgRemovedIntervalCapNormal
            || !targets.csgRemovedIntervalData
            || !targets.csgRemovedIntervalCount
            || targets.csgRemovedIntervalLayerCount == 0u
        )
    )
        return false;

    Core::BindingSetDesc bindingSetDesc(arena);
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(
        NWB_CSG_INTERVAL_BINDING_BINDLESS_RESOURCES,
        targets.bindless.slotsBuffer.get()
    ));
    if(usesSampleState)
        bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CSG_INTERVAL_BINDING_SAMPLE_STATE, sampleStateBuffer));

    bindingSet = device.createBindingSet(bindingSetDesc, layout);
    return bindingSet != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateReceiverSpanBuildBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
    bindingLayoutDesc.setUseDescriptorBuffer(true);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CSG_INTERVAL_BINDING_BINDLESS_RESOURCES, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CsgIntervalDispatchPushConstants)));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateReceiverSpanBuildBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;
    if(
        !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !layout
        || !targets.bindless.valid()
        || !targets.bindless.slotsBuffer
    )
        return false;

    Core::BindingSetDesc bindingSetDesc(arena);
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(
        NWB_CSG_INTERVAL_BINDING_BINDLESS_RESOURCES,
        targets.bindless.slotsBuffer.get()
    ));

    bindingSet = device.createBindingSet(bindingSetDesc, layout);
    return bindingSet != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateIntervalCombineBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
    bindingLayoutDesc.setUseDescriptorBuffer(true);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CSG_INTERVAL_BINDING_BINDLESS_RESOURCES, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CsgIntervalDispatchPushConstants)));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateIntervalCombineBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;
    if(
        !targets.csgCapBackNormal
        || !targets.csgIntervalDepth
        || !targets.csgIntervalId
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || !layout
        || !targets.bindless.valid()
        || !targets.bindless.slotsBuffer
    )
        return false;

    Core::BindingSetDesc bindingSetDesc(arena);
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(
        NWB_CSG_INTERVAL_BINDING_BINDLESS_RESOURCES,
        targets.bindless.slotsBuffer.get()
    ));

    bindingSet = device.createBindingSet(bindingSetDesc, layout);
    return bindingSet != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateIntervalSampleBindingLayouts(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& intervalSampleBindingLayout,
    Core::BindingLayoutHandle& receiverSurfaceBindingLayout
){
    if(!CreateCsgIntervalBindingLayout(
        arena,
        device,
        intervalSampleBindingLayout,
        Core::ShaderType::Pixel,
        CsgTextureAccess::None,
        CsgTextureAccess::None,
        CsgTextureAccess::SRV
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample binding layout"));
        return false;
    }

    if(!CreateCsgIntervalBindingLayout(
        arena,
        device,
        receiverSurfaceBindingLayout,
        Core::ShaderType::Pixel,
        CsgTextureAccess::None,
        CsgTextureAccess::UAV,
        CsgTextureAccess::None
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver surface binding layout"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateIntervalPeelPipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& intervalBindingLayout,
    const Core::BindingLayoutHandle& clipBindingLayout
){
    if(pipeline)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(intervalBindingLayout)
        .addBindingLayout(clipBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateReceiverSpanBuildPipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& receiverSpanBuildBindingLayout
){
    if(pipeline)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(receiverSpanBuildBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateIntervalCombinePipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& intervalCombineBindingLayout
){
    if(pipeline)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(intervalCombineBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateIntervalCapFillPipeline(
    Core::Device& device,
    Core::GraphicsPipelineHandle& pipeline,
    const Core::ShaderHandle& vertexShader,
    const Core::ShaderHandle& pixelShader,
    const Core::BindingLayoutHandle& intervalSampleBindingLayout,
    const Core::BindingLayoutHandle& clipBindingLayout,
    const Core::FramebufferInfo& framebufferInfo
){
    if(pipeline && pipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(pixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(intervalSampleBindingLayout)
        .addBindingLayout(clipBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    return pipeline != nullptr;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgIntervalPeelResources(DeferredFrameTargets& targets, const bool capFillRequired){
    auto* device = graphics().getDevice();
    if(!createCsgClipResources())
        return false;
    if(!csgState().m_clipBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a CSG clip binding layout"));
        return false;
    }
    if(!drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a mesh view buffer"));
        return false;
    }
    if(
        !targets.csgCapBackNormal
        || !targets.csgIntervalDepth
        || !targets.csgIntervalId
        || !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || targets.csgPeelLayerCount == 0u
        || targets.csgReceiverEventLayerCount == 0u
        || targets.csgReceiverSpanLayerCount == 0u
        || targets.csgRemovedIntervalLayerCount == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires valid peel targets"));
        return false;
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingLayout(
        arena(),
        *device,
        csgState().m_intervalPeelBindingLayout,
        Core::ShaderType::Compute,
        __hidden_csg_interval_peel::CsgTextureAccess::UAV,
        __hidden_csg_interval_peel::CsgTextureAccess::None,
        __hidden_csg_interval_peel::CsgTextureAccess::None
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateReceiverSpanBuildBindingLayout(
        arena(),
        *device,
        csgState().m_receiverSpanBuildBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalCombineBindingLayout(
        arena(),
        *device,
        csgState().m_intervalCombineBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalSampleBindingLayouts(
        arena(),
        *device,
        csgState().m_intervalSampleBindingLayout,
        csgState().m_receiverSurfaceBindingLayout
    ))
        return false;

    if(!csgState().m_intervalPeelComputeShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_intervalPeelComputeShader,
            AssetsGraphicsCsg::s_IntervalPeelComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_CsgIntervalPeelCS"
        ))
            return false;
    }

    if(!csgState().m_receiverSpanBuildComputeShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_receiverSpanBuildComputeShader,
            AssetsGraphicsCsg::s_ReceiverSpanBuildComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_CsgReceiverSpanBuildCS"
        ))
            return false;
    }

    if(!csgState().m_intervalCombineComputeShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_intervalCombineComputeShader,
            AssetsGraphicsCsg::s_IntervalCombineComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_CsgIntervalCombineCS"
        ))
            return false;
    }

    if(capFillRequired){
        if(!m_renderer.shaderSystem().loadDeferredCompositeVertexShader())
            return false;

        if(!csgState().m_intervalCapFillPixelShader){
            if(!m_renderer.shaderSystem().loadShader(
                csgState().m_intervalCapFillPixelShader,
                AssetsGraphicsCsg::s_IntervalCapFillPixelShaderName,
                Core::ShaderArchive::s_DefaultVariant,
                Core::ShaderType::Pixel,
                "ECSRender_CsgIntervalCapFillPS"
            ))
                return false;
        }
    }

    if(!__hidden_csg_interval_peel::CreateIntervalPeelPipeline(
        *device,
        csgState().m_intervalPeelPipeline,
        csgState().m_intervalPeelComputeShader,
        csgState().m_intervalPeelBindingLayout,
        csgState().m_clipBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel pipeline"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateReceiverSpanBuildPipeline(
        *device,
        csgState().m_receiverSpanBuildPipeline,
        csgState().m_receiverSpanBuildComputeShader,
        csgState().m_receiverSpanBuildBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build pipeline"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalCombinePipeline(
        *device,
        csgState().m_intervalCombinePipeline,
        csgState().m_intervalCombineComputeShader,
        csgState().m_intervalCombineBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine pipeline"));
        return false;
    }

    if(capFillRequired && !__hidden_csg_interval_peel::CreateIntervalCapFillPipeline(
        *device,
        csgState().m_intervalCapFillPipeline,
        deferredState().m_compositeVertexShader,
        csgState().m_intervalCapFillPixelShader,
        csgState().m_intervalSampleBindingLayout,
        csgState().m_clipBindingLayout,
        targets.framebuffer->getFramebufferInfo()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval cap fill pipeline"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingSet(
        arena(),
        *device,
        targets,
        nullptr,
        csgState().m_intervalPeelBindingLayout.get(),
        csgState().m_intervalPeelBindingSet,
        __hidden_csg_interval_peel::CsgTextureAccess::UAV,
        __hidden_csg_interval_peel::CsgTextureAccess::None,
        __hidden_csg_interval_peel::CsgTextureAccess::None
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding set"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateReceiverSpanBuildBindingSet(
        arena(),
        *device,
        targets,
        csgState().m_receiverSpanBuildBindingLayout.get(),
        csgState().m_receiverSpanBuildBindingSet
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build binding set"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalCombineBindingSet(
        arena(),
        *device,
        targets,
        csgState().m_intervalCombineBindingLayout.get(),
        csgState().m_intervalCombineBindingSet
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine binding set"));
        return false;
    }

    if(!createCsgIntervalSampleResources(targets))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgIntervalSampleResources(DeferredFrameTargets& targets){
    auto* device = graphics().getDevice();
    if(!drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires a mesh view buffer"));
        return false;
    }
    if(
        !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || targets.csgReceiverEventLayerCount == 0u
        || targets.csgRemovedIntervalLayerCount == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires valid peel targets"));
        return false;
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;

    if(!__hidden_csg_interval_peel::CreateIntervalSampleBindingLayouts(
        arena(),
        *device,
        csgState().m_intervalSampleBindingLayout,
        csgState().m_receiverSurfaceBindingLayout
    ))
        return false;

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingSet(
        arena(),
        *device,
        targets,
        csgState().m_intervalSampleStateBuffer.get(),
        csgState().m_receiverSurfaceBindingLayout.get(),
        csgState().m_receiverSurfaceBindingSet,
        __hidden_csg_interval_peel::CsgTextureAccess::None,
        __hidden_csg_interval_peel::CsgTextureAccess::UAV,
        __hidden_csg_interval_peel::CsgTextureAccess::None
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver surface binding set"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingSet(
        arena(),
        *device,
        targets,
        csgState().m_intervalSampleStateBuffer.get(),
        csgState().m_intervalSampleBindingLayout.get(),
        csgState().m_intervalSampleBindingSet,
        __hidden_csg_interval_peel::CsgTextureAccess::None,
        __hidden_csg_interval_peel::CsgTextureAccess::None,
        __hidden_csg_interval_peel::CsgTextureAccess::SRV
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample binding set"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgIntervalSampleStateBuffer(){
    if(csgState().m_intervalSampleStateBuffer)
        return true;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(sizeof(__hidden_csg_interval_peel::CsgIntervalSampleStateGpuData))
        .setIsConstantBuffer(true)
        .setDebugName("engine/csg/interval_sample_state")
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    csgState().m_intervalSampleStateBuffer = graphics().createBuffer(bufferDesc);
    if(!csgState().m_intervalSampleStateBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample state buffer"));
        return false;
    }

    csgState().m_receiverSurfaceBindingSet.reset();
    csgState().m_intervalSampleBindingSet.reset();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

