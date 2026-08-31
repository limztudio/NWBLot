// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/task_graph_present_task.h>

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/backend_selection.h>


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
        || !payload.presentationFrame.valid()
        || !payload.backBuffer.valid()
        || !payload.timingTicket
        || !shadowVisibilityQueue
        || (shadowVisibilityRunsOnCompute && !payload.asyncFinalTiming)
    )
        return false;
    const Core::Framebuffer& presentationFramebuffer = *payload.presentationFrame.framebuffer;
    const Core::FramebufferDesc& presentationFramebufferDesc = presentationFramebuffer.getDescription();
    if(
        presentationFramebufferDesc.colorAttachments.size() != 1u
        || presentationFramebufferDesc.colorAttachments[0].texture != payload.presentationFrame.backBuffer.texture.get()
        || context.taskGraph.textureForResource(payload.backBuffer) != payload.presentationFrame.backBuffer.texture.get()
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
        payload.presentationFrame
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

