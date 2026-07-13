// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_interval_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


[[nodiscard]] static Core::Rect ResolveCsgFrameWorkRect(const DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData){
    return csgFrameData.workRegion.resolveRect(targets.width, targets.height);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


static void DispatchCsgIntervalCompute(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    Core::ComputePipeline* pipeline,
    Core::BindingSet* bindingSet,
    Core::BindingSet* extraBindingSet = nullptr
){
    Core::ComputeState computeState;
    computeState.setPipeline(pipeline);
    computeState.addBindingSet(bindingSet);
    if(extraBindingSet)
        computeState.addBindingSet(extraBindingSet);
    commandList.setComputeState(computeState);

    const CsgIntervalDispatchPushConstants pushConstants =
        BuildCsgIntervalDispatchPushConstants(targets, csgFrameData)
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::uploadCsgIntervalSampleState(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return true;
    if(!csgState().m_intervalSampleStateBuffer)
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgSampleStateUpload, graphics().getDevice(), commandList);

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::destroyCsgIntervalPeelBindingSet(){
    csgState().m_intervalPeelBindingSet.reset();
    csgState().m_receiverSpanBuildBindingSet.reset();
    csgState().m_intervalCombineBindingSet.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalPeel, graphics().getDevice(), commandList);

    commandList.setResourceStatesForBindingSet(csgState().m_intervalPeelBindingSet.get());
    commandList.setResourceStatesForBindingSet(csgState().m_clipBindingSet.get());
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        targets,
        csgFrameData,
        csgState().m_intervalPeelPipeline.get(),
        csgState().m_intervalPeelBindingSet.get(),
        csgState().m_clipBindingSet.get()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::dispatchCsgReceiverSpanBuild(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(csgState().m_receiverSpanBuildPipeline);
    NWB_ASSERT(csgState().m_receiverSpanBuildBindingSet);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgReceiverSpanBuild, graphics().getDevice(), commandList);

    commandList.endRenderPass();
    commandList.setResourceStatesForBindingSet(csgState().m_receiverSpanBuildBindingSet.get());
    commandList.commitBarriers();

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        targets,
        csgFrameData,
        csgState().m_receiverSpanBuildPipeline.get(),
        csgState().m_receiverSpanBuildBindingSet.get()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::dispatchCsgIntervalCombine(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(csgState().m_intervalCombinePipeline);
    NWB_ASSERT(csgState().m_intervalCombineBindingSet);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalCombine, graphics().getDevice(), commandList);

    commandList.endRenderPass();
    commandList.setResourceStatesForBindingSet(csgState().m_intervalCombineBindingSet.get());
    commandList.commitBarriers();

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        targets,
        csgFrameData,
        csgState().m_intervalCombinePipeline.get(),
        csgState().m_intervalCombineBindingSet.get()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData){
    NWB_ASSERT(csgState().m_intervalCapFillPipeline);
    NWB_ASSERT(csgState().m_intervalSampleBindingSet);
    NWB_ASSERT(csgState().m_clipBindingSet);
    NWB_ASSERT(targets.framebuffer);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgCapFill, graphics().getDevice(), commandList);

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

