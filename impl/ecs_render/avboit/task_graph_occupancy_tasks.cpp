// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_occupancy_tasks.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_timing_feedback.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/task_graph/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitPreGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
        return false;
    // A transparent CSG upload has no safe native fallback: accepting its packet without the paired frozen
    // stream would leave the declared clears and later CSG consumers detached from their interval producer.
    if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems transparentCsgReceiverSurfaceDrawItems{ scratchArena };
    CsgFrameGpuData transparentCsgFrameData{ scratchArena };
    const MaterialPassDrawItems* preparedTransparentCsgReceiverSurfaceDrawItems = nullptr;
    const CsgFrameGpuData* preparedTransparentCsgFrameData = nullptr;
    const ECSRenderDetail::CsgGraphResourceSnapshot* preparedTransparentCsgResources = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* preparedTransparentCsgFrameBindings = nullptr;
    usize preparedTransparentCsgInstanceCount = 0u;
    usize preparedTransparentCsgMaterialTypedByteCount = 0u;
    if(payload.transparentCsgStreamsUploaded && payload.transparentCsgSnapshot.captured){
        payload.transparentCsgSnapshot.materialize(
            transparentCsgReceiverSurfaceDrawItems,
            transparentCsgFrameData
        );
        preparedTransparentCsgReceiverSurfaceDrawItems = &transparentCsgReceiverSurfaceDrawItems;
        preparedTransparentCsgFrameData = &transparentCsgFrameData;
        preparedTransparentCsgResources = &payload.csgResources;
        preparedTransparentCsgFrameBindings = &payload.frameBindings;
        preparedTransparentCsgInstanceCount = payload.transparentCsgSnapshot.instanceCount;
        preparedTransparentCsgMaterialTypedByteCount = payload.transparentCsgSnapshot.materialTypedByteCount;
    }
    if(payload.hasTransparentRenderers){
        payload.avboitSystem->renderAvboitTransparentCsgIntervals(
            commandList,
            *payload.targets,
            preparedTransparentCsgReceiverSurfaceDrawItems,
            preparedTransparentCsgFrameData,
            preparedTransparentCsgResources,
            preparedTransparentCsgFrameBindings,
            preparedTransparentCsgInstanceCount,
            preparedTransparentCsgMaterialTypedByteCount,
            payload.transparentCsgIntervalTargetsGraphOwned,
            payload.transparentCsgReceiverSurfaceImageStatesGraphOwned,
            payload.transparentCsgIntervalPeelTargetStatesGraphOwned,
            payload.transparentCsgReceiverSpanOutputImageStatesGraphOwned,
            payload.transparentCsgRemovedIntervalOutputImageStatesGraphOwned,
            payload.transparentCsgClipBufferStatesGraphOwned,
            payload.transparentCsgMaterialFrameStatesGraphOwned,
            payload.transparentCsgMaterialGeometryStatesGraphOwned,
            payload.deferTransparentCsgIntervalCombine,
            payload.transparentCsgIntervalsTiming
        );
    }
    return true;
}

void AvboitPreGraphTask::discarded(Payload& payload){
    if(payload.transparentCsgIntervalsTiming && payload.transparentCsgIntervalsTiming->has_value()){
        payload.transparentCsgIntervalsTiming->value().discardTiming();
        payload.transparentCsgIntervalsTiming->reset();
    }
}

void AvboitOccupancyComputeEmulationGraphTask::discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}

[[nodiscard]] bool AvboitOccupancyComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.materialSystem
        || !payload.targets
        || !payload.timingTicket
        || !payload.occupancyTiming
        || (!payload.plan.captured && !payload.csgPlan.captured)
        || payload.plan.captured == payload.csgPlan.captured
    )
        return false;

    Core::Graphics& graphics = *payload.graphics;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const bool csgComputeEmulation = payload.csgPlan.captured;
    if(
        !(csgComputeEmulation
            ? payload.csgPlan.matches()
            : payload.plan.matches())
        || !payload.materialDrawBuffersUploaded
        || !payload.frameBindings.frameReady(
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
    // The following raster task is graph-owned and cannot safely fall back to its local bridge. Reject a late
    // material/pipeline loss so the packet is discarded and the next frame re-preflights instead of accepting
    // Occupancy with stale generated vertices.
    if(
        !materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings)
        || (csgComputeEmulation && (
            !payload.csgFrameBuffersUploaded
            || !payload.csgIntervalSampleImageStatesGraphOwned
            || !payload.csgClipBufferStatesGraphOwned
            || !csgFrameData.hasWork()
            || !payload.csgResources.frameReady(csgFrameData)
        ))
    )
        return false;
    if(payload.occupancyTiming->has_value())
        return false;

    commandList.endRenderPass();
    payload.occupancyTiming->emplace(
        graphics.gpuTiming(),
        RendererGpuTimingScope::s_AvboitOccupancy,
        graphics.getDevice(),
        commandList
    );
    // Occupancy's raster half records after this producer in the same selected Graphics packet. Close the
    // marker before this command list completes; its consumer owns finishTiming/discard.
    payload.occupancyTiming->value().finishMarker();
    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        nullptr,
        MaterialPipelinePass::AvboitOccupancy,
        &payload.targets->avboit,
        viewportState,
        false,
        csgComputeEmulation && payload.csgIntervalSampleImageStatesGraphOwned,
        csgComputeEmulation && payload.csgClipBufferStatesGraphOwned,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
        csgComputeEmulation ? &payload.csgResources : nullptr,
        payload.frameBindings
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}

void AvboitOccupancyComputeEmulationGraphTask::discarded(Payload& payload){ discardTiming(payload.occupancyTiming); }

