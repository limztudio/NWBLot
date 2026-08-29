// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_extinction_integration_tasks.h"

#include <impl/ecs_render/kernel/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AvboitExtinctionComputeEmulationGraphTask::discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}

[[nodiscard]] bool AvboitExtinctionComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.meshSystem
        || !payload.materialSystem
        || !payload.csgSystem
        || !payload.targets
        || !payload.timingTicket
        || !payload.extinctionTiming
        || (!payload.plan.captured && !payload.csgPlan.captured)
        || payload.plan.captured == payload.csgPlan.captured
    )
        return false;

    Core::Graphics& graphics = *payload.graphics;
    RendererMeshSystem& meshSystem = *payload.meshSystem;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const bool csgComputeEmulation = payload.csgPlan.captured;
    if(
        !(csgComputeEmulation
            ? payload.csgPlan.matches(meshSystem)
            : payload.plan.matches(meshSystem))
        || !payload.materialDrawBuffersUploaded
        || !materialSystem.materialPassDrawBuffersReady(
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
    // of accepting an all-or-nothing Extinction phase with stale generated vertices.
    if(
        !materialSystem.materialPassDrawResourcesReady(drawItems)
        || (csgComputeEmulation && (
            !payload.csgFrameBuffersUploaded
            || !payload.csgIntervalSampleImageStatesGraphOwned
            || !payload.csgClipBufferStatesGraphOwned
            || !csgFrameData.hasWork()
            || !csgSystem.csgFrameBuffersReady(csgFrameData)
        ))
    )
        return false;
    if(payload.extinctionTiming->has_value())
        return false;

    commandList.endRenderPass();
    payload.extinctionTiming->emplace(
        graphics.gpuTiming(),
        RendererGpuTimingScope::s_AvboitExtinction,
        graphics.getDevice(),
        commandList
    );
    // Extinction's raster half records after this producer in the same selected Graphics packet. Close the
    // marker before this command list completes; its consumer owns finishTiming/discard.
    payload.extinctionTiming->value().finishMarker();
    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        nullptr,
        MaterialPipelinePass::AvboitExtinction,
        &payload.targets->avboit,
        viewportState,
        false,
        csgComputeEmulation && payload.csgIntervalSampleImageStatesGraphOwned,
        csgComputeEmulation && payload.csgClipBufferStatesGraphOwned,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}

void AvboitExtinctionComputeEmulationGraphTask::discarded(Payload& payload){ discardTiming(payload.extinctionTiming); }

void AvboitExtinctionSharedComputeEmulationGraphTask::discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}

[[nodiscard]] bool AvboitExtinctionSharedComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.meshSystem
        || !payload.materialSystem
        || !payload.targets
        || !payload.targets->avboit.lowFramebuffer
        || !payload.timingTicket
        || !payload.extinctionTiming
        || !payload.plan.captured
        || payload.drawIndex >= payload.plan.drawCount
    )
        return false;

    Core::Graphics& graphics = *payload.graphics;
    RendererMeshSystem& meshSystem = *payload.meshSystem;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(
        !payload.plan.matches(meshSystem, payload.drawIndex)
        || !payload.materialDrawBuffersUploaded
        || !materialSystem.materialPassDrawBuffersReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    )
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    payload.plan.materialize(payload.drawIndex, drawItems);
    if(!materialSystem.materialPassDrawResourcesReady(drawItems))
        return false;

    if(payload.phase == Phase::Generate){
        // Raster A leaves dynamic rendering active.  End it before the next compute generator, retaining the
        // original per-item material order and allowing the terminal typed Integration successor to record.
        commandList.endRenderPass();
        if(payload.beginTiming){
            if(payload.extinctionTiming->has_value())
                return false;
            payload.extinctionTiming->emplace(
                graphics.gpuTiming(),
                RendererGpuTimingScope::s_AvboitExtinction,
                graphics.getDevice(),
                commandList
            );
            payload.extinctionTiming->value().finishMarker();
        }
        else if(!payload.extinctionTiming->has_value())
            return false;
    }
    else if(!payload.extinctionTiming->has_value())
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        payload.phase == Phase::Raster ? payload.targets->avboit.lowFramebuffer.get() : nullptr,
        MaterialPipelinePass::AvboitExtinction,
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
        materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    }
    else{
        materialSystem.renderComputeMaterialPassDrawItemsRasterOnly(
            drawContext,
            drawItems.computeDrawItems
        );
        if(payload.finishTiming){
            payload.extinctionTiming->value().finishTiming(commandList);
            payload.extinctionTiming->reset();
        }
        // Integration binds a compute pipeline in the next graph callback.
        commandList.endRenderPass();
    }
    return true;
}

