// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report.h"

#include "report_dot.h"

#include "report_json.h"

#include "memory_report.h"

#include <global/hash_utils.h>
#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_PerfCsvFixedReserveBytes = 128u;
static constexpr usize s_PerfCsvBytesPerEvent = 128u;

[[nodiscard]] usize EventKindBucket(const Telemetry::EventKind::Enum kind)noexcept{
    const usize index = static_cast<usize>(kind);
    return index < s_TelemetryReportEventKindCount ? index : static_cast<usize>(Telemetry::EventKind::Unknown);
}

void RecordFrameRange(TelemetryReportSummary& summary, const u64 frameIndex){
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

void AppendPerfCsvRow(
    AString<TelemetryArena>& out,
    const Telemetry::PerfTimingSource::Enum source,
    const Telemetry::PerfTimingPayload& payload
){
    out += PerfTimingSourceText(source);
    out += ',';
    AppendCsvCell(out, payload.scopeText);
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

void AddTiming(TelemetryReportSummary& summary, const Telemetry::PerfTimingPayload& payload){
    if(payload.source == Telemetry::PerfTimingSource::Cpu){
        ++summary.cpuTimingEventCount;
        summary.cpuTimingSampleCount += payload.stats.sampleCount;
        summary.cpuTimingSeconds += payload.stats.seconds;
        if(payload.stats.seconds > summary.maxCpuTimingSeconds)
            summary.maxCpuTimingSeconds = payload.stats.seconds;
    }
    else if(payload.source == Telemetry::PerfTimingSource::Gpu){
        ++summary.gpuTimingEventCount;
        summary.gpuTimingSampleCount += payload.stats.sampleCount;
        summary.gpuTimingSeconds += payload.stats.seconds;
        if(payload.stats.seconds > summary.maxGpuTimingSeconds)
            summary.maxGpuTimingSeconds = payload.stats.seconds;
    }
}

void AddFrameGraph(TelemetryReportSummary& summary, const Telemetry::FrameGraphPayload& payload){
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const char* EventKindText(const Telemetry::EventKind::Enum kind)noexcept{
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
        return "unknown";
    }
}

const char* PerfTimingSourceText(const Telemetry::PerfTimingSource::Enum source)noexcept{
    switch(source){
    case Telemetry::PerfTimingSource::Cpu:
        return "cpu";
    case Telemetry::PerfTimingSource::Gpu:
        return "gpu";
    case Telemetry::PerfTimingSource::Unknown:
    default:
        return "unknown";
    }
}

bool BuildTelemetryReport(TelemetryArena& arena, const Telemetry::EventView& events, TelemetryReport& outReport){
    outReport = TelemetryReport(arena);
    if(events.valid())
        outReport.perfCsv.reserve(__hidden_telemetry_report::EstimatePerfCsvReserve(events.eventCount()));
    __hidden_telemetry_report::AppendPerfCsvHeader(outReport.perfCsv);

    if(!events.valid())
        return false;

    outReport.summary.eventCount = events.eventCount();
    AString<TelemetryArena> memoryRecords(arena);

    __hidden_telemetry_report::GraphTimingMap timingByFrameAndScope(
        0,
        __hidden_telemetry_report::GraphTimingKeyHasher(),
        EqualTo<__hidden_telemetry_report::GraphTimingKey>(),
        arena
    );
    timingByFrameAndScope.reserve(events.eventCount());
    __hidden_telemetry_report::FrameGraphReportRecords frameGraphs(arena);
    frameGraphs.reserve(events.eventCount());

    for(usize i = 0u; i < events.eventCount(); ++i){
        const Telemetry::EventRecord* const event = events.eventAt(i);
        if(!event){
            ++outReport.summary.parseFailureCount;
            continue;
        }

        ++outReport.summary.eventKindCounts[__hidden_telemetry_report::EventKindBucket(event->header.kind)];
        __hidden_telemetry_report::RecordFrameRange(outReport.summary, event->header.frameIndex);

        switch(event->header.kind){
        case Telemetry::EventKind::TextLog: {
            Telemetry::TextLogPayload payload(arena);
            if(!Telemetry::ParseTextLogPayload(arena, event->payload.data(), event->payload.size(), payload))
                ++outReport.summary.parseFailureCount;
            break;
        }
        case Telemetry::EventKind::Diagnostic: {
            Telemetry::DiagnosticPayload payload(arena);
            if(!Telemetry::ParseDiagnosticPayload(arena, event->payload.data(), event->payload.size(), payload))
                ++outReport.summary.parseFailureCount;
            break;
        }
        case Telemetry::EventKind::PerfFrame: {
            Telemetry::PerfTimingPayload payload(arena);
            if(!Telemetry::ParsePerfTimingPayload(arena, event->payload.data(), event->payload.size(), payload)){
                ++outReport.summary.parseFailureCount;
                break;
            }
            __hidden_telemetry_report::AddTiming(outReport.summary, payload);
            __hidden_telemetry_report::AppendPerfCsvRow(outReport.perfCsv, payload.source, payload);
            // first/last are arrival endpoints, so only one sample proves an exact source-frame association when
            // different queues complete out of order. Aggregated windows remain available in the Perf CSV.
            if(
                payload.stats.sampleCount == 1u
                && payload.stats.firstSampleFrameIndex == payload.stats.lastSampleFrameIndex
            ){
                timingByFrameAndScope.insert_or_assign(
                    __hidden_telemetry_report::GraphTimingKey{ payload.stats.firstSampleFrameIndex, payload.scopeName },
                    payload.stats.seconds
                );
            }
            break;
        }
        case Telemetry::EventKind::MemoryFrame: {
            Telemetry::PerfMemoryPayload payload(arena);
            if(!Telemetry::ParsePerfMemoryPayload(arena, event->payload.data(), event->payload.size(), payload)){
                ++outReport.summary.parseFailureCount;
                break;
            }
            AddTelemetryMemorySummary(outReport.summary, payload);
            AppendTelemetryMemoryRecordJson(memoryRecords, payload, event->header.streamId);
            break;
        }
        case Telemetry::EventKind::FrameGraphFrame: {
            __hidden_telemetry_report::FrameGraphReportRecord& record = frameGraphs.emplace_back(arena);
            record.streamId = event->header.streamId;
            if(!Telemetry::ParseFrameGraphPayload(arena, event->payload.data(), event->payload.size(), record.payload)){
                frameGraphs.pop_back();
                ++outReport.summary.parseFailureCount;
                break;
            }
            __hidden_telemetry_report::AddFrameGraph(outReport.summary, record.payload);
            break;
        }
        default:
            break;
        }
    }

    __hidden_telemetry_report::BuildTimedGraphsDot(arena, frameGraphs, timingByFrameAndScope, outReport.graph);
    __hidden_telemetry_report::BuildJson(outReport.summary, frameGraphs, memoryRecords, outReport.json);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

