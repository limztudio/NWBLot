// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_frame_timing_reset.h"
#include "runtime_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_timing_reset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_FrameTimingResetIdentity("graphics.frame_timing.reset");


struct FrameTimingResetGraphTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::None, true, GpuQueueCapability::Compute | GpuQueueCapability::Graphics, };

    struct Payload{
        NotNull<GpuTimingRecorder*> timing;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, CommandList& commandList, [[maybe_unused]] const GpuTaskRecordContext& context){
        payload.timing->recordFrameReset(commandList);
        return true;
    }

    static void Accepted(Payload& payload, const QueueSubmissionToken& token){
        if(token.valid())
            payload.timing->confirmFrameReset(token);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.timing->discardFrameReset();
    }
};

[[nodiscard]] static GpuTaskSchedulingHint FrameTimingResetScheduling()noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    // Complete CPU-visible preamble boundary. Later timestamp scopes reserve only after this packet accepts; never merge.
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.overlapPreferred = false;
    return scheduling;
}

struct FrameTimingResetSubmissionData{
    GpuTimingRecorder& timing;
};

[[nodiscard]] static Expected<GpuTaskId> DeclareFrameTimingResetGraph(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *MakeNotNull(static_cast<FrameTimingResetSubmissionData*>(userData));
    GpuTaskDesc resetDesc;
    resetDesc.setIdentity(s_FrameTimingResetIdentity).setMarkerLabel("Frame GPU-Timing Reset").setScheduling(FrameTimingResetScheduling());
    const auto taskResult = graph.addTask<FrameTimingResetGraphTask>(resetDesc, FrameTimingResetGraphTask::Payload{ .timing = MakeNotNull(&submissionData.timing), });
    if(!taskResult)
        return MakeUnexpected(Failure{});
    const GpuTaskId task = *taskResult;
    return task;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameTimingResetGraph::Submit(const GraphicsRuntime& graphics, StandaloneGraphWorkspacePool& workspaces, GpuTimingRecorder& timing){
    auto& device = graphics.getDevice();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!graphicsQueue.valid())
        return false;

    __hidden_frame_timing_reset::FrameTimingResetSubmissionData submissionData{ .timing = timing, };
    const auto acceptedToken = SubmitGraphOwnedStandaloneTask(graphics, MakeNotNull(&__hidden_frame_timing_reset::DeclareFrameTimingResetGraph), &submissionData, workspaces, graphicsQueue);
    if(!acceptedToken)
        return false;

    return acceptedToken->queue == CommandQueue::Graphics && acceptedToken->matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


