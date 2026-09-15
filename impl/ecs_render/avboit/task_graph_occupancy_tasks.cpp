// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_occupancy_tasks.h"

#include <impl/ecs_render/avboit/compute_emulation_record.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_timing_feedback.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/compiled_graph.h>


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
    // A CSG upload without its frozen stream would detach clears and consumers.
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

[[nodiscard]] bool AvboitOccupancyComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    static_cast<void>(context);
    const AvboitComputeEmulationRecordTrait trait{
        &RendererGpuTimingScope::s_AvboitOccupancy,
        MaterialPipelinePass::AvboitOccupancy,
        &AvboitFrameTargets::lowFramebuffer,
    };
    return RecordAvboitComputeEmulationFromPayload(payload, commandList, context, &Payload::occupancyTiming, trait);
}

[[nodiscard]] bool AvboitOccupancySharedComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    static_cast<void>(context);
    const AvboitSharedComputeEmulationRecordTrait trait{
        &RendererGpuTimingScope::s_AvboitOccupancy,
        MaterialPipelinePass::AvboitOccupancy,
        &AvboitFrameTargets::lowFramebuffer,
    };
    return RecordAvboitSharedComputeEmulationFromPayload(payload, commandList, context, &Payload::occupancyTiming, payload.phase == AvboitOccupancySharedComputeEmulationGraphTask::Phase::Raster, trait);
}

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
            // Declared uses already lowered their barrier.
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
    // Graph-established states remain valid for either continuation.
    return true;
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
        const Core::GpuPhysicalQueueInfo* const queueInfo = context.compiledPlan.queueInfo(context.queue);
        const Core::GpuCompiledTaskView compiledTask = context.compiledPlan.findTask(context.task);
        if(queueInfo && compiledTask.valid()){
            const Core::GpuTaskGraphTaskView task = context.declarations.taskAt(context.task.index);
            payload.timingAttribution = payload.timingFeedback->beginSample(
                payload.timingScope->identity,
                Core::GpuTaskTimingKey{
                    .task = task.identity,
                    .variant = task.timing.variant,
                    .resolutionClass = task.timing.resolutionClass,
                    .queue = queueInfo->queueClass,
                },
                context.queue,
                compiledTask.plan->recordsNonCommittingTimingSample
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

void AvboitDepthWarpGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
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

