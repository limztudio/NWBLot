// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "report.h"

#include <logger/telemetry/report_values.adhd.h>

#include <core/telemetry/frame_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryReportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_UnknownReportField = ::NWB::Log::s_LogTelemetryReportDotUnknownReportField;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameGraphReportRecord{
    u32 streamId = 0u;
    Core::Telemetry::FrameGraphPayload payload;


public:
    explicit FrameGraphReportRecord(Core::Telemetry::TelemetryArena& arena)noexcept
        : payload(arena)
    {}
};

using FrameGraphReportRecords = Vector<FrameGraphReportRecord, Core::Alloc::ScratchArena>;

struct FrameGraphOwnerStatisticsRange{
    usize physicalQueueBegin = 0u;
    usize physicalQueueEnd = 0u;
    usize packetSubmissionBegin = 0u;
    usize packetSubmissionEnd = 0u;


public:
    // Parsed report graphs have strictly owner-sorted tables. Visit their nodes in ascending order once per output.
    void advance(const Core::Telemetry::FrameGraphPayload& graph, const u32 ownerNodeIndex)noexcept;
};

struct GraphTimingKey{
    u64 frameIndex = 0u;
    Name scopeName = s_NameNone;
};

inline b8 operator==(const GraphTimingKey& lhs, const GraphTimingKey& rhs)noexcept{
    return lhs.frameIndex == rhs.frameIndex && lhs.scopeName == rhs.scopeName;
}

struct GraphTimingKeyHasher{
    usize operator()(const GraphTimingKey& key)const noexcept;
};

using GraphTimingMap = HashMap<GraphTimingKey, f64, Core::Alloc::ScratchArena, GraphTimingKeyHasher, EqualTo<GraphTimingKey>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView EventKindText(Core::Telemetry::EventKind::Enum kind)noexcept;
[[nodiscard]] AStringView FrameGraphNodeKindText(const Core::Telemetry::FrameGraphNodeKind::Enum kind)noexcept;
[[nodiscard]] AStringView FrameGraphEdgeLabel(const Core::Telemetry::FrameGraphEdgeKind::Enum kind)noexcept;
[[nodiscard]] AStringView FrameGraphQueueClassText(const Core::Telemetry::FrameGraphQueueClass::Enum queueClass)noexcept;
[[nodiscard]] AStringView FrameGraphQueueAssignmentReasonText(const Core::Telemetry::FrameGraphQueueAssignmentReason::Enum reason)noexcept;
[[nodiscard]] AStringView FrameGraphQueueAssignmentAcceptanceText(const Core::Telemetry::FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept;
[[nodiscard]] AStringView FrameGraphTaskPacketizationDecisionText(const Core::Telemetry::FrameGraphTaskPacketizationDecision::Enum decision)noexcept;
void BuildTimedGraphsDot(Span<const FrameGraphReportRecord> graphs, const GraphTimingMap& timing, Core::Alloc::ScratchArena& scratchArena, AString<Core::Telemetry::TelemetryArena>& out);
void BuildJson(const TelemetryReportSummary& summary, Span<const FrameGraphReportRecord> graphs, const AStringView memoryRecords, AString<Core::Telemetry::TelemetryArena>& out);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

