// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/assets/graphics/avboit/binding_slots.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_pass{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



static void DispatchAvboitCompute(const AvboitFrameTargets& targets, const u32 groupCountX, const b8 hdr10OutputActive, Core::CommandList& commandList, Core::ComputePipeline& pipeline, Core::GpuDescriptorHeap& heap){
    NWB_ASSERT(heap.isInitialized());

    Core::ComputeState computeState;
    computeState.setPipeline(&pipeline);
    // Low set is push-only; work resources use heap slots.
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, pipeline);

    const RendererAvboitPushConstants pushConstants = BuildRendererAvboitPushConstants(targets, hdr10OutputActive);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(groupCountX, 1, 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererAvboitSystem::prepareAvboitPassResources(Core::Alloc::ScratchArena& scratchArena, const CsgFrameState& csgFrameState, DeferredFrameTargets& targets){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    if(!avboitTargets.valid())
        return false;

    const b8 hasTransparentCsgWork = csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork;
    if(hasTransparentCsgWork && !m_materialSystem.prepareMaterialPassResources(scratchArena, targets.framebuffer.get(), MaterialPipelinePass::CsgReceiverSurface, true, csgFrameState, nullptr))
        return false;

    return
        m_materialSystem.prepareMaterialPassResources(scratchArena, avboitTargets.lowFramebuffer.get(), MaterialPipelinePass::AvboitOccupancy, true, csgFrameState, &avboitTargets)
        && m_materialSystem.prepareMaterialPassResources(scratchArena, avboitTargets.lowFramebuffer.get(), MaterialPipelinePass::AvboitExtinction, true, csgFrameState, &avboitTargets)
        && m_materialSystem.prepareMaterialPassResources(scratchArena, avboitTargets.accumulationFramebuffer.get(), MaterialPipelinePass::AvboitAccumulate, true, csgFrameState, &avboitTargets)
        && (!avboitTargets.refractionFramebuffer || m_materialSystem.prepareMaterialPassResources(scratchArena, avboitTargets.refractionFramebuffer.get(), MaterialPipelinePass::AvboitRefractionCapture, true, csgFrameState, &avboitTargets))
    ;
}

void RendererAvboitSystem::renderAvboitTransparentCsgIntervals(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItemsView& receiverSurfaceDrawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const usize instanceCount,
    const usize materialTypedByteCount,
    const b8 materialGeometryStatesGraphOwned,
    Optional<Core::GpuTimingMeasure>& intervalTiming
){
    if(!targets.framebuffer || receiverSurfaceDrawItems.empty() || !csgFrameData.hasWork() || !csgResources.frameReady(csgFrameData) || !frameBindings.bindingValid())
        return;

    if(intervalTiming.has_value()){
        intervalTiming.value().discardTiming();
        intervalTiming.reset();
    }
    intervalTiming.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_TransparentCsgIntervals, m_graphics.getDevice(), commandList);

    // The native step only consumes graph-owned data; never rebuild it.
    const b8 drawBuffersReady = frameBindings.frameReady(instanceCount, materialTypedByteCount);
    const b8 receiverSurfaceDrawResourcesReady = m_materialSystem.drawRecorder().materialPassDrawResourcesReady(receiverSurfaceDrawItems, frameBindings);
    if(!drawBuffersReady || !receiverSurfaceDrawResourcesReady){
        // Preserve the short interval; drop the unfinished reservation.
        if(!Core::FinishSplitGpuTimingMarker(&intervalTiming))
            return;
        intervalTiming.value().finishTiming(commandList);
        intervalTiming.reset();
        return;
    }

    Core::ViewportState viewportState;
    viewportState.addViewport(targets.framebuffer->getFramebufferInfo().getViewport()).addScissorRect(ToCoreRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height)));

    m_csgSystem.dispatchCsgIntervalPeels(commandList, targets, csgFrameData, csgResources, frameBindings);

    const MaterialPassDrawContext csgReceiverSurfaceDrawContext{ commandList, targets.framebuffer.get(), nullptr, viewportState, &csgResources, frameBindings, MaterialPipelinePass::CsgReceiverSurface, true, materialGeometryStatesGraphOwned, false };
    m_materialSystem.drawRecorder().renderMaterialPassDrawItems(csgReceiverSurfaceDrawContext, receiverSurfaceDrawItems);

    // Span and Combine are separate graph tasks; the aggregate endpoint remains open.
    if(!Core::FinishSplitGpuTimingMarker(&intervalTiming))
        return;
    // Close the raster pass even when the span dispatch moved out.
    commandList.endRenderPass();
}

