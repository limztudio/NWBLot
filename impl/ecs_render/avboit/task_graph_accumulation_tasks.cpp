// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_accumulation_tasks.h"

#include <impl/ecs_render/avboit/compute_emulation_record.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitAccumulationComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    static_cast<void>(context);
    const AvboitComputeEmulationRecordTrait trait{
        &RendererGpuTimingScope::s_AvboitAccumulate,
        MaterialPipelinePass::AvboitAccumulate,
        &AvboitFrameTargets::accumulationFramebuffer,
    };
    return RecordAvboitComputeEmulationFromPayload(payload, commandList, context, &Payload::accumulationTiming, trait);
}

[[nodiscard]] bool AvboitAccumulationSharedComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    static_cast<void>(context);
    const AvboitSharedComputeEmulationRecordTrait trait{
        &RendererGpuTimingScope::s_AvboitAccumulate,
        MaterialPipelinePass::AvboitAccumulate,
        &AvboitFrameTargets::accumulationFramebuffer,
    };
    return RecordAvboitSharedComputeEmulationFromPayload(payload, commandList, context, &Payload::accumulationTiming, payload.phase == AvboitAccumulationSharedComputeEmulationGraphTask::Phase::Raster, trait);
}

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
            &payload.csgResources,
            &payload.frameBindings,
            preparedAccumulationInstanceCount,
            preparedAccumulationMaterialTypedByteCount,
            // The mergeable finalizer owns the framebuffer handoff.
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

