// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "memory_report.h"

#include "report.h"

#include <core/common/name_symbols.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_memory_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const char* MemorySourceText(const Core::Perf::MemorySource::Enum source)noexcept{
    switch(source){
    case Core::Perf::MemorySource::Arena:
        return "arena";
    case Core::Perf::MemorySource::HeapBacking:
        return "heapBacking";
    case Core::Perf::MemorySource::ExplicitScope:
    default:
        return "explicitScope";
    }
}

[[nodiscard]] AStringView MemoryPeakBasisText(const Core::Perf::MemorySource::Enum source)noexcept{
    switch(source){
    case Core::Perf::MemorySource::Arena:
        return "largestArena";
    case Core::Perf::MemorySource::HeapBacking:
        return "sampledHeap";
    case Core::Perf::MemorySource::ExplicitScope:
    default:
        return "scope";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AddTelemetryMemorySummary(TelemetryReportSummary& summary, const Core::Telemetry::PerfMemoryPayload& payload){
    const usize sourceIndex = static_cast<usize>(payload.snapshot.source);
    if(sourceIndex >= s_TelemetryMemorySourceCount)
        return;

    ++summary.memoryEventCount;
    TelemetryMemorySummary& source = summary.memorySources[sourceIndex];
    ++source.eventCount;
    if(payload.snapshot.usedBytes > source.maxUsedBytes)
        source.maxUsedBytes = payload.snapshot.usedBytes;
    if(payload.snapshot.peakUsedBytes > source.maxPeakUsedBytes)
        source.maxPeakUsedBytes = payload.snapshot.peakUsedBytes;
    if(payload.delta.hasSamples)
        source.totalUsedDeltaBytes += payload.delta.usedBytes;
}

void FinalizeTelemetryMemorySummary(TelemetryReportSummary& summary){
    const TelemetryMemorySummary& arenas = summary.memorySources[Core::Perf::MemorySource::Arena];
    const TelemetryMemorySummary& source = arenas.eventCount != 0u
        ? arenas
        : summary.memorySources[Core::Perf::MemorySource::ExplicitScope]
    ;
    summary.maxMemoryUsedBytes = source.maxUsedBytes;
    summary.maxMemoryPeakUsedBytes = source.maxPeakUsedBytes;
    summary.totalMemoryUsedDeltaBytes = source.totalUsedDeltaBytes;
}

void AppendTelemetryMemorySourcesJson(
    AString<Core::Telemetry::TelemetryArena>& out,
    const TelemetryReportSummary& summary){
    out += "    \"memorySources\": {\n";
    for(usize sourceIndex = 0u; sourceIndex < s_TelemetryMemorySourceCount; ++sourceIndex){
        const auto memorySource = static_cast<Core::Perf::MemorySource::Enum>(sourceIndex);
        out += "      ";
        AppendJsonQuotedText(out, __hidden_memory_report::MemorySourceText(memorySource));
        out += ": {\"peakBasis\": ";
        AppendJsonQuotedText(out, __hidden_memory_report::MemoryPeakBasisText(memorySource));
        const TelemetryMemorySummary& source = summary.memorySources[sourceIndex];
        StringAppendFormat(
            out,
            ", \"eventCount\": {}, \"maxUsedBytes\": {}, \"maxPeakUsedBytes\": {}, \"totalUsedDeltaBytes\": {}}}{}",
            source.eventCount,
            source.maxUsedBytes,
            source.maxPeakUsedBytes,
            source.totalUsedDeltaBytes,
            sourceIndex + 1u == s_TelemetryMemorySourceCount ? "\n" : ",\n"
        );
    }
    out += "    },\n";
}

void AppendTelemetryMemoryRecordJson(
    AString<Core::Telemetry::TelemetryArena>& out,
    const Core::Telemetry::PerfMemoryPayload& payload,
    const u32 streamId){
    if(!out.empty())
        out += ",\n";
    out += "      {\"source\": ";
    AppendJsonQuotedText(out, __hidden_memory_report::MemorySourceText(payload.snapshot.source));
    out += ", \"peakBasis\": ";
    AppendJsonQuotedText(out, __hidden_memory_report::MemoryPeakBasisText(payload.snapshot.source));
    char identityText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(payload.scopeName.hash(), identityText, sizeof(identityText));
    char resolvedText[Core::Common::NameSymbols::s_MaxResolvedTextLength] = {};
    AStringView scopeText(payload.scopeText.data(), payload.scopeText.size());
    if(
        scopeText == AStringView(identityText)
        && Core::Common::NameSymbols::Resolve(payload.scopeName.hash(), resolvedText, sizeof(resolvedText))
    )
        scopeText = AStringView(resolvedText);
    out += ", \"scope\": ";
    AppendJsonQuotedText(out, scopeText);
    out += ", \"identity\": ";
    AppendJsonQuotedText(out, identityText);
    StringAppendFormat(
        out,
        ", \"streamId\": {}, \"frameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, "
        "\"peakUsedBytes\": {}, \"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}, \"delta\": ",
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
            "{{\"previousFrameIndex\": {}, \"reservedBytes\": {}, \"usedBytes\": {}, \"peakUsedBytes\": {}, "
            "\"allocationCount\": {}, \"reallocationCount\": {}, \"deallocationCount\": {}}}",
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
        out += "null";
    out += '}';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