void RendererAvboitSystem::renderAvboitOccupancyPass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItemPartitionsView& preparedOccupancyDrawItems,
    const CsgFrameGpuDataView& preparedOccupancyCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& preparedOccupancyCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& preparedOccupancyFrameBindings,
    const usize preparedOccupancyInstanceCount,
    const usize preparedOccupancyMaterialTypedByteCount,
    const b8 occupancyMaterialFrameStatesGraphOwned,
    const b8 occupancyMaterialGeometryStatesGraphOwned,
    const b8 occupancyComputeEmulationOutputStatesGraphOwned,
    const b8 occupancyCsgComputeEmulationOutputStatesGraphOwned,
    const b8 generatedGeometryReused,
    Optional<Core::GpuTimingMeasure>* const occupancyComputeEmulationTiming
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(m_avboitState.m_depthWarpPipeline);
    NWB_ASSERT(m_avboitState.m_integratePipeline);

    renderAvboitMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitOccupancy,
        avboitTargets,
        preparedOccupancyDrawItems,
        preparedOccupancyCsgFrameData,
        preparedOccupancyCsgResources,
        preparedOccupancyFrameBindings,
        preparedOccupancyInstanceCount,
        preparedOccupancyMaterialTypedByteCount,
        occupancyMaterialFrameStatesGraphOwned,
        occupancyMaterialGeometryStatesGraphOwned,
        occupancyComputeEmulationOutputStatesGraphOwned,
        occupancyCsgComputeEmulationOutputStatesGraphOwned,
        generatedGeometryReused,
        occupancyComputeEmulationTiming
    );
}

void RendererAvboitSystem::renderAvboitExtinctionPass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItemPartitionsView& preparedExtinctionDrawItems,
    const CsgFrameGpuDataView& preparedExtinctionCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& preparedExtinctionCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& preparedExtinctionFrameBindings,
    const usize preparedExtinctionInstanceCount,
    const usize preparedExtinctionMaterialTypedByteCount,
    const b8 extinctionMaterialFrameStatesGraphOwned,
    const b8 extinctionMaterialGeometryStatesGraphOwned,
    const b8 extinctionComputeEmulationOutputStatesGraphOwned,
    const b8 extinctionCsgComputeEmulationOutputStatesGraphOwned,
    const b8 generatedGeometryReused,
    Optional<Core::GpuTimingMeasure>* const extinctionComputeEmulationTiming
){
    AvboitFrameTargets& avboitTargets = targets.avboit;

    renderAvboitMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitExtinction,
        avboitTargets,
        preparedExtinctionDrawItems,
        preparedExtinctionCsgFrameData,
        preparedExtinctionCsgResources,
        preparedExtinctionFrameBindings,
        preparedExtinctionInstanceCount,
        preparedExtinctionMaterialTypedByteCount,
        extinctionMaterialFrameStatesGraphOwned,
        extinctionMaterialGeometryStatesGraphOwned,
        extinctionComputeEmulationOutputStatesGraphOwned,
        extinctionCsgComputeEmulationOutputStatesGraphOwned,
        generatedGeometryReused,
        extinctionComputeEmulationTiming
    );
}

