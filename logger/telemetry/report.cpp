// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report.h"

#include "report_dot.h"
#include "report_json.h"
#include "memory_report.h"

#include <core/alloc/scratch.h>

#include <global/bit.h>
#include <global/hash_utils.h>
#include <global/simd/vector.h>
#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr StringView s_UnknownReportField = "unknown";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendCsvCell(AString<NWB::Core::Telemetry::TelemetryArena>& out, const AStringView text){
    bool quote = false;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryReportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_TimingSecondsExponentMask = 0x7FF0000000000000ull;
static constexpr usize s_PerfCsvFixedReserveBytes = 128u;
static constexpr usize s_PerfCsvBytesPerEvent = 128u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] usize EventKindBucket(const Telemetry::EventKind::Enum kind)noexcept{
    const usize index = static_cast<usize>(kind);
    return index < s_TelemetryReportEventKindCount ? index : static_cast<usize>(Telemetry::EventKind::Unknown);
}

void RecordFrameRange(TelemetryReportSummary& summary, const u64 frameIndex)noexcept{
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

void AppendPerfCsvHeader(AString<TelemetryArena>& out){
    out += "source,scope,publish_frame,seconds,min_seconds,max_seconds,last_seconds,sample_count,first_sample_frame,last_sample_frame\n";
}

void AppendPerfCsvRow(AString<TelemetryArena>& out, const Telemetry::PerfTimingSource::Enum source, const Telemetry::PerfTimingPayload& payload){
    out += PerfTimingSourceText(source);
    out += ',';
    __hidden_telemetry_report::AppendCsvCell(out, payload.scopeText);
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

void AddTiming(TelemetryReportSummary& summary, const Telemetry::PerfTimingPayload& payload)noexcept{
#pragma STDC FENV_ACCESS ON
    if(payload.source == Telemetry::PerfTimingSource::Cpu){
        ++summary.cpuTimingEventCount;
        summary.cpuTimingSampleCount += payload.stats.sampleCount;
        if(summary.cpuTimingSecondsValid){
            const f64 total = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(summary.cpuTimingSeconds), VectorDoubleReplicate(payload.stats.seconds)));
            if((BitCast<u64>(total) & s_TimingSecondsExponentMask) != s_TimingSecondsExponentMask)
                summary.cpuTimingSeconds = total;
            else
                summary.cpuTimingSecondsValid = false;
        }
        if(payload.stats.seconds > summary.maxCpuTimingSeconds)
            summary.maxCpuTimingSeconds = payload.stats.seconds;
    }
    else if(payload.source == Telemetry::PerfTimingSource::Gpu){
        ++summary.gpuTimingEventCount;
        summary.gpuTimingSampleCount += payload.stats.sampleCount;
        if(summary.gpuTimingSecondsValid){
            const f64 total = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(summary.gpuTimingSeconds), VectorDoubleReplicate(payload.stats.seconds)));
            if((BitCast<u64>(total) & s_TimingSecondsExponentMask) != s_TimingSecondsExponentMask)
                summary.gpuTimingSeconds = total;
            else
                summary.gpuTimingSecondsValid = false;
        }
        if(payload.stats.seconds > summary.maxGpuTimingSeconds)
            summary.maxGpuTimingSeconds = payload.stats.seconds;
    }
}

