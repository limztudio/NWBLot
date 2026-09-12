// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report_dot.h"

#include "report.h"

#include <global/hash_utils.h>
#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_TimedGraphDotFixedReserveBytes = 96u;
static constexpr usize s_TimedGraphDotBytesPerGraph = 128u;
static constexpr usize s_TimedGraphDotBytesPerNode = 768u;
static constexpr usize s_TimedGraphDotBytesPerEdge = 40u;
static constexpr usize s_TimedGraphDotTimingLabelExtraBytes = 16u;
static constexpr usize s_TimedGraphDotRuntimeStatisticsBytes = 192u;
static constexpr f64 s_MillisecondsPerSecond = 1000.0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FrameGraphOwnerStatisticsRange::advance(const Telemetry::FrameGraphPayload& graph, const u32 ownerNodeIndex)noexcept{
    physicalQueueBegin = physicalQueueEnd;
    while(
        physicalQueueEnd < graph.physicalQueueRuntimeStatistics.size()
        && graph.physicalQueueRuntimeStatistics[physicalQueueEnd].ownerNodeIndex == ownerNodeIndex
    )
        ++physicalQueueEnd;

    packetSubmissionBegin = packetSubmissionEnd;
    while(
        packetSubmissionEnd < graph.packetSubmissionStatistics.size()
        && graph.packetSubmissionStatistics[packetSubmissionEnd].ownerNodeIndex == ownerNodeIndex
    )
        ++packetSubmissionEnd;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GraphTimingKeyHasher::operator()(const GraphTimingKey& key)const noexcept{
    usize seed = Hasher<u64>{}(key.frameIndex);
    ::HashCombine(seed, key.scopeName);
    return seed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] usize EstimateTimedGraphDotReserve(const Telemetry::FrameGraphPayload& graph)noexcept{
    usize labelBytes = 0u;
    usize runtimeStatisticsBytes = 0u;
    for(const Telemetry::FrameGraphNodePayload& node : graph.nodes){
        labelBytes += node.label.size();
        if(node.runtimeStatistics.present)
            runtimeStatisticsBytes += s_TimedGraphDotRuntimeStatisticsBytes;
    }

    return s_TimedGraphDotFixedReserveBytes
        + s_TimedGraphDotBytesPerGraph
        + graph.nodes.size() * s_TimedGraphDotBytesPerNode
        + graph.edges.size() * s_TimedGraphDotBytesPerEdge
        + labelBytes
        + runtimeStatisticsBytes
    ;
}

[[nodiscard]] usize EstimateTimedGraphsDotReserve(const FrameGraphReportRecords& graphs)noexcept{
    usize reserveBytes = 0u;
    for(const FrameGraphReportRecord& graph : graphs)
        reserveBytes += EstimateTimedGraphDotReserve(graph.payload);
    return reserveBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView FrameGraphNodeIdentityText(
    const Telemetry::FrameGraphNodePayload& node,
    char (&identityText)[NameDetail::s_DebugHashTextLength + 1u]
)noexcept{
    NameDetail::HashToDebugString(node.name.hash(), identityText, sizeof(identityText));
    return AStringView(identityText, NameDetail::s_DebugHashTextLength);
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

[[nodiscard]] const char* FrameGraphNodeKindText(const Telemetry::FrameGraphNodeKind::Enum kind)noexcept{
    switch(kind){
    case Telemetry::FrameGraphNodeKind::Pass:
        return "pass";
    case Telemetry::FrameGraphNodeKind::Resource:
        return "resource";
    case Telemetry::FrameGraphNodeKind::External:
        return "external";
    case Telemetry::FrameGraphNodeKind::Unknown:
    default:
        return "unknown";
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

[[nodiscard]] const char* FrameGraphQueueClassText(const Telemetry::FrameGraphQueueClass::Enum queueClass)noexcept{
    switch(queueClass){
    case Telemetry::FrameGraphQueueClass::Graphics:
        return "graphics";
    case Telemetry::FrameGraphQueueClass::Compute:
        return "compute";
    case Telemetry::FrameGraphQueueClass::Transfer:
        return "transfer";
    case Telemetry::FrameGraphQueueClass::Unknown:
    default:
        return "unknown";
    }
}

[[nodiscard]] const char* FrameGraphQueueAssignmentReasonText(
    const Telemetry::FrameGraphQueueAssignmentReason::Enum reason
)noexcept{
    switch(reason){
    case Telemetry::FrameGraphQueueAssignmentReason::RequiredGraphics:
        return "requiredGraphics";
    case Telemetry::FrameGraphQueueAssignmentReason::PreferredQueue:
        return "preferredQueue";
    case Telemetry::FrameGraphQueueAssignmentReason::DedicatedCompute:
        return "dedicatedCompute";
    case Telemetry::FrameGraphQueueAssignmentReason::DedicatedTransfer:
        return "dedicatedTransfer";
    case Telemetry::FrameGraphQueueAssignmentReason::Fallback:
        return "fallback";
    case Telemetry::FrameGraphQueueAssignmentReason::ConservativeAny:
        return "conservativeAny";
    case Telemetry::FrameGraphQueueAssignmentReason::SameClassRouting:
        return "sameClassRouting";
    case Telemetry::FrameGraphQueueAssignmentReason::CompilerOverride:
        return "compilerOverride";
    case Telemetry::FrameGraphQueueAssignmentReason::ScoredAny:
        return "scoredAny";
    case Telemetry::FrameGraphQueueAssignmentReason::Unknown:
    default:
        return "unknown";
    }
}

[[nodiscard]] const char* FrameGraphQueueAssignmentAcceptanceText(
    const Telemetry::FrameGraphQueueAssignmentAcceptance::Enum acceptance
)noexcept{
    switch(acceptance){
    case Telemetry::FrameGraphQueueAssignmentAcceptance::First:
        return "first";
    case Telemetry::FrameGraphQueueAssignmentAcceptance::Unchanged:
        return "unchanged";
    case Telemetry::FrameGraphQueueAssignmentAcceptance::Changed:
        return "changed";
    case Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted:
        return "notAccepted";
    default:
        return "unknown";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const char* FrameGraphTaskPacketizationDecisionText(
    const Telemetry::FrameGraphTaskPacketizationDecision::Enum decision
)noexcept{
    switch(decision){
    case Telemetry::FrameGraphTaskPacketizationDecision::FirstTask:
        return "firstTask";
    case Telemetry::FrameGraphTaskPacketizationDecision::MergeNotRequested:
        return "mergeNotRequested";
    case Telemetry::FrameGraphTaskPacketizationDecision::TaskForcesBoundary:
        return "taskForcesBoundary";
    case Telemetry::FrameGraphTaskPacketizationDecision::QueueChanged:
        return "queueChanged";
    case Telemetry::FrameGraphTaskPacketizationDecision::PrecedingTaskForcesBoundary:
        return "precedingTaskForcesBoundary";
    case Telemetry::FrameGraphTaskPacketizationDecision::ScoredMergeIneligible:
        return "scoredMergeIneligible";
    case Telemetry::FrameGraphTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency:
        return "mergeRequiresExplicitImmediateDependency";
    case Telemetry::FrameGraphTaskPacketizationDecision::CrossQueueConsumerFrontier:
        return "crossQueueConsumerFrontier";
    case Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit:
        return "mergedExplicit";
    case Telemetry::FrameGraphTaskPacketizationDecision::MergedFrontierScored:
        return "mergedFrontierScored";
    case Telemetry::FrameGraphTaskPacketizationDecision::ScoredMergeDomainMismatch:
        return "scoredMergeDomainMismatch";
    case Telemetry::FrameGraphTaskPacketizationDecision::Unknown:
    default:
        return "unknown";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendFrameGraphPhysicalQueueDot(
    AString<TelemetryArena>& out,
    const AStringView prefix,
    const Telemetry::FrameGraphPhysicalQueueId& queue
){
    if(!queue.valid()){
        StringAppendFormat(out, ", {}_index=\"none\", {}_device_generation=\"none\"", prefix, prefix);
        return;
    }

    StringAppendFormat(out, ", {}_index={}, {}_device_generation={}", prefix, queue.index, prefix, queue.deviceGeneration);
}

void AppendFrameGraphQueueAssignmentDot(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphQueueAssignment& assignment
){
    if(!assignment.present){
        out += ", queue_assignment=\"none\"";
        return;
    }

    out += ", queue_assignment=\"present\"";
    AppendFrameGraphPhysicalQueueDot(out, "queue_initial", assignment.initialQueue);
    AppendFrameGraphPhysicalQueueDot(out, "queue_planned", assignment.plannedQueue);
    AppendFrameGraphPhysicalQueueDot(out, "queue_accepted", assignment.acceptedQueue);
    AppendFrameGraphPhysicalQueueDot(out, "queue_previous_accepted", assignment.previousAcceptedQueue);
    out += ", queue_class=";
    AppendDotQuotedText(out, AStringView(FrameGraphQueueClassText(assignment.queueClass)));
    out += ", queue_reason=";
    AppendDotQuotedText(out, AStringView(FrameGraphQueueAssignmentReasonText(assignment.reason)));
    StringAppendFormat(out, ", queue_modifier_mask={}", static_cast<u32>(assignment.modifiers));
    out += ", queue_acceptance=";
    AppendDotQuotedText(out, AStringView(FrameGraphQueueAssignmentAcceptanceText(assignment.acceptance)));
    StringAppendFormat(out, ", queue_dedicated={}", assignment.dedicated ? "true" : "false");
    StringAppendFormat(
        out,
        ", queue_score_preference={}, queue_score_overlap={}, queue_score_queue_load={}, queue_score_incoming_crossings={}, "
        "queue_score_outgoing_crossings={}, queue_score_ownership_transfers={}, queue_score_total={}",
        assignment.score.preference,
        assignment.score.overlap,
        assignment.score.queueLoad,
        assignment.score.incomingCrossings,
        assignment.score.outgoingCrossings,
        assignment.score.ownershipTransfers,
        assignment.score.total
    );
}

void AppendFrameGraphCompiledTaskDot(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphCompiledTask& compiledTask
){
    if(!compiledTask.present){
        out += ", compiled_task=\"none\"";
        return;
    }

    StringAppendFormat(
        out,
        ", compiled_task=\"present\", compiled_plan_generation={}, compiled_packet_index={}",
        compiledTask.planGeneration,
        compiledTask.packetIndex
    );
    out += ", packetization_decision=";
    AppendDotQuotedText(
        out,
        AStringView(FrameGraphTaskPacketizationDecisionText(compiledTask.packetizationDecision))
    );
}

void AppendFrameGraphRuntimeStatisticsDot(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const usize physicalQueueRuntimeStatisticsCount,
    const usize packetSubmissionStatisticsCount,
    const bool physicalQueueRuntimeStatisticsPresent,
    const bool packetSubmissionStatisticsPresent
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

// Joins each decoded frame-graph topology with timing from its exact frame and scope Name while retaining every
// capture, stable identity, and opaque producer-owned flag byte. A timing stream need not match the graph stream.
void AppendTimedGraphDot(
    TelemetryArena& arena,
    const FrameGraphReportRecord& record,
    const usize graphIndex,
    const GraphTimingMap& timing,
    AString<TelemetryArena>& out
){
    const Telemetry::FrameGraphPayload& graph = record.payload;
    StringAppendFormat(out, "digraph frame_graph_{}_{}_{} {{\n", graph.frameIndex, record.streamId, graphIndex);
    StringAppendFormat(out, "  graph [label=\"Frame {} stream {}\"];\n", graph.frameIndex, record.streamId);
    out += "  rankdir=LR;\n";
    out += "  node [shape=box, fontname=\"monospace\"];\n";

    AString<TelemetryArena> timedLabel(arena);
    FrameGraphOwnerStatisticsRange ownerRange;
    for(usize i = 0u; i < graph.nodes.size(); ++i){
        const Telemetry::FrameGraphNodePayload& node = graph.nodes[i];
        char identityText[NameDetail::s_DebugHashTextLength + 1u] = {};
        const AStringView identity = FrameGraphNodeIdentityText(node, identityText);

        const GraphTimingKey timingKey{ graph.frameIndex, node.name };
        const auto timed = timing.find(timingKey);
        const AStringView labelView(node.label.data(), node.label.size());
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
        AppendFrameGraphQueueAssignmentDot(out, node.queueAssignment);
        AppendFrameGraphCompiledTaskDot(out, node.compiledTask);
        ownerRange.advance(graph, static_cast<u32>(i));
        const usize physicalQueueRuntimeStatisticsCount = ownerRange.physicalQueueEnd - ownerRange.physicalQueueBegin;
        const usize packetSubmissionStatisticsCount = ownerRange.packetSubmissionEnd - ownerRange.packetSubmissionBegin;
        AppendFrameGraphRuntimeStatisticsDot(
            out,
            node.runtimeStatistics,
            physicalQueueRuntimeStatisticsCount,
            packetSubmissionStatisticsCount,
            graph.physicalQueueRuntimeStatisticsPresent && physicalQueueRuntimeStatisticsCount != 0u,
            graph.packetSubmissionStatisticsPresent
        );
        out += "];\n";
    }

    for(const Telemetry::FrameGraphEdgePayload& edge : graph.edges){
        StringAppendFormat(out, "  n{} -> n{} [label=", edge.fromNodeIndex, edge.toNodeIndex);
        AppendDotQuotedText(out, AStringView(FrameGraphEdgeLabel(edge.kind)));
        StringAppendFormat(out, ", flags={}];\n", static_cast<u32>(edge.flags));
    }

    out += "}\n";
}

void BuildTimedGraphsDot(
    TelemetryArena& arena,
    const FrameGraphReportRecords& graphs,
    const GraphTimingMap& timing,
    AString<TelemetryArena>& out
){
    out.clear();
    out.reserve(EstimateTimedGraphsDotReserve(graphs));
    for(usize graphIndex = 0u; graphIndex < graphs.size(); ++graphIndex){
        AppendTimedGraphDot(arena, graphs[graphIndex], graphIndex, timing, out);
        if(graphIndex + 1u != graphs.size())
            out += '\n';
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