void RendererAvboitSystem::renderAvboitAccumulatePass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItemPartitionsView& preparedAccumulationDrawItems,
    const CsgFrameGpuDataView& preparedAccumulationCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& preparedAccumulationCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& preparedAccumulationFrameBindings,
    const usize preparedAccumulationInstanceCount,
    const usize preparedAccumulationMaterialTypedByteCount,
    const b8 accumulationMaterialFrameStatesGraphOwned,
    const b8 accumulationMaterialGeometryStatesGraphOwned,
    const b8 accumulationComputeEmulationOutputStatesGraphOwned,
    const b8 accumulationCsgComputeEmulationOutputStatesGraphOwned,
    const b8 generatedGeometryReused,
    Optional<Core::GpuTimingMeasure>* const accumulationComputeEmulationTiming
){
    AvboitFrameTargets& avboitTargets = targets.avboit;

    renderAvboitMaterialPass(
        commandList,
        avboitTargets.accumulationFramebuffer.get(),
        MaterialPipelinePass::AvboitAccumulate,
        avboitTargets,
        preparedAccumulationDrawItems,
        preparedAccumulationCsgFrameData,
        preparedAccumulationCsgResources,
        preparedAccumulationFrameBindings,
        preparedAccumulationInstanceCount,
        preparedAccumulationMaterialTypedByteCount,
        accumulationMaterialFrameStatesGraphOwned,
        accumulationMaterialGeometryStatesGraphOwned,
        accumulationComputeEmulationOutputStatesGraphOwned,
        accumulationCsgComputeEmulationOutputStatesGraphOwned,
        generatedGeometryReused,
        accumulationComputeEmulationTiming
    );
}

void RendererAvboitSystem::dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets, const Core::GpuTimingSampleAttribution timingAttribution, b8* const timingRecorded){
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_AvboitDepthWarp, m_graphics.getDevice(), commandList, timingAttribution);

    if(timingRecorded)
        *timingRecorded = timing.valid();

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    __hidden_avboit_pass::DispatchAvboitCompute(targets, NWB_AVBOIT_DEPTH_WARP_DISPATCH_GROUP_COUNT_X, m_graphics.isHDR10OutputActive(), commandList, *m_avboitState.m_depthWarpPipeline.get(), heap);
}

void RendererAvboitSystem::dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets, const Core::GpuTimingSampleAttribution timingAttribution, b8* const timingRecorded){
    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_AvboitIntegration, m_graphics.getDevice(), commandList, timingAttribution);

    if(timingRecorded)
        *timingRecorded = timing.valid();

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    __hidden_avboit_pass::DispatchAvboitCompute(targets, DivideUp(pixelCount, static_cast<u32>(NWB_AVBOIT_INTEGRATE_GROUP_SIZE_X)), m_graphics.isHDR10OutputActive(), commandList, *m_avboitState.m_integratePipeline.get(), heap);
}

void RendererAvboitSystem::renderAvboitMaterialPass(
    Core::CommandList& commandList,
    Core::Framebuffer* const framebuffer,
    const MaterialPipelinePass::Enum pass,
    AvboitFrameTargets& avboitTargets,
    const MaterialPassDrawItemPartitionsView& preparedDrawItems,
    const CsgFrameGpuDataView& preparedCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& preparedCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& preparedFrameBindings,
    const usize preparedInstanceCount,
    const usize preparedMaterialTypedByteCount,
    const b8 materialFrameStatesGraphOwned,
    const b8 materialGeometryStatesGraphOwned,
    const b8 computeEmulationOutputStatesGraphOwned,
    const b8 csgComputeEmulationOutputStatesGraphOwned,
    const b8 generatedGeometryReused,
    Optional<Core::GpuTimingMeasure>* const computeEmulationTiming
){
    NWB_ASSERT(avboitTargets.valid());

    m_materialSystem.renderPreparedMaterialPass(
        commandList,
        framebuffer,
        pass,
        &avboitTargets,
        preparedDrawItems,
        preparedCsgFrameData,
        preparedCsgResources,
        preparedFrameBindings,
        preparedInstanceCount,
        preparedMaterialTypedByteCount,
        materialFrameStatesGraphOwned,
        materialGeometryStatesGraphOwned,
        computeEmulationOutputStatesGraphOwned,
        csgComputeEmulationOutputStatesGraphOwned,
        generatedGeometryReused,
        computeEmulationTiming
    );
    commandList.endRenderPass();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

