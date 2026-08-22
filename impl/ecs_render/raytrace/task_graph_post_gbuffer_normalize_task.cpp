// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/task_graph_post_gbuffer_normalize_task.h>

#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/gpu_timing.h>

#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PostGbufferNormalizeGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(
        context,
        payload.shadowVisibilityTask
    );
    if(
        !payload.raytracingSystem
        || !payload.timingTicket
        || !*payload.timingTicket
        || !shadowVisibilityQueue
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    // The graph's explicit uses below lower the ordinary G-buffer, scene, descriptor, and dynamically selected
    // trace-geometry states before this task records.
    if(
        shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute
        && payload.asyncPrefixTiming
        && *payload.asyncPrefixTiming
    ){
        (*payload.asyncPrefixTiming)->finishTiming(commandList);
        payload.asyncPrefixTiming->reset();
    }
    return true;
}


void PostGbufferNormalizeGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    static_cast<void>(token);
    if(payload.raytracingSystem)
        payload.raytracingSystem->confirmPreparedShadowTraceGeometryNormalization();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

