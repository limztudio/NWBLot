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


[[nodiscard]] bool AvboitPreGraphTask::Record(
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
    if(payload.hasTransparentRenderers && payload.transparentCsgStreamsUploaded){
        if(!payload.transparentCsgIntervalsTiming)
            return false;
        payload.transparentCsgSnapshot.materialize(transparentCsgReceiverSurfaceDrawItems, transparentCsgFrameData);
        payload.avboitSystem->renderAvboitTransparentCsgIntervals(
            commandList,
            *payload.targets,
            transparentCsgReceiverSurfaceDrawItems,
            transparentCsgFrameData,
            payload.csgResources,
            payload.frameBindings,
            payload.transparentCsgSnapshot.instanceCount,
            payload.transparentCsgSnapshot.materialTypedByteCount,
            payload.transparentCsgMaterialGeometryStatesGraphOwned,
            *payload.transparentCsgIntervalsTiming
        );
    }
    return true;
}

[[nodiscard]] bool AvboitOccupancyComputeEmulationGraphTask::Record(
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

[[nodiscard]] bool AvboitOccupancySharedComputeEmulationGraphTask::Record(
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

[[nodiscard]] bool AvboitOccupancyGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    return RecordAvboitRasterPassFromPayload(
        payload,
        commandList,
        context,
        &Payload::occupancyPhasePrepared,
        &Payload::occupancySnapshot,
        &Payload::occupancyComputeEmulationOutputStatesGraphOwned,
        &Payload::occupancyCsgComputeEmulationOutputStatesGraphOwned,
        &Payload::occupancyComputeEmulationTiming,
        [&](
            Core::CommandList& dispatchCommandList,
            const MaterialPassDrawItemPartitions& dispatchDrawItems,
            const CsgFrameGpuData& dispatchCsgFrameData,
            const usize dispatchInstanceCount,
            const usize dispatchMaterialTypedByteCount
        ){
            payload.avboitSystem->renderAvboitOccupancyPass(
                dispatchCommandList,
                *payload.targets,
                dispatchDrawItems,
                dispatchCsgFrameData,
                payload.csgResources,
                payload.frameBindings,
                dispatchInstanceCount,
                dispatchMaterialTypedByteCount,
                payload.occupancyMaterialFrameStatesGraphOwned,
                payload.occupancyMaterialGeometryStatesGraphOwned,
                payload.occupancyComputeEmulationOutputStatesGraphOwned,
                payload.occupancyComputeEmulationTiming,
                payload.occupancyCsgComputeEmulationOutputStatesGraphOwned,
                payload.generatedGeometryReused
            );
        }
    );
}

[[nodiscard]] bool AvboitDepthWarpGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    bool timingRecorded = false;
    payload.timingAttribution = ECSRenderDetail::BeginTaskTimingSample(payload.timingFeedback, payload.timingScope, context);
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

void AvboitDepthWarpGraphTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
    if(payload.timingFeedback)
        payload.timingFeedback->acceptSubmission(payload.timingAttribution, token);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}

void AvboitDepthWarpGraphTask::Discarded(Payload& payload){
    if(payload.timingFeedback)
        payload.timingFeedback->discardRecording(payload.timingAttribution);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

