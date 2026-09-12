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
    const Telemetry::FrameGraphCompileRuntimeStatistics& statistics,
    const bool resourceVersionStatisticsPresent
);
void AppendFrameGraphRecordingRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRecordingRuntimeStatistics& statistics
);
void AppendFrameGraphSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphSubmissionRuntimeStatistics& statistics,
    const bool recoverySubmissionCountPresent
);
void AppendFrameGraphPhysicalQueueCompileRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphCompileRuntimeStatistics& statistics,
    const bool resourceVersionStatisticsPresent
);
void AppendFrameGraphPhysicalQueueRecordingRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRecordingRuntimeStatistics& statistics
);
void AppendFrameGraphPhysicalQueueSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphSubmissionRuntimeStatistics& statistics,
    const bool recoverySubmissionCountPresent
);
void AppendFrameGraphPhysicalQueueRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& statistics,
    const bool recoverySubmissionCountPresent
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
    const bool packetSubmissionStatisticsPresent,
    const bool recoverySubmissionCountPresent,
    const bool resourceVersionStatisticsPresent
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

