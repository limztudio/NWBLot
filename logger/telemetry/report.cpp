// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report.h"

#include "report_dot.h"
#include "memory_report.h"

#include <core/telemetry/diagnostic.h>
#include <core/telemetry/text_log.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryReportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_TimingSecondsExponentMask = 0x7FF0000000000000ull;
static constexpr usize s_PerfCsvFixedReserveBytes = 128u;
static constexpr usize s_PerfCsvBytesPerEvent = 128u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView FormatNameIdentityText(const Name& name, Array<char, s_DebugHashTextLength + 1u>& outIdentityText)noexcept{
    HashToDebugString(outIdentityText.data(), outIdentityText.size(), name.hash());
    return AStringView(outIdentityText.data(), s_DebugHashTextLength);
}

[[nodiscard]] usize EventKindBucket(const Core::Telemetry::EventKind::Enum kind)noexcept{
    const usize index = static_cast<usize>(kind);
    return index < s_TelemetryReportEventKindCount ? index : static_cast<usize>(Core::Telemetry::EventKind::Unknown);
}

void RecordFrameRange(const u64 frameIndex, TelemetryReportSummary& summary)noexcept{
    if(!summary.hasFrameRange){
        summary.hasFrameRange = true;
        summary.minFrameIndex = frameIndex;
        summary.maxFrameIndex = frameIndex;
        return;
    }

    if(frameIndex < summary.minFrameIndex)
        summary.minFrameIndex = frameIndex;
    if(frameIndex > summary.maxFrameIndex)
        summary.maxFrameIndex = frameIndex;
}

[[nodiscard]] AStringView EventKindText(const Core::Telemetry::EventKind::Enum kind)noexcept{
    switch(kind){
    case Core::Telemetry::EventKind::TextLog:
        return "textLog";
    case Core::Telemetry::EventKind::Diagnostic:
        return "diagnostic";
    case Core::Telemetry::EventKind::PerfFrame:
        return "perfFrame";
    case Core::Telemetry::EventKind::FrameGraphFrame:
        return "frameGraphFrame";
    case Core::Telemetry::EventKind::MemoryFrame:
        return "memoryFrame";
    case Core::Telemetry::EventKind::Unknown:
    default:
        return s_UnknownReportField;
    }
}

[[nodiscard]] AStringView PerfTimingSourceText(const Core::Telemetry::PerfTimingSource::Enum source)noexcept{
    switch(source){
    case Core::Telemetry::PerfTimingSource::Cpu:
        return "cpu";
    case Core::Telemetry::PerfTimingSource::Gpu:
        return "gpu";
    case Core::Telemetry::PerfTimingSource::Unknown:
    default:
        return s_UnknownReportField;
    }
}

template<typename StringT>
void AppendCsvCell(const AStringView text, StringT& out)noexcept(noexcept(out.append(text.data(), text.size())) && noexcept(out += '"') && noexcept(out += "\"\"")){
    b8 quote = false;
    for(const char ch : text){
        if(ch == ',' || ch == '"' || ch == '\n' || ch == '\r'){
            quote = true;
            break;
        }
    }

    if(!quote){
        out.append(text.data(), text.size());
        return;
    }

    out += '"';
    for(const char ch : text){
        if(ch == '"')
            out += "\"\"";
        else
            out += ch;
    }
    out += '"';
}

void AppendPerfCsvRow(const Core::Telemetry::PerfTimingSource::Enum source, const Core::Telemetry::PerfTimingPayload& payload, AString<Core::Telemetry::TelemetryArena>& out){
    out += PerfTimingSourceText(source);
    out += ',';
    AppendCsvCell(payload.scopeText, out);
    StringAppendFormat(
        out,
        ",{},{:.9},{:.9},{:.9},{:.9},{},{},{}\n",
        payload.stats.publishFrameIndex,
        payload.stats.seconds,
        payload.stats.minSeconds,
        payload.stats.maxSeconds,
        payload.stats.lastSeconds,
        payload.stats.sampleCount,
        payload.stats.firstSampleFrameIndex,
        payload.stats.lastSampleFrameIndex
    );
}

void AddTiming(const Core::Telemetry::PerfTimingPayload& payload, TelemetryReportSummary& summary)noexcept{
#pragma STDC FENV_ACCESS ON
    if(payload.source == Core::Telemetry::PerfTimingSource::Cpu){
        ++summary.cpuTimingEventCount;
        summary.cpuTimingSampleCount += payload.stats.sampleCount;
        if(summary.cpuTimingSecondsValid){
            const f64 total = summary.cpuTimingSeconds + payload.stats.seconds;
            if((BitCast<u64>(total) & s_TimingSecondsExponentMask) != s_TimingSecondsExponentMask)
                summary.cpuTimingSeconds = total;
            else
                summary.cpuTimingSecondsValid = false;
        }
        if(payload.stats.seconds > summary.maxCpuTimingSeconds)
            summary.maxCpuTimingSeconds = payload.stats.seconds;
    }
    else if(payload.source == Core::Telemetry::PerfTimingSource::Gpu){
        ++summary.gpuTimingEventCount;
        summary.gpuTimingSampleCount += payload.stats.sampleCount;
        if(summary.gpuTimingSecondsValid){
            const f64 total = summary.gpuTimingSeconds + payload.stats.seconds;
            if((BitCast<u64>(total) & s_TimingSecondsExponentMask) != s_TimingSecondsExponentMask)
                summary.gpuTimingSeconds = total;
            else
                summary.gpuTimingSecondsValid = false;
        }
        if(payload.stats.seconds > summary.maxGpuTimingSeconds)
            summary.maxGpuTimingSeconds = payload.stats.seconds;
    }
}

