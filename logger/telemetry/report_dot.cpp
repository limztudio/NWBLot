// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report_dot.h"

#include <global/dot_text.h>
#include <global/hash_utils.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryReportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_TimedGraphDotFixedReserveBytes = 96u;
static constexpr usize s_TimedGraphDotBytesPerGraph = 128u;
static constexpr usize s_TimedGraphDotBytesPerNode = 768u;
static constexpr usize s_TimedGraphDotBytesPerEdge = 40u;
static constexpr usize s_TimedGraphDotTimingLabelExtraBytes = 16u;
static constexpr usize s_TimedGraphDotRuntimeStatisticsBytes = 192u;
static constexpr f64 s_MillisecondsPerSecond = 1000.0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FrameGraphOwnerStatisticsRange::advance(const Core::Telemetry::FrameGraphPayload& graph, const u32 ownerNodeIndex)noexcept{
    physicalQueueBegin = physicalQueueEnd;
    while(physicalQueueEnd < graph.physicalQueueRuntimeStatistics.size() && graph.physicalQueueRuntimeStatistics[physicalQueueEnd].ownerNodeIndex == ownerNodeIndex)
        ++physicalQueueEnd;

    packetSubmissionBegin = packetSubmissionEnd;
    while(packetSubmissionEnd < graph.packetSubmissionStatistics.size() && graph.packetSubmissionStatistics[packetSubmissionEnd].ownerNodeIndex == ownerNodeIndex)
        ++packetSubmissionEnd;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GraphTimingKeyHasher::operator()(const GraphTimingKey& key)const noexcept{
    usize seed = Hasher<u64>{}(key.frameIndex);
    ::HashCombine(seed, key.scopeName);
    return seed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] usize EstimateTimedGraphDotReserve(const Core::Telemetry::FrameGraphPayload& graph)noexcept{
    usize labelBytes = 0u;
    usize runtimeStatisticsBytes = 0u;
    for(const Core::Telemetry::FrameGraphNodePayload& node : graph.nodes){
        labelBytes += node.label.size();
        if(node.runtimeStatistics.present)
            runtimeStatisticsBytes += s_TimedGraphDotRuntimeStatisticsBytes;
    }

    return s_TimedGraphDotFixedReserveBytes + s_TimedGraphDotBytesPerGraph + graph.nodes.size() * s_TimedGraphDotBytesPerNode + graph.edges.size() * s_TimedGraphDotBytesPerEdge + labelBytes + runtimeStatisticsBytes;
}

[[nodiscard]] usize EstimateTimedGraphsDotReserve(const Span<const FrameGraphReportRecord> graphs)noexcept{
    usize reserveBytes = 0u;
    for(const FrameGraphReportRecord& graph : graphs)
        reserveBytes += EstimateTimedGraphDotReserve(graph.payload);
    return reserveBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView FrameGraphNodeShape(const Core::Telemetry::FrameGraphNodeKind::Enum kind)noexcept{
    switch(kind){
    case Core::Telemetry::FrameGraphNodeKind::Resource:
        return "ellipse";
    case Core::Telemetry::FrameGraphNodeKind::External:
        return "diamond";
    case Core::Telemetry::FrameGraphNodeKind::Pass:
    case Core::Telemetry::FrameGraphNodeKind::Unknown:
    default:
        return "box";
    }
}

[[nodiscard]] AStringView FrameGraphNodeKindText(const Core::Telemetry::FrameGraphNodeKind::Enum kind)noexcept{
    switch(kind){
    case Core::Telemetry::FrameGraphNodeKind::Pass:
        return "pass";
    case Core::Telemetry::FrameGraphNodeKind::Resource:
        return "resource";
    case Core::Telemetry::FrameGraphNodeKind::External:
        return "external";
    case Core::Telemetry::FrameGraphNodeKind::Unknown:
    default:
        return s_UnknownReportField;
    }
}

[[nodiscard]] AStringView FrameGraphEdgeLabel(const Core::Telemetry::FrameGraphEdgeKind::Enum kind)noexcept{
    switch(kind){
    case Core::Telemetry::FrameGraphEdgeKind::Reads:
        return "reads";
    case Core::Telemetry::FrameGraphEdgeKind::Writes:
        return "writes";
    case Core::Telemetry::FrameGraphEdgeKind::DependsOn:
        return "dependsOn";
    case Core::Telemetry::FrameGraphEdgeKind::Unknown:
    default:
        return "";
    }
}

[[nodiscard]] AStringView FrameGraphQueueClassText(const Core::Telemetry::FrameGraphQueueClass::Enum queueClass)noexcept{
    switch(queueClass){
    case Core::Telemetry::FrameGraphQueueClass::Graphics:
        return "graphics";
    case Core::Telemetry::FrameGraphQueueClass::Compute:
        return "compute";
    case Core::Telemetry::FrameGraphQueueClass::Transfer:
        return "transfer";
    case Core::Telemetry::FrameGraphQueueClass::Unknown:
    default:
        return s_UnknownReportField;
    }
}

[[nodiscard]] AStringView FrameGraphQueueAssignmentReasonText(const Core::Telemetry::FrameGraphQueueAssignmentReason::Enum reason)noexcept{
    switch(reason){
    case Core::Telemetry::FrameGraphQueueAssignmentReason::RequiredGraphics:
        return "requiredGraphics";
    case Core::Telemetry::FrameGraphQueueAssignmentReason::Conservative:
        return "conservative";
    case Core::Telemetry::FrameGraphQueueAssignmentReason::Scored:
        return "scored";
    case Core::Telemetry::FrameGraphQueueAssignmentReason::Unknown:
    default:
        return s_UnknownReportField;
    }
}

[[nodiscard]] AStringView FrameGraphQueueAssignmentAcceptanceText(const Core::Telemetry::FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept{
    switch(acceptance){
    case Core::Telemetry::FrameGraphQueueAssignmentAcceptance::First:
        return "first";
    case Core::Telemetry::FrameGraphQueueAssignmentAcceptance::Unchanged:
        return "unchanged";
    case Core::Telemetry::FrameGraphQueueAssignmentAcceptance::Changed:
        return "changed";
    case Core::Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted:
        return "notAccepted";
    default:
        return s_UnknownReportField;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView FrameGraphTaskPacketizationDecisionText(const Core::Telemetry::FrameGraphTaskPacketizationDecision::Enum decision)noexcept{
    switch(decision){
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::FirstTask:
        return "firstTask";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::MergeNotRequested:
        return "mergeNotRequested";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::TaskForcesBoundary:
        return "taskForcesBoundary";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::QueueChanged:
        return "queueChanged";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::PrecedingTaskForcesBoundary:
        return "precedingTaskForcesBoundary";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::ScoredMergeIneligible:
        return "scoredMergeIneligible";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency:
        return "mergeRequiresExplicitImmediateDependency";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::CrossQueueConsumerFrontier:
        return "crossQueueConsumerFrontier";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit:
        return "mergedExplicit";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::MergedFrontierScored:
        return "mergedFrontierScored";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::ScoredMergeDomainMismatch:
        return "scoredMergeDomainMismatch";
    case Core::Telemetry::FrameGraphTaskPacketizationDecision::Unknown:
    default:
        return s_UnknownReportField;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendFrameGraphPhysicalQueueDot(const AStringView prefix, const Core::Telemetry::FrameGraphPhysicalQueueId& queue, AString<Core::Telemetry::TelemetryArena>& out){
    if(!queue.valid()){
        StringAppendFormat(out, ", {}_index=\"none\", {}_device_generation=\"none\"", prefix, prefix);
        return;
    }

    StringAppendFormat(out, ", {}_index={}, {}_device_generation={}", prefix, queue.index, prefix, queue.deviceGeneration);
}

void AppendFrameGraphQueueAssignmentDot(const Core::Telemetry::FrameGraphQueueAssignment& assignment, AString<Core::Telemetry::TelemetryArena>& out){
    if(!assignment.present){
        out += ", queue_assignment=\"none\"";
        return;
    }

    out += ", queue_assignment=\"present\"";
    AppendFrameGraphPhysicalQueueDot("queue_initial", assignment.initialQueue, out);
    AppendFrameGraphPhysicalQueueDot("queue_planned", assignment.plannedQueue, out);
    AppendFrameGraphPhysicalQueueDot("queue_accepted", assignment.acceptedQueue, out);
    AppendFrameGraphPhysicalQueueDot("queue_previous_accepted", assignment.previousAcceptedQueue, out);
    out += ", queue_class=";
    AppendDotQuotedText(out, AStringView(FrameGraphQueueClassText(assignment.queueClass)));
    out += ", queue_reason=";
    AppendDotQuotedText(out, AStringView(FrameGraphQueueAssignmentReasonText(assignment.reason)));
    StringAppendFormat(out, ", queue_modifier_mask={}", static_cast<u32>(assignment.modifiers));
    out += ", queue_acceptance=";
    AppendDotQuotedText(out, AStringView(FrameGraphQueueAssignmentAcceptanceText(assignment.acceptance)));
    StringAppendFormat(out, ", queue_dedicated={}", assignment.dedicated ? s_TrueText : s_FalseText);
    StringAppendFormat(
        out,
        ", queue_score_overlap={}, queue_score_queue_load={}, queue_score_incoming_crossings={}, "
        "queue_score_outgoing_crossings={}, queue_score_ownership_transfers={}, queue_score_total={}",
        assignment.score.overlap,
        assignment.score.queueLoad,
        assignment.score.incomingCrossings,
        assignment.score.outgoingCrossings,
        assignment.score.ownershipTransfers,
        assignment.score.total
    );
}

void AppendFrameGraphCompiledTaskDot(const Core::Telemetry::FrameGraphCompiledTask& compiledTask, AString<Core::Telemetry::TelemetryArena>& out){
    if(!compiledTask.present){
        out += ", compiled_task=\"none\"";
        return;
    }

    StringAppendFormat(out, ", compiled_task=\"present\", compiled_plan_generation={}, compiled_packet_index={}", compiledTask.planGeneration, compiledTask.packetIndex);
    out += ", packetization_decision=";
    AppendDotQuotedText(out, AStringView(FrameGraphTaskPacketizationDecisionText(compiledTask.packetizationDecision)));
}

void AppendFrameGraphRuntimeStatisticsDot(
    const Core::Telemetry::FrameGraphRuntimeStatistics& statistics,
    const usize physicalQueueRuntimeStatisticsCount,
    const usize packetSubmissionStatisticsCount,
    const b8 physicalQueueRuntimeStatisticsPresent,
    const b8 packetSubmissionStatisticsPresent,
    AString<Core::Telemetry::TelemetryArena>& out
){
    if(!statistics.present){
        out += ", runtime_statistics=\"none\"";
        return;
    }

    StringAppendFormat(
        out,
        ", runtime_statistics=\"present\", runtime_graph_generation={}, runtime_plan_generation={}, "
        "runtime_recording_attempt_generation={}, runtime_device_generation={}",
        statistics.graphGeneration,
        statistics.planGeneration,
        statistics.recordingAttemptGeneration,
        statistics.deviceGeneration
    );
    if(physicalQueueRuntimeStatisticsPresent)
        StringAppendFormat(out, ", runtime_physical_queue_count={}", physicalQueueRuntimeStatisticsCount);
    else
        out += ", runtime_physical_queue_count=\"unknown\"";
    if(packetSubmissionStatisticsPresent)
        StringAppendFormat(out, ", runtime_packet_submission_count={}", packetSubmissionStatisticsCount);
    else
        out += ", runtime_packet_submission_count=\"unknown\"";
}

// Join timing by exact frame and scope Name; retain every graph, stable identity, and opaque producer flag.
// Timing and graph streams may differ.
void AppendTimedGraphDot(const FrameGraphReportRecord& record, const usize graphIndex, const GraphTimingMap& timing, Core::Alloc::ScratchArena& scratchArena, AString<Core::Telemetry::TelemetryArena>& out){
    const Core::Telemetry::FrameGraphPayload& graph = record.payload;
    StringAppendFormat(out, "digraph frame_graph_{}_{}_{} {{\n", graph.frameIndex, record.streamId, graphIndex);
    StringAppendFormat(out, "  graph [label=\"Frame {} stream {}\"];\n", graph.frameIndex, record.streamId);
    out += "  rankdir=LR;\n";
    out += "  node [shape=box, fontname=\"monospace\"];\n";

    AString<Core::Alloc::ScratchArena> timedLabel(scratchArena);
    FrameGraphOwnerStatisticsRange ownerRange;
    for(usize i = 0u; i < graph.nodes.size(); ++i){
        const Core::Telemetry::FrameGraphNodePayload& node = graph.nodes[i];
        Array<char, s_DebugHashTextLength + 1u> identityText;
        const AStringView identity = FormatNameIdentityText(node.name, identityText);

        const GraphTimingKey timingKey{ graph.frameIndex, node.name };
        const auto timed = timing.find(timingKey);
        const AStringView labelView = node.label;
        StringAppendFormat(out, "  n{} [shape={}, label=", i, FrameGraphNodeShape(node.kind));
        if(timed != timing.end()){
            timedLabel.clear();
            timedLabel.reserve(labelView.size() + s_TimedGraphDotTimingLabelExtraBytes);
            timedLabel.append(labelView.data(), labelView.size());
            timedLabel += '\n';
            StringAppendFormat(timedLabel, "{:.3f} ms", timed.value() * s_MillisecondsPerSecond);

            AppendDotQuotedText(out, AStringView(timedLabel.data(), timedLabel.size()));
        }
        else
            AppendDotQuotedText(out, labelView);
        out += ", identity=";
        AppendDotQuotedText(out, identity);
        out += ", kind=";
        AppendDotQuotedText(out, AStringView(FrameGraphNodeKindText(node.kind)));
        StringAppendFormat(out, ", flags={}", static_cast<u32>(node.flags));
        AppendFrameGraphQueueAssignmentDot(node.queueAssignment, out);
        AppendFrameGraphCompiledTaskDot(node.compiledTask, out);
        ownerRange.advance(graph, static_cast<u32>(i));
        const usize physicalQueueRuntimeStatisticsCount = ownerRange.physicalQueueEnd - ownerRange.physicalQueueBegin;
        const usize packetSubmissionStatisticsCount = ownerRange.packetSubmissionEnd - ownerRange.packetSubmissionBegin;
        AppendFrameGraphRuntimeStatisticsDot(node.runtimeStatistics, physicalQueueRuntimeStatisticsCount, packetSubmissionStatisticsCount, physicalQueueRuntimeStatisticsCount != 0u, graph.packetSubmissionStatisticsPresent, out);
        out += "];\n";
    }

    for(const Core::Telemetry::FrameGraphEdgePayload& edge : graph.edges){
        StringAppendFormat(out, "  n{} -> n{} [label=", edge.fromNodeIndex, edge.toNodeIndex);
        AppendDotQuotedText(out, AStringView(FrameGraphEdgeLabel(edge.kind)));
        StringAppendFormat(out, ", flags={}];\n", static_cast<u32>(edge.flags));
    }

    out += "}\n";
}

void BuildTimedGraphsDot(const Span<const FrameGraphReportRecord> graphs, const GraphTimingMap& timing, Core::Alloc::ScratchArena& scratchArena, AString<Core::Telemetry::TelemetryArena>& out){
    out.clear();
    out.reserve(EstimateTimedGraphsDotReserve(graphs));
    for(usize graphIndex = 0u; graphIndex < graphs.size(); ++graphIndex){
        AppendTimedGraphDot(graphs[graphIndex], graphIndex, timing, scratchArena, out);
        if(graphIndex + 1u != graphs.size())
            out += '\n';
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

