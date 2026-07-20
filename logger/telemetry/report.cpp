// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_MillisecondsPerSecond = 1000.0;
static constexpr usize s_PerfCsvFixedReserveBytes = 128u;
static constexpr usize s_PerfCsvBytesPerEvent = 128u;
static constexpr usize s_TimedGraphDotFixedReserveBytes = 96u;
static constexpr usize s_TimedGraphDotBytesPerNode = 64u;
static constexpr usize s_TimedGraphDotBytesPerEdge = 40u;
static constexpr usize s_TimedGraphDotTimingLabelExtraBytes = 16u;
static constexpr usize s_JsonReportReserveBytes = 1024u;

template<typename... Args>
void AppendFormat(AString<TelemetryArena>& out, AFormatString<Args...> fmt, Args&&... args){
    std::vformat_to(std::back_inserter(out), fmt.get(), std::make_format_args<AFormatContext>(args...));
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
    AString<TelemetryArena>& out,
    const Telemetry::PerfTimingSource::Enum source,
    const Telemetry::PerfTimingPayload& payload
){
    out += PerfTimingSourceText(source);
    out += ',';
    AppendCsvCell(out, payload.scopeText);
    AppendFormat(
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

[[nodiscard]] usize EstimatePerfCsvReserve(const usize eventCount)noexcept{
    return s_PerfCsvFixedReserveBytes + eventCount * s_PerfCsvBytesPerEvent;
}

[[nodiscard]] usize EstimateTimedGraphDotReserve(const Telemetry::FrameGraphPayload& graph)noexcept{
    usize labelBytes = 0u;
    for(const Telemetry::FrameGraphNodePayload& node : graph.nodes)
        labelBytes += node.label.size();

    return s_TimedGraphDotFixedReserveBytes
        + graph.nodes.size() * s_TimedGraphDotBytesPerNode
        + graph.edges.size() * s_TimedGraphDotBytesPerEdge
        + labelBytes
    ;
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
    out.reserve(EstimateTimedGraphDotReserve(graph));
    out += "digraph frame_graph {\n";
    out += "  rankdir=LR;\n";
    out += "  node [shape=box, fontname=\"monospace\"];\n";

    for(usize i = 0u; i < graph.nodes.size(); ++i){
        const Telemetry::FrameGraphNodePayload& node = graph.nodes[i];

        const auto timed = timing.find(node.name);
        const AStringView labelView(node.label.data(), node.label.size());
        AppendFormat(out, "  n{} [shape={}, label=", i, FrameGraphNodeShape(node.kind));
        if(timed != timing.end()){
            AString<TelemetryArena> label(arena);
            label.reserve(labelView.size() + s_TimedGraphDotTimingLabelExtraBytes);
            label.append(labelView.data(), labelView.size());
            label += '\n';
            AppendFormat(label, "{:.3f} ms", timed.value() * s_MillisecondsPerSecond);

            AppendDotQuotedText(out, AStringView(label.data(), label.size()));
        }
        else
            AppendDotQuotedText(out, labelView);
        out += "];\n";
    }

    for(const Telemetry::FrameGraphEdgePayload& edge : graph.edges){
        AppendFormat(out, "  n{} -> n{} [label=", edge.fromNodeIndex, edge.toNodeIndex);
        AppendDotQuotedText(out, AStringView(FrameGraphEdgeLabel(edge.kind)));
        out += "];\n";
    }

    out += "}\n";
}

void BuildJson(const TelemetryReportSummary& summary, AString<TelemetryArena>& out){
    out.clear();
    out.reserve(s_JsonReportReserveBytes);
    out += "{\n";
    AppendFormat(out, "  \"eventCount\": {},\n", summary.eventCount);
    out += "  \"frameRange\": {";
    AppendFormat(out, "\"present\": {}", summary.hasFrameRange ? "true" : "false");
    if(summary.hasFrameRange)
        AppendFormat(out, ", \"min\": {}, \"max\": {}", summary.minFrameIndex, summary.maxFrameIndex);
    out += "},\n";

    out += "  \"events\": {\n";
    for(usize i = 0u; i < s_TelemetryReportEventKindCount; ++i){
        out += "    ";
        AppendJsonQuotedText(out, EventKindText(static_cast<Telemetry::EventKind::Enum>(i)));
        AppendFormat(out, ": {}{}", summary.eventKindCounts[i], i + 1u == s_TelemetryReportEventKindCount ? "\n" : ",\n");
    }
    out += "  },\n";

    out += "  \"perf\": {\n";
    AppendFormat(out, "    \"cpuTimingEvents\": {},\n", summary.cpuTimingEventCount);
    AppendFormat(out, "    \"cpuTimingSamples\": {},\n", summary.cpuTimingSampleCount);
    AppendFormat(out, "    \"cpuTimingSeconds\": {:.9},\n", summary.cpuTimingSeconds);
    AppendFormat(out, "    \"maxCpuTimingSeconds\": {:.9},\n", summary.maxCpuTimingSeconds);
    AppendFormat(out, "    \"gpuTimingEvents\": {},\n", summary.gpuTimingEventCount);
    AppendFormat(out, "    \"gpuTimingSamples\": {},\n", summary.gpuTimingSampleCount);
    AppendFormat(out, "    \"gpuTimingSeconds\": {:.9},\n", summary.gpuTimingSeconds);
    AppendFormat(out, "    \"maxGpuTimingSeconds\": {:.9},\n", summary.maxGpuTimingSeconds);
    AppendFormat(out, "    \"memoryEvents\": {},\n", summary.memoryEventCount);
    AppendFormat(out, "    \"maxMemoryUsedBytes\": {},\n", summary.maxMemoryUsedBytes);
    AppendFormat(out, "    \"maxMemoryPeakUsedBytes\": {},\n", summary.maxMemoryPeakUsedBytes);
    AppendFormat(out, "    \"totalMemoryUsedDeltaBytes\": {}\n", summary.totalMemoryUsedDeltaBytes);
    out += "  },\n";

    out += "  \"frameGraph\": {\n";
    AppendFormat(out, "    \"frames\": {},\n", summary.frameGraphFrameCount);
    AppendFormat(out, "    \"nodes\": {},\n", summary.frameGraphNodeCount);
    AppendFormat(out, "    \"edges\": {},\n", summary.frameGraphEdgeCount);
    AppendFormat(out, "    \"maxNodes\": {},\n", summary.maxFrameGraphNodeCount);
    AppendFormat(out, "    \"maxEdges\": {}\n", summary.maxFrameGraphEdgeCount);
    out += "  },\n";
    AppendFormat(out, "  \"parseFailures\": {}\n", summary.parseFailureCount);
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

    __hidden_telemetry_report::GraphTimingMap timingByScope(0, Hasher<Name>(), EqualTo<Name>(), arena);
    timingByScope.reserve(events.eventCount());
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
            __hidden_telemetry_report::AppendPerfCsvRow(outReport.perfCsv, payload.source, payload);
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

    __hidden_telemetry_report::BuildJson(outReport.summary, outReport.json);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

