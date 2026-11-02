// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void DispatchCsgIntervalCompute(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    Core::ComputePipeline& pipeline,
    const u32 csgContextHeapSlot,
    const u32 meshViewHeapSlot = 0u
){
    Core::ComputeState computeState;
    computeState.setPipeline(&pipeline);
    // Push-only layout; the rest is heap-selected.
    commandList.setComputeState(computeState);
    // Bind after setComputeState so the StorageImage table resolves.
    heap.bindCompute(commandList, pipeline);

    const CsgIntervalDispatchPushConstants pushConstants = ResolveCsgIntervalDispatchPushConstants(
        csgFrameData.workRegion,
        targets.width,
        targets.height,
        targets.csgPeelLayerCount,
        static_cast<u32>(csgFrameData.receiverRanges.size()),
        meshViewHeapSlot,
        csgContextHeapSlot
    );
    if(pushConstants.workExtentX == 0u || pushConstants.workExtentY == 0u)
        return;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(pushConstants.workExtentX, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X)), DivideUp(pushConstants.workExtentY, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y)), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::dispatchCsgIntervalPeels(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_intervalPeelPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));
    NWB_ASSERT(frameBindings.bindingValid());

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgIntervalPeel, m_graphics.getDevice(), commandList);

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        m_graphics.getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        *m_csgState.m_intervalPeelPipeline,
        csgResources.clipContextSlotsHeapHandle.slot(),
        frameBindings.meshView.heapHandle.slot()
    );
}

void RendererCsgSystem::dispatchCsgReceiverSpanBuild(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_receiverSpanBuildPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgReceiverSpanBuild, m_graphics.getDevice(), commandList);

    commandList.endRenderPass();
    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(commandList, m_graphics.getDevice().getDescriptorHeap(), targets, csgFrameData, *m_csgState.m_receiverSpanBuildPipeline, csgResources.clipContextSlotsHeapHandle.slot());
}

void RendererCsgSystem::dispatchCsgIntervalCombine(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_intervalCombinePipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgIntervalCombine, m_graphics.getDevice(), commandList);

    commandList.endRenderPass();
    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(commandList, m_graphics.getDevice().getDescriptorHeap(), targets, csgFrameData, *m_csgState.m_intervalCombinePipeline, csgResources.clipContextSlotsHeapHandle.slot());
}

void RendererCsgSystem::renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources){
    NWB_ASSERT(m_csgState.m_intervalCapFillPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));
    NWB_ASSERT(targets.framebuffer);

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgCapFill, m_graphics.getDevice(), commandList);

    Core::ViewportState viewportState;
    viewportState.addViewport(targets.framebuffer->getFramebufferInfo().getViewport()).addScissorRect(ToCoreRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height)));

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_csgState.m_intervalCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.framebuffer.get());
    graphicsState.setViewport(viewportState);
    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *m_csgState.m_intervalCapFillPipeline);

    MeshFrameHeapSlots frameHeapSlots;
    frameHeapSlots.generatedVertex = csgResources.clipContextSlotsHeapHandle.slot();
    ECSRenderDetail::SetShaderDrivenPushConstants(commandList, 0u, 0u, 0u, viewportState, frameHeapSlots, 0u);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
}

void RendererCsgSystem::renderRefractionIntervalCaps(Core::CommandList& commandList, const DeferredFrameTargets& targets, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources, const Core::Rect& workRect, const Span<const u32> receiverIndices){
    NWB_ASSERT(csgResources.refractionCapFillPipeline);
    NWB_ASSERT(csgResources.bindingValid());
    NWB_ASSERT(targets.avboit.refractionFramebuffer);
    NWB_ASSERT(targets.bindless.slotsBufferDescriptor.valid());
    NWB_ASSERT(!receiverIndices.empty());

    Core::ViewportState viewportState;
    viewportState.addViewport(targets.avboit.refractionFramebuffer->getFramebufferInfo().getViewport()).addScissorRect(workRect);
    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(csgResources.refractionCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.avboit.refractionFramebuffer.get());
    graphicsState.setViewport(viewportState);
    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *csgResources.refractionCapFillPipeline);

    MeshFrameHeapSlots frameHeapSlots;
    frameHeapSlots.view = targets.bindless.slotsBufferDescriptor.slot();
    frameHeapSlots.generatedVertex = csgResources.clipContextSlotsHeapHandle.slot();
    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(s_FullscreenTriangleVertexCount);
    for(const u32 receiverIndex : receiverIndices){
        ECSRenderDetail::SetShaderDrivenPushConstants(commandList, 0u, receiverIndex, 0u, viewportState, frameHeapSlots, 0u);
        commandList.draw(drawArgs);
    }
}

Expected<CsgIntervalSampleStateGpuData> RendererCsgSystem::prepareCsgIntervalSampleStateData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
)const noexcept{
    CsgIntervalSampleStateGpuData state{};
    if(!csgFrameData.hasWork())
        return state;
    if(!csgResources.frameReady(csgFrameData) || !frameBindings.bindingValid())
        return MakeUnexpected(Failure{});

    // Freeze rect and slot before recording; later state must not leak in.
    state = ResolveCsgIntervalSampleState(csgFrameData.workRegion, targets.width, targets.height, frameBindings.meshView.heapHandle.slot());
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

