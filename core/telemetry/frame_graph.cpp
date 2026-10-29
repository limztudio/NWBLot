// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include "frame_graph_queue_codec_internal.h"
#include "frame_graph_statistics_codec_internal.h"

#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/binary.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 BuildFrameGraphPayloadImpl(
    Alloc::ScratchArena& scratchArena,
    u64 frameIndex,
    Span<const FrameGraphNodeDesc> nodes,
    Span<const FrameGraphEdgeDesc> edges,
    Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const Span<const FrameGraphPacketSubmissionStatisticsRecord>* packetSubmissionStatistics,
    TelemetryBytes& outPayload
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsValidStringTableText(const AStringView text)noexcept{
    return !text.empty() && !HasEmbeddedNull(text) && text.size() < static_cast<usize>(Limit<u32>::s_Max);
}


[[nodiscard]] static b8 ValidateHeader(const u32 magic, const u16 reserved)noexcept{
    return magic == s_FrameGraphPayloadMagic && reserved == 0u;
}


[[nodiscard]] static b8 ValidateNodeInput(const FrameGraphNodeDesc& node)noexcept{
    return
        static_cast<b8>(node.name)
        && IsValidFrameGraphNodeKind(node.kind)
        && IsValidStringTableText(node.label)
        && (!node.queueAssignment.present || (node.kind == FrameGraphNodeKind::Pass && IsValidFrameGraphQueueAssignment(node.queueAssignment)))
        && (!node.compiledTask.present || (node.kind == FrameGraphNodeKind::Pass && IsValidFrameGraphCompiledTask(node.compiledTask)))
        && (!node.runtimeStatistics.present || (node.kind == FrameGraphNodeKind::Pass && IsValidFrameGraphRuntimeStatistics(node.runtimeStatistics)))
    ;
}

[[nodiscard]] static b8 ValidateEdgeInput(const FrameGraphEdgeDesc& edge, const usize nodeCount)noexcept{
    return IsValidFrameGraphEdgeKind(edge.kind) && static_cast<usize>(edge.fromNodeIndex) < nodeCount && static_cast<usize>(edge.toNodeIndex) < nodeCount;
}

[[nodiscard]] static b8 ValidateEncodedNode(const EncodedFrameGraphNode& node)noexcept{
    return node.reserved == 0u && !NameDetail::IsZeroHash(node.nameHash) && IsValidFrameGraphNodeKind(static_cast<FrameGraphNodeKind::Enum>(node.kind));
}

[[nodiscard]] static b8 ValidateEncodedEdge(const EncodedFrameGraphEdge& edge, const usize nodeCount)noexcept{
    return edge.reserved == 0u && IsValidFrameGraphEdgeKind(static_cast<FrameGraphEdgeKind::Enum>(edge.kind)) && static_cast<usize>(edge.fromNodeIndex) < nodeCount && static_cast<usize>(edge.toNodeIndex) < nodeCount;
}


[[nodiscard]] static EncodedFrameGraphCompiledTask EncodeCompiledTask(const u32 nodeIndex, const FrameGraphCompiledTask& compiledTask)noexcept{
    EncodedFrameGraphCompiledTask encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.packetIndex = compiledTask.packetIndex;
    encoded.planGeneration = compiledTask.planGeneration;
    encoded.packetizationDecision = compiledTask.packetizationDecision;
    return encoded;
}

