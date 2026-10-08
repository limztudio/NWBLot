// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_interval_private.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CsgIntervalDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::Rect ResolveCsgFrameWorkRect(const DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData){
    return csgFrameData.workRegion.resolveRect(targets.width, targets.height);
}


[[nodiscard]] static CsgIntervalSampleStateGpuData BuildCsgIntervalSampleState(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const u32 meshViewHeapSlot
){
    const Core::Rect workRect = ResolveCsgFrameWorkRect(targets, csgFrameData);

    const SIMDVector workRectLanes = VectorMax(VectorSet(static_cast<f32>(workRect.minX), static_cast<f32>(workRect.minY), static_cast<f32>(workRect.maxX), static_cast<f32>(workRect.maxY)), VectorZero());
    CsgIntervalSampleStateGpuData state;
    state.workMinX = static_cast<u32>(VectorGetX(workRectLanes));
    state.workMinY = static_cast<u32>(VectorGetY(workRectLanes));
    state.workMaxX = static_cast<u32>(VectorGetZ(workRectLanes));
    state.workMaxY = static_cast<u32>(VectorGetW(workRectLanes));
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
    const SIMDVector workOffsetExtentLanes = VectorMax(VectorSet(static_cast<f32>(workRect.minX), static_cast<f32>(workRect.minY), static_cast<f32>(workRect.width()), static_cast<f32>(workRect.height())), VectorZero());
    pushConstants.layerCount = static_cast<u32>(VectorGetX(VectorMin(VectorReplicate(static_cast<f32>(targets.csgPeelLayerCount)), VectorReplicate(static_cast<f32>(NWB_CSG_PEEL_LAYER_COUNT)))));
    pushConstants.workOffsetX = static_cast<u32>(VectorGetX(workOffsetExtentLanes));
    pushConstants.workOffsetY = static_cast<u32>(VectorGetY(workOffsetExtentLanes));
    pushConstants.workExtentX = static_cast<u32>(VectorGetZ(workOffsetExtentLanes));
    pushConstants.workExtentY = static_cast<u32>(VectorGetW(workOffsetExtentLanes));
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
    // Push-only layout; the rest is heap-selected.
    commandList.setComputeState(computeState);
    // Bind after setComputeState so the StorageImage table resolves.
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


Expected<CsgIntervalSampleStateGpuData> RendererCsgSystem::prepareCsgIntervalSampleStateData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
)const{
    CsgIntervalSampleStateGpuData state{};
    if(!csgFrameData.hasWork())
        return state;
    if(
        !csgResources.frameReady(csgFrameData)
        || !frameBindings.bindingValid()
    )
        return MakeUnexpected(Failure{});

    // Freeze rect and slot before recording; later state must not leak in.
    state = CsgIntervalDetail::BuildCsgIntervalSampleState(
        targets,
        csgFrameData,
        frameBindings.meshView.heapHandle.slot()
    );
    return state;
}

void RendererCsgSystem::dispatchCsgIntervalPeels(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_intervalPeelPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));
    NWB_ASSERT(frameBindings.bindingValid());

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgIntervalPeel, m_graphics.getDevice(), commandList);

    CsgIntervalDetail::DispatchCsgIntervalCompute(
        commandList,
        m_graphics.getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        m_csgState.m_intervalPeelPipeline.get(),
        csgResources.clipContextSlotsHeapHandle.slot(),
        frameBindings.meshView.heapHandle.slot()
    );
}

void RendererCsgSystem::dispatchCsgReceiverSpanBuild(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_receiverSpanBuildPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgReceiverSpanBuild, m_graphics.getDevice(), commandList);

    commandList.endRenderPass();
    CsgIntervalDetail::DispatchCsgIntervalCompute(
        commandList,
        m_graphics.getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        m_csgState.m_receiverSpanBuildPipeline.get(),
        csgResources.clipContextSlotsHeapHandle.slot()
    );
}

void RendererCsgSystem::dispatchCsgIntervalCombine(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_intervalCombinePipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgIntervalCombine, m_graphics.getDevice(), commandList);

    commandList.endRenderPass();
    CsgIntervalDetail::DispatchCsgIntervalCompute(
        commandList,
        m_graphics.getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        m_csgState.m_intervalCombinePipeline.get(),
        csgResources.clipContextSlotsHeapHandle.slot()
    );
}

void RendererCsgSystem::renderCsgIntervalCaps(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources
){
    NWB_ASSERT(m_csgState.m_intervalCapFillPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));
    NWB_ASSERT(targets.framebuffer);

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgCapFill, m_graphics.getDevice(), commandList);

    Core::ViewportState viewportState;
    viewportState
        .addViewport(targets.framebuffer->getFramebufferInfo().getViewport())
        .addScissorRect(CsgIntervalDetail::ResolveCsgFrameWorkRect(targets, csgFrameData))
    ;

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_csgState.m_intervalCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.framebuffer.get());
    graphicsState.setViewport(viewportState);
    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *m_csgState.m_intervalCapFillPipeline);

    ECSRenderDetail::MeshFrameHeapSlots frameHeapSlots;
    frameHeapSlots.generatedVertex = csgResources.clipContextSlotsHeapHandle.slot();
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

