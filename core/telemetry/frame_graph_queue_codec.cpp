// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_queue_codec_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphQueueCodecDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EncodedFrameGraphQueueAssignment EncodeQueueAssignment(const u32 nodeIndex, const FrameGraphQueueAssignment& assignment)noexcept{
    return EncodedFrameGraphQueueAssignment{
        .nodeIndex = nodeIndex,
        .initialQueue = { .index = assignment.initialQueue.index, .deviceGeneration = assignment.initialQueue.deviceGeneration },
        .plannedQueue = { .index = assignment.plannedQueue.index, .deviceGeneration = assignment.plannedQueue.deviceGeneration },
        .acceptedQueue = { .index = assignment.acceptedQueue.index, .deviceGeneration = assignment.acceptedQueue.deviceGeneration },
        .previousAcceptedQueue = { .index = assignment.previousAcceptedQueue.index, .deviceGeneration = assignment.previousAcceptedQueue.deviceGeneration },
        .scoreOverlap = assignment.score.overlap,
        .scoreQueueLoad = assignment.score.queueLoad,
        .scoreIncomingCrossings = assignment.score.incomingCrossings,
        .scoreOutgoingCrossings = assignment.score.outgoingCrossings,
        .scoreOwnershipTransfers = assignment.score.ownershipTransfers,
        .scoreTotal = assignment.score.total,
        .queueClass = assignment.queueClass,
        .reason = assignment.reason,
        .modifiers = assignment.modifiers,
        .acceptance = assignment.acceptance,
        .dedicated = static_cast<u8>(assignment.dedicated ? 1u : 0u),
    };
}

bool DecodeQueueAssignment(const EncodedFrameGraphQueueAssignment& encoded, FrameGraphQueueAssignment& outAssignment)noexcept{
    if(
        encoded.dedicated > 1u
        || encoded.reserved[0u] != 0u
        || encoded.reserved[1u] != 0u
        || encoded.reserved[2u] != 0u
    )
        return false;

    outAssignment = {
        .initialQueue = { .index = encoded.initialQueue.index, .deviceGeneration = encoded.initialQueue.deviceGeneration },
        .plannedQueue = { .index = encoded.plannedQueue.index, .deviceGeneration = encoded.plannedQueue.deviceGeneration },
        .acceptedQueue = { .index = encoded.acceptedQueue.index, .deviceGeneration = encoded.acceptedQueue.deviceGeneration },
        .previousAcceptedQueue = { .index = encoded.previousAcceptedQueue.index, .deviceGeneration = encoded.previousAcceptedQueue.deviceGeneration },
        .score = {
            .overlap = encoded.scoreOverlap,
            .queueLoad = encoded.scoreQueueLoad,
            .incomingCrossings = encoded.scoreIncomingCrossings,
            .outgoingCrossings = encoded.scoreOutgoingCrossings,
            .ownershipTransfers = encoded.scoreOwnershipTransfers,
            .total = encoded.scoreTotal,
        },
        .queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass),
        .reason = static_cast<FrameGraphQueueAssignmentReason::Enum>(encoded.reason),
        .modifiers = static_cast<FrameGraphQueueAssignmentModifier::Mask>(encoded.modifiers),
        .acceptance = static_cast<FrameGraphQueueAssignmentAcceptance::Enum>(encoded.acceptance),
        .dedicated = encoded.dedicated != 0u,
        .present = true,
    };
    return IsValidFrameGraphQueueAssignment(outAssignment);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

