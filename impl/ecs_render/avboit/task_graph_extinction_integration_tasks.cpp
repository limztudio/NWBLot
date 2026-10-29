// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_extinction_integration_tasks.h"

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


[[nodiscard]] b8 AvboitExtinctionComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitExtinction, MaterialPipelinePass::AvboitExtinction, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitComputeEmulationFromPayload(payload, commandList, trait, payload.extinctionTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitExtinctionSharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    const AvboitSharedComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitExtinction, MaterialPipelinePass::AvboitExtinction, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitSharedComputeEmulationFromPayload(payload, commandList, payload.phase == AvboitExtinctionSharedComputeEmulationGraphTask::Phase::Raster, trait, payload.extinctionTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitExtinctionGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);

    return RecordAvboitRasterPassFromPayload(
        payload,
        commandList,
        &Payload::extinctionPhasePrepared,
        &Payload::extinctionSnapshot,
        &Payload::extinctionComputeEmulationOutputStatesGraphOwned,
        &Payload::extinctionCsgComputeEmulationOutputStatesGraphOwned,
        [&](Core::CommandList& dispatchCommandList, const MaterialPassDrawItemPartitionsView& dispatchDrawItems, const CsgFrameGpuDataView& dispatchCsgFrameData, const usize dispatchInstanceCount, const usize dispatchMaterialTypedByteCount){
            payload.avboitSystem.renderAvboitExtinctionPass(
                dispatchCommandList,
                payload.targets,
                dispatchDrawItems,
                dispatchCsgFrameData,
                payload.csgResources,
                payload.frameBindings,
                dispatchInstanceCount,
                dispatchMaterialTypedByteCount,
                payload.extinctionMaterialFrameStatesGraphOwned,
                payload.extinctionMaterialGeometryStatesGraphOwned,
                payload.extinctionComputeEmulationOutputStatesGraphOwned,
                payload.extinctionCsgComputeEmulationOutputStatesGraphOwned,
                payload.generatedGeometryReused,
                payload.extinctionComputeEmulationTiming
            );
        },
        payload.extinctionComputeEmulationTiming
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitIntegrationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    return RecordAvboitComputeEffect(
        payload,
        commandList,
        context,
        [&](Core::CommandList& dispatchCommandList, AvboitFrameTargets& targets, const Core::GpuTimingSampleAttribution attribution, b8* const timingRecorded){
            payload.avboitSystem.dispatchAvboitIntegration(dispatchCommandList, targets, attribution, timingRecorded);
        }
    );
}

void AvboitIntegrationGraphTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
    AcceptAvboitComputeEffect(payload, token);
}

void AvboitIntegrationGraphTask::Discarded(Payload& payload)noexcept{
    DiscardAvboitComputeEffect(payload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

