// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/telemetry/frame_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Normal completion accepts every task, or discards only the independent recovery task without submitting it.
[[nodiscard]] inline bool ValidateNormalFrameSnapshotCompletion(const Core::Telemetry::FrameGraphPayload& payload, const Core::Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
    const auto& compile = statistics.compile;
    const auto& submission = statistics.submission;
    const bool allAccepted = submission.rejectedPacketCount == 0u && submission.rejectedTaskCount == 0u;
    const bool unusedRecovery = submission.rejectedPacketCount == 1u && submission.rejectedTaskCount == 1u;
    const u64 discarded = unusedRecovery ? 1u : 0u;
    if(compile.packetCount == 0u || compile.taskCount == 0u || (!allAccepted && !unusedRecovery)
        || submission.rejectedSubmissionCount != 0u || submission.recoverySubmissionCount != 0u || submission.acceptedPacketCount != compile.packetCount - discarded
        || submission.acceptedTaskCount != compile.taskCount - discarded || payload.packetSubmissionStatistics.size() != submission.acceptedPacketCount)
        return false;

    u64 acceptedTasksRemaining = submission.acceptedTaskCount;
    for(usize index = 0u; index < payload.packetSubmissionStatistics.size(); ++index){
        const auto& packet = payload.packetSubmissionStatistics[index];
        if(packet.packetGeneration != statistics.planGeneration || packet.packetIndex >= compile.packetCount || !packet.queue.valid() || packet.recoverySubmission || packet.taskCount == 0u || packet.taskCount > acceptedTasksRemaining)
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(payload.packetSubmissionStatistics[previous].packetIndex == packet.packetIndex)
                return false;
        }
        u64 compiledTasks = 0u;
        for(const auto& node : payload.nodes){
            if(node.compiledTask.present && node.compiledTask.packetIndex == packet.packetIndex)
                ++compiledTasks;
        }
        if(compiledTasks != packet.taskCount)
            return false;
        acceptedTasksRemaining -= packet.taskCount;
    }
    if(acceptedTasksRemaining != 0u)
        return false;

    u64 compiledTasks = 0u;
    u64 unusedRecoveryTasks = 0u;
    for(const auto& node : payload.nodes){
        if(!node.compiledTask.present)
            continue;
        ++compiledTasks;
        if(node.compiledTask.planGeneration != statistics.planGeneration || node.compiledTask.packetIndex >= compile.packetCount)
            return false;
        bool accepted = false;
        for(const auto& packet : payload.packetSubmissionStatistics)
            accepted = accepted || packet.packetIndex == node.compiledTask.packetIndex;
        if(node.name == Name("render.frame_recovery")){
            if(!unusedRecovery || accepted)
                return false;
            ++unusedRecoveryTasks;
        }
        else if(!accepted)
            return false;
    }
    return compiledTasks == compile.taskCount && unusedRecoveryTasks == discarded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

