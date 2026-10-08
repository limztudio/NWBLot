// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/expected.h>
#include "recorder.h"


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

struct FrameGraphNodePayload{
    Name name = s_NameNone;
    AString<TelemetryArena> label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    FrameGraphQueueAssignment queueAssignment;
    FrameGraphCompiledTask compiledTask;
    FrameGraphRuntimeStatistics runtimeStatistics;

    explicit FrameGraphNodePayload(TelemetryArena& arena)
        : label(arena)
    {}
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
    bool packetSubmissionStatisticsPresent = false;

    explicit FrameGraphPayload(TelemetryArena& arena)
        : nodes(arena)
        , edges(arena)
        , physicalQueueRuntimeStatistics(arena)
        , packetSubmissionStatistics(arena)
    {}
};

using FrameGraphNodeDescs = Vector<FrameGraphNodeDesc, TelemetryArena>;
using FrameGraphEdgeDescs = Vector<FrameGraphEdgeDesc, TelemetryArena>;
using FrameGraphPhysicalQueueRuntimeStatisticsRecords =
    Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, TelemetryArena>
;
using FrameGraphPacketSubmissionStatisticsRecords =
    Vector<FrameGraphPacketSubmissionStatisticsRecord, TelemetryArena>
;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsValidFrameGraphNodeKind(FrameGraphNodeKind::Enum kind)noexcept;
[[nodiscard]] bool IsValidFrameGraphEdgeKind(FrameGraphEdgeKind::Enum kind)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueClass(FrameGraphQueueClass::Enum queueClass)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignmentReason(FrameGraphQueueAssignmentReason::Enum reason)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignmentAcceptance(FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept;
[[nodiscard]] bool IsValidFrameGraphTaskPacketizationDecision(FrameGraphTaskPacketizationDecision::Enum decision)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignment(const FrameGraphQueueAssignment& assignment)noexcept;
[[nodiscard]] bool IsValidFrameGraphCompiledTask(const FrameGraphCompiledTask& compiledTask)noexcept;
[[nodiscard]] bool IsValidFrameGraphRuntimeStatistics(const FrameGraphRuntimeStatistics& statistics)noexcept;
[[nodiscard]] bool IsValidFrameGraphPhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics
)noexcept;
[[nodiscard]] bool IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics,
    const FrameGraphRuntimeStatistics& ownerStatistics
)noexcept;
[[nodiscard]] bool IsValidFrameGraphPacketSubmissionStatistics(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics
)noexcept;
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    TelemetryBytes& outPayload
);
[[nodiscard]] Expected<FrameGraphPayload> ParseFrameGraphPayload(TelemetryArena& arena, const void* payload, usize payloadBytes);
[[nodiscard]] bool RecordFrameGraph(
    Recorder& recorder,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    u32 streamId = 0u
);
[[nodiscard]] bool RecordFrameGraph(
    Recorder& recorder,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    u32 streamId = 0u
);
[[nodiscard]] bool RecordFrameGraph(
    Recorder& recorder,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    u32 streamId = 0u
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

