// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "memory_report.h"

#include "report.h"

#include <core/common/name_symbols.h>

#include <global/json_text.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_memory_report{


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
inline constexpr StringView s_MemorySourceMetricsFormat = ", \"eventCount\": {}, \"maxUsedBytes\": {}, \"maxPeakUsedBytes\": {}, \"totalUsedDeltaBytes\": ";
inline constexpr StringView s_MemorySourceDeltaFormat = "{}";
inline constexpr StringView s_MemorySourcesSectionFooter = "    },\n";
inline constexpr StringView s_JsonEntrySeparator = ",\n";
inline constexpr StringView s_JsonEntryTerminator = "\n";
inline constexpr StringView s_MemoryRecordSeparator = ",\n";
inline constexpr StringView s_MemoryRecordOpen = "      {\"source\": ";
inline constexpr StringView s_MemoryRecordPeakBasisKey = ", \"peakBasis\": ";
inline constexpr StringView s_MemoryRecordScopeKey = ", \"scope\": ";
inline constexpr StringView s_MemoryRecordIdentityKey = ", \"identity\": ";
inline constexpr StringView s_MemoryRecordMetricsFormat = ", \"streamId\": {}, \"frameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, " "\"peakUsedBytes\": {}, \"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}, \"delta\": ";
inline constexpr StringView s_MemoryDeltaMetricsFormat = "{{\"previousFrameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, \"peakUsedBytes\": {}, " "\"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}}}";
inline constexpr StringView s_JsonNullText = "null";
inline constexpr char s_JsonRecordClose = '}';


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView MemorySourceText(const Core::Perf::MemorySource::Enum source)noexcept{
    switch(source){
    case Core::Perf::MemorySource::Arena:
        return s_ArenaSourceText;
    case Core::Perf::MemorySource::HeapBacking:
        return s_HeapBackingSourceText;
    case Core::Perf::MemorySource::ExplicitScope:
    default:
        return s_ExplicitScopeSourceText;
    }
}

[[nodiscard]] AStringView MemoryPeakBasisText(const Core::Perf::MemorySource::Enum source)noexcept{
    switch(source){
    case Core::Perf::MemorySource::Arena:
        return s_LargestArenaPeakBasis;
    case Core::Perf::MemorySource::HeapBacking:
        return s_SampledHeapPeakBasis;
    case Core::Perf::MemorySource::ExplicitScope:
    default:
        return s_ScopePeakBasis;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AddTelemetryMemorySummary(TelemetryReportSummary& summary, const Core::Telemetry::PerfMemoryPayload& payload)noexcept{
    const usize sourceIndex = static_cast<usize>(payload.snapshot.source);
    NWB_ASSERT(sourceIndex < s_TelemetryMemorySourceCount);

    ++summary.memoryEventCount;
    TelemetryMemorySummary& source = summary.memorySources[sourceIndex];
    ++source.eventCount;
    if(payload.snapshot.usedBytes > source.maxUsedBytes)
        source.maxUsedBytes = payload.snapshot.usedBytes;
    if(payload.snapshot.peakUsedBytes > source.maxPeakUsedBytes)
        source.maxPeakUsedBytes = payload.snapshot.peakUsedBytes;
    if(payload.delta.hasSamples && source.totalUsedDeltaValid){
        const auto total = AddNoOverflow<i64>(source.totalUsedDeltaBytes, payload.delta.usedBytes);
        if(total)
            source.totalUsedDeltaBytes = *total;
        else
            source.totalUsedDeltaValid = false;
    }
}

void AppendTelemetryMemorySourcesJson(AString<Core::Telemetry::TelemetryArena>& out, const TelemetryReportSummary& summary){
    out += __hidden_memory_report::s_MemorySourcesSectionHeader;
    for(usize sourceIndex = 0u; sourceIndex < s_TelemetryMemorySourceCount; ++sourceIndex){
        const auto memorySource = static_cast<Core::Perf::MemorySource::Enum>(sourceIndex);
        out += __hidden_memory_report::s_MemorySourceEntryIndent;
        AppendJsonQuotedText(out, __hidden_memory_report::MemorySourceText(memorySource));
        out += __hidden_memory_report::s_PeakBasisKeyPrefix;
        AppendJsonQuotedText(out, __hidden_memory_report::MemoryPeakBasisText(memorySource));
        const TelemetryMemorySummary& source = summary.memorySources[sourceIndex];
        StringAppendFormat(out, __hidden_memory_report::s_MemorySourceMetricsFormat, source.eventCount, source.maxUsedBytes, source.maxPeakUsedBytes);
        if(source.totalUsedDeltaValid)
            StringAppendFormat(out, __hidden_memory_report::s_MemorySourceDeltaFormat, source.totalUsedDeltaBytes);
        else
            out += __hidden_memory_report::s_JsonNullText;
        out += __hidden_memory_report::s_JsonRecordClose;
        out += sourceIndex + 1u == s_TelemetryMemorySourceCount ? __hidden_memory_report::s_JsonEntryTerminator : __hidden_memory_report::s_JsonEntrySeparator;
    }
    out += __hidden_memory_report::s_MemorySourcesSectionFooter;
}

void AppendTelemetryMemoryRecordJson(AString<Core::Alloc::ScratchArena>& out, const Core::Telemetry::PerfMemoryPayload& payload, const u32 streamId){
    if(!out.empty())
        out += __hidden_memory_report::s_MemoryRecordSeparator;
    out += __hidden_memory_report::s_MemoryRecordOpen;
    AppendJsonQuotedText(out, __hidden_memory_report::MemorySourceText(payload.snapshot.source));
    out += __hidden_memory_report::s_MemoryRecordPeakBasisKey;
    AppendJsonQuotedText(out, __hidden_memory_report::MemoryPeakBasisText(payload.snapshot.source));
    char identityText[NameDetail::s_DebugHashTextLength + 1u];
    NameDetail::HashToDebugString(identityText, sizeof(identityText), payload.scopeName.hash());
    const AStringView identity(identityText, NameDetail::s_DebugHashTextLength);
    char resolvedText[Core::Common::NameSymbols::s_MaxResolvedTextLength];
    AStringView scopeText = payload.scopeText;
    if(scopeText == identity && Core::Common::NameSymbols::Resolve(resolvedText, sizeof(resolvedText), payload.scopeName.hash()))
        scopeText = AStringView(resolvedText);
    out += __hidden_memory_report::s_MemoryRecordScopeKey;
    AppendJsonQuotedText(out, scopeText);
    out += __hidden_memory_report::s_MemoryRecordIdentityKey;
    AppendJsonQuotedText(out, identity);
    StringAppendFormat(
        out,
        __hidden_memory_report::s_MemoryRecordMetricsFormat,
        streamId,
        payload.snapshot.frameIndex,
        payload.snapshot.reservedBytes,
        payload.snapshot.usedBytes,
        payload.snapshot.peakUsedBytes,
        payload.snapshot.allocationCount,
        payload.snapshot.reallocationCount,
        payload.snapshot.deallocationCount
    );
    if(payload.delta.hasSamples){
        StringAppendFormat(
            out,
            __hidden_memory_report::s_MemoryDeltaMetricsFormat,
            payload.delta.previousFrameIndex,
            payload.delta.reservedBytes,
            payload.delta.usedBytes,
            payload.delta.peakUsedBytes,
            payload.delta.allocationCount,
            payload.delta.reallocationCount,
            payload.delta.deallocationCount
        );
    }
    else
        out += __hidden_memory_report::s_JsonNullText;
    out += __hidden_memory_report::s_JsonRecordClose;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

