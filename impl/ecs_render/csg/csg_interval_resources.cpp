// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_interval_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


static void AddCsgTextureBindingLayoutItem(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    u32 slot,
    CsgTextureAccess::Enum access
){
    switch(access){
    case CsgTextureAccess::None:
        break;
    case CsgTextureAccess::SRV:
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(slot, 1));
        break;
    case CsgTextureAccess::UAV:
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(slot, 1));
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgTextureBindingSetItem(
    Core::BindingSetDesc& bindingSetDesc,
    u32 slot,
    Core::Texture* texture,
    Core::Format::Enum format,
    const Core::TextureSubresourceSet& subresources,
    CsgTextureAccess::Enum access
){
    switch(access){
    case CsgTextureAccess::None:
        break;
    case CsgTextureAccess::SRV:
        bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
            slot,
            texture,
            format,
            subresources,
            Core::TextureDimension::Texture2DArray
        ));
        break;
    case CsgTextureAccess::UAV:
        bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
            slot,
            texture,
            format,
            subresources,
            Core::TextureDimension::Texture2DArray
        ));
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgIntervalTargetLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_CAP_BACK_NORMAL, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_ID, access);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgReceiverEventLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DATA, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_COUNT, access);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgReceiverSpanLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DATA, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_COUNT, access);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgRemovedIntervalLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_CAP_NORMAL, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DATA, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_COUNT, access);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgIntervalTargetBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& subresources,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_CAP_BACK_NORMAL,
        targets.csgCapBackNormal.get(),
        targets.csgCapNormalFormat,
        subresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_DEPTH,
        targets.csgIntervalDepth.get(),
        targets.csgIntervalDepthFormat,
        subresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_ID,
        targets.csgIntervalId.get(),
        targets.csgIntervalIdFormat,
        subresources,
        access
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgReceiverEventBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& eventSubresources,
    const Core::TextureSubresourceSet& eventCounterSubresources,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DATA,
        targets.csgReceiverEventData.get(),
        targets.csgReceiverEventDataFormat,
        eventSubresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_COUNT,
        targets.csgReceiverEventCount.get(),
        targets.csgReceiverEventCountFormat,
        eventCounterSubresources,
        access
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgReceiverSpanBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& spanSubresources,
    const Core::TextureSubresourceSet& spanCounterSubresources,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DATA,
        targets.csgReceiverSpanData.get(),
        targets.csgReceiverSpanDataFormat,
        spanSubresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_COUNT,
        targets.csgReceiverSpanCount.get(),
        targets.csgReceiverSpanCountFormat,
        spanCounterSubresources,
        access
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddCsgRemovedIntervalBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& removedIntervalSubresources,
    const Core::TextureSubresourceSet& removedIntervalCounterSubresources,
    CsgTextureAccess::Enum access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DEPTH,
        targets.csgRemovedIntervalDepth.get(),
        targets.csgRemovedIntervalDepthFormat,
        removedIntervalSubresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_CAP_NORMAL,
        targets.csgRemovedIntervalCapNormal.get(),
        targets.csgRemovedIntervalCapNormalFormat,
        removedIntervalSubresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DATA,
        targets.csgRemovedIntervalData.get(),
        targets.csgRemovedIntervalDataFormat,
        removedIntervalSubresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_COUNT,
        targets.csgRemovedIntervalCount.get(),
        targets.csgRemovedIntervalCountFormat,
        removedIntervalCounterSubresources,
        access
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    if(intervalAccess != CsgTextureAccess::None)
        AddCsgIntervalTargetLayoutItems(bindingLayoutDesc, intervalAccess);
    if(receiverEventAccess != CsgTextureAccess::None)
        AddCsgReceiverEventLayoutItems(bindingLayoutDesc, receiverEventAccess);
    if(removedIntervalAccess != CsgTextureAccess::None)
        AddCsgRemovedIntervalLayoutItems(bindingLayoutDesc, removedIntervalAccess);
    const bool usesSampleState = receiverEventAccess != CsgTextureAccess::None || removedIntervalAccess != CsgTextureAccess::None;
    if(usesSampleState)
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CSG_INTERVAL_BINDING_SAMPLE_STATE, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateCsgIntervalBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::Buffer* meshViewBuffer,
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
        !meshViewBuffer
        || (usesSampleState && !sampleStateBuffer)
        || !layout
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
    if(intervalAccess != CsgTextureAccess::None){
        const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
        AddCsgIntervalTargetBindingSetItems(bindingSetDesc, targets, csgPeelSubresources, intervalAccess);
    }
    if(receiverEventAccess != CsgTextureAccess::None){
        const Core::TextureSubresourceSet csgReceiverEventSubresources(0, 1, 0, targets.csgReceiverEventLayerCount);
        const Core::TextureSubresourceSet csgReceiverEventCounterSubresources(0, 1, 0, 1);
        AddCsgReceiverEventBindingSetItems(
            bindingSetDesc,
            targets,
            csgReceiverEventSubresources,
            csgReceiverEventCounterSubresources,
            receiverEventAccess
        );
    }
    if(removedIntervalAccess != CsgTextureAccess::None){
        const Core::TextureSubresourceSet csgRemovedIntervalSubresources(0, 1, 0, targets.csgRemovedIntervalLayerCount);
        const Core::TextureSubresourceSet csgRemovedIntervalCounterSubresources(0, 1, 0, 1);
        AddCsgRemovedIntervalBindingSetItems(
            bindingSetDesc,
            targets,
            csgRemovedIntervalSubresources,
            csgRemovedIntervalCounterSubresources,
            removedIntervalAccess
        );
    }
    if(usesSampleState)
        bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CSG_INTERVAL_BINDING_SAMPLE_STATE, sampleStateBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, meshViewBuffer));

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
    AddCsgReceiverEventLayoutItems(bindingLayoutDesc, CsgTextureAccess::SRV);
    AddCsgReceiverSpanLayoutItems(bindingLayoutDesc, CsgTextureAccess::UAV);
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
    )
        return false;

    const Core::TextureSubresourceSet csgReceiverEventSubresources(0, 1, 0, targets.csgReceiverEventLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventCounterSubresources(0, 1, 0, 1);
    const Core::TextureSubresourceSet csgReceiverSpanSubresources(0, 1, 0, targets.csgReceiverSpanLayerCount);
    const Core::TextureSubresourceSet csgReceiverSpanCounterSubresources(0, 1, 0, 1);
    Core::BindingSetDesc bindingSetDesc(arena);
    AddCsgReceiverEventBindingSetItems(
        bindingSetDesc,
        targets,
        csgReceiverEventSubresources,
        csgReceiverEventCounterSubresources,
        CsgTextureAccess::SRV
    );
    AddCsgReceiverSpanBindingSetItems(
        bindingSetDesc,
        targets,
        csgReceiverSpanSubresources,
        csgReceiverSpanCounterSubresources,
        CsgTextureAccess::UAV
    );

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
    AddCsgIntervalTargetLayoutItems(bindingLayoutDesc, CsgTextureAccess::SRV);
    AddCsgReceiverSpanLayoutItems(bindingLayoutDesc, CsgTextureAccess::SRV);
    AddCsgRemovedIntervalLayoutItems(bindingLayoutDesc, CsgTextureAccess::UAV);
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
    )
        return false;

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
    const Core::TextureSubresourceSet csgReceiverSpanSubresources(0, 1, 0, targets.csgReceiverSpanLayerCount);
    const Core::TextureSubresourceSet csgReceiverSpanCounterSubresources(0, 1, 0, 1);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(0, 1, 0, targets.csgRemovedIntervalLayerCount);
    const Core::TextureSubresourceSet csgRemovedIntervalCounterSubresources(0, 1, 0, 1);
    Core::BindingSetDesc bindingSetDesc(arena);
    AddCsgIntervalTargetBindingSetItems(bindingSetDesc, targets, csgPeelSubresources, CsgTextureAccess::SRV);
    AddCsgReceiverSpanBindingSetItems(
        bindingSetDesc,
        targets,
        csgReceiverSpanSubresources,
        csgReceiverSpanCounterSubresources,
        CsgTextureAccess::SRV
    );
    AddCsgRemovedIntervalBindingSetItems(
        bindingSetDesc,
        targets,
        csgRemovedIntervalSubresources,
        csgRemovedIntervalCounterSubresources,
        CsgTextureAccess::UAV
    );

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

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(intervalBindingLayout)
        .addBindingLayout(clipBindingLayout)
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

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(receiverSpanBuildBindingLayout)
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

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(intervalCombineBindingLayout)
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

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(pixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(intervalSampleBindingLayout)
        .addBindingLayout(clipBindingLayout)
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
        drawState().m_meshViewBuffer.get(),
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
        drawState().m_meshViewBuffer.get(),
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
        drawState().m_meshViewBuffer.get(),
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

