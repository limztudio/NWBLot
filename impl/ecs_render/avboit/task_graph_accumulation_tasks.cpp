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

    return RecordAvboitRasterPassFromPayload(
        payload,
        commandList,
        context,
        &Payload::accumulationPhasePrepared,
        &Payload::accumulationSnapshot,
        &Payload::accumulationComputeEmulationOutputStatesGraphOwned,
        &Payload::accumulationCsgComputeEmulationOutputStatesGraphOwned,
        &Payload::accumulationComputeEmulationTiming,
        [&](
            Core::CommandList& dispatchCommandList,
            const MaterialPassDrawItemPartitions* dispatchDrawItems,
            const CsgFrameGpuData* dispatchCsgFrameData,
            const usize dispatchInstanceCount,
            const usize dispatchMaterialTypedByteCount
        ){
            payload.avboitSystem->renderAvboitAccumulatePass(
                dispatchCommandList,
                *payload.targets,
                dispatchDrawItems,
                dispatchCsgFrameData,
                &payload.csgResources,
                &payload.frameBindings,
                dispatchInstanceCount,
                dispatchMaterialTypedByteCount,
                // The mergeable finalizer owns the framebuffer handoff.
                true,
                payload.accumulationCsgIntervalSampleImageStatesGraphOwned,
                payload.accumulationCsgClipBufferStatesGraphOwned,
                payload.accumulationMaterialFrameStatesGraphOwned,
                payload.accumulationMaterialGeometryStatesGraphOwned,
                payload.accumulationComputeEmulationOutputStatesGraphOwned,
                payload.accumulationComputeEmulationTiming,
                payload.accumulationCsgComputeEmulationOutputStatesGraphOwned,
                payload.generatedGeometryReused
            );
        }
    );
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

