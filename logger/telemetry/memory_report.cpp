// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "memory_report.h"

#include "report.h"

#include <logger/telemetry/report_values.adhd.h>

#include <core/common/name_symbols.h>

#include <global/name_hash_text.h>
#include <global/json_text.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_memory_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_ArenaSourceText = ::NWB::Log::s_LogTelemetryMemoryReportArenaSourceText;
inline constexpr StringView s_HeapBackingSourceText = ::NWB::Log::s_LogTelemetryMemoryReportHeapBackingSourceText;
inline constexpr StringView s_ExplicitScopeSourceText = ::NWB::Log::s_LogTelemetryMemoryReportExplicitScopeSourceText;
inline constexpr StringView s_LargestArenaPeakBasis = ::NWB::Log::s_LogTelemetryMemoryReportLargestArenaPeakBasis;
inline constexpr StringView s_SampledHeapPeakBasis = ::NWB::Log::s_LogTelemetryMemoryReportSampledHeapPeakBasis;
inline constexpr StringView s_ScopePeakBasis = ::NWB::Log::s_LogTelemetryMemoryReportScopePeakBasis;
inline constexpr StringView s_MemorySourcesSectionHeader = ::NWB::Log::s_LogTelemetryMemoryReportMemorySourcesSectionHeader;
inline constexpr StringView s_MemorySourceEntryIndent = ::NWB::Log::s_LogTelemetryMemoryReportMemorySourceEntryIndent;
inline constexpr StringView s_PeakBasisKeyPrefix = ::NWB::Log::s_LogTelemetryMemoryReportPeakBasisKeyPrefix;
inline constexpr StringView s_MemorySourceMetricsFormat = ::NWB::Log::s_LogTelemetryMemoryReportMemorySourceMetricsFormat;
inline constexpr StringView s_MemorySourceDeltaFormat = ::NWB::Log::s_LogTelemetryMemoryReportMemorySourceDeltaFormat;
inline constexpr StringView s_MemorySourcesSectionFooter = ::NWB::Log::s_LogTelemetryMemoryReportMemorySourcesSectionFooter;
inline constexpr StringView s_JsonEntrySeparator = ::NWB::Log::s_LogTelemetryMemoryReportJsonEntrySeparator;
inline constexpr StringView s_JsonEntryTerminator = ::NWB::Log::s_LogTelemetryMemoryReportJsonEntryTerminator;
inline constexpr StringView s_MemoryRecordSeparator = ::NWB::Log::s_LogTelemetryMemoryReportMemoryRecordSeparator;
inline constexpr StringView s_MemoryRecordOpen = ::NWB::Log::s_LogTelemetryMemoryReportMemoryRecordOpen;
inline constexpr StringView s_MemoryRecordPeakBasisKey = ::NWB::Log::s_LogTelemetryMemoryReportMemoryRecordPeakBasisKey;
inline constexpr StringView s_MemoryRecordScopeKey = ::NWB::Log::s_LogTelemetryMemoryReportMemoryRecordScopeKey;
inline constexpr StringView s_MemoryRecordIdentityKey = ::NWB::Log::s_LogTelemetryMemoryReportMemoryRecordIdentityKey;
inline constexpr StringView s_MemoryRecordMetricsFormat = ::NWB::Log::s_LogTelemetryMemoryReportMemoryRecordMetricsFormat;
inline constexpr StringView s_MemoryDeltaMetricsFormat = ::NWB::Log::s_LogTelemetryMemoryReportMemoryDeltaMetricsFormat;
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


void AddTelemetryMemorySummary(const Core::Telemetry::PerfMemoryPayload& payload, TelemetryReportSummary& summary)noexcept{
    const usize sourceIndex = static_cast<usize>(payload.snapshot.source);
    FOUNDATION_ASSERT(sourceIndex < s_TelemetryMemorySourceCount);

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

void AppendTelemetryMemorySourcesJson(const TelemetryReportSummary& summary, AString<Core::Telemetry::TelemetryArena>& out){
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
            out += s_JsonNullText;
        out += __hidden_memory_report::s_JsonRecordClose;
        out += sourceIndex + 1u == s_TelemetryMemorySourceCount ? __hidden_memory_report::s_JsonEntryTerminator : __hidden_memory_report::s_JsonEntrySeparator;
    }
    out += __hidden_memory_report::s_MemorySourcesSectionFooter;
}

void AppendTelemetryMemoryRecordJson(const Core::Telemetry::PerfMemoryPayload& payload, const u32 streamId, AString<Core::Alloc::ScratchArena>& out){
    if(!out.empty())
        out += __hidden_memory_report::s_MemoryRecordSeparator;
    out += __hidden_memory_report::s_MemoryRecordOpen;
    AppendJsonQuotedText(out, __hidden_memory_report::MemorySourceText(payload.snapshot.source));
    out += __hidden_memory_report::s_MemoryRecordPeakBasisKey;
    AppendJsonQuotedText(out, __hidden_memory_report::MemoryPeakBasisText(payload.snapshot.source));
    Array<char, NameHashTextCodec::s_TextLength + 1u> identityText;
    const AStringView identity = TelemetryReportDetail::FormatNameIdentityText(payload.scopeName, identityText);
    Array<char, Core::Common::NameSymbols::s_MaxResolvedTextLength> resolvedText;
    AStringView scopeText = payload.scopeText;
    if(scopeText == identity && Core::Common::NameSymbols::Resolve(resolvedText.data(), resolvedText.size(), payload.scopeName.hash()))
        scopeText = AStringView(resolvedText.data());
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
        out += s_JsonNullText;
    out += __hidden_memory_report::s_JsonRecordClose;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

