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


[[nodiscard]] bool AvboitPreGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    // A CSG upload without its frozen stream would detach clears and consumers.
    if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems transparentCsgReceiverSurfaceDrawItems{ scratchArena };
    CsgFrameGpuData transparentCsgFrameData{ scratchArena };
    if(payload.hasTransparentRenderers && payload.transparentCsgStreamsUploaded){
        payload.transparentCsgSnapshot.materialize(transparentCsgReceiverSurfaceDrawItems, transparentCsgFrameData);
        payload.avboitSystem.renderAvboitTransparentCsgIntervals(
            commandList,
            payload.targets,
            transparentCsgReceiverSurfaceDrawItems,
            transparentCsgFrameData,
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


[[nodiscard]] bool AvboitOccupancyComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

    return RecordAvboitComputeEmulationFromPayload(scratchArena, payload, commandList, trait, payload.occupancyTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitOccupancySharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    const AvboitSharedComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitSharedComputeEmulationFromPayload(payload, commandList, payload.phase == AvboitOccupancySharedComputeEmulationGraphTask::Phase::Raster, trait, payload.occupancyTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitOccupancyGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

    return RecordAvboitRasterPassFromPayload(
        scratchArena,
        payload,
        commandList,
        &Payload::occupancyPhasePrepared,
        &Payload::occupancySnapshot,
        &Payload::occupancyComputeEmulationOutputStatesGraphOwned,
        &Payload::occupancyCsgComputeEmulationOutputStatesGraphOwned,
        [&](Core::CommandList& dispatchCommandList, const MaterialPassDrawItemPartitions& dispatchDrawItems, const CsgFrameGpuData& dispatchCsgFrameData, const usize dispatchInstanceCount, const usize dispatchMaterialTypedByteCount){
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


[[nodiscard]] bool AvboitDepthWarpGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    return RecordAvboitComputeEffect(
        payload,
        commandList,
        context,
        [&](Core::CommandList& dispatchCommandList, AvboitFrameTargets& targets, const Core::GpuTimingSampleAttribution attribution, bool* const timingRecorded){
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

