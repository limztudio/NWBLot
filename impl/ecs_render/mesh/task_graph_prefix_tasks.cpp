// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshViewSetupGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(
        context,
        payload.shadowVisibilityTask
    );
    if(
        !payload.renderer
        || !payload.asyncPrefixTiming
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.asyncPrefixTimingSpansOnePacket
        || !shadowVisibilityQueue
    )
        return false;

    RendererSystem& renderer = *payload.renderer;
    const bool shadowVisibilityRunsOnCompute =
        shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    const bool recordsGraphicsFrameMarker =
        !shadowVisibilityRunsOnCompute && RendererGpuTimingScope::s_Frame.valid()
    ;
    if(recordsGraphicsFrameMarker)
        commandList.beginMarker(RendererGpuTimingScope::s_Frame.markerLabel);

    if(shadowVisibilityRunsOnCompute && *payload.asyncPrefixTimingSpansOnePacket){
        payload.asyncPrefixTiming->emplace(
            renderer.m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_AsyncPrefix,
            renderer.m_graphics.getDevice(),
            commandList
        );
        payload.asyncPrefixTiming->value().finishMarker();
    }

    if(recordsGraphicsFrameMarker)
        commandList.endMarker();
    return true;
}


bool MeshViewUploadCommitGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(commandList);
    static_cast<void>(context);
    if(!payload.renderer || !payload.ready)
        return false;
    *payload.ready = true;
    return true;
}


void MeshViewUploadCommitGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    static_cast<void>(token);
    if(payload.renderer && payload.uploadRequired)
        payload.renderer->m_meshSystem.confirmMeshViewBufferUpload(payload.viewState);
}


void MeshViewUploadCommitGraphTask::discarded(Payload& payload){
    if(payload.ready)
        *payload.ready = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

