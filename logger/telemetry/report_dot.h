// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/global.h>

#include <core/telemetry/frame_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;
using TelemetryArena = Telemetry::TelemetryArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameGraphReportRecord{
    u32 streamId = 0u;
    Telemetry::FrameGraphPayload payload;

    explicit FrameGraphReportRecord(TelemetryArena& arena)
        : payload(arena)
    {}
};

using FrameGraphReportRecords = Vector<FrameGraphReportRecord, TelemetryArena>;

struct FrameGraphOwnerStatisticsRange{
    usize physicalQueueBegin = 0u;
    usize physicalQueueEnd = 0u;
    usize packetSubmissionBegin = 0u;
    usize packetSubmissionEnd = 0u;

    // Parsed report graphs have strictly owner-sorted tables. Visit their nodes in ascending order once per output.
    void advance(const Telemetry::FrameGraphPayload& graph, const u32 ownerNodeIndex)noexcept;
};

struct GraphTimingKey{
    u64 frameIndex = 0u;
    Name scopeName = NAME_NONE;
};

inline bool operator==(const GraphTimingKey& lhs, const GraphTimingKey& rhs)noexcept{
    return lhs.frameIndex == rhs.frameIndex && lhs.scopeName == rhs.scopeName;
}

struct GraphTimingKeyHasher{
    usize operator()(const GraphTimingKey& key)const noexcept;
};

using GraphTimingMap = HashMap<GraphTimingKey, f64, GraphTimingKeyHasher, EqualTo<GraphTimingKey>, TelemetryArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView FrameGraphNodeIdentityText(const Telemetry::FrameGraphNodePayload& node, char (&identityText)[NameDetail::s_DebugHashTextLength + 1u])noexcept;
[[nodiscard]] const char* FrameGraphNodeShape(const Telemetry::FrameGraphNodeKind::Enum kind)noexcept;
[[nodiscard]] const char* FrameGraphNodeKindText(const Telemetry::FrameGraphNodeKind::Enum kind)noexcept;
[[nodiscard]] const char* FrameGraphEdgeLabel(const Telemetry::FrameGraphEdgeKind::Enum kind)noexcept;
[[nodiscard]] const char* FrameGraphQueueClassText(const Telemetry::FrameGraphQueueClass::Enum queueClass)noexcept;
[[nodiscard]] const char* FrameGraphQueueAssignmentReasonText(const Telemetry::FrameGraphQueueAssignmentReason::Enum reason)noexcept;
[[nodiscard]] const char* FrameGraphQueueAssignmentAcceptanceText(const Telemetry::FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept;
[[nodiscard]] const char* FrameGraphTaskPacketizationDecisionText(const Telemetry::FrameGraphTaskPacketizationDecision::Enum decision)noexcept;
[[nodiscard]] usize EstimateTimedGraphDotReserve(const Telemetry::FrameGraphPayload& graph)noexcept;
[[nodiscard]] usize EstimateTimedGraphsDotReserve(const FrameGraphReportRecords& graphs)noexcept;
void BuildTimedGraphsDot(TelemetryArena& arena, const FrameGraphReportRecords& graphs, const GraphTimingMap& timing, AString<TelemetryArena>& out);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

