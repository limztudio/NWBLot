// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_accumulation_tasks.h"

#include <impl/ecs_render/kernel/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AvboitAccumulationComputeEmulationGraphTask::discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}

[[nodiscard]] bool AvboitAccumulationComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.renderer
        || !payload.targets
        || !payload.timingTicket
        || !payload.accumulationTiming
        || (!payload.plan.captured && !payload.csgPlan.captured)
        || payload.plan.captured == payload.csgPlan.captured
    )
        return false;

    RendererSystem& renderer = *payload.renderer;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const bool csgComputeEmulation = payload.csgPlan.captured;
    if(
        !(csgComputeEmulation
            ? payload.csgPlan.matches(renderer.meshSystem())
            : payload.plan.matches(renderer.meshSystem()))
        || !payload.materialDrawBuffersUploaded
        || !renderer.materialSystem().materialPassDrawBuffersReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    )
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    if(csgComputeEmulation)
        payload.csgPlan.materialize(drawItems, csgFrameData);
    else
        payload.plan.materialize(drawItems);
    // The following raster task is deliberately graph-owned and cannot safely fall back to its local bridge.
    // Reject a late material/pipeline loss so the packet is discarded and the next frame re-preflights instead
    // of accepting an all-or-nothing Accumulation phase with stale generated vertices.
    if(
        !renderer.materialSystem().materialPassDrawResourcesReady(drawItems)
        || (csgComputeEmulation && (
            !payload.csgFrameBuffersUploaded
            || !payload.csgIntervalSampleImageStatesGraphOwned
            || !payload.csgClipBufferStatesGraphOwned
            || !csgFrameData.hasWork()
            || !renderer.csgSystem().csgFrameBuffersReady(csgFrameData)
        ))
    )
        return false;
    if(payload.accumulationTiming->has_value())
        return false;

    commandList.endRenderPass();
    payload.accumulationTiming->emplace(
        renderer.graphics().gpuTiming(),
        RendererGpuTimingScope::s_AvboitAccumulate,
        renderer.graphics().getDevice(),
        commandList
    );
    // Accumulation's raster half records after this producer in the selected terminal Graphics packet. Close
    // the opening command-list marker now; its consumer owns finishTiming/discard.
    payload.accumulationTiming->value().finishMarker();
    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        payload.targets->avboit.accumulationFramebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        nullptr,
        MaterialPipelinePass::AvboitAccumulate,
        &payload.targets->avboit,
        viewportState,
        false,
        csgComputeEmulation && payload.csgIntervalSampleImageStatesGraphOwned,
        csgComputeEmulation && payload.csgClipBufferStatesGraphOwned,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
    };
    renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}

void AvboitAccumulationComputeEmulationGraphTask::discarded(Payload& payload){ discardTiming(payload.accumulationTiming); }

void AvboitAccumulationSharedComputeEmulationGraphTask::discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}

[[nodiscard]] bool AvboitAccumulationSharedComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.renderer
        || !payload.targets
        || !payload.targets->avboit.accumulationFramebuffer
        || !payload.timingTicket
        || !payload.accumulationTiming
        || !payload.plan.captured
        || payload.drawIndex >= payload.plan.drawCount
    )
        return false;

    RendererSystem& renderer = *payload.renderer;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(
        !payload.plan.matches(renderer.meshSystem(), payload.drawIndex)
        || !payload.materialDrawBuffersUploaded
        || !renderer.materialSystem().materialPassDrawBuffersReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    )
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    payload.plan.materialize(payload.drawIndex, drawItems);
    if(!renderer.materialSystem().materialPassDrawResourcesReady(drawItems))
        return false;

    if(payload.phase == Phase::Generate){
        // A preceding raster phase leaves dynamic rendering active. End it before the timing marker and
        // compute-state bind, matching the retained local D/R interleaving order.
        commandList.endRenderPass();
        if(payload.beginTiming){
            if(payload.accumulationTiming->has_value())
                return false;
            payload.accumulationTiming->emplace(
                renderer.graphics().gpuTiming(),
                RendererGpuTimingScope::s_AvboitAccumulate,
                renderer.graphics().getDevice(),
                commandList
            );
            // The range spans serial callbacks, but this opening command list still needs its marker closed
            // before recording advances to the raster consumer.
            payload.accumulationTiming->value().finishMarker();
        }
        else if(!payload.accumulationTiming->has_value())
            return false;
    }
    else if(!payload.accumulationTiming->has_value())
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        payload.targets->avboit.accumulationFramebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        payload.phase == Phase::Raster ? payload.targets->avboit.accumulationFramebuffer.get() : nullptr,
        MaterialPipelinePass::AvboitAccumulate,
        &payload.targets->avboit,
        viewportState,
        false,
        false,
        false,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
    };
    if(payload.phase == Phase::Generate){
        renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    }
    else{
        renderer.materialSystem().renderComputeMaterialPassDrawItemsRasterOnly(
            drawContext,
            drawItems.computeDrawItems
        );
        if(payload.finishTiming){
            payload.accumulationTiming->value().finishTiming(commandList);
            payload.accumulationTiming->reset();
        }
        // The next generator must never bind a compute pipeline while dynamic rendering remains active.
        commandList.endRenderPass();
    }
    return true;
}

