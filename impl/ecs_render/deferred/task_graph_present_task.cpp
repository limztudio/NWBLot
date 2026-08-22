// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/task_graph_present_task.h>

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool DeferredPresentGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(
        context,
        payload.shadowVisibilityTask
    );
    const bool shadowVisibilityRunsOnCompute = shadowVisibilityQueue
        && shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
    if(
        !payload.deferredSystem
        || !payload.graphics
        || !payload.targets
        || !payload.presentationFramebuffer
        || !payload.timingTicket
        || !shadowVisibilityQueue
        || (shadowVisibilityRunsOnCompute && !payload.asyncFinalTiming)
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(shadowVisibilityRunsOnCompute){
        payload.asyncFinalTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_AsyncFinal,
            payload.graphics->getDevice(),
            commandList
        );
        payload.asyncFinalTiming->value().finishMarker();
    }

    const bool presentRecorded = payload.deferredSystem->renderDeferredPresent(
        commandList,
        *payload.targets,
        payload.presentationFramebuffer
    );
    if(shadowVisibilityRunsOnCompute && presentRecorded && payload.asyncFinalTiming->has_value()){
        payload.asyncFinalTiming->value().finishTiming(commandList);
        payload.asyncFinalTiming->reset();
    }
    return presentRecorded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

