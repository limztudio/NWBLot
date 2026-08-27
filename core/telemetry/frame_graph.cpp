// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsValidStringTableText(const AStringView text)noexcept{
    return !text.empty()
        && !HasEmbeddedNull(text)
        && text.size() < static_cast<usize>(Limit<u32>::s_Max)
    ;
}

[[nodiscard]] static i32 QueueAssignmentScoreTotal(const FrameGraphQueueAssignmentScore& score)noexcept{
    const i64 total = static_cast<i64>(score.preference)
        + static_cast<i64>(score.overlap)
        - static_cast<i64>(score.queueLoad)
        - static_cast<i64>(score.incomingCrossings)
        - static_cast<i64>(score.outgoingCrossings)
        - static_cast<i64>(score.ownershipTransfers)
    ;
    if(total < static_cast<i64>(Limit<i32>::s_Min))
        return Limit<i32>::s_Min;
    if(total > static_cast<i64>(Limit<i32>::s_Max))
        return Limit<i32>::s_Max;
    return static_cast<i32>(total);
}

[[nodiscard]] static bool ValidateHeader(const u32 magic, const u16 reserved)noexcept{
    return magic == s_FrameGraphPayloadMagic
        && reserved == 0u
    ;
}

[[nodiscard]] static bool IsCanonicalQueue(const FrameGraphPhysicalQueueId& queue)noexcept{
    return queue.valid()
        || (queue.index == Limit<u16>::s_Max && queue.deviceGeneration == 0u)
    ;
}

[[nodiscard]] static bool ValidateNodeInput(const FrameGraphNodeDesc& node)noexcept{
    return static_cast<bool>(node.name)
        && IsValidFrameGraphNodeKind(node.kind)
        && IsValidStringTableText(node.label)
        && (!node.queueAssignment.present || (
            node.kind == FrameGraphNodeKind::Pass
            && IsValidFrameGraphQueueAssignment(node.queueAssignment)
        ))
        && (!node.compiledTask.present || (
            node.kind == FrameGraphNodeKind::Pass
            && IsValidFrameGraphCompiledTask(node.compiledTask)
        ))
    ;
}

[[nodiscard]] static bool ValidateEdgeInput(const FrameGraphEdgeDesc& edge, const usize nodeCount)noexcept{
    return IsValidFrameGraphEdgeKind(edge.kind)
        && static_cast<usize>(edge.fromNodeIndex) < nodeCount
        && static_cast<usize>(edge.toNodeIndex) < nodeCount
    ;
}

[[nodiscard]] static bool ValidateEncodedNode(const EncodedFrameGraphNode& node)noexcept{
    return node.reserved == 0u
        && !NameDetail::IsZeroHash(node.nameHash)
        && IsValidFrameGraphNodeKind(static_cast<FrameGraphNodeKind::Enum>(node.kind))
    ;
}

