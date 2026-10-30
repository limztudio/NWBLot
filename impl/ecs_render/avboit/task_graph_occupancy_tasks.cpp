// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_occupancy_tasks.h"

#include <impl/ecs_render/avboit/compute_emulation_record.h>
#include <impl/ecs_render/avboit/avboit_system.h>
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


[[nodiscard]] b8 AvboitPreGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    // A CSG upload without its frozen stream would detach clears and consumers.
    if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.hasTransparentRenderers && payload.transparentCsgStreamsUploaded){
        payload.avboitSystem.renderAvboitTransparentCsgIntervals(
            commandList,
            payload.targets,
            payload.transparentCsgSnapshot.receiverSurfaceDrawItemsView(),
            payload.transparentCsgSnapshot.csgFrameDataView(),
            payload.csgResources,
            payload.frameBindings,
            payload.transparentCsgSnapshot.instanceCount,
            payload.transparentCsgSnapshot.materialTypedByteCount,
            payload.transparentCsgMaterialGeometryStatesGraphOwned,
            payload.transparentCsgIntervalsTiming
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitOccupancyComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitComputeEmulationFromPayload(payload, commandList, trait, payload.occupancyTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitOccupancySharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitSharedComputeEmulationFromPayload(payload, commandList, trait, payload.occupancyTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitOccupancyGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    return RecordAvboitRasterPassFromPayload(
        payload,
        commandList,
        &Payload::occupancyPhasePrepared,
        &Payload::occupancySnapshot,
        &Payload::occupancyComputeEmulationOutputStatesGraphOwned,
        &Payload::occupancyCsgComputeEmulationOutputStatesGraphOwned,
        [&](Core::CommandList& dispatchCommandList, const MaterialPassDrawItemPartitionsView& dispatchDrawItems, const CsgFrameGpuDataView& dispatchCsgFrameData, const usize dispatchInstanceCount, const usize dispatchMaterialTypedByteCount){
            payload.avboitSystem.renderAvboitOccupancyPass(
                dispatchCommandList,
                payload.targets,
                dispatchDrawItems,
                dispatchCsgFrameData,
                payload.csgResources,
                payload.frameBindings,
                dispatchInstanceCount,
                dispatchMaterialTypedByteCount,
                payload.occupancyMaterialFrameStatesGraphOwned,
                payload.occupancyMaterialGeometryStatesGraphOwned,
                payload.occupancyComputeEmulationOutputStatesGraphOwned,
                payload.occupancyCsgComputeEmulationOutputStatesGraphOwned,
                payload.generatedGeometryReused,
                payload.occupancyComputeEmulationTiming
            );
        },
        payload.occupancyComputeEmulationTiming
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitDepthWarpGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    return RecordAvboitComputeEffect(
        payload,
        commandList,
        context,
        [&](Core::CommandList& dispatchCommandList, AvboitFrameTargets& targets, const Core::GpuTimingSampleAttribution attribution, b8* const timingRecorded){
            payload.avboitSystem.dispatchAvboitDepthWarp(dispatchCommandList, targets, attribution, timingRecorded);
        }
    );
}

void AvboitDepthWarpGraphTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
    AcceptAvboitComputeEffect(payload, token);
}

void AvboitDepthWarpGraphTask::Discarded(Payload& payload)noexcept{
    DiscardAvboitComputeEffect(payload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