void AvboitOccupancySharedComputeEmulationGraphTask::discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}

[[nodiscard]] bool AvboitOccupancySharedComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.materialSystem
        || !payload.targets
        || !payload.targets->avboit.lowFramebuffer
        || !payload.timingTicket
        || !payload.occupancyTiming
        || !payload.plan.captured
        || payload.drawIndex >= payload.plan.drawCount
    )
        return false;

    Core::Graphics& graphics = *payload.graphics;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(
        !payload.plan.matches(payload.drawIndex)
        || !payload.materialDrawBuffersUploaded
        || !payload.frameBindings.frameReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    )
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    payload.plan.materialize(payload.drawIndex, drawItems);
    if(!materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings))
        return false;

    if(payload.phase == Phase::Generate){
        // A preceding raster phase leaves dynamic rendering active. End it before the timing marker and
        // compute-state bind, matching the retained local D/R interleaving order.
        commandList.endRenderPass();
        if(payload.beginTiming){
            if(payload.occupancyTiming->has_value())
                return false;
            payload.occupancyTiming->emplace(
                graphics.gpuTiming(),
                RendererGpuTimingScope::s_AvboitOccupancy,
                graphics.getDevice(),
                commandList
            );
            // The range spans serial callbacks, but this opening command list still needs its marker closed
            // before recording advances to the raster consumer.
            payload.occupancyTiming->value().finishMarker();
        }
        else if(!payload.occupancyTiming->has_value())
            return false;
    }
    else if(!payload.occupancyTiming->has_value())
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        payload.phase == Phase::Raster ? payload.targets->avboit.lowFramebuffer.get() : nullptr,
        MaterialPipelinePass::AvboitOccupancy,
        &payload.targets->avboit,
        viewportState,
        false,
        false,
        false,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
        nullptr,
        payload.frameBindings
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
            payload.occupancyTiming->value().finishTiming(commandList);
            payload.occupancyTiming->reset();
        }
        // The next generator must never bind a compute pipeline while dynamic rendering remains active.
        commandList.endRenderPass();
    }
    return true;
}

void AvboitOccupancySharedComputeEmulationGraphTask::discarded(Payload& payload){ discardTiming(payload.occupancyTiming); }

[[nodiscard]] bool AvboitOccupancyGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.avboitSystem
        || !payload.targets
        || !payload.timingTicket
        || ((payload.occupancyComputeEmulationOutputStatesGraphOwned
                || payload.occupancyCsgComputeEmulationOutputStatesGraphOwned)
            && !payload.occupancyComputeEmulationTiming)
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItemPartitions occupancyDrawItems{ scratchArena };
    CsgFrameGpuData occupancyCsgFrameData{ scratchArena };
    const MaterialPassDrawItemPartitions* preparedOccupancyDrawItems = nullptr;
    const CsgFrameGpuData* preparedOccupancyCsgFrameData = nullptr;
    usize preparedOccupancyInstanceCount = 0u;
    usize preparedOccupancyMaterialTypedByteCount = 0u;
    if(payload.hasTransparentRenderers && (!payload.occupancyPhasePrepared || !payload.occupancySnapshot.captured))
        return false;
    if(payload.occupancyPhasePrepared && payload.occupancySnapshot.captured){
        payload.occupancySnapshot.materialize(occupancyDrawItems, occupancyCsgFrameData);
        preparedOccupancyDrawItems = &occupancyDrawItems;
        preparedOccupancyCsgFrameData = &occupancyCsgFrameData;
        preparedOccupancyInstanceCount = payload.occupancySnapshot.instanceCount;
        preparedOccupancyMaterialTypedByteCount = payload.occupancySnapshot.materialTypedByteCount;
    }
    if(payload.hasTransparentRenderers){
        payload.avboitSystem->renderAvboitOccupancyPass(
            commandList,
            *payload.targets,
            preparedOccupancyDrawItems,
            preparedOccupancyCsgFrameData,
            &payload.csgResources,
            &payload.frameBindings,
            preparedOccupancyInstanceCount,
            preparedOccupancyMaterialTypedByteCount,
            // The task's declared depth/coverage uses have already lowered and committed their graph barrier.
            true,
            payload.occupancyCsgIntervalSampleImageStatesGraphOwned,
            payload.occupancyCsgClipBufferStatesGraphOwned,
            payload.occupancyMaterialFrameStatesGraphOwned,
            payload.occupancyMaterialGeometryStatesGraphOwned,
            payload.occupancyComputeEmulationOutputStatesGraphOwned,
            payload.occupancyComputeEmulationTiming,
            payload.occupancyCsgComputeEmulationOutputStatesGraphOwned
        );
    }
    // The declared sampled G-buffer uses remain authoritative here. Occupancy's low-resolution framebuffer
    // does not attach any deferred target, so the graph-established states remain valid for either continuation.
    return true;
}

void AvboitOccupancyGraphTask::discarded(Payload& payload){
    if(
        !payload.occupancyComputeEmulationTiming
        || !payload.occupancyComputeEmulationTiming->has_value()
    )
        return;
    payload.occupancyComputeEmulationTiming->value().discardTiming();
    payload.occupancyComputeEmulationTiming->reset();
}

[[nodiscard]] bool AvboitDepthWarpGraphTask::record(
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
    payload.avboitSystem->dispatchAvboitDepthWarp(
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

void AvboitDepthWarpGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    if(payload.timingFeedback)
        payload.timingFeedback->acceptSubmission(payload.timingAttribution, token);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}

void AvboitDepthWarpGraphTask::discarded(Payload& payload){
    if(payload.timingFeedback)
        payload.timingFeedback->discardRecording(payload.timingAttribution);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

