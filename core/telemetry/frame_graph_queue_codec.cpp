// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_queue_codec_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_queue_codec{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma pack(push, 1)
struct LegacyQueueAssignment{
    u32 nodeIndex = 0u;
    EncodedFrameGraphPhysicalQueueId initialQueue;
    EncodedFrameGraphPhysicalQueueId plannedQueue;
    EncodedFrameGraphPhysicalQueueId acceptedQueue;
    EncodedFrameGraphPhysicalQueueId previousAcceptedQueue;
    i32 preference = 0;
    i32 overlap = 0;
    i32 queueLoad = 0;
    i32 incomingCrossings = 0;
    i32 outgoingCrossings = 0;
    i32 ownershipTransfers = 0;
    i32 total = 0;
    u8 queueClass = 0u;
    u8 reason = 0u;
    u8 modifiers = 0u;
    u8 acceptance = 0u;
    u8 dedicated = 0u;
    u8 reserved[3u] = {};
};
#pragma pack(pop)
static_assert(sizeof(LegacyQueueAssignment) == FrameGraphQueueCodecDetail::s_LegacyQueueAssignmentRecordBytes);
static_assert(IsTriviallyCopyable_V<LegacyQueueAssignment>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static i32 SaturateScore(const i64 total)noexcept{
    if(total < static_cast<i64>(Limit<i32>::s_Min))
        return Limit<i32>::s_Min;
    if(total > static_cast<i64>(Limit<i32>::s_Max))
        return Limit<i32>::s_Max;
    return static_cast<i32>(total);
}

[[nodiscard]] static FrameGraphQueueAssignmentReason::Enum DecodeLegacyReason(const u8 reason)noexcept{
    switch(reason){
    case 1u:
        return FrameGraphQueueAssignmentReason::RequiredGraphics;
    case 6u:
        return FrameGraphQueueAssignmentReason::Conservative;
    case 2u:
    case 3u:
    case 4u:
    case 5u:
    case 7u:
    case 8u:
    case 9u:
        return FrameGraphQueueAssignmentReason::Scored;
    default:
        return FrameGraphQueueAssignmentReason::Unknown;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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

bool ReadQueueAssignment(
    const BinaryByteView& encoded,
    usize& cursor,
    const u16 version,
    EncodedFrameGraphQueueAssignment& outAssignment)noexcept{
    if(version >= s_FrameGraphAutomaticQueueAssignmentPayloadVersion)
        return ReadPOD(encoded, cursor, outAssignment);

    __hidden_frame_graph_queue_codec::LegacyQueueAssignment legacy;
    if(!ReadPOD(encoded, cursor, legacy))
        return false;
    const i64 activeTotal = static_cast<i64>(legacy.overlap)
        - static_cast<i64>(legacy.queueLoad)
        - static_cast<i64>(legacy.incomingCrossings)
        - static_cast<i64>(legacy.outgoingCrossings)
        - static_cast<i64>(legacy.ownershipTransfers)
    ;
    if(legacy.total != __hidden_frame_graph_queue_codec::SaturateScore(activeTotal + static_cast<i64>(legacy.preference)))
        return false;

    outAssignment = {
        .nodeIndex = legacy.nodeIndex,
        .initialQueue = legacy.initialQueue,
        .plannedQueue = legacy.plannedQueue,
        .acceptedQueue = legacy.acceptedQueue,
        .previousAcceptedQueue = legacy.previousAcceptedQueue,
        .scoreOverlap = legacy.overlap,
        .scoreQueueLoad = legacy.queueLoad,
        .scoreIncomingCrossings = legacy.incomingCrossings,
        .scoreOutgoingCrossings = legacy.outgoingCrossings,
        .scoreOwnershipTransfers = legacy.ownershipTransfers,
        .scoreTotal = __hidden_frame_graph_queue_codec::SaturateScore(activeTotal),
        .queueClass = legacy.queueClass,
        .reason = __hidden_frame_graph_queue_codec::DecodeLegacyReason(legacy.reason),
        .modifiers = legacy.modifiers,
        .acceptance = legacy.acceptance,
        .dedicated = legacy.dedicated,
        .reserved = { legacy.reserved[0u], legacy.reserved[1u], legacy.reserved[2u] },
    };
    return true;
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

