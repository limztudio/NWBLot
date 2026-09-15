// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_extinction_integration_tasks.h"

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


[[nodiscard]] bool AvboitExtinctionComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    static_cast<void>(context);
    const AvboitComputeEmulationRecordTrait trait{
        &RendererGpuTimingScope::s_AvboitExtinction,
        MaterialPipelinePass::AvboitExtinction,
        &AvboitFrameTargets::lowFramebuffer,
    };
    return RecordAvboitComputeEmulationFromPayload(payload, commandList, context, &Payload::extinctionTiming, trait);
}

[[nodiscard]] bool AvboitExtinctionSharedComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    static_cast<void>(context);
    const AvboitSharedComputeEmulationRecordTrait trait{
        &RendererGpuTimingScope::s_AvboitExtinction,
        MaterialPipelinePass::AvboitExtinction,
        &AvboitFrameTargets::lowFramebuffer,
    };
    return RecordAvboitSharedComputeEmulationFromPayload(payload, commandList, context, &Payload::extinctionTiming, payload.phase == AvboitExtinctionSharedComputeEmulationGraphTask::Phase::Raster, trait);
}

[[nodiscard]] bool AvboitExtinctionGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){

    return RecordAvboitRasterPassFromPayload(
        payload,
        commandList,
        context,
        &Payload::extinctionPhasePrepared,
        &Payload::extinctionSnapshot,
        &Payload::extinctionComputeEmulationOutputStatesGraphOwned,
        &Payload::extinctionCsgComputeEmulationOutputStatesGraphOwned,
        &Payload::extinctionComputeEmulationTiming,
        [&](
            Core::CommandList& dispatchCommandList,
            const MaterialPassDrawItemPartitions* dispatchDrawItems,
            const CsgFrameGpuData* dispatchCsgFrameData,
            const usize dispatchInstanceCount,
            const usize dispatchMaterialTypedByteCount
        ){
            payload.avboitSystem->renderAvboitExtinctionPass(
                dispatchCommandList,
                *payload.targets,
                dispatchDrawItems,
                dispatchCsgFrameData,
                &payload.csgResources,
                &payload.frameBindings,
                dispatchInstanceCount,
                dispatchMaterialTypedByteCount,
                payload.extinctionCsgIntervalSampleImageStatesGraphOwned,
                payload.extinctionCsgClipBufferStatesGraphOwned,
                payload.extinctionMaterialFrameStatesGraphOwned,
                payload.extinctionMaterialGeometryStatesGraphOwned,
                payload.extinctionComputeEmulationOutputStatesGraphOwned,
                payload.extinctionComputeEmulationTiming,
                payload.extinctionCsgComputeEmulationOutputStatesGraphOwned
            );
        }
    );
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

void AvboitIntegrationGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
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

