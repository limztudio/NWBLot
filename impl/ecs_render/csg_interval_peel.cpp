// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include <impl/assets/graphics/csg/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgIntervalDispatchPushConstants{
    u32 frameWidth = 0u;
    u32 frameHeight = 0u;
    u32 receiverCount = 0u;
    u32 layerCount = 0u;
    u32 workOffsetX = 0u;
    u32 workOffsetY = 0u;
    u32 workExtentX = 0u;
    u32 workExtentY = 0u;
};

static_assert(sizeof(CsgIntervalDispatchPushConstants) == NWB_CSG_INTERVAL_DISPATCH_PUSH_CONSTANT_BYTE_SIZE, "CSG interval dispatch push constants must match shader layout");

struct CsgIntervalSampleStateGpuData{
    u32 workMinX = 0u;
    u32 workMinY = 0u;
    u32 workMaxX = 0u;
    u32 workMaxY = 0u;
};

static_assert(sizeof(CsgIntervalSampleStateGpuData) == sizeof(u32) * 4u, "CSG interval sample state must match shader layout");

[[nodiscard]] static Core::Rect ResolveCsgFrameWorkRect(const DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData){
    return csgFrameData.workRegion.resolveRect(targets.width, targets.height);
}

[[nodiscard]] static CsgIntervalSampleStateGpuData BuildCsgIntervalSampleState(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    const Core::Rect workRect = ResolveCsgFrameWorkRect(targets, csgFrameData);

    CsgIntervalSampleStateGpuData state;
    state.workMinX = static_cast<u32>(Max(workRect.minX, 0));
    state.workMinY = static_cast<u32>(Max(workRect.minY, 0));
    state.workMaxX = static_cast<u32>(Max(workRect.maxX, 0));
    state.workMaxY = static_cast<u32>(Max(workRect.maxY, 0));
    return state;
}

[[nodiscard]] static CsgIntervalDispatchPushConstants BuildCsgIntervalDispatchPushConstants(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    const Core::Rect workRect = ResolveCsgFrameWorkRect(targets, csgFrameData);

    CsgIntervalDispatchPushConstants pushConstants;
    pushConstants.frameWidth = targets.width;
    pushConstants.frameHeight = targets.height;
    pushConstants.receiverCount = static_cast<u32>(csgFrameData.receiverRanges.size());
    pushConstants.layerCount = Min(targets.csgPeelLayerCount, static_cast<u32>(NWB_CSG_PEEL_LAYER_COUNT));
    pushConstants.workOffsetX = static_cast<u32>(Max(workRect.minX, 0));
    pushConstants.workOffsetY = static_cast<u32>(Max(workRect.minY, 0));
    pushConstants.workExtentX = static_cast<u32>(Max(workRect.width(), 0));
    pushConstants.workExtentY = static_cast<u32>(Max(workRect.height(), 0));
    return pushConstants;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum class CsgTextureAccess{
    None,
    SRV,
    UAV
};

static void AddCsgTextureBindingLayoutItem(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    u32 slot,
    CsgTextureAccess access
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

static void AddCsgTextureBindingSetItem(
    Core::BindingSetDesc& bindingSetDesc,
    u32 slot,
    Core::Texture* texture,
    Core::Format::Enum format,
    const Core::TextureSubresourceSet& subresources,
    CsgTextureAccess access
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

static void AddCsgIntervalTargetLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_CAP_BACK_NORMAL, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_LINEAR_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_ID, access);
}

static void AddCsgReceiverEventLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DATA, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_COUNT, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_FLAGS, access);
}

static void AddCsgReceiverSpanLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DATA, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_COUNT, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_FLAGS, access);
}

static void AddCsgRemovedIntervalLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_CAP_NORMAL, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DATA, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_COUNT, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_FLAGS, access);
}

static void AddCsgIntervalTargetBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& subresources,
    CsgTextureAccess access
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
        NWB_CSG_INTERVAL_BINDING_LINEAR_DEPTH,
        targets.csgIntervalLinearDepth.get(),
        targets.csgIntervalLinearDepthFormat,
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

