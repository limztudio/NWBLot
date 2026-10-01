// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "report.h"
#include "report_dot.h"
#include "memory_report.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_JsonNullText = "null";
inline constexpr AStringView s_JsonQueueIndexFormat = "{{\"index\": {}, \"deviceGeneration\": {}}}";
inline constexpr AStringView s_JsonInitialQueueKey = "{\"initialQueue\": ";
inline constexpr AStringView s_JsonPlannedQueueKey = ", \"plannedQueue\": ";
inline constexpr AStringView s_JsonAcceptedQueueKey = ", \"acceptedQueue\": ";
inline constexpr AStringView s_JsonPreviousAcceptedQueueKey = ", \"previousAcceptedQueue\": ";
inline constexpr AStringView s_JsonQueueClassKey = ", \"queueClass\": ";
inline constexpr AStringView s_JsonReasonKey = ", \"reason\": ";
inline constexpr AStringView s_JsonAcceptanceKey = ", \"acceptance\": ";


[[nodiscard]] usize EstimateJsonReportReserve(const FrameGraphReportRecords& graphs)noexcept;
void AppendFrameGraphPhysicalQueueJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueId& queue
);
void AppendFrameGraphQueueAssignmentJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphQueueAssignment& assignment
);
void AppendFrameGraphCompiledTaskJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphCompiledTask& compiledTask
);
void AppendFrameGraphCompileRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphCompileRuntimeStatistics& statistics
);
void AppendFrameGraphRecordingRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRecordingRuntimeStatistics& statistics
);
void AppendFrameGraphSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphSubmissionRuntimeStatistics& statistics
);
void AppendFrameGraphPhysicalQueueCompileRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueCompileRuntimeStatistics& statistics
);
void AppendFrameGraphPhysicalQueueRecordingRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueRecordingRuntimeStatistics& statistics
);
void AppendFrameGraphPhysicalQueueSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueSubmissionRuntimeStatistics& statistics
);
void AppendFrameGraphPhysicalQueueRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& statistics
);
void AppendFrameGraphPacketSubmissionStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord& statistics
);
void AppendFrameGraphRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    const FrameGraphOwnerStatisticsRange& ownerRange,
    const bool physicalQueueRuntimeStatisticsPresent,
    const bool packetSubmissionStatisticsPresent
);
void AppendFrameGraphJson(
    const FrameGraphReportRecord& record,
    const usize graphIndex,
    const bool finalGraph,
    AString<TelemetryArena>& out
);
void BuildJson(
    const TelemetryReportSummary& summary,
    const FrameGraphReportRecords& graphs,
    const AStringView memoryRecords,
    AString<TelemetryArena>& out
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

