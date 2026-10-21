// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_validators{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static i32 QueueAssignmentScoreTotal(const FrameGraphQueueAssignmentScore& score)noexcept{
    const i64 total = static_cast<i64>(score.overlap) - static_cast<i64>(score.queueLoad) - static_cast<i64>(score.incomingCrossings) - static_cast<i64>(score.outgoingCrossings) - static_cast<i64>(score.ownershipTransfers);
    if(total < static_cast<i64>(Limit<i32>::s_Min))
        return Limit<i32>::s_Min;
    if(total > static_cast<i64>(Limit<i32>::s_Max))
        return Limit<i32>::s_Max;
    return static_cast<i32>(total);
}

[[nodiscard]] static bool IsCanonicalQueue(const FrameGraphPhysicalQueueId& queue)noexcept{
    return queue.valid() || (queue.index == Limit<u16>::s_Max && queue.deviceGeneration == 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsValidFrameGraphNodeKind(const FrameGraphNodeKind::Enum kind)noexcept{
    switch(kind){
    case FrameGraphNodeKind::Pass:
    case FrameGraphNodeKind::Resource:
    case FrameGraphNodeKind::External:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphEdgeKind(const FrameGraphEdgeKind::Enum kind)noexcept{
    switch(kind){
    case FrameGraphEdgeKind::Reads:
    case FrameGraphEdgeKind::Writes:
    case FrameGraphEdgeKind::DependsOn:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueClass(const FrameGraphQueueClass::Enum queueClass)noexcept{
    switch(queueClass){
    case FrameGraphQueueClass::Graphics:
    case FrameGraphQueueClass::Compute:
    case FrameGraphQueueClass::Transfer:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueAssignmentReason(const FrameGraphQueueAssignmentReason::Enum reason)noexcept{
    switch(reason){
    case FrameGraphQueueAssignmentReason::RequiredGraphics:
    case FrameGraphQueueAssignmentReason::Conservative:
    case FrameGraphQueueAssignmentReason::Scored:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueAssignmentAcceptance(const FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept{
    switch(acceptance){
    case FrameGraphQueueAssignmentAcceptance::NotAccepted:
    case FrameGraphQueueAssignmentAcceptance::First:
    case FrameGraphQueueAssignmentAcceptance::Unchanged:
    case FrameGraphQueueAssignmentAcceptance::Changed:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphTaskPacketizationDecision(const FrameGraphTaskPacketizationDecision::Enum decision)noexcept{
    switch(decision){
    case FrameGraphTaskPacketizationDecision::FirstTask:
    case FrameGraphTaskPacketizationDecision::MergeNotRequested:
    case FrameGraphTaskPacketizationDecision::TaskForcesBoundary:
    case FrameGraphTaskPacketizationDecision::QueueChanged:
    case FrameGraphTaskPacketizationDecision::PrecedingTaskForcesBoundary:
    case FrameGraphTaskPacketizationDecision::ScoredMergeIneligible:
    case FrameGraphTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency:
    case FrameGraphTaskPacketizationDecision::CrossQueueConsumerFrontier:
    case FrameGraphTaskPacketizationDecision::MergedExplicit:
    case FrameGraphTaskPacketizationDecision::MergedFrontierScored:
    case FrameGraphTaskPacketizationDecision::ScoredMergeDomainMismatch:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueAssignment(const FrameGraphQueueAssignment& assignment)noexcept{
    if(
        !assignment.present
        || !assignment.initialQueue.valid()
        || !assignment.plannedQueue.valid()
        || !__hidden_frame_graph_validators::IsCanonicalQueue(assignment.acceptedQueue)
        || !__hidden_frame_graph_validators::IsCanonicalQueue(assignment.previousAcceptedQueue)
        || assignment.initialQueue.deviceGeneration != assignment.plannedQueue.deviceGeneration
        || !IsValidFrameGraphQueueClass(assignment.queueClass)
        || !IsValidFrameGraphQueueAssignmentReason(assignment.reason)
        || !IsValidFrameGraphQueueAssignmentAcceptance(assignment.acceptance)
        || (static_cast<u8>(assignment.modifiers) & static_cast<u8>(~static_cast<u8>(FrameGraphQueueAssignmentModifier::All))) != 0u
        || assignment.score.total != __hidden_frame_graph_validators::QueueAssignmentScoreTotal(assignment.score)
    )
        return false;

    const u16 deviceGeneration = assignment.plannedQueue.deviceGeneration;
    if(assignment.acceptedQueue.valid() && assignment.acceptedQueue.deviceGeneration != deviceGeneration)
        return false;
    if(assignment.previousAcceptedQueue.valid() && assignment.previousAcceptedQueue.deviceGeneration != deviceGeneration)
        return false;

    switch(assignment.acceptance){
    case FrameGraphQueueAssignmentAcceptance::NotAccepted:
        return !assignment.acceptedQueue.valid();
    case FrameGraphQueueAssignmentAcceptance::First:
        return assignment.acceptedQueue == assignment.plannedQueue && !assignment.previousAcceptedQueue.valid();
    case FrameGraphQueueAssignmentAcceptance::Unchanged:
        return assignment.acceptedQueue == assignment.plannedQueue && assignment.previousAcceptedQueue == assignment.acceptedQueue;
    case FrameGraphQueueAssignmentAcceptance::Changed:
        return assignment.acceptedQueue == assignment.plannedQueue && assignment.previousAcceptedQueue.valid() && assignment.previousAcceptedQueue != assignment.acceptedQueue;
    default:
        return false;
    }
}

bool IsValidFrameGraphCompiledTask(const FrameGraphCompiledTask& compiledTask)noexcept{
    return compiledTask.present && compiledTask.planGeneration != 0u && compiledTask.packetIndex != Limit<u32>::s_Max && IsValidFrameGraphTaskPacketizationDecision(compiledTask.packetizationDecision);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

