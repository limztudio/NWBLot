// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;


namespace __hidden_telemetry_report{


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
    TelemetryArena& arena,
    AString<TelemetryArena>& out,
    const Telemetry::PerfTimingSource::Enum source,
    const Telemetry::PerfTimingPayload& payload
){
    out += PerfTimingSourceText(source);
    out += ',';
    AppendCsvCell(out, payload.scopeText);
    AppendFormat(
        arena,
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

void AddMemory(TelemetryReportSummary& summary, const Telemetry::PerfMemoryPayload& payload){
    ++summary.memoryEventCount;
    if(payload.snapshot.usedBytes > summary.maxMemoryUsedBytes)
        summary.maxMemoryUsedBytes = payload.snapshot.usedBytes;
    if(payload.snapshot.peakUsedBytes > summary.maxMemoryPeakUsedBytes)
        summary.maxMemoryPeakUsedBytes = payload.snapshot.peakUsedBytes;
    if(payload.delta.hasSamples)
        summary.totalMemoryUsedDeltaBytes += payload.delta.usedBytes;
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

using GraphTimingMap = HashMap<Name, f64, Hasher<Name>, EqualTo<Name>, TelemetryArena>;

void AppendDotQuoted(AString<TelemetryArena>& out, const AStringView text){
    out += '"';
    for(const char ch : text){
        if(ch == '\n'){
            out += "\\n";
            continue;
        }
        if(ch == '"' || ch == '\\')
            out += '\\';
        out += ch;
    }
    out += '"';
}

[[nodiscard]] const char* FrameGraphNodeShape(const Telemetry::FrameGraphNodeKind::Enum kind)noexcept{
    switch(kind){
    case Telemetry::FrameGraphNodeKind::Resource:
        return "ellipse";
    case Telemetry::FrameGraphNodeKind::External:
        return "diamond";
    case Telemetry::FrameGraphNodeKind::Pass:
    case Telemetry::FrameGraphNodeKind::Unknown:
    default:
        return "box";
    }
}

[[nodiscard]] const char* FrameGraphEdgeLabel(const Telemetry::FrameGraphEdgeKind::Enum kind)noexcept{
    switch(kind){
    case Telemetry::FrameGraphEdgeKind::Reads:
        return "reads";
    case Telemetry::FrameGraphEdgeKind::Writes:
        return "writes";
    case Telemetry::FrameGraphEdgeKind::DependsOn:
        return "dependsOn";
    case Telemetry::FrameGraphEdgeKind::Unknown:
    default:
        return "";
    }
}

// Joins the decoded frame-graph topology with per-scope timing (keyed by Name hash, which is
// shared between graph nodes and perf-timing scopes) and emits a Graphviz DOT timed frame graph.
void BuildTimedGraphDot(TelemetryArena& arena, const Telemetry::FrameGraphPayload& graph, const GraphTimingMap& timing, AString<TelemetryArena>& out){
    out.clear();
    out += "digraph frame_graph {\n";
    out += "  rankdir=LR;\n";
    out += "  node [shape=box, fontname=\"monospace\"];\n";

    for(usize i = 0u; i < graph.nodes.size(); ++i){
        const Telemetry::FrameGraphNodePayload& node = graph.nodes[i];

        AString<TelemetryArena> label(arena);
        label.append(node.label.data(), node.label.size());
        const auto timed = timing.find(node.name);
        if(timed != timing.end()){
            label += '\n';
            label += StringFormat(arena, "{:.3f} ms", timed.value() * 1000.0);
        }

        AppendFormat(arena, out, "  n{} [shape={}, label=", i, FrameGraphNodeShape(node.kind));
        AppendDotQuoted(out, AStringView(label.data(), label.size()));
        out += "];\n";
    }

    for(const Telemetry::FrameGraphEdgePayload& edge : graph.edges){
        AppendFormat(arena, out, "  n{} -> n{} [label=", edge.fromNodeIndex, edge.toNodeIndex);
        AppendDotQuoted(out, AStringView(FrameGraphEdgeLabel(edge.kind)));
        out += "];\n";
    }

    out += "}\n";
}

void BuildJson(TelemetryArena& arena, const TelemetryReportSummary& summary, AString<TelemetryArena>& out){
    out.clear();
    out += "{\n";
    AppendFormat(arena, out, "  \"eventCount\": {},\n", summary.eventCount);
    out += "  \"frameRange\": {";
    AppendFormat(arena, out, "\"present\": {}", summary.hasFrameRange ? "true" : "false");
    if(summary.hasFrameRange)
        AppendFormat(arena, out, ", \"min\": {}, \"max\": {}", summary.minFrameIndex, summary.maxFrameIndex);
    out += "},\n";

    out += "  \"events\": {\n";
    for(usize i = 0u; i < s_TelemetryReportEventKindCount; ++i){
        out += "    ";
        AppendJsonString(out, EventKindText(static_cast<Telemetry::EventKind::Enum>(i)));
        AppendFormat(arena, out, ": {}{}", summary.eventKindCounts[i], i + 1u == s_TelemetryReportEventKindCount ? "\n" : ",\n");
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


const char* EventKindText(const Telemetry::EventKind::Enum kind)noexcept{
    switch(kind){
    case Telemetry::EventKind::TextLog:
        return "textLog";
    case Telemetry::EventKind::Diagnostic:
        return "diagnostic";
    case Telemetry::EventKind::CrashUpload:
        return "crashUpload";
    case Telemetry::EventKind::PerfFrame:
        return "perfFrame";
    case Telemetry::EventKind::FrameGraphFrame:
        return "frameGraphFrame";
    case Telemetry::EventKind::Custom:
        return "custom";
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
    __hidden_telemetry_report::AppendPerfCsvHeader(outReport.perfCsv);

    if(!events.valid())
        return false;

    outReport.summary.eventCount = events.eventCount();

    __hidden_telemetry_report::GraphTimingMap timingByScope(0, Hasher<Name>(), EqualTo<Name>(), arena);
    usize lastFrameGraphIndex = events.eventCount();

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
            __hidden_telemetry_report::AppendPerfCsvRow(arena, outReport.perfCsv, payload.source, payload);
            timingByScope.insert_or_assign(payload.scopeName, payload.stats.seconds);
            break;
        }
        case Telemetry::EventKind::MemoryFrame: {
            Telemetry::PerfMemoryPayload payload(arena);
            if(!Telemetry::ParsePerfMemoryPayload(arena, event->payload.data(), event->payload.size(), payload)){
                ++outReport.summary.parseFailureCount;
                break;
            }
            __hidden_telemetry_report::AddMemory(outReport.summary, payload);
            break;
        }
        case Telemetry::EventKind::FrameGraphFrame: {
            Telemetry::FrameGraphPayload payload(arena);
            if(!Telemetry::ParseFrameGraphPayload(arena, event->payload.data(), event->payload.size(), payload)){
                ++outReport.summary.parseFailureCount;
                break;
            }
            __hidden_telemetry_report::AddFrameGraph(outReport.summary, payload);
            lastFrameGraphIndex = i;
            break;
        }
        default:
            break;
        }
    }

    if(lastFrameGraphIndex < events.eventCount()){
        const Telemetry::EventRecord* const graphEvent = events.eventAt(lastFrameGraphIndex);
        if(graphEvent){
            Telemetry::FrameGraphPayload graphPayload(arena);
            if(Telemetry::ParseFrameGraphPayload(arena, graphEvent->payload.data(), graphEvent->payload.size(), graphPayload))
                __hidden_telemetry_report::BuildTimedGraphDot(arena, graphPayload, timingByScope, outReport.graph);
        }
    }

    __hidden_telemetry_report::BuildJson(arena, outReport.summary, outReport.json);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