[[nodiscard]] static Expected<FrameGraphCompiledTask> DecodeCompiledTask(const EncodedFrameGraphCompiledTask& encoded)noexcept{
    FrameGraphCompiledTask decoded;
    if(encoded.reserved[0u] != 0u || encoded.reserved[1u] != 0u || encoded.reserved[2u] != 0u)
        return MakeUnexpected(Failure{});

    decoded.planGeneration = encoded.planGeneration;
    decoded.packetIndex = encoded.packetIndex;
    decoded.packetizationDecision = static_cast<FrameGraphTaskPacketizationDecision::Enum>(encoded.packetizationDecision);
    decoded.present = true;
    if(!IsValidFrameGraphCompiledTask(decoded))
        return MakeUnexpected(Failure{});
    return decoded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildFrameGraphPayload(Alloc::ScratchArena& scratchArena, const u64 frameIndex, const Span<const FrameGraphNodeDesc> nodes, const Span<const FrameGraphEdgeDesc> edges, TelemetryBytes& outPayload){
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(scratchArena, frameIndex, nodes, edges, Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord>{}, nullptr, outPayload);
}

b8 BuildFrameGraphPayload(
    Alloc::ScratchArena& scratchArena,
    const u64 frameIndex,
    const Span<const FrameGraphNodeDesc> nodes,
    const Span<const FrameGraphEdgeDesc> edges,
    const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    TelemetryBytes& outPayload
){
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(scratchArena, frameIndex, nodes, edges, physicalQueueRuntimeStatistics, nullptr, outPayload);
}

b8 BuildFrameGraphPayload(
    Alloc::ScratchArena& scratchArena,
    const u64 frameIndex,
    const Span<const FrameGraphNodeDesc> nodes,
    const Span<const FrameGraphEdgeDesc> edges,
    const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const Span<const FrameGraphPacketSubmissionStatisticsRecord> packetSubmissionStatistics,
    TelemetryBytes& outPayload
){
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(scratchArena, frameIndex, nodes, edges, physicalQueueRuntimeStatistics, &packetSubmissionStatistics, outPayload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildFrameGraphPayloadImpl(
    Alloc::ScratchArena& scratchArena,
    const u64 frameIndex,
    const Span<const FrameGraphNodeDesc> nodes,
    const Span<const FrameGraphEdgeDesc> edges,
    const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const Span<const FrameGraphPacketSubmissionStatisticsRecord>* const packetSubmissionStatistics,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(!FitsU32(nodes.size()) || !FitsU32(edges.size()) || !FitsU32(physicalQueueRuntimeStatistics.size()) || (packetSubmissionStatistics && !FitsU32(packetSubmissionStatistics->size())))
        return false;

    Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, Alloc::ScratchArena> orderedPhysicalQueueRuntimeStatistics(scratchArena);
    orderedPhysicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatistics.size());
    for(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record : physicalQueueRuntimeStatistics)
        orderedPhysicalQueueRuntimeStatistics.push_back(record);
    Sort(orderedPhysicalQueueRuntimeStatistics.begin(), orderedPhysicalQueueRuntimeStatistics.end(), FrameGraphStatisticsCodecDetail::PhysicalQueueRuntimeStatisticsRecordLess);

    Vector<FrameGraphPacketSubmissionStatisticsRecord, Alloc::ScratchArena> orderedPacketSubmissionStatistics(scratchArena);
    if(packetSubmissionStatistics){
        orderedPacketSubmissionStatistics.reserve(packetSubmissionStatistics->size());
        for(const FrameGraphPacketSubmissionStatisticsRecord& record : *packetSubmissionStatistics)
            orderedPacketSubmissionStatistics.push_back(record);
        Sort(orderedPacketSubmissionStatistics.begin(), orderedPacketSubmissionStatistics.end(), FrameGraphStatisticsCodecDetail::PacketSubmissionStatisticsRecordLess);
    }

    usize stringTableBytes = 0u;
    usize queueAssignmentCount = 0u;
    usize compiledTaskCount = 0u;
    usize runtimeStatisticsCount = 0u;
    for(const FrameGraphNodeDesc& node : nodes){
        if(!__hidden_telemetry_frame_graph::ValidateNodeInput(node))
            return false;
        if(!AddStringTableTextReserveBytes(stringTableBytes, node.label))
            return false;
        if(node.queueAssignment.present)
            ++queueAssignmentCount;
        if(node.compiledTask.present)
            ++compiledTaskCount;
        if(node.runtimeStatistics.present)
            ++runtimeStatisticsCount;
    }
    if(!FitsU32(queueAssignmentCount) || !FitsU32(compiledTaskCount) || !FitsU32(runtimeStatisticsCount))
        return false;

    for(const FrameGraphEdgeDesc& edge : edges){
        if(!__hidden_telemetry_frame_graph::ValidateEdgeInput(edge, nodes.size()))
            return false;
    }

    u32 accumulatedOwnerNodeIndex = Limit<u32>::s_Max;
    FrameGraphStatisticsCodecDetail::FrameGraphPhysicalQueueRuntimeStatisticsAccumulator accumulatedStatistics;
    for(usize statisticsIndex = 0u; statisticsIndex < orderedPhysicalQueueRuntimeStatistics.size(); ++statisticsIndex){
        const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record = orderedPhysicalQueueRuntimeStatistics[statisticsIndex];
        if(record.ownerNodeIndex >= nodes.size())
            return false;
        if(!FrameGraphStatisticsCodecDetail::ValidatePhysicalQueueRuntimeStatisticsOwner(record, nodes.size(), nodes[record.ownerNodeIndex].kind, nodes[record.ownerNodeIndex].runtimeStatistics))
            return false;
        if(record.ownerNodeIndex != accumulatedOwnerNodeIndex){
            accumulatedOwnerNodeIndex = record.ownerNodeIndex;
            accumulatedStatistics = {};
        }
        if(!FrameGraphStatisticsCodecDetail::AccumulatePhysicalQueueRuntimeStatistics(record.statistics, nodes[record.ownerNodeIndex].runtimeStatistics, accumulatedStatistics))
            return false;
        if(statisticsIndex != 0u && !FrameGraphStatisticsCodecDetail::PhysicalQueueRuntimeStatisticsRecordLess(orderedPhysicalQueueRuntimeStatistics[statisticsIndex - 1u], record))
            return false;
    }

    const b8 hasPacketSubmissionStatistics = packetSubmissionStatistics != nullptr;
    if(hasPacketSubmissionStatistics){
        if(!FrameGraphStatisticsCodecDetail::ValidatePacketSubmissionStatisticsTable(scratchArena, nodes, orderedPhysicalQueueRuntimeStatistics, orderedPacketSubmissionStatistics))
            return false;
    }

    usize payloadBytes = sizeof(EncodedFrameGraphPayloadHeader);
    if(
        !AddBinaryRepeatedReserveBytes(payloadBytes, nodes.size(), sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, edges.size(), sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, queueAssignmentCount, sizeof(EncodedFrameGraphQueueAssignment))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, compiledTaskCount, sizeof(EncodedFrameGraphCompiledTask))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, runtimeStatisticsCount, sizeof(EncodedFrameGraphRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, orderedPhysicalQueueRuntimeStatistics.size(), sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, orderedPacketSubmissionStatistics.size(), sizeof(EncodedFrameGraphPacketSubmissionStatistics))
        || !AddBinaryReserveBytes(payloadBytes, stringTableBytes)
    )
        return false;

    Vector<u8, Alloc::ScratchArena> stringTable(scratchArena);
    stringTable.reserve(stringTableBytes);
    Vector<EncodedFrameGraphNode, Alloc::ScratchArena> encodedNodes(scratchArena);
    encodedNodes.reserve(nodes.size());

    for(const FrameGraphNodeDesc& node : nodes){
        const auto labelOffset = AppendStringTableText(stringTable, node.label);
        if(!labelOffset)
            return false;

        EncodedFrameGraphNode encodedNode;
        encodedNode.nameHash = node.name.hash();
        encodedNode.labelOffset = *labelOffset;
        encodedNode.kind = node.kind;
        encodedNode.flags = node.flags;
        encodedNodes.push_back(encodedNode);
    }

    outPayload.reserve(payloadBytes);
    EncodedFrameGraphPayloadHeader header;
    header.frameIndex = frameIndex;
    header.nodeCount = static_cast<u32>(nodes.size());
    header.edgeCount = static_cast<u32>(edges.size());
    header.stringTableBytes = static_cast<u32>(stringTable.size());
    header.queueAssignmentCount = static_cast<u32>(queueAssignmentCount);
    header.compiledTaskCount = static_cast<u32>(compiledTaskCount);
    header.runtimeStatisticsCount = static_cast<u32>(runtimeStatisticsCount);
    header.physicalQueueRuntimeStatisticsCount = static_cast<u32>(orderedPhysicalQueueRuntimeStatistics.size());
    header.packetSubmissionStatisticsCount = static_cast<u32>(orderedPacketSubmissionStatistics.size());
    header.packetSubmissionStatisticsPresent = hasPacketSubmissionStatistics ? 1u : 0u;
    AppendPOD(outPayload, header);
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
            AppendPOD(outPayload, FrameGraphQueueCodecDetail::EncodeQueueAssignment(nodeIndex, nodes[nodeIndex].queueAssignment));
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].compiledTask.present)
            AppendPOD(outPayload, __hidden_telemetry_frame_graph::EncodeCompiledTask(nodeIndex, nodes[nodeIndex].compiledTask));
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].runtimeStatistics.present)
            AppendPOD(outPayload, FrameGraphStatisticsCodecDetail::EncodeRuntimeStatistics(nodeIndex, nodes[nodeIndex].runtimeStatistics));
    }
    for(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record : orderedPhysicalQueueRuntimeStatistics){
        AppendPOD(outPayload, FrameGraphStatisticsCodecDetail::EncodePhysicalQueueRuntimeStatistics(record));
    }
    for(const FrameGraphPacketSubmissionStatisticsRecord& statistics : orderedPacketSubmissionStatistics)
        AppendPOD(outPayload, FrameGraphStatisticsCodecDetail::EncodePacketSubmissionStatistics(statistics));
    if(!stringTable.empty())
        AppendBytesNoReserveUnchecked(outPayload, stringTable.data(), stringTable.size());

    return outPayload.size() == payloadBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphPayload> ParseFrameGraphPayload(TelemetryArena& arena, Alloc::ScratchArena& scratchArena, const void* const payload, const usize payloadBytes){
    FrameGraphPayload parsedPayload(arena);

    if(payloadBytes < sizeof(EncodedFrameGraphPayloadHeader) || !payload)
        return MakeUnexpected(Failure{});

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    const auto decodedHeader = ReadPOD<EncodedFrameGraphPayloadHeader>(encoded, cursor);
    if(!decodedHeader)
        return MakeUnexpected(Failure{});
    const EncodedFrameGraphPayloadHeader& header = *decodedHeader;
    if(
        header.version != s_FrameGraphPayloadVersion
        || !__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved)
        || header.packetSubmissionStatisticsPresent > 1u
        || header.reservedTail[0u] != 0u
        || header.reservedTail[1u] != 0u
        || header.reservedTail[2u] != 0u
        || (header.packetSubmissionStatisticsPresent == 0u && header.packetSubmissionStatisticsCount != 0u)
    )
        return MakeUnexpected(Failure{});

    const usize headerBytes = sizeof(EncodedFrameGraphPayloadHeader);
    const u32 queueAssignmentCount = header.queueAssignmentCount;
    const u32 compiledTaskCount = header.compiledTaskCount;
    const u32 runtimeStatisticsCount = header.runtimeStatisticsCount;
    const u32 physicalQueueRuntimeStatisticsCount = header.physicalQueueRuntimeStatisticsCount;
    const u32 packetSubmissionStatisticsCount = header.packetSubmissionStatisticsCount;
    const b8 packetSubmissionStatisticsPresent = header.packetSubmissionStatisticsPresent != 0u;
    parsedPayload.packetSubmissionStatisticsPresent = packetSubmissionStatisticsPresent;
    if(queueAssignmentCount > header.nodeCount || compiledTaskCount > header.nodeCount || runtimeStatisticsCount > header.nodeCount)
        return MakeUnexpected(Failure{});

    usize expectedBytes = headerBytes;
    if(
        !AddBinaryRepeatedReserveBytes(expectedBytes, header.nodeCount, sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, header.edgeCount, sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, queueAssignmentCount, sizeof(EncodedFrameGraphQueueAssignment))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, compiledTaskCount, sizeof(EncodedFrameGraphCompiledTask))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, runtimeStatisticsCount, sizeof(EncodedFrameGraphRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, physicalQueueRuntimeStatisticsCount, sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, packetSubmissionStatisticsCount, sizeof(EncodedFrameGraphPacketSubmissionStatistics))
        || !AddBinaryReserveBytes(expectedBytes, header.stringTableBytes)
        || expectedBytes != payloadBytes
    )
        return MakeUnexpected(Failure{});

    usize edgeOffset = headerBytes;
    if(!AddBinaryRepeatedReserveBytes(edgeOffset, header.nodeCount, sizeof(EncodedFrameGraphNode)))
        return MakeUnexpected(Failure{});

    usize queueAssignmentOffset = edgeOffset;
    if(!AddBinaryRepeatedReserveBytes(queueAssignmentOffset, header.edgeCount, sizeof(EncodedFrameGraphEdge)))
        return MakeUnexpected(Failure{});

    usize compiledTaskOffset = queueAssignmentOffset;
    if(!AddBinaryRepeatedReserveBytes(compiledTaskOffset, queueAssignmentCount, sizeof(EncodedFrameGraphQueueAssignment)))
        return MakeUnexpected(Failure{});

    usize runtimeStatisticsOffset = compiledTaskOffset;
    if(!AddBinaryRepeatedReserveBytes(runtimeStatisticsOffset, compiledTaskCount, sizeof(EncodedFrameGraphCompiledTask)))
        return MakeUnexpected(Failure{});

    usize physicalQueueRuntimeStatisticsOffset = runtimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(physicalQueueRuntimeStatisticsOffset, runtimeStatisticsCount, sizeof(EncodedFrameGraphRuntimeStatistics)))
        return MakeUnexpected(Failure{});

    usize stringTableOffset = physicalQueueRuntimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(stringTableOffset, physicalQueueRuntimeStatisticsCount, sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics)))
        return MakeUnexpected(Failure{});

    if(!AddBinaryRepeatedReserveBytes(stringTableOffset, packetSubmissionStatisticsCount, sizeof(EncodedFrameGraphPacketSubmissionStatistics)))
        return MakeUnexpected(Failure{});

    parsedPayload.frameIndex = header.frameIndex;
    parsedPayload.nodes.reserve(header.nodeCount);
    parsedPayload.edges.reserve(header.edgeCount);
    parsedPayload.physicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatisticsCount);
    parsedPayload.packetSubmissionStatistics.reserve(packetSubmissionStatisticsCount);

    for(u32 nodeIndex = 0u; nodeIndex < header.nodeCount; ++nodeIndex){
        const auto decodedEncodedNode = ReadPOD<EncodedFrameGraphNode>(encoded, cursor);
        if(!decodedEncodedNode)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphNode& encodedNode = *decodedEncodedNode;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedNode(encodedNode))
            return MakeUnexpected(Failure{});

        const auto labelView = ReadStringTableTextView(encoded, stringTableOffset, header.stringTableBytes, encodedNode.labelOffset);
        if(!labelView)
            return MakeUnexpected(Failure{});

        FrameGraphNodePayload& node = parsedPayload.nodes.emplace_back();
        node.name = Name(encodedNode.nameHash);
        node.label = *labelView;
        node.kind = static_cast<FrameGraphNodeKind::Enum>(encodedNode.kind);
        node.flags = encodedNode.flags;
    }

    for(u32 edgeIndex = 0u; edgeIndex < header.edgeCount; ++edgeIndex){
        const auto decodedEncodedEdge = ReadPOD<EncodedFrameGraphEdge>(encoded, cursor);
        if(!decodedEncodedEdge)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphEdge& encodedEdge = *decodedEncodedEdge;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedEdge(encodedEdge, header.nodeCount))
            return MakeUnexpected(Failure{});

        FrameGraphEdgePayload& edge = parsedPayload.edges.emplace_back();
        edge.fromNodeIndex = encodedEdge.fromNodeIndex;
        edge.toNodeIndex = encodedEdge.toNodeIndex;
        edge.kind = static_cast<FrameGraphEdgeKind::Enum>(encodedEdge.kind);
        edge.flags = encodedEdge.flags;
    }

    u32 previousNodeIndex = 0u;
    for(u32 assignmentIndex = 0u; assignmentIndex < queueAssignmentCount; ++assignmentIndex){
        const auto decodedEncodedAssignment = ReadPOD<EncodedFrameGraphQueueAssignment>(encoded, cursor);
        if(!decodedEncodedAssignment)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphQueueAssignment& encodedAssignment = *decodedEncodedAssignment;
        if(encodedAssignment.nodeIndex >= header.nodeCount || (assignmentIndex != 0u && encodedAssignment.nodeIndex <= previousNodeIndex) || parsedPayload.nodes[encodedAssignment.nodeIndex].kind != FrameGraphNodeKind::Pass)
            return MakeUnexpected(Failure{});
        const auto assignment = FrameGraphQueueCodecDetail::DecodeQueueAssignment(encodedAssignment);
        if(!assignment)
            return MakeUnexpected(Failure{});
        parsedPayload.nodes[encodedAssignment.nodeIndex].queueAssignment = *assignment;
        previousNodeIndex = encodedAssignment.nodeIndex;
    }

    previousNodeIndex = 0u;
    for(u32 compiledTaskIndex = 0u; compiledTaskIndex < compiledTaskCount; ++compiledTaskIndex){
        const auto decodedEncodedCompiledTask = ReadPOD<EncodedFrameGraphCompiledTask>(encoded, cursor);
        if(!decodedEncodedCompiledTask)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphCompiledTask& encodedCompiledTask = *decodedEncodedCompiledTask;
        if(encodedCompiledTask.nodeIndex >= header.nodeCount || (compiledTaskIndex != 0u && encodedCompiledTask.nodeIndex <= previousNodeIndex) || parsedPayload.nodes[encodedCompiledTask.nodeIndex].kind != FrameGraphNodeKind::Pass)
            return MakeUnexpected(Failure{});
        const auto compiledTask = __hidden_telemetry_frame_graph::DecodeCompiledTask(encodedCompiledTask);
        if(!compiledTask)
            return MakeUnexpected(Failure{});
        parsedPayload.nodes[encodedCompiledTask.nodeIndex].compiledTask = *compiledTask;
        previousNodeIndex = encodedCompiledTask.nodeIndex;
    }

    previousNodeIndex = 0u;
    for(u32 statisticsIndex = 0u; statisticsIndex < runtimeStatisticsCount; ++statisticsIndex){
        const auto decodedEncodedStatistics = ReadPOD<EncodedFrameGraphRuntimeStatistics>(encoded, cursor);
        if(!decodedEncodedStatistics)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphRuntimeStatistics& encodedStatistics = *decodedEncodedStatistics;
        const u32 nodeIndex = encodedStatistics.nodeIndex;
        const auto statistics = FrameGraphStatisticsCodecDetail::DecodeRuntimeStatistics(encodedStatistics);
        if(!statistics)
            return MakeUnexpected(Failure{});
        if(nodeIndex >= header.nodeCount || (statisticsIndex != 0u && nodeIndex <= previousNodeIndex) || parsedPayload.nodes[nodeIndex].kind != FrameGraphNodeKind::Pass)
            return MakeUnexpected(Failure{});
        parsedPayload.nodes[nodeIndex].runtimeStatistics = *statistics;
        previousNodeIndex = nodeIndex;
    }

    FrameGraphPhysicalQueueRuntimeStatisticsRecord previousPhysicalQueueStatistics;
    u32 accumulatedOwnerNodeIndex = Limit<u32>::s_Max;
    FrameGraphStatisticsCodecDetail::FrameGraphPhysicalQueueRuntimeStatisticsAccumulator accumulatedStatistics;
    for(u32 statisticsIndex = 0u; statisticsIndex < physicalQueueRuntimeStatisticsCount; ++statisticsIndex){
        const auto decodedEncodedStatistics = ReadPOD<EncodedFrameGraphPhysicalQueueRuntimeStatistics>(encoded, cursor);
        if(!decodedEncodedStatistics)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphPhysicalQueueRuntimeStatistics& encodedStatistics = *decodedEncodedStatistics;
        if(encodedStatistics.ownerNodeIndex >= parsedPayload.nodes.size())
            return MakeUnexpected(Failure{});
        const auto decodedStatistics = FrameGraphStatisticsCodecDetail::DecodePhysicalQueueRuntimeStatistics(encodedStatistics, parsedPayload.nodes[encodedStatistics.ownerNodeIndex].runtimeStatistics);
        if(!decodedStatistics)
            return MakeUnexpected(Failure{});
        const FrameGraphPhysicalQueueRuntimeStatisticsRecord& statistics = *decodedStatistics;
        if(statistics.ownerNodeIndex != accumulatedOwnerNodeIndex){
            accumulatedOwnerNodeIndex = statistics.ownerNodeIndex;
            accumulatedStatistics = {};
        }
        if(!FrameGraphStatisticsCodecDetail::AccumulatePhysicalQueueRuntimeStatistics(statistics.statistics, parsedPayload.nodes[statistics.ownerNodeIndex].runtimeStatistics, accumulatedStatistics))
            return MakeUnexpected(Failure{});
        if(!FrameGraphStatisticsCodecDetail::ValidatePhysicalQueueRuntimeStatisticsOwner(statistics, parsedPayload.nodes.size(), parsedPayload.nodes[statistics.ownerNodeIndex].kind, parsedPayload.nodes[statistics.ownerNodeIndex].runtimeStatistics))
            return MakeUnexpected(Failure{});
        if(statisticsIndex != 0u && !FrameGraphStatisticsCodecDetail::PhysicalQueueRuntimeStatisticsRecordLess(previousPhysicalQueueStatistics, statistics))
            return MakeUnexpected(Failure{});
        parsedPayload.physicalQueueRuntimeStatistics.push_back(statistics);
        previousPhysicalQueueStatistics = statistics;
    }

    for(u32 statisticsIndex = 0u; statisticsIndex < packetSubmissionStatisticsCount; ++statisticsIndex){
        const auto decodedEncodedStatistics = ReadPOD<EncodedFrameGraphPacketSubmissionStatistics>(encoded, cursor);
        if(!decodedEncodedStatistics)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphPacketSubmissionStatistics& encodedStatistics = *decodedEncodedStatistics;

        const auto statistics = FrameGraphStatisticsCodecDetail::DecodePacketSubmissionStatistics(encodedStatistics);
        if(!statistics)
            return MakeUnexpected(Failure{});
        parsedPayload.packetSubmissionStatistics.push_back(*statistics);
    }
    if(parsedPayload.packetSubmissionStatisticsPresent){
        if(!FrameGraphStatisticsCodecDetail::ValidatePacketSubmissionStatisticsTable<FrameGraphNodePayload>(scratchArena, parsedPayload.nodes, parsedPayload.physicalQueueRuntimeStatistics, parsedPayload.packetSubmissionStatistics))
            return MakeUnexpected(Failure{});
    }

    if(cursor != stringTableOffset)
        return MakeUnexpected(Failure{});
    return parsedPayload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

