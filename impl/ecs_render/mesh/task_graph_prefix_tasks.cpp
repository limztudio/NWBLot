// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>

#include <impl/ecs_render/mesh/mesh_system.h>

#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/compiled_graph.h>


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
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue =
        payload.shadowVisibilityTask && payload.shadowVisibilityTask->valid()
            ? context.compiledPlan.queueInfoForTask(*payload.shadowVisibilityTask)
            : nullptr
    ;
    if(
        !payload.graphics
        || !payload.asyncPrefixTiming
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.asyncPrefixTimingSpansOnePacket
        || !shadowVisibilityQueue
    )
        return false;

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
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_AsyncPrefix,
            payload.graphics->getDevice(),
            commandList
        );
        if(!Core::FinishSplitGpuTimingMarker(payload.asyncPrefixTiming))
            return false;
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
    if(!payload.meshSystem || !payload.ready)
        return false;
    *payload.ready = true;
    return true;
}


void MeshViewUploadCommitGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    static_cast<void>(token);
    if(payload.meshSystem && payload.uploadRequired)
        payload.meshSystem->confirmMeshViewBufferUpload(payload.viewState);
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

