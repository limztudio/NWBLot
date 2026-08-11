// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_interval_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::Rect ResolveCsgFrameWorkRect(const DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData){
    return csgFrameData.workRegion.resolveRect(targets.width, targets.height);
}


static void SetCsgIntervalPeelStorageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    commandList.setTextureState(targets.csgCapBackNormal.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgIntervalDepth.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgIntervalId.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}

static void SetCsgReceiverSpanStorageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    // These load-only inputs still use StorageImage heap descriptors, whose Vulkan image layout is GENERAL.
    commandList.setTextureState(targets.csgReceiverEventData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgReceiverEventCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgReceiverSpanData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgReceiverSpanCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}

static void SetCsgIntervalCombineStorageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    // The combine pass loads through StorageImage aliases, so all inputs retain the descriptor's GENERAL layout.
    commandList.setTextureState(targets.csgCapBackNormal.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgIntervalDepth.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgIntervalId.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgReceiverSpanData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgReceiverSpanCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}

static void SetCsgIntervalSampleStorageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    // Sampling is implemented as Load through StorageImage aliases, not sampled-image descriptors; keep GENERAL.
    commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}


[[nodiscard]] static CsgIntervalSampleStateGpuData BuildCsgIntervalSampleState(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const u32 meshViewHeapSlot
){
    const Core::Rect workRect = ResolveCsgFrameWorkRect(targets, csgFrameData);

    CsgIntervalSampleStateGpuData state;
    state.workMinX = static_cast<u32>(Max(workRect.minX, 0));
    state.workMinY = static_cast<u32>(Max(workRect.minY, 0));
    state.workMaxX = static_cast<u32>(Max(workRect.maxX, 0));
    state.workMaxY = static_cast<u32>(Max(workRect.maxY, 0));
    state.meshViewHeapSlot = meshViewHeapSlot;
    return state;
}


[[nodiscard]] static CsgIntervalDispatchPushConstants BuildCsgIntervalDispatchPushConstants(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const u32 meshViewHeapSlot,
    const u32 csgContextHeapSlot
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
    pushConstants.meshViewHeapSlot = meshViewHeapSlot;
    pushConstants.csgContextHeapSlot = csgContextHeapSlot;
    return pushConstants;
}


