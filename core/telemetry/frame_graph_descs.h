// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph_encoded.h"
#include "recorder.h"

#include <core/alloc/scratch.h>

#include <global/expected.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameGraphNodeDesc{
    Name name = s_NameNone;
    AStringView label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    FrameGraphQueueAssignment queueAssignment;
    FrameGraphCompiledTask compiledTask;
    FrameGraphRuntimeStatistics runtimeStatistics;
};

struct FrameGraphEdgeDesc{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    FrameGraphEdgeKind::Enum kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
};

// label borrows the parsed payload string table; keep the payload bytes alive and unchanged while reading.
struct FrameGraphNodePayload{
    Name name = s_NameNone;
    AStringView label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    FrameGraphQueueAssignment queueAssignment;
    FrameGraphCompiledTask compiledTask;
    FrameGraphRuntimeStatistics runtimeStatistics;
};

struct FrameGraphEdgePayload{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    FrameGraphEdgeKind::Enum kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
};

struct FrameGraphPayload{
    Vector<FrameGraphNodePayload, TelemetryArena> nodes;
    Vector<FrameGraphEdgePayload, TelemetryArena> edges;
    Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, TelemetryArena> physicalQueueRuntimeStatistics;
    Vector<FrameGraphPacketSubmissionStatisticsRecord, TelemetryArena> packetSubmissionStatistics;
    u64 frameIndex = 0u;
    b8 packetSubmissionStatisticsPresent = false;


public:
    explicit FrameGraphPayload(TelemetryArena& arena)noexcept
        : nodes(arena)
        , edges(arena)
        , physicalQueueRuntimeStatistics(arena)
        , packetSubmissionStatistics(arena)
    {}
};

using FrameGraphNodeDescs = Vector<FrameGraphNodeDesc, TelemetryArena>;
using FrameGraphEdgeDescs = Vector<FrameGraphEdgeDesc, TelemetryArena>;
using FrameGraphPhysicalQueueRuntimeStatisticsRecords = Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, TelemetryArena>;
using FrameGraphPacketSubmissionStatisticsRecords = Vector<FrameGraphPacketSubmissionStatisticsRecord, TelemetryArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsValidFrameGraphNodeKind(FrameGraphNodeKind::Enum kind)noexcept;
[[nodiscard]] b8 IsValidFrameGraphEdgeKind(FrameGraphEdgeKind::Enum kind)noexcept;
[[nodiscard]] b8 IsValidFrameGraphQueueClass(FrameGraphQueueClass::Enum queueClass)noexcept;
[[nodiscard]] b8 IsValidFrameGraphQueueAssignmentReason(FrameGraphQueueAssignmentReason::Enum reason)noexcept;
[[nodiscard]] b8 IsValidFrameGraphQueueAssignmentAcceptance(FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept;
[[nodiscard]] b8 IsValidFrameGraphTaskPacketizationDecision(FrameGraphTaskPacketizationDecision::Enum decision)noexcept;
[[nodiscard]] b8 IsValidFrameGraphQueueAssignment(const FrameGraphQueueAssignment& assignment)noexcept;
[[nodiscard]] b8 IsValidFrameGraphCompiledTask(const FrameGraphCompiledTask& compiledTask)noexcept;
[[nodiscard]] b8 IsValidFrameGraphRuntimeStatistics(const FrameGraphRuntimeStatistics& statistics)noexcept;
[[nodiscard]] b8 IsValidFrameGraphPhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept;
[[nodiscard]] b8 IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(const FrameGraphPhysicalQueueRuntimeStatistics& statistics, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept;
[[nodiscard]] b8 IsValidFrameGraphPacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics)noexcept;
[[nodiscard]] b8 BuildFrameGraphPayload(u64 frameIndex, Span<const FrameGraphNodeDesc> nodes, Span<const FrameGraphEdgeDesc> edges, Alloc::ScratchArena& scratchArena, TelemetryBytes& outPayload);
[[nodiscard]] b8 BuildFrameGraphPayload(
    u64 frameIndex,
    Span<const FrameGraphNodeDesc> nodes,
    Span<const FrameGraphEdgeDesc> edges,
    Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    Alloc::ScratchArena& scratchArena,
    TelemetryBytes& outPayload
);
[[nodiscard]] b8 BuildFrameGraphPayload(
    u64 frameIndex,
    Span<const FrameGraphNodeDesc> nodes,
    Span<const FrameGraphEdgeDesc> edges,
    Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    Span<const FrameGraphPacketSubmissionStatisticsRecord> packetSubmissionStatistics,
    Alloc::ScratchArena& scratchArena,
    TelemetryBytes& outPayload
);
[[nodiscard]] Expected<FrameGraphPayload> ParseFrameGraphPayload(TelemetryArena& arena, Alloc::ScratchArena& scratchArena, const void* payload, usize payloadBytes);
[[nodiscard]] inline b8 RecordFrameGraph(Recorder& recorder, Alloc::ScratchArena& scratchArena, const u64 frameIndex, const Span<const FrameGraphNodeDesc> nodes, const Span<const FrameGraphEdgeDesc> edges, const u32 streamId = 0u){
    return recorder.recordBuiltPayload(EventKind::FrameGraphFrame, frameIndex, streamId, [frameIndex, &scratchArena, nodes, edges](TelemetryArena&, TelemetryBytes& payload){
        return BuildFrameGraphPayload(frameIndex, nodes, edges, scratchArena, payload);
    });
}
[[nodiscard]] inline b8 RecordFrameGraph(
    Recorder& recorder,
    Alloc::ScratchArena& scratchArena,
    const u64 frameIndex,
    const Span<const FrameGraphNodeDesc> nodes,
    const Span<const FrameGraphEdgeDesc> edges,
    const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const Span<const FrameGraphPacketSubmissionStatisticsRecord> packetSubmissionStatistics,
    const u32 streamId = 0u
){
    return recorder.recordBuiltPayload(EventKind::FrameGraphFrame, frameIndex, streamId, [frameIndex, &scratchArena, &nodes, &edges, &physicalQueueRuntimeStatistics, &packetSubmissionStatistics](TelemetryArena&, TelemetryBytes& payload){
        return BuildFrameGraphPayload(frameIndex, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, scratchArena, payload);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

