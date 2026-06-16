// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "export.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_export{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendJsonString(AString<TelemetryArena>& out, const AStringView text){
    out += '"';
    for(const char ch : text){
        switch(ch){
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if(static_cast<unsigned char>(ch) < 0x20u)
                out += '?';
            else
                out += ch;
            break;
        }
    }
    out += '"';
}

void AppendCsvCell(AString<TelemetryArena>& out, const AStringView text){
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

template<typename... Args>
void AppendFormat(TelemetryArena& arena, AString<TelemetryArena>& out, AFormatString<Args...> fmt, Args&&... args){
    out += StringFormat(arena, fmt, Forward<Args>(args)...);
}

[[nodiscard]] usize EventKindBucket(const EventKind::Enum kind)noexcept{
    const usize index = static_cast<usize>(kind);
    return index < s_ArchiveReportEventKindCount ? index : static_cast<usize>(EventKind::Unknown);
}

void RecordFrameRange(ArchiveReportSummary& summary, const u64 frameIndex){
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
    out += "source,scope,publish_frame,seconds,sample_count,first_sample_frame,last_sample_frame\n";
}

void AppendPerfCsvRow(
    TelemetryArena& arena,
    AString<TelemetryArena>& out,
    const PerfTimingSource::Enum source,
    const PerfTimingPayload& payload
){
    out += PerfTimingSourceText(source);
    out += ',';
    AppendCsvCell(out, payload.scopeText);
    AppendFormat(
        arena,
        out,
        ",{},{:.9},{},{},{}\n",
        payload.stats.publishFrameIndex,
        payload.stats.seconds,
        payload.stats.sampleCount,
        payload.stats.firstSampleFrameIndex,
        payload.stats.lastSampleFrameIndex
    );
}

void AddTiming(ArchiveReportSummary& summary, const PerfTimingPayload& payload){
    if(payload.source == PerfTimingSource::Cpu){
        ++summary.cpuTimingEventCount;
        summary.cpuTimingSampleCount += payload.stats.sampleCount;
        summary.cpuTimingSeconds += payload.stats.seconds;
        if(payload.stats.seconds > summary.maxCpuTimingSeconds)
            summary.maxCpuTimingSeconds = payload.stats.seconds;
    }
    else if(payload.source == PerfTimingSource::Gpu){
        ++summary.gpuTimingEventCount;
        summary.gpuTimingSampleCount += payload.stats.sampleCount;
        summary.gpuTimingSeconds += payload.stats.seconds;
        if(payload.stats.seconds > summary.maxGpuTimingSeconds)
            summary.maxGpuTimingSeconds = payload.stats.seconds;
    }
}

void AddMemory(ArchiveReportSummary& summary, const PerfMemoryPayload& payload){
    ++summary.memoryEventCount;
    if(payload.snapshot.usedBytes > summary.maxMemoryUsedBytes)
        summary.maxMemoryUsedBytes = payload.snapshot.usedBytes;
    if(payload.snapshot.peakUsedBytes > summary.maxMemoryPeakUsedBytes)
        summary.maxMemoryPeakUsedBytes = payload.snapshot.peakUsedBytes;
    if(payload.delta.hasSamples)
        summary.totalMemoryUsedDeltaBytes += payload.delta.usedBytes;
}

void AddFrameGraph(ArchiveReportSummary& summary, const FrameGraphPayload& payload){
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

void BuildJson(TelemetryArena& arena, const ArchiveReportSummary& summary, AString<TelemetryArena>& out){
    out.clear();
    out += "{\n";
    AppendFormat(arena, out, "  \"eventCount\": {},\n", summary.eventCount);
    out += "  \"frameRange\": {";
    AppendFormat(arena, out, "\"present\": {}", summary.hasFrameRange ? "true" : "false");
    if(summary.hasFrameRange)
        AppendFormat(arena, out, ", \"min\": {}, \"max\": {}", summary.minFrameIndex, summary.maxFrameIndex);
    out += "},\n";

    out += "  \"events\": {\n";
    for(usize i = 0u; i < s_ArchiveReportEventKindCount; ++i){
        out += "    ";
        AppendJsonString(out, EventKindText(static_cast<EventKind::Enum>(i)));
        AppendFormat(arena, out, ": {}{}", summary.eventKindCounts[i], i + 1u == s_ArchiveReportEventKindCount ? "\n" : ",\n");
    }
    out += "  },\n";

    out += "  \"perf\": {\n";
    AppendFormat(arena, out, "    \"cpuTimingEvents\": {},\n", summary.cpuTimingEventCount);
    AppendFormat(arena, out, "    \"cpuTimingSamples\": {},\n", summary.cpuTimingSampleCount);
    AppendFormat(arena, out, "    \"cpuTimingSeconds\": {:.9},\n", summary.cpuTimingSeconds);
    AppendFormat(arena, out, "    \"maxCpuTimingSeconds\": {:.9},\n", summary.maxCpuTimingSeconds);
    AppendFormat(arena, out, "    \"gpuTimingEvents\": {},\n", summary.gpuTimingEventCount);
    AppendFormat(arena, out, "    \"gpuTimingSamples\": {},\n", summary.gpuTimingSampleCount);
    AppendFormat(arena, out, "    \"gpuTimingSeconds\": {:.9},\n", summary.gpuTimingSeconds);
    AppendFormat(arena, out, "    \"maxGpuTimingSeconds\": {:.9},\n", summary.maxGpuTimingSeconds);
    AppendFormat(arena, out, "    \"memoryEvents\": {},\n", summary.memoryEventCount);
    AppendFormat(arena, out, "    \"maxMemoryUsedBytes\": {},\n", summary.maxMemoryUsedBytes);
    AppendFormat(arena, out, "    \"maxMemoryPeakUsedBytes\": {},\n", summary.maxMemoryPeakUsedBytes);
    AppendFormat(arena, out, "    \"totalMemoryUsedDeltaBytes\": {}\n", summary.totalMemoryUsedDeltaBytes);
    out += "  },\n";

    out += "  \"frameGraph\": {\n";
    AppendFormat(arena, out, "    \"frames\": {},\n", summary.frameGraphFrameCount);
    AppendFormat(arena, out, "    \"nodes\": {},\n", summary.frameGraphNodeCount);
    AppendFormat(arena, out, "    \"edges\": {},\n", summary.frameGraphEdgeCount);
    AppendFormat(arena, out, "    \"maxNodes\": {},\n", summary.maxFrameGraphNodeCount);
    AppendFormat(arena, out, "    \"maxEdges\": {}\n", summary.maxFrameGraphEdgeCount);
    out += "  },\n";
    AppendFormat(arena, out, "  \"parseFailures\": {}\n", summary.parseFailureCount);
    out += "}\n";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const char* EventKindText(const EventKind::Enum kind)noexcept{
    switch(kind){
    case EventKind::TextLog:
        return "textLog";
    case EventKind::Diagnostic:
        return "diagnostic";
    case EventKind::CrashUpload:
        return "crashUpload";
    case EventKind::PerfFrame:
        return "perfFrame";
    case EventKind::FrameGraphFrame:
        return "frameGraphFrame";
    case EventKind::Custom:
        return "custom";
    case EventKind::MemoryFrame:
        return "memoryFrame";
    case EventKind::Unknown:
    default:
        return "unknown";
    }
}

const char* PerfTimingSourceText(const PerfTimingSource::Enum source)noexcept{
    switch(source){
    case PerfTimingSource::Cpu:
        return "cpu";
    case PerfTimingSource::Gpu:
        return "gpu";
    case PerfTimingSource::Unknown:
    default:
        return "unknown";
    }
}

bool BuildArchiveReport(TelemetryArena& arena, const EventView& events, ArchiveReport& outReport){
    outReport = ArchiveReport(arena);
    __hidden_telemetry_export::AppendPerfCsvHeader(outReport.perfCsv);

    if(!events.valid())
        return false;

    outReport.summary.eventCount = events.eventCount();
    for(usize i = 0u; i < events.eventCount(); ++i){
        const EventRecord* const event = events.eventAt(i);
        if(!event){
            ++outReport.summary.parseFailureCount;
            continue;
        }

        ++outReport.summary.eventKindCounts[__hidden_telemetry_export::EventKindBucket(event->header.kind)];
        __hidden_telemetry_export::RecordFrameRange(outReport.summary, event->header.frameIndex);

        switch(event->header.kind){
        case EventKind::TextLog: {
            TextLogPayload payload(arena);
            if(!ParseTextLogPayload(arena, event->payload.data(), event->payload.size(), payload))
                ++outReport.summary.parseFailureCount;
            break;
        }
        case EventKind::Diagnostic: {
            DiagnosticPayload payload(arena);
            if(!ParseDiagnosticPayload(arena, event->payload.data(), event->payload.size(), payload))
                ++outReport.summary.parseFailureCount;
            break;
        }
        case EventKind::PerfFrame: {
            PerfTimingPayload payload(arena);
            if(!ParsePerfTimingPayload(arena, event->payload.data(), event->payload.size(), payload)){
                ++outReport.summary.parseFailureCount;
                break;
            }
            __hidden_telemetry_export::AddTiming(outReport.summary, payload);
            __hidden_telemetry_export::AppendPerfCsvRow(arena, outReport.perfCsv, payload.source, payload);
            break;
        }
        case EventKind::MemoryFrame: {
            PerfMemoryPayload payload(arena);
            if(!ParsePerfMemoryPayload(arena, event->payload.data(), event->payload.size(), payload)){
                ++outReport.summary.parseFailureCount;
                break;
            }
            __hidden_telemetry_export::AddMemory(outReport.summary, payload);
            break;
        }
        case EventKind::FrameGraphFrame: {
            FrameGraphPayload payload(arena);
            if(!ParseFrameGraphPayload(arena, event->payload.data(), event->payload.size(), payload)){
                ++outReport.summary.parseFailureCount;
                break;
            }
            __hidden_telemetry_export::AddFrameGraph(outReport.summary, payload);
            break;
        }
        default:
            break;
        }
    }

    __hidden_telemetry_export::BuildJson(arena, outReport.summary, outReport.json);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