[[nodiscard]] static bool ValidateEncodedEdge(const EncodedFrameGraphEdge& edge, const usize nodeCount)noexcept{
    return edge.reserved == 0u
        && IsValidFrameGraphEdgeKind(static_cast<FrameGraphEdgeKind::Enum>(edge.kind))
        && static_cast<usize>(edge.fromNodeIndex) < nodeCount
        && static_cast<usize>(edge.toNodeIndex) < nodeCount
    ;
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueId EncodeQueue(const FrameGraphPhysicalQueueId& queue)noexcept{
    return EncodedFrameGraphPhysicalQueueId{
        .index = queue.index,
        .deviceGeneration = queue.deviceGeneration,
    };
}

[[nodiscard]] static FrameGraphPhysicalQueueId DecodeQueue(const EncodedFrameGraphPhysicalQueueId& queue)noexcept{
    return FrameGraphPhysicalQueueId{
        .index = queue.index,
        .deviceGeneration = queue.deviceGeneration,
    };
}

[[nodiscard]] static EncodedFrameGraphQueueAssignment EncodeQueueAssignment(
    const u32 nodeIndex,
    const FrameGraphQueueAssignment& assignment
)noexcept{
    EncodedFrameGraphQueueAssignment encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.initialQueue = EncodeQueue(assignment.initialQueue);
    encoded.plannedQueue = EncodeQueue(assignment.plannedQueue);
    encoded.acceptedQueue = EncodeQueue(assignment.acceptedQueue);
    encoded.previousAcceptedQueue = EncodeQueue(assignment.previousAcceptedQueue);
    encoded.scorePreference = assignment.score.preference;
    encoded.scoreOverlap = assignment.score.overlap;
    encoded.scoreQueueLoad = assignment.score.queueLoad;
    encoded.scoreIncomingCrossings = assignment.score.incomingCrossings;
    encoded.scoreOutgoingCrossings = assignment.score.outgoingCrossings;
    encoded.scoreOwnershipTransfers = assignment.score.ownershipTransfers;
    encoded.scoreTotal = assignment.score.total;
    encoded.queueClass = assignment.queueClass;
    encoded.reason = assignment.reason;
    encoded.modifiers = assignment.modifiers;
    encoded.acceptance = assignment.acceptance;
    encoded.dedicated = assignment.dedicated ? 1u : 0u;
    return encoded;
}

[[nodiscard]] static bool DecodeQueueAssignment(
    const EncodedFrameGraphQueueAssignment& encoded,
    FrameGraphQueueAssignment& outAssignment
)noexcept{
    if(
        encoded.dedicated > 1u
        || encoded.reserved[0u] != 0u
        || encoded.reserved[1u] != 0u
        || encoded.reserved[2u] != 0u
    )
        return false;

    outAssignment.initialQueue = DecodeQueue(encoded.initialQueue);
    outAssignment.plannedQueue = DecodeQueue(encoded.plannedQueue);
    outAssignment.acceptedQueue = DecodeQueue(encoded.acceptedQueue);
    outAssignment.previousAcceptedQueue = DecodeQueue(encoded.previousAcceptedQueue);
    outAssignment.score.preference = encoded.scorePreference;
    outAssignment.score.overlap = encoded.scoreOverlap;
    outAssignment.score.queueLoad = encoded.scoreQueueLoad;
    outAssignment.score.incomingCrossings = encoded.scoreIncomingCrossings;
    outAssignment.score.outgoingCrossings = encoded.scoreOutgoingCrossings;
    outAssignment.score.ownershipTransfers = encoded.scoreOwnershipTransfers;
    outAssignment.score.total = encoded.scoreTotal;
    outAssignment.queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass);
    outAssignment.reason = static_cast<FrameGraphQueueAssignmentReason::Enum>(encoded.reason);
    outAssignment.modifiers = static_cast<FrameGraphQueueAssignmentModifier::Mask>(encoded.modifiers);
    outAssignment.acceptance = static_cast<FrameGraphQueueAssignmentAcceptance::Enum>(encoded.acceptance);
    outAssignment.dedicated = encoded.dedicated != 0u;
    outAssignment.present = true;
    return IsValidFrameGraphQueueAssignment(outAssignment);
}

[[nodiscard]] static EncodedFrameGraphCompiledTask EncodeCompiledTask(
    const u32 nodeIndex,
    const FrameGraphCompiledTask& compiledTask
)noexcept{
    EncodedFrameGraphCompiledTask encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.packetIndex = compiledTask.packetIndex;
    encoded.planGeneration = compiledTask.planGeneration;
    encoded.packetizationDecision = compiledTask.packetizationDecision;
    return encoded;
}