void AvboitAccumulationSharedComputeEmulationGraphTask::discarded(Payload& payload){ discardTiming(payload.accumulationTiming); }

[[nodiscard]] bool AvboitAccumulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.avboitSystem
        || !payload.targets
        || !payload.timingTicket
        || ((payload.accumulationComputeEmulationOutputStatesGraphOwned
                || payload.accumulationCsgComputeEmulationOutputStatesGraphOwned)
            && !payload.accumulationComputeEmulationTiming)
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItemPartitions accumulationDrawItems{ scratchArena };
    CsgFrameGpuData accumulationCsgFrameData{ scratchArena };
    const MaterialPassDrawItemPartitions* preparedAccumulationDrawItems = nullptr;
    const CsgFrameGpuData* preparedAccumulationCsgFrameData = nullptr;
    usize preparedAccumulationInstanceCount = 0u;
    usize preparedAccumulationMaterialTypedByteCount = 0u;
    if(payload.hasTransparentRenderers && (!payload.accumulationPhasePrepared || !payload.accumulationSnapshot.captured))
        return false;
    if(payload.accumulationPhasePrepared && payload.accumulationSnapshot.captured){
        payload.accumulationSnapshot.materialize(accumulationDrawItems, accumulationCsgFrameData);
        preparedAccumulationDrawItems = &accumulationDrawItems;
        preparedAccumulationCsgFrameData = &accumulationCsgFrameData;
        preparedAccumulationInstanceCount = payload.accumulationSnapshot.instanceCount;
        preparedAccumulationMaterialTypedByteCount = payload.accumulationSnapshot.materialTypedByteCount;
    }
    if(payload.hasTransparentRenderers){
        payload.avboitSystem->renderAvboitAccumulatePass(
            commandList,
            *payload.targets,
            preparedAccumulationDrawItems,
            preparedAccumulationCsgFrameData,
            preparedAccumulationInstanceCount,
            preparedAccumulationMaterialTypedByteCount,
            // The following mergeable Graphics finalizer owns every accumulation-framebuffer handoff.
            true,
            payload.accumulationCsgIntervalSampleImageStatesGraphOwned,
            payload.accumulationCsgClipBufferStatesGraphOwned,
            payload.accumulationMaterialFrameStatesGraphOwned,
            payload.accumulationMaterialGeometryStatesGraphOwned,
            payload.accumulationComputeEmulationOutputStatesGraphOwned,
            payload.accumulationComputeEmulationTiming,
            payload.accumulationCsgComputeEmulationOutputStatesGraphOwned
        );
    }
    return true;
}

void AvboitAccumulationGraphTask::discarded(Payload& payload){
    if(
        !payload.accumulationComputeEmulationTiming
        || !payload.accumulationComputeEmulationTiming->has_value()
    )
        return;
    payload.accumulationComputeEmulationTiming->value().discardTiming();
    payload.accumulationComputeEmulationTiming->reset();
}

[[nodiscard]] bool AvboitAccumulationFinalizeGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(payload);
    static_cast<void>(commandList);
    static_cast<void>(context);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

