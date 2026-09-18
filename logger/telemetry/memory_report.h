// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/global.h>
#include <core/telemetry/perf.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TelemetryReportSummary;

namespace TelemetryMemoryReportDetail{

inline constexpr char s_ArenaSourceText[] = "arena";
inline constexpr char s_HeapBackingSourceText[] = "heapBacking";
inline constexpr char s_ExplicitScopeSourceText[] = "explicitScope";
inline constexpr char s_LargestArenaPeakBasis[] = "largestArena";
inline constexpr char s_SampledHeapPeakBasis[] = "sampledHeap";
inline constexpr char s_ScopePeakBasis[] = "scope";
inline constexpr char s_MemorySourcesSectionHeader[] = "    \"memorySources\": {\n";
inline constexpr char s_MemorySourceEntryIndent[] = "      ";
inline constexpr char s_PeakBasisKeyPrefix[] = ": {\"peakBasis\": ";
inline constexpr char s_MemorySourceMetricsFormat[] =
    ", \"eventCount\": {}, \"maxUsedBytes\": {}, \"maxPeakUsedBytes\": {}, \"totalUsedDeltaBytes\": {}}}{}"
;
inline constexpr char s_MemorySourcesSectionFooter[] = "    },\n";
inline constexpr char s_JsonEntrySeparator[] = ",\n";
inline constexpr char s_JsonEntryTerminator[] = "\n";
inline constexpr char s_MemoryRecordSeparator[] = ",\n";
inline constexpr char s_MemoryRecordOpen[] = "      {\"source\": ";
inline constexpr char s_MemoryRecordPeakBasisKey[] = ", \"peakBasis\": ";
inline constexpr char s_MemoryRecordScopeKey[] = ", \"scope\": ";
inline constexpr char s_MemoryRecordIdentityKey[] = ", \"identity\": ";
inline constexpr char s_MemoryRecordMetricsFormat[] =
    ", \"streamId\": {}, \"frameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, "
    "\"peakUsedBytes\": {}, \"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}, \"delta\": "
;
inline constexpr char s_MemoryDeltaMetricsFormat[] =
    "{{\"previousFrameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, \"peakUsedBytes\": {}, "
    "\"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}}}"
;
inline constexpr char s_JsonNullText[] = "null";
inline constexpr char s_JsonRecordClose = '}';

};

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