static void DispatchCsgIntervalCompute(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    Core::ComputePipeline* pipeline,
    const u32 csgContextHeapSlot,
    const u32 meshViewHeapSlot = 0u
){
    Core::ComputeState computeState;
    computeState.setPipeline(pipeline);
    // This CSG layout owns only the dispatch push range.  Images, view data, and CSG context are selected from the
    // global descriptor heap, so no pipeline-local resource descriptor is installed.
    commandList.setComputeState(computeState);
    // The CSG pipeline carries the persistent heap layouts at sets 8/9; bind after setComputeState installs that
    // pipeline layout so its StorageImage table resolves the target-generation slots.
    heap.bindCompute(commandList, *pipeline);

    const CsgIntervalDispatchPushConstants pushConstants =
        BuildCsgIntervalDispatchPushConstants(targets, csgFrameData, meshViewHeapSlot, csgContextHeapSlot)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::prepareCsgIntervalSampleStateData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    CsgIntervalSampleStateGpuData& outState
)const{
    outState = CsgIntervalSampleStateGpuData{};
    if(!csgFrameData.hasWork())
        return true;
    if(
        !csgState().m_intervalSampleStateBuffer
        || !drawState().m_meshViewBufferHeapHandle.valid()
        || !m_renderer.meshSystem().meshFrameHeapHandlesReady()
    )
        return false;

    // The work rectangle and heap slot are both resolved after preflight chose this frame's target generation.
    // Freeze them before graph recording so retry/acceptance cannot observe later mutable renderer state.
    outState = __hidden_csg_interval_peel::BuildCsgIntervalSampleState(
        targets,
        csgFrameData,
        drawState().m_meshViewBufferHeapHandle.slot()
    );
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
    NWB_ASSERT(m_renderer.meshSystem().meshFrameHeapHandlesReady());

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgSampleStateUpload, graphics().getDevice(), commandList);

    CsgIntervalSampleStateGpuData state;
    if(!prepareCsgIntervalSampleStateData(targets, csgFrameData, state))
        return false;
    commandList.setBufferState(csgState().m_intervalSampleStateBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(csgState().m_intervalSampleStateBuffer.get(), &state, sizeof(state));
    commandList.setBufferState(csgState().m_intervalSampleStateBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

void RendererCsgSystem::invalidateCsgIntervalPeelPipelines(){
    // Target descriptors are target-generation heap entries, not pipeline-local descriptor objects. Pipelines remain reusable
    // across target replacement; CreateIntervalCapFillPipeline refreshes its framebuffer-dependent variant.
}

void RendererCsgSystem::dispatchCsgIntervalPeels(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(csgState().m_intervalPeelPipeline);
    NWB_ASSERT(csgState().m_clipContextSlotsHeapHandle.valid());
    NWB_ASSERT(drawState().m_meshViewBuffer);
    NWB_ASSERT(m_renderer.meshSystem().meshFrameHeapHandlesReady());

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalPeel, graphics().getDevice(), commandList);

    __hidden_csg_interval_peel::SetCsgIntervalPeelStorageStates(commandList, targets);
    // The view is heap-selected; transition its UniformBuffer
    // explicitly before the standalone peel dispatch.
    commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        graphics().getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        csgState().m_intervalPeelPipeline.get(),
        csgState().m_clipContextSlotsHeapHandle.slot(),
        drawState().m_meshViewBufferHeapHandle.slot()
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
    NWB_ASSERT(csgState().m_clipContextSlotsHeapHandle.valid());

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgReceiverSpanBuild, graphics().getDevice(), commandList);

    commandList.endRenderPass();
    __hidden_csg_interval_peel::SetCsgReceiverSpanStorageStates(commandList, targets);
    commandList.commitBarriers();

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        graphics().getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        csgState().m_receiverSpanBuildPipeline.get(),
        csgState().m_clipContextSlotsHeapHandle.slot()
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
    NWB_ASSERT(csgState().m_clipContextSlotsHeapHandle.valid());

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalCombine, graphics().getDevice(), commandList);

    commandList.endRenderPass();
    __hidden_csg_interval_peel::SetCsgIntervalCombineStorageStates(commandList, targets);
    commandList.commitBarriers();

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        graphics().getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        csgState().m_intervalCombinePipeline.get(),
        csgState().m_clipContextSlotsHeapHandle.slot()
    );
}

void RendererCsgSystem::renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData){
    NWB_ASSERT(csgState().m_intervalCapFillPipeline);
    NWB_ASSERT(csgState().m_clipContextSlotsHeapHandle.valid());
    NWB_ASSERT(drawState().m_materialTypedBuffer);
    NWB_ASSERT(drawState().m_instanceBuffer);
    NWB_ASSERT(drawState().m_meshViewBuffer);
    NWB_ASSERT(targets.framebuffer);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgCapFill, graphics().getDevice(), commandList);

    __hidden_csg_interval_peel::SetCsgIntervalSampleStorageStates(commandList, targets);
    // The cap-fill surface evaluator reaches typed words and mesh instances through heap slots, so its pipeline-local
    // material descriptor cannot contribute these transitions automatically.
    commandList.setBufferState(drawState().m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(drawState().m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
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
    commandList.setGraphicsState(graphicsState);
    graphics().getDevice().getDescriptorHeap().bindGraphics(commandList, *csgState().m_intervalCapFillPipeline);

    ECSRenderDetail::MeshFrameHeapSlots frameHeapSlots;
    frameHeapSlots.generatedVertex = csgState().m_clipContextSlotsHeapHandle.slot();
    ECSRenderDetail::SetShaderDrivenPushConstants(
        commandList,
        0u,
        0u,
        0u,
        viewportState,
        frameHeapSlots,
        0u
    );

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(ECSRenderDetail::s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

