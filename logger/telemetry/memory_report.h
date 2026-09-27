// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/global.h>
#include <core/telemetry/perf.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TelemetryReportSummary;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryMemoryReportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_ArenaSourceText = "arena";
inline constexpr StringView s_HeapBackingSourceText = "heapBacking";
inline constexpr StringView s_ExplicitScopeSourceText = "explicitScope";
inline constexpr StringView s_LargestArenaPeakBasis = "largestArena";
inline constexpr StringView s_SampledHeapPeakBasis = "sampledHeap";
inline constexpr StringView s_ScopePeakBasis = "scope";
inline constexpr StringView s_MemorySourcesSectionHeader = "    \"memorySources\": {\n";
inline constexpr StringView s_MemorySourceEntryIndent = "      ";
inline constexpr StringView s_PeakBasisKeyPrefix = ": {\"peakBasis\": ";
inline constexpr StringView s_MemorySourceMetricsFormat =
    ", \"eventCount\": {}, \"maxUsedBytes\": {}, \"maxPeakUsedBytes\": {}, \"totalUsedDeltaBytes\": {}}}{}"
;
inline constexpr StringView s_MemorySourcesSectionFooter = "    },\n";
inline constexpr StringView s_JsonEntrySeparator = ",\n";
inline constexpr StringView s_JsonEntryTerminator = "\n";
inline constexpr StringView s_MemoryRecordSeparator = ",\n";
inline constexpr StringView s_MemoryRecordOpen = "      {\"source\": ";
inline constexpr StringView s_MemoryRecordPeakBasisKey = ", \"peakBasis\": ";
inline constexpr StringView s_MemoryRecordScopeKey = ", \"scope\": ";
inline constexpr StringView s_MemoryRecordIdentityKey = ", \"identity\": ";
inline constexpr StringView s_MemoryRecordMetricsFormat =
    ", \"streamId\": {}, \"frameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, "
    "\"peakUsedBytes\": {}, \"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}, \"delta\": "
;
inline constexpr StringView s_MemoryDeltaMetricsFormat =
    "{{\"previousFrameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, \"peakUsedBytes\": {}, "
    "\"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}}}"
;
inline constexpr StringView s_JsonNullText = "null";
inline constexpr char s_JsonRecordClose = '}';


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AddTelemetryMemorySummary(TelemetryReportSummary& summary, const Core::Telemetry::PerfMemoryPayload& payload);
void AppendTelemetryMemorySourcesJson(
    AString<Core::Telemetry::TelemetryArena>& out,
    const TelemetryReportSummary& summary
);
void AppendTelemetryMemoryRecordJson(
    AString<Core::Telemetry::TelemetryArena>& out,
    const Core::Telemetry::PerfMemoryPayload& payload,
    u32 streamId
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

