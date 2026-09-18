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
        return TelemetryMemoryReportDetail::s_ArenaSourceText;
    case Core::Perf::MemorySource::HeapBacking:
        return TelemetryMemoryReportDetail::s_HeapBackingSourceText;
    case Core::Perf::MemorySource::ExplicitScope:
    default:
        return TelemetryMemoryReportDetail::s_ExplicitScopeSourceText;
    }
}

[[nodiscard]] AStringView MemoryPeakBasisText(const Core::Perf::MemorySource::Enum source)noexcept{
    switch(source){
    case Core::Perf::MemorySource::Arena:
        return TelemetryMemoryReportDetail::s_LargestArenaPeakBasis;
    case Core::Perf::MemorySource::HeapBacking:
        return TelemetryMemoryReportDetail::s_SampledHeapPeakBasis;
    case Core::Perf::MemorySource::ExplicitScope:
    default:
        return TelemetryMemoryReportDetail::s_ScopePeakBasis;
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

void AppendTelemetryMemorySourcesJson(
    AString<Core::Telemetry::TelemetryArena>& out,
    const TelemetryReportSummary& summary){
    out += TelemetryMemoryReportDetail::s_MemorySourcesSectionHeader;
    for(usize sourceIndex = 0u; sourceIndex < s_TelemetryMemorySourceCount; ++sourceIndex){
        const auto memorySource = static_cast<Core::Perf::MemorySource::Enum>(sourceIndex);
        out += TelemetryMemoryReportDetail::s_MemorySourceEntryIndent;
        AppendJsonQuotedText(out, __hidden_memory_report::MemorySourceText(memorySource));
        out += TelemetryMemoryReportDetail::s_PeakBasisKeyPrefix;
        AppendJsonQuotedText(out, __hidden_memory_report::MemoryPeakBasisText(memorySource));
        const TelemetryMemorySummary& source = summary.memorySources[sourceIndex];
        StringAppendFormat(
            out,
            TelemetryMemoryReportDetail::s_MemorySourceMetricsFormat,
            source.eventCount,
            source.maxUsedBytes,
            source.maxPeakUsedBytes,
            source.totalUsedDeltaBytes,
            sourceIndex + 1u == s_TelemetryMemorySourceCount
                ? TelemetryMemoryReportDetail::s_JsonEntryTerminator
                : TelemetryMemoryReportDetail::s_JsonEntrySeparator
        );
    }
    out += TelemetryMemoryReportDetail::s_MemorySourcesSectionFooter;
}

void AppendTelemetryMemoryRecordJson(
    AString<Core::Telemetry::TelemetryArena>& out,
    const Core::Telemetry::PerfMemoryPayload& payload,
    const u32 streamId){
    if(!out.empty())
        out += TelemetryMemoryReportDetail::s_MemoryRecordSeparator;
    out += TelemetryMemoryReportDetail::s_MemoryRecordOpen;
    AppendJsonQuotedText(out, __hidden_memory_report::MemorySourceText(payload.snapshot.source));
    out += TelemetryMemoryReportDetail::s_MemoryRecordPeakBasisKey;
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
    out += TelemetryMemoryReportDetail::s_MemoryRecordScopeKey;
    AppendJsonQuotedText(out, scopeText);
    out += TelemetryMemoryReportDetail::s_MemoryRecordIdentityKey;
    AppendJsonQuotedText(out, identityText);
    StringAppendFormat(
        out,
        TelemetryMemoryReportDetail::s_MemoryRecordMetricsFormat,
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
            TelemetryMemoryReportDetail::s_MemoryDeltaMetricsFormat,
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
        out += TelemetryMemoryReportDetail::s_JsonNullText;
    out += TelemetryMemoryReportDetail::s_JsonRecordClose;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