static void AddCsgReceiverEventBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& eventSubresources,
    const Core::TextureSubresourceSet& eventCounterSubresources,
    CsgTextureAccess access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DEPTH,
        targets.csgReceiverEventDepth.get(),
        targets.csgReceiverEventDepthFormat,
        eventSubresources,
        access
    );
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
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_FLAGS,
        targets.csgReceiverEventFlags.get(),
        targets.csgReceiverEventFlagsFormat,
        eventCounterSubresources,
        access
    );
}

static void AddCsgReceiverSpanBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& spanSubresources,
    const Core::TextureSubresourceSet& spanCounterSubresources,
    CsgTextureAccess access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DEPTH,
        targets.csgReceiverSpanDepth.get(),
        targets.csgReceiverSpanDepthFormat,
        spanSubresources,
        access
    );
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
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_FLAGS,
        targets.csgReceiverSpanFlags.get(),
        targets.csgReceiverSpanFlagsFormat,
        spanCounterSubresources,
        access
    );
}

static void AddCsgRemovedIntervalBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& removedIntervalSubresources,
    const Core::TextureSubresourceSet& removedIntervalCounterSubresources,
    CsgTextureAccess access
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
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_FLAGS,
        targets.csgRemovedIntervalFlags.get(),
        targets.csgRemovedIntervalFlagsFormat,
        removedIntervalCounterSubresources,
        access
    );
}

[[nodiscard]] static bool CreateCsgIntervalBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    Core::ShaderType::Mask visibility,
    CsgTextureAccess intervalAccess,
    CsgTextureAccess receiverEventAccess,
    CsgTextureAccess removedIntervalAccess
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