[[nodiscard]] static bool DecodeCompiledTask(
    const EncodedFrameGraphCompiledTask& encoded,
    FrameGraphCompiledTask& outCompiledTask
)noexcept{
    if(encoded.reserved[0u] != 0u || encoded.reserved[1u] != 0u || encoded.reserved[2u] != 0u)
        return false;

    outCompiledTask.planGeneration = encoded.planGeneration;
    outCompiledTask.packetIndex = encoded.packetIndex;
    outCompiledTask.packetizationDecision = static_cast<FrameGraphTaskPacketizationDecision::Enum>(
        encoded.packetizationDecision
    );
    outCompiledTask.present = true;
    return IsValidFrameGraphCompiledTask(outCompiledTask);
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
    case FrameGraphQueueAssignmentReason::PreferredQueue:
    case FrameGraphQueueAssignmentReason::DedicatedCompute:
    case FrameGraphQueueAssignmentReason::DedicatedTransfer:
    case FrameGraphQueueAssignmentReason::Fallback:
    case FrameGraphQueueAssignmentReason::ConservativeAny:
    case FrameGraphQueueAssignmentReason::SameClassRouting:
    case FrameGraphQueueAssignmentReason::CompilerOverride:
    case FrameGraphQueueAssignmentReason::ScoredAny:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueAssignmentAcceptance(
    const FrameGraphQueueAssignmentAcceptance::Enum acceptance
)noexcept{
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

bool IsValidFrameGraphTaskPacketizationDecision(
    const FrameGraphTaskPacketizationDecision::Enum decision
)noexcept{
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
        || !__hidden_telemetry_frame_graph::IsCanonicalQueue(assignment.acceptedQueue)
        || !__hidden_telemetry_frame_graph::IsCanonicalQueue(assignment.previousAcceptedQueue)
        || assignment.initialQueue.deviceGeneration != assignment.plannedQueue.deviceGeneration
        || !IsValidFrameGraphQueueClass(assignment.queueClass)
        || !IsValidFrameGraphQueueAssignmentReason(assignment.reason)
        || !IsValidFrameGraphQueueAssignmentAcceptance(assignment.acceptance)
        || (
            static_cast<u8>(assignment.modifiers)
            & static_cast<u8>(~static_cast<u8>(FrameGraphQueueAssignmentModifier::All))
        ) != 0u
        || assignment.score.total != __hidden_telemetry_frame_graph::QueueAssignmentScoreTotal(assignment.score)
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
        return assignment.acceptedQueue == assignment.plannedQueue
            && assignment.previousAcceptedQueue == assignment.acceptedQueue
        ;
    case FrameGraphQueueAssignmentAcceptance::Changed:
        return assignment.acceptedQueue == assignment.plannedQueue
            && assignment.previousAcceptedQueue.valid()
            && assignment.previousAcceptedQueue != assignment.acceptedQueue
        ;
    default:
        return false;
    }
}

bool IsValidFrameGraphCompiledTask(const FrameGraphCompiledTask& compiledTask)noexcept{
    return compiledTask.present
        && compiledTask.planGeneration != 0u
        && compiledTask.packetIndex != Limit<u32>::s_Max
        && IsValidFrameGraphTaskPacketizationDecision(compiledTask.packetizationDecision)
    ;
}

bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(!FitsU32(nodes.size()) || !FitsU32(edges.size()))
        return false;

    usize stringTableBytes = 0u;
    usize queueAssignmentCount = 0u;
    usize compiledTaskCount = 0u;
    for(const FrameGraphNodeDesc& node : nodes){
        if(!__hidden_telemetry_frame_graph::ValidateNodeInput(node))
            return false;
        if(!AddStringTableTextReserveBytes(stringTableBytes, node.label))
            return false;
        if(node.queueAssignment.present)
            ++queueAssignmentCount;
        if(node.compiledTask.present)
            ++compiledTaskCount;
    }
    if(!FitsU32(queueAssignmentCount) || !FitsU32(compiledTaskCount))
        return false;

    for(const FrameGraphEdgeDesc& edge : edges){
        if(!__hidden_telemetry_frame_graph::ValidateEdgeInput(edge, nodes.size()))
            return false;
    }

    const bool hasQueueAssignments = queueAssignmentCount != 0u;
    const bool hasCompiledTasks = compiledTaskCount != 0u;
    usize payloadBytes = hasCompiledTasks
        ? sizeof(EncodedFrameGraphPayloadHeaderV3)
        : (hasQueueAssignments ? sizeof(EncodedFrameGraphPayloadHeaderV2) : sizeof(EncodedFrameGraphPayloadHeader))
    ;
    if(
        !AddBinaryRepeatedReserveBytes(payloadBytes, nodes.size(), sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, edges.size(), sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            queueAssignmentCount,
            sizeof(EncodedFrameGraphQueueAssignment)
        )
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            compiledTaskCount,
            sizeof(EncodedFrameGraphCompiledTask)
        )
        || !AddBinaryReserveBytes(payloadBytes, stringTableBytes)
    )
        return false;

    TelemetryBytes stringTable(arena);
    stringTable.reserve(stringTableBytes);
    Vector<EncodedFrameGraphNode, TelemetryArena> encodedNodes(arena);
    encodedNodes.reserve(nodes.size());

    for(const FrameGraphNodeDesc& node : nodes){
        u32 labelOffset = Limit<u32>::s_Max;
        if(!AppendStringTableText(stringTable, node.label, labelOffset))
            return false;

        EncodedFrameGraphNode encodedNode;
        encodedNode.nameHash = node.name.hash();
        encodedNode.labelOffset = labelOffset;
        encodedNode.kind = node.kind;
        encodedNode.flags = node.flags;
        encodedNodes.push_back(encodedNode);
    }

    outPayload.reserve(payloadBytes);
    if(hasCompiledTasks){
        EncodedFrameGraphPayloadHeaderV3 header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        header.queueAssignmentCount = static_cast<u32>(queueAssignmentCount);
        header.compiledTaskCount = static_cast<u32>(compiledTaskCount);
        AppendPOD(outPayload, header);
    }
    else if(hasQueueAssignments){
        EncodedFrameGraphPayloadHeaderV2 header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        header.queueAssignmentCount = static_cast<u32>(queueAssignmentCount);
        AppendPOD(outPayload, header);
    }
    else{
        EncodedFrameGraphPayloadHeader header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        AppendPOD(outPayload, header);
    }
    for(const EncodedFrameGraphNode& node : encodedNodes)
        AppendPOD(outPayload, node);
    for(const FrameGraphEdgeDesc& edge : edges){
        EncodedFrameGraphEdge encodedEdge;
        encodedEdge.fromNodeIndex = edge.fromNodeIndex;
        encodedEdge.toNodeIndex = edge.toNodeIndex;
        encodedEdge.kind = edge.kind;
        encodedEdge.flags = edge.flags;
        AppendPOD(outPayload, encodedEdge);
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].queueAssignment.present)
            AppendPOD(
                outPayload,
                __hidden_telemetry_frame_graph::EncodeQueueAssignment(nodeIndex, nodes[nodeIndex].queueAssignment)
            );
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].compiledTask.present)
            AppendPOD(
                outPayload,
                __hidden_telemetry_frame_graph::EncodeCompiledTask(nodeIndex, nodes[nodeIndex].compiledTask)
            );
    }
    if(!stringTable.empty())
        BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, stringTable.data(), stringTable.size());

    return outPayload.size() == payloadBytes;
}

