
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

[[nodiscard]] static bool ValidateHeader(const EncodedFrameGraphPayloadHeader& header)noexcept{
    return header.magic == s_FrameGraphPayloadMagic
        && header.reserved == 0u
    ;
}

[[nodiscard]] static bool ValidateNodeInput(const FrameGraphNodeDesc& node)noexcept{
    return static_cast<bool>(node.name)
        && IsValidFrameGraphNodeKind(node.kind)
        && IsValidStringTableText(node.label)
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

bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(
        !FitsU32(nodes.size())
        || !FitsU32(edges.size())
    )
        return false;

    usize stringTableBytes = 0u;
    for(const FrameGraphNodeDesc& node : nodes){
        if(!__hidden_telemetry_frame_graph::ValidateNodeInput(node))
            return false;
        if(!AddStringTableTextReserveBytes(stringTableBytes, node.label))
            return false;
    }

    for(const FrameGraphEdgeDesc& edge : edges){
        if(!__hidden_telemetry_frame_graph::ValidateEdgeInput(edge, nodes.size()))
            return false;
    }

    usize payloadBytes = sizeof(EncodedFrameGraphPayloadHeader);
    if(
        !AddBinaryRepeatedReserveBytes(payloadBytes, nodes.size(), sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, edges.size(), sizeof(EncodedFrameGraphEdge))
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

    EncodedFrameGraphPayloadHeader header;
    header.frameIndex = frameIndex;
    header.nodeCount = static_cast<u32>(nodes.size());
    header.edgeCount = static_cast<u32>(edges.size());
    header.stringTableBytes = static_cast<u32>(stringTable.size());

    outPayload.reserve(payloadBytes);
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

    EncodedFrameGraphPayloadHeader header;
    if(!ReadPOD(encoded, cursor, header))
        return false;
    if(!__hidden_telemetry_frame_graph::ValidateHeader(header))
        return false;

    usize expectedBytes = sizeof(EncodedFrameGraphPayloadHeader);
    if(
        !AddBinaryRepeatedReserveBytes(expectedBytes, header.nodeCount, sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, header.edgeCount, sizeof(EncodedFrameGraphEdge))
        || !AddBinaryReserveBytes(expectedBytes, header.stringTableBytes)
        || expectedBytes != payloadBytes
    )
        return false;

    usize edgeOffset = sizeof(EncodedFrameGraphPayloadHeader);
    if(!AddBinaryRepeatedReserveBytes(edgeOffset, header.nodeCount, sizeof(EncodedFrameGraphNode)))
        return false;

    usize stringTableOffset = edgeOffset;
    if(!AddBinaryRepeatedReserveBytes(stringTableOffset, header.edgeCount, sizeof(EncodedFrameGraphEdge)))
        return false;

    outPayload.frameIndex = header.frameIndex;
    outPayload.nodes.reserve(header.nodeCount);
    outPayload.edges.reserve(header.edgeCount);

    for(u32 nodeIndex = 0u; nodeIndex < header.nodeCount; ++nodeIndex){
        EncodedFrameGraphNode encodedNode;
        if(!ReadPOD(encoded, cursor, encodedNode))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedNode(encodedNode))
            return false;

        AStringView labelView;
        if(!BinaryDetail::ReadStringTableTextView(
            encoded,
            stringTableOffset,
            header.stringTableBytes,
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

    for(u32 edgeIndex = 0u; edgeIndex < header.edgeCount; ++edgeIndex){
        EncodedFrameGraphEdge encodedEdge;
        if(!ReadPOD(encoded, cursor, encodedEdge))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedEdge(encodedEdge, header.nodeCount))
            return false;

        FrameGraphEdgePayload& edge = outPayload.edges.emplace_back();
        edge.fromNodeIndex = encodedEdge.fromNodeIndex;
        edge.toNodeIndex = encodedEdge.toNodeIndex;
        edge.kind = static_cast<FrameGraphEdgeKind::Enum>(encodedEdge.kind);
        edge.flags = encodedEdge.flags;
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