void AvboitExtinctionSharedComputeEmulationGraphTask::discarded(Payload& payload){ discardTiming(payload.extinctionTiming); }

[[nodiscard]] bool AvboitExtinctionGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.avboitSystem
        || !payload.targets
        || !payload.timingTicket
        || ((payload.extinctionComputeEmulationOutputStatesGraphOwned
                || payload.extinctionCsgComputeEmulationOutputStatesGraphOwned)
            && !payload.extinctionComputeEmulationTiming)
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItemPartitions extinctionDrawItems{ scratchArena };
    CsgFrameGpuData extinctionCsgFrameData{ scratchArena };
    const MaterialPassDrawItemPartitions* preparedExtinctionDrawItems = nullptr;
    const CsgFrameGpuData* preparedExtinctionCsgFrameData = nullptr;
    usize preparedExtinctionInstanceCount = 0u;
    usize preparedExtinctionMaterialTypedByteCount = 0u;
    if(payload.hasTransparentRenderers && (!payload.extinctionPhasePrepared || !payload.extinctionSnapshot.captured))
        return false;
    if(payload.extinctionPhasePrepared && payload.extinctionSnapshot.captured){
        payload.extinctionSnapshot.materialize(extinctionDrawItems, extinctionCsgFrameData);
        preparedExtinctionDrawItems = &extinctionDrawItems;
        preparedExtinctionCsgFrameData = &extinctionCsgFrameData;
        preparedExtinctionInstanceCount = payload.extinctionSnapshot.instanceCount;
        preparedExtinctionMaterialTypedByteCount = payload.extinctionSnapshot.materialTypedByteCount;
    }
    if(payload.hasTransparentRenderers){
        payload.avboitSystem->renderAvboitExtinctionPass(
            commandList,
            *payload.targets,
            preparedExtinctionDrawItems,
            preparedExtinctionCsgFrameData,
            preparedExtinctionInstanceCount,
            preparedExtinctionMaterialTypedByteCount,
            payload.extinctionCsgIntervalSampleImageStatesGraphOwned,
            payload.extinctionCsgClipBufferStatesGraphOwned,
            payload.extinctionMaterialFrameStatesGraphOwned,
            payload.extinctionMaterialGeometryStatesGraphOwned,
            payload.extinctionComputeEmulationOutputStatesGraphOwned,
            payload.extinctionComputeEmulationTiming,
            payload.extinctionCsgComputeEmulationOutputStatesGraphOwned
        );
    }
    return true;
}

void AvboitExtinctionGraphTask::discarded(Payload& payload){
    if(
        !payload.extinctionComputeEmulationTiming
        || !payload.extinctionComputeEmulationTiming->has_value()
    )
        return;
    payload.extinctionComputeEmulationTiming->value().discardTiming();
    payload.extinctionComputeEmulationTiming->reset();
}

[[nodiscard]] bool AvboitIntegrationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    bool timingRecorded = false;
    if(payload.timingFeedback && payload.timingScope){
        const Core::GpuPhysicalQueueInfo* const queueInfo = context.graph.queueInfo(context.queue);
        const Core::GpuCompiledTask* const compiledTask = context.graph.findTask(context.task);
        if(queueInfo && compiledTask){
            const Core::GpuTaskGraphTaskView task = context.taskGraph.taskAt(context.task.index);
            payload.timingAttribution = payload.timingFeedback->beginSample(
                payload.timingScope->identity,
                Core::GpuTaskTimingKey{
                    .task = task.identity,
                    .variant = task.timing.variant,
                    .resolutionClass = task.timing.resolutionClass,
                    .queue = queueInfo->queueClass,
                },
                context.queue,
                compiledTask->recordsNonCommittingTimingSample
            );
        }
    }
    payload.avboitSystem->dispatchAvboitIntegration(
        commandList,
        *payload.targets,
        payload.timingAttribution,
        &timingRecorded
    );
    if(!timingRecorded && payload.timingFeedback){
        payload.timingFeedback->discardRecording(payload.timingAttribution);
        payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
    }
    return true;
}

void AvboitIntegrationGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    if(payload.timingFeedback)
        payload.timingFeedback->acceptSubmission(payload.timingAttribution, token);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}

void AvboitIntegrationGraphTask::discarded(Payload& payload){
    if(payload.timingFeedback)
        payload.timingFeedback->discardRecording(payload.timingAttribution);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