bool ParseFrameGraphPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes,
    FrameGraphPayload& outPayload
){
    outPayload = FrameGraphPayload(arena);

    if(payloadBytes < sizeof(EncodedFrameGraphPayloadHeader) || !payload)
        return false;

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    EncodedFrameGraphPayloadHeader legacyHeader;
    if(!ReadPOD(encoded, cursor, legacyHeader))
        return false;
    if(!__hidden_telemetry_frame_graph::ValidateHeader(legacyHeader.magic, legacyHeader.reserved))
        return false;

    usize headerBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    switch(legacyHeader.version){
    case s_FrameGraphLegacyPayloadVersion:
        headerBytes = sizeof(EncodedFrameGraphPayloadHeader);
        break;
    case s_FrameGraphQueueAssignmentPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV2 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV2);
        queueAssignmentCount = header.queueAssignmentCount;
        break;
    }
    case s_FrameGraphPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV3 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV3);
        queueAssignmentCount = header.queueAssignmentCount;
        compiledTaskCount = header.compiledTaskCount;
        break;
    }
    default:
        return false;
    }
    if(queueAssignmentCount > legacyHeader.nodeCount || compiledTaskCount > legacyHeader.nodeCount)
        return false;

    usize expectedBytes = headerBytes;
    if(
        !AddBinaryRepeatedReserveBytes(expectedBytes, legacyHeader.nodeCount, sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, legacyHeader.edgeCount, sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            queueAssignmentCount,
            sizeof(EncodedFrameGraphQueueAssignment)
        )
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            compiledTaskCount,
            sizeof(EncodedFrameGraphCompiledTask)
        )
        || !AddBinaryReserveBytes(expectedBytes, legacyHeader.stringTableBytes)
        || expectedBytes != payloadBytes
    )
        return false;

    usize edgeOffset = headerBytes;
    if(!AddBinaryRepeatedReserveBytes(edgeOffset, legacyHeader.nodeCount, sizeof(EncodedFrameGraphNode)))
        return false;

    usize queueAssignmentOffset = edgeOffset;
    if(!AddBinaryRepeatedReserveBytes(queueAssignmentOffset, legacyHeader.edgeCount, sizeof(EncodedFrameGraphEdge)))
        return false;

    usize compiledTaskOffset = queueAssignmentOffset;
    if(!AddBinaryRepeatedReserveBytes(
        compiledTaskOffset,
        queueAssignmentCount,
        sizeof(EncodedFrameGraphQueueAssignment)
    ))
        return false;

    usize stringTableOffset = compiledTaskOffset;
    if(!AddBinaryRepeatedReserveBytes(
        stringTableOffset,
        compiledTaskCount,
        sizeof(EncodedFrameGraphCompiledTask)
    ))
        return false;

    outPayload.frameIndex = legacyHeader.frameIndex;
    outPayload.nodes.reserve(legacyHeader.nodeCount);
    outPayload.edges.reserve(legacyHeader.edgeCount);

    for(u32 nodeIndex = 0u; nodeIndex < legacyHeader.nodeCount; ++nodeIndex){
        EncodedFrameGraphNode encodedNode;
        if(!ReadPOD(encoded, cursor, encodedNode))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedNode(encodedNode))
            return false;

        AStringView labelView;
        if(!BinaryDetail::ReadStringTableTextView(
            encoded,
            stringTableOffset,
            legacyHeader.stringTableBytes,
            encodedNode.labelOffset,
            labelView
        ))
            return false;

        FrameGraphNodePayload& node = outPayload.nodes.emplace_back(arena);
        node.name = Name(encodedNode.nameHash);
        node.label.assign(labelView.data(), labelView.size());
        node.kind = static_cast<FrameGraphNodeKind::Enum>(encodedNode.kind);
        node.flags = encodedNode.flags;
    }

    for(u32 edgeIndex = 0u; edgeIndex < legacyHeader.edgeCount; ++edgeIndex){
        EncodedFrameGraphEdge encodedEdge;
        if(!ReadPOD(encoded, cursor, encodedEdge))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedEdge(encodedEdge, legacyHeader.nodeCount))
            return false;

        FrameGraphEdgePayload& edge = outPayload.edges.emplace_back();
        edge.fromNodeIndex = encodedEdge.fromNodeIndex;
        edge.toNodeIndex = encodedEdge.toNodeIndex;
        edge.kind = static_cast<FrameGraphEdgeKind::Enum>(encodedEdge.kind);
        edge.flags = encodedEdge.flags;
    }

    u32 previousNodeIndex = 0u;
    for(u32 assignmentIndex = 0u; assignmentIndex < queueAssignmentCount; ++assignmentIndex){
        EncodedFrameGraphQueueAssignment encodedAssignment;
        if(!ReadPOD(encoded, cursor, encodedAssignment))
            return false;
        if(
            encodedAssignment.nodeIndex >= legacyHeader.nodeCount
            || (assignmentIndex != 0u && encodedAssignment.nodeIndex <= previousNodeIndex)
            || outPayload.nodes[encodedAssignment.nodeIndex].kind != FrameGraphNodeKind::Pass
            || !__hidden_telemetry_frame_graph::DecodeQueueAssignment(
                encodedAssignment,
                outPayload.nodes[encodedAssignment.nodeIndex].queueAssignment
            )
        )
            return false;
        previousNodeIndex = encodedAssignment.nodeIndex;
    }

    previousNodeIndex = 0u;
    for(u32 compiledTaskIndex = 0u; compiledTaskIndex < compiledTaskCount; ++compiledTaskIndex){
        EncodedFrameGraphCompiledTask encodedCompiledTask;
        if(!ReadPOD(encoded, cursor, encodedCompiledTask))
            return false;
        if(
            encodedCompiledTask.nodeIndex >= legacyHeader.nodeCount
            || (compiledTaskIndex != 0u && encodedCompiledTask.nodeIndex <= previousNodeIndex)
            || outPayload.nodes[encodedCompiledTask.nodeIndex].kind != FrameGraphNodeKind::Pass
            || !__hidden_telemetry_frame_graph::DecodeCompiledTask(
                encodedCompiledTask,
                outPayload.nodes[encodedCompiledTask.nodeIndex].compiledTask
            )
        )
            return false;
        previousNodeIndex = encodedCompiledTask.nodeIndex;
    }

    return cursor == stringTableOffset;
}

bool RecordFrameGraph(
    Recorder& recorder,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const u32 streamId
){
    return Detail::RecordBuiltPayload(
        recorder,
        EventKind::FrameGraphFrame,
        frameIndex,
        streamId,
        [frameIndex, &nodes, &edges](TelemetryArena& arena, TelemetryBytes& payload){
            return BuildFrameGraphPayload(arena, frameIndex, nodes, edges, payload);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