void AddFrameGraph(const Core::Telemetry::FrameGraphPayload& payload, TelemetryReportSummary& summary)noexcept{
    ++summary.frameGraphFrameCount;
    summary.frameGraphNodeCount += payload.nodes.size();
    summary.frameGraphEdgeCount += payload.edges.size();
    const u32 nodeCount = static_cast<u32>(payload.nodes.size());
    const u32 edgeCount = static_cast<u32>(payload.edges.size());
    if(nodeCount > summary.maxFrameGraphNodeCount)
        summary.maxFrameGraphNodeCount = nodeCount;
    if(edgeCount > summary.maxFrameGraphEdgeCount)
        summary.maxFrameGraphEdgeCount = edgeCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TelemetryReport> BuildTelemetryReport(const Core::Telemetry::EventView& events, Core::Telemetry::TelemetryArena& arena, Core::Alloc::ScratchArena& scratchArena){
    if(!events.valid())
        return MakeUnexpected(Failure{});

    TelemetryReport report(arena);
    report.perfCsv.reserve(TelemetryReportDetail::s_PerfCsvFixedReserveBytes + events.eventCount() * TelemetryReportDetail::s_PerfCsvBytesPerEvent);
    report.perfCsv += "source,scope,publish_frame,seconds,min_seconds,max_seconds,last_seconds,sample_count,first_sample_frame,last_sample_frame\n";

    report.summary.eventCount = events.eventCount();
    AString<Core::Alloc::ScratchArena> memoryRecords(scratchArena);

    TelemetryReportDetail::GraphTimingMap timingByFrameAndScope(0, TelemetryReportDetail::GraphTimingKeyHasher(), EqualTo<TelemetryReportDetail::GraphTimingKey>(), scratchArena);
    timingByFrameAndScope.reserve(events.eventCount());
    TelemetryReportDetail::FrameGraphReportRecords frameGraphs(scratchArena);
    frameGraphs.reserve(events.eventCount());

    for(usize i = 0u; i < events.eventCount(); ++i){
        const Core::Telemetry::EventRecord& event = *MakeNotNull(events.eventAt(i));

        ++report.summary.eventKindCounts[TelemetryReportDetail::EventKindBucket(event.header.kind)];
        TelemetryReportDetail::RecordFrameRange(event.header.frameIndex, report.summary);

        switch(event.header.kind){
        case Core::Telemetry::EventKind::TextLog: {
            const auto payload = Core::Telemetry::ParseTextLogPayload(event.payload.data(), event.payload.size());
            if(!payload)
                ++report.summary.parseFailureCount;
            break;
        }
        case Core::Telemetry::EventKind::Diagnostic: {
            const auto payload = Core::Telemetry::ParseDiagnosticPayload(event.payload.data(), event.payload.size());
            if(!payload)
                ++report.summary.parseFailureCount;
            break;
        }
        case Core::Telemetry::EventKind::PerfFrame: {
            const auto payload = Core::Telemetry::ParsePerfTimingPayload(event.payload.data(), event.payload.size());
            if(!payload){
                ++report.summary.parseFailureCount;
                break;
            }
            TelemetryReportDetail::AddTiming(*payload, report.summary);
            TelemetryReportDetail::AppendPerfCsvRow(payload->source, *payload, report.perfCsv);
            // first/last are arrival endpoints, so only one sample proves an exact source-frame association when different queues complete out of order.
            // Aggregated windows remain available in the Perf CSV.
            if(payload->stats.sampleCount == 1u && payload->stats.firstSampleFrameIndex == payload->stats.lastSampleFrameIndex){
                timingByFrameAndScope.insert_or_assign(TelemetryReportDetail::GraphTimingKey{ payload->stats.firstSampleFrameIndex, payload->scopeName }, payload->stats.seconds);
            }
            break;
        }
        case Core::Telemetry::EventKind::MemoryFrame: {
            const auto payload = Core::Telemetry::ParsePerfMemoryPayload(event.payload.data(), event.payload.size());
            if(!payload){
                ++report.summary.parseFailureCount;
                break;
            }
            AddTelemetryMemorySummary(*payload, report.summary);
            AppendTelemetryMemoryRecordJson(*payload, event.header.streamId, memoryRecords);
            break;
        }
        case Core::Telemetry::EventKind::FrameGraphFrame: {
            TelemetryReportDetail::FrameGraphReportRecord& record = frameGraphs.emplace_back(arena);
            record.streamId = event.header.streamId;
            auto payload = Core::Telemetry::ParseFrameGraphPayload(arena, scratchArena, event.payload.data(), event.payload.size());
            if(!payload){
                frameGraphs.pop_back();
                ++report.summary.parseFailureCount;
                break;
            }
            record.payload = Move(*payload);
            TelemetryReportDetail::AddFrameGraph(record.payload, report.summary);
            break;
        }
        default:
            break;
        }
    }

    TelemetryReportDetail::BuildTimedGraphsDot(frameGraphs, timingByFrameAndScope, scratchArena, report.graph);
    TelemetryReportDetail::BuildJson(report.summary, frameGraphs, memoryRecords, report.json);
    return report;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