void AddFrameGraph(TelemetryReportSummary& summary, const Telemetry::FrameGraphPayload& payload)noexcept{
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

[[nodiscard]] usize EstimatePerfCsvReserve(const usize eventCount)noexcept{
    return s_PerfCsvFixedReserveBytes + eventCount * s_PerfCsvBytesPerEvent;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AStringView EventKindText(const Telemetry::EventKind::Enum kind)noexcept{
    switch(kind){
    case Telemetry::EventKind::TextLog:
        return "textLog";
    case Telemetry::EventKind::Diagnostic:
        return "diagnostic";
    case Telemetry::EventKind::PerfFrame:
        return "perfFrame";
    case Telemetry::EventKind::FrameGraphFrame:
        return "frameGraphFrame";
    case Telemetry::EventKind::MemoryFrame:
        return "memoryFrame";
    case Telemetry::EventKind::Unknown:
    default:
        return ::__hidden_telemetry_report::s_UnknownReportField;
    }
}

AStringView PerfTimingSourceText(const Telemetry::PerfTimingSource::Enum source)noexcept{
    switch(source){
    case Telemetry::PerfTimingSource::Cpu:
        return "cpu";
    case Telemetry::PerfTimingSource::Gpu:
        return "gpu";
    case Telemetry::PerfTimingSource::Unknown:
    default:
        return ::__hidden_telemetry_report::s_UnknownReportField;
    }
}

Expected<TelemetryReport> BuildTelemetryReport(TelemetryArena& arena, const Telemetry::EventView& events, Core::Alloc::ScratchArena& scratchArena){
    if(!events.valid())
        return MakeUnexpected(Failure{});

    TelemetryReport report(arena);
    report.perfCsv.reserve(TelemetryReportDetail::EstimatePerfCsvReserve(events.eventCount()));
    TelemetryReportDetail::AppendPerfCsvHeader(report.perfCsv);

    report.summary.eventCount = events.eventCount();
    AString<Core::Alloc::ScratchArena> memoryRecords(scratchArena);

    TelemetryReportDetail::GraphTimingMap timingByFrameAndScope(0, TelemetryReportDetail::GraphTimingKeyHasher(), EqualTo<TelemetryReportDetail::GraphTimingKey>(), scratchArena);
    timingByFrameAndScope.reserve(events.eventCount());
    TelemetryReportDetail::FrameGraphReportRecords frameGraphs(scratchArena);
    frameGraphs.reserve(events.eventCount());

    for(usize i = 0u; i < events.eventCount(); ++i){
        const Telemetry::EventRecord* const event = events.eventAt(i);
        NWB_ASSERT(event);

        ++report.summary.eventKindCounts[TelemetryReportDetail::EventKindBucket(event->header.kind)];
        TelemetryReportDetail::RecordFrameRange(report.summary, event->header.frameIndex);

        switch(event->header.kind){
        case Telemetry::EventKind::TextLog: {
            const auto payload = Telemetry::ParseTextLogPayload(arena, event->payload.data(), event->payload.size());
            if(!payload)
                ++report.summary.parseFailureCount;
            break;
        }
        case Telemetry::EventKind::Diagnostic: {
            const auto payload = Telemetry::ParseDiagnosticPayload(arena, event->payload.data(), event->payload.size());
            if(!payload)
                ++report.summary.parseFailureCount;
            break;
        }
        case Telemetry::EventKind::PerfFrame: {
            const auto payload = Telemetry::ParsePerfTimingPayload(arena, event->payload.data(), event->payload.size());
            if(!payload){
                ++report.summary.parseFailureCount;
                break;
            }
            TelemetryReportDetail::AddTiming(report.summary, *payload);
            TelemetryReportDetail::AppendPerfCsvRow(report.perfCsv, payload->source, *payload);
            // first/last are arrival endpoints, so only one sample proves an exact source-frame association when different queues complete out of order.
            // Aggregated windows remain available in the Perf CSV.
            if(payload->stats.sampleCount == 1u && payload->stats.firstSampleFrameIndex == payload->stats.lastSampleFrameIndex){
                timingByFrameAndScope.insert_or_assign(TelemetryReportDetail::GraphTimingKey{ payload->stats.firstSampleFrameIndex, payload->scopeName }, payload->stats.seconds);
            }
            break;
        }
        case Telemetry::EventKind::MemoryFrame: {
            const auto payload = Telemetry::ParsePerfMemoryPayload(arena, event->payload.data(), event->payload.size());
            if(!payload){
                ++report.summary.parseFailureCount;
                break;
            }
            AddTelemetryMemorySummary(report.summary, *payload);
            AppendTelemetryMemoryRecordJson(memoryRecords, *payload, event->header.streamId);
            break;
        }
        case Telemetry::EventKind::FrameGraphFrame: {
            TelemetryReportDetail::FrameGraphReportRecord& record = frameGraphs.emplace_back(arena);
            record.streamId = event->header.streamId;
            auto payload = Telemetry::ParseFrameGraphPayload(arena, scratchArena, event->payload.data(), event->payload.size());
            if(!payload){
                frameGraphs.pop_back();
                ++report.summary.parseFailureCount;
                break;
            }
            record.payload = Move(*payload);
            TelemetryReportDetail::AddFrameGraph(report.summary, record.payload);
            break;
        }
        default:
            break;
        }
    }

    TelemetryReportDetail::BuildTimedGraphsDot(scratchArena, frameGraphs, timingByFrameAndScope, report.graph);
    TelemetryReportDetail::BuildJson(report.summary, frameGraphs, memoryRecords, report.json);
    return report;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

