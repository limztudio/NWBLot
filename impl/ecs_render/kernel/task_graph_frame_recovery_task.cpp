// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/task_graph_frame_recovery_task.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameRecoveryGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    return payload.frameTimingTransaction
        && payload.armed
        && payload.retiresFrameTiming
        && *payload.armed
        && (!*payload.retiresFrameTiming || payload.frameTimingTransaction->recordEnd(commandList))
    ;
}

void FrameRecoveryGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    static_cast<void>(token);
    if(
        payload.armed
        && payload.retiresFrameTiming
        && *payload.armed
        && *payload.retiresFrameTiming
        && payload.frameTimingTransaction
        && !payload.frameTimingTransaction->confirmEndSubmission(false)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire frame recovery timing query"));
        payload.frameTimingTransaction->discard();
    }
    if(payload.armed)
        *payload.armed = false;
    if(payload.retiresFrameTiming)
        *payload.retiresFrameTiming = false;
}

void FrameRecoveryGraphTask::discarded(Payload& payload){
    if(payload.armed && *payload.armed && payload.frameTimingTransaction)
        payload.frameTimingTransaction->discard();
    if(payload.armed)
        *payload.armed = false;
    if(payload.retiresFrameTiming)
        *payload.retiresFrameTiming = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