[[nodiscard]] static bool CreateCsgIntervalBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::Buffer* meshViewBuffer,
    Core::Buffer* sampleStateBuffer,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet,
    CsgTextureAccess intervalAccess,
    CsgTextureAccess receiverEventAccess,
    CsgTextureAccess removedIntervalAccess
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
            || !targets.csgIntervalLinearDepth
            || !targets.csgIntervalId
            || targets.csgPeelLayerCount == 0u
        )
    )
        return false;
    if(
        receiverEventAccess != CsgTextureAccess::None
        && (
            !targets.csgReceiverEventDepth
            || !targets.csgReceiverEventData
            || !targets.csgReceiverEventCount
            || !targets.csgReceiverEventFlags
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
            || !targets.csgRemovedIntervalFlags
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
        !targets.csgReceiverEventDepth
        || !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgReceiverEventFlags
        || !targets.csgReceiverSpanDepth
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !targets.csgReceiverSpanFlags
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
        || !targets.csgIntervalLinearDepth
        || !targets.csgIntervalId
        || !targets.csgReceiverSpanDepth
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !targets.csgReceiverSpanFlags
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || !targets.csgRemovedIntervalFlags
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        || !targets.csgIntervalLinearDepth
        || !targets.csgIntervalId
        || !targets.csgReceiverEventDepth
        || !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgReceiverEventFlags
        || !targets.csgReceiverSpanDepth
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !targets.csgReceiverSpanFlags
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || !targets.csgRemovedIntervalFlags
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

bool RendererCsgSystem::createCsgIntervalSampleResources(DeferredFrameTargets& targets){
    auto* device = graphics().getDevice();
    if(!drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires a mesh view buffer"));
        return false;
    }
    if(
        !targets.csgReceiverEventDepth
        || !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgReceiverEventFlags
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || !targets.csgRemovedIntervalFlags
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

bool RendererCsgSystem::uploadCsgIntervalSampleState(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return true;
    if(!csgState().m_intervalSampleStateBuffer)
        return false;

    const __hidden_csg_interval_peel::CsgIntervalSampleStateGpuData state =
        __hidden_csg_interval_peel::BuildCsgIntervalSampleState(targets, csgFrameData)
    ;
    commandList.setBufferState(csgState().m_intervalSampleStateBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(csgState().m_intervalSampleStateBuffer.get(), &state, sizeof(state));
    commandList.setBufferState(csgState().m_intervalSampleStateBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

void RendererCsgSystem::destroyCsgIntervalPeelBindingSet(){
    csgState().m_intervalPeelBindingSet.reset();
    csgState().m_receiverSpanBuildBindingSet.reset();
    csgState().m_intervalCombineBindingSet.reset();
}

void RendererCsgSystem::dispatchCsgIntervalPeels(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(csgState().m_intervalPeelPipeline);
    NWB_ASSERT(csgState().m_intervalPeelBindingSet);
    NWB_ASSERT(csgState().m_clipBindingSet);

    commandList.setResourceStatesForBindingSet(csgState().m_intervalPeelBindingSet.get());
    commandList.setResourceStatesForBindingSet(csgState().m_clipBindingSet.get());
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(csgState().m_intervalPeelPipeline.get());
    computeState.addBindingSet(csgState().m_intervalPeelBindingSet.get());
    computeState.addBindingSet(csgState().m_clipBindingSet.get());
    commandList.setComputeState(computeState);

    const __hidden_csg_interval_peel::CsgIntervalDispatchPushConstants pushConstants =
        __hidden_csg_interval_peel::BuildCsgIntervalDispatchPushConstants(targets, csgFrameData)
    ;
    if(pushConstants.workExtentX == 0u || pushConstants.workExtentY == 0u)
        return;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(
        DivideUp(pushConstants.workExtentX, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X)),
        DivideUp(pushConstants.workExtentY, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y)),
        1u
    );
}

void RendererCsgSystem::dispatchCsgReceiverSpanBuild(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(csgState().m_receiverSpanBuildPipeline);
    NWB_ASSERT(csgState().m_receiverSpanBuildBindingSet);

    commandList.endRenderPass();
    commandList.setResourceStatesForBindingSet(csgState().m_receiverSpanBuildBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(csgState().m_receiverSpanBuildPipeline.get());
    computeState.addBindingSet(csgState().m_receiverSpanBuildBindingSet.get());
    commandList.setComputeState(computeState);

    const __hidden_csg_interval_peel::CsgIntervalDispatchPushConstants pushConstants =
        __hidden_csg_interval_peel::BuildCsgIntervalDispatchPushConstants(targets, csgFrameData)
    ;
    if(pushConstants.workExtentX == 0u || pushConstants.workExtentY == 0u)
        return;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(
        DivideUp(pushConstants.workExtentX, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X)),
        DivideUp(pushConstants.workExtentY, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y)),
        1u
    );
}

void RendererCsgSystem::dispatchCsgIntervalCombine(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(csgState().m_intervalCombinePipeline);
    NWB_ASSERT(csgState().m_intervalCombineBindingSet);

    commandList.endRenderPass();
    commandList.setResourceStatesForBindingSet(csgState().m_intervalCombineBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(csgState().m_intervalCombinePipeline.get());
    computeState.addBindingSet(csgState().m_intervalCombineBindingSet.get());
    commandList.setComputeState(computeState);

    const __hidden_csg_interval_peel::CsgIntervalDispatchPushConstants pushConstants =
        __hidden_csg_interval_peel::BuildCsgIntervalDispatchPushConstants(targets, csgFrameData)
    ;
    if(pushConstants.workExtentX == 0u || pushConstants.workExtentY == 0u)
        return;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(
        DivideUp(pushConstants.workExtentX, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X)),
        DivideUp(pushConstants.workExtentY, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y)),
        1u
    );
}

void RendererCsgSystem::renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData){
    NWB_ASSERT(csgState().m_intervalCapFillPipeline);
    NWB_ASSERT(csgState().m_intervalSampleBindingSet);
    NWB_ASSERT(csgState().m_clipBindingSet);
    NWB_ASSERT(targets.framebuffer);

    commandList.setResourceStatesForBindingSet(csgState().m_intervalSampleBindingSet.get());
    commandList.setResourceStatesForBindingSet(csgState().m_clipBindingSet.get());
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();

    Core::ViewportState viewportState;
    viewportState
        .addViewport(targets.framebuffer->getFramebufferInfo().getViewport())
        .addScissorRect(__hidden_csg_interval_peel::ResolveCsgFrameWorkRect(targets, csgFrameData))
    ;

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(csgState().m_intervalCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.framebuffer.get());
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(csgState().m_intervalSampleBindingSet.get());
    graphicsState.addBindingSet(csgState().m_clipBindingSet.get());
    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

