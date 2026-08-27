// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report.h"

#include <global/hash_utils.h>
#include <global/type_properties.h>


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
static constexpr usize s_TimedGraphDotBytesPerGraph = 128u;
static constexpr usize s_TimedGraphDotBytesPerNode = 768u;
static constexpr usize s_TimedGraphDotBytesPerEdge = 40u;
static constexpr usize s_TimedGraphDotTimingLabelExtraBytes = 16u;
static constexpr usize s_TimedGraphDotRuntimeStatisticsBytes = 192u;
static constexpr usize s_JsonReportReserveBytes = 1024u;
static constexpr usize s_JsonReportBytesPerGraph = 128u;
static constexpr usize s_JsonReportBytesPerNode = 896u;
static constexpr usize s_JsonReportBytesPerEdge = 96u;
static constexpr usize s_JsonReportRuntimeStatisticsBytes = 4096u;

struct FrameGraphReportRecord{
    u32 streamId = 0u;
    Telemetry::FrameGraphPayload payload;

    explicit FrameGraphReportRecord(TelemetryArena& arena)
        : payload(arena)
    {}
};

using FrameGraphReportRecords = Vector<FrameGraphReportRecord, TelemetryArena>;

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

struct GraphTimingKey{
    u64 frameIndex = 0u;
    Name scopeName = NAME_NONE;
};

inline bool operator==(const GraphTimingKey& lhs, const GraphTimingKey& rhs)noexcept{
    return lhs.frameIndex == rhs.frameIndex && lhs.scopeName == rhs.scopeName;
}

struct GraphTimingKeyHasher{
    usize operator()(const GraphTimingKey& key)const noexcept{
        usize seed = Hasher<u64>{}(key.frameIndex);
        ::HashCombine(seed, key.scopeName);
        return seed;
    }
};

using GraphTimingMap = HashMap<GraphTimingKey, f64, GraphTimingKeyHasher, EqualTo<GraphTimingKey>, TelemetryArena>;

[[nodiscard]] usize EstimatePerfCsvReserve(const usize eventCount)noexcept{
    return s_PerfCsvFixedReserveBytes + eventCount * s_PerfCsvBytesPerEvent;
}

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

[[nodiscard]] usize EstimateJsonReportReserve(const FrameGraphReportRecords& graphs)noexcept{
    usize reserveBytes = s_JsonReportReserveBytes;
    for(const FrameGraphReportRecord& graph : graphs){
        reserveBytes += s_JsonReportBytesPerGraph;
        reserveBytes += graph.payload.nodes.size() * s_JsonReportBytesPerNode;
        reserveBytes += graph.payload.edges.size() * s_JsonReportBytesPerEdge;
        for(const Telemetry::FrameGraphNodePayload& node : graph.payload.nodes){
            reserveBytes += node.label.size();
            if(node.runtimeStatistics.present)
                reserveBytes += s_JsonReportRuntimeStatisticsBytes;
        }
    }
    return reserveBytes;
}

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

void AppendFrameGraphPhysicalQueueJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueId& queue
){
    if(!queue.valid()){
        out += "null";
        return;
    }

    StringAppendFormat(out, "{{\"index\": {}, \"deviceGeneration\": {}}}", queue.index, queue.deviceGeneration);
}

void AppendFrameGraphQueueAssignmentJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphQueueAssignment& assignment
){
    if(!assignment.present){
        out += "null";
        return;
    }

    out += "{\"initialQueue\": ";
    AppendFrameGraphPhysicalQueueJson(out, assignment.initialQueue);
    out += ", \"plannedQueue\": ";
    AppendFrameGraphPhysicalQueueJson(out, assignment.plannedQueue);
    out += ", \"acceptedQueue\": ";
    AppendFrameGraphPhysicalQueueJson(out, assignment.acceptedQueue);
    out += ", \"previousAcceptedQueue\": ";
    AppendFrameGraphPhysicalQueueJson(out, assignment.previousAcceptedQueue);
    out += ", \"queueClass\": ";
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueClassText(assignment.queueClass)));
    out += ", \"reason\": ";
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueAssignmentReasonText(assignment.reason)));
    StringAppendFormat(out, ", \"modifierMask\": {}", static_cast<u32>(assignment.modifiers));
    out += ", \"acceptance\": ";
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueAssignmentAcceptanceText(assignment.acceptance)));
    StringAppendFormat(out, ", \"dedicated\": {}", assignment.dedicated ? "true" : "false");
    StringAppendFormat(
        out,
        ", \"score\": {{\"preference\": {}, \"overlap\": {}, \"queueLoad\": {}, \"incomingCrossings\": {}, "
        "\"outgoingCrossings\": {}, \"ownershipTransfers\": {}, \"total\": {}}}}}",
        assignment.score.preference,
        assignment.score.overlap,
        assignment.score.queueLoad,
        assignment.score.incomingCrossings,
        assignment.score.outgoingCrossings,
        assignment.score.ownershipTransfers,
        assignment.score.total
    );
}

void AppendFrameGraphCompiledTaskJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphCompiledTask& compiledTask
){
    if(!compiledTask.present){
        out += "null";
        return;
    }

    StringAppendFormat(
        out,
        "{{\"planGeneration\": {}, \"packetIndex\": {}, \"packetizationDecision\": ",
        compiledTask.planGeneration,
        compiledTask.packetIndex
    );
    AppendJsonQuotedText(
        out,
        AStringView(FrameGraphTaskPacketizationDecisionText(compiledTask.packetizationDecision))
    );
    out += '}';
}

void AppendFrameGraphCompileRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphCompileRuntimeStatistics& statistics
){
    StringAppendFormat(
        out,
        "{{\"taskCount\": {}, \"resourceCount\": {}, \"resourceUseCount\": {}, \"explicitDependencyCount\": {}, "
        "\"inferredDependencyCount\": {}, \"packetCount\": {}, \"packetDependencyCount\": {}, \"mergedTaskCount\": {}",
        statistics.taskCount,
        statistics.resourceCount,
        statistics.resourceUseCount,
        statistics.explicitDependencyCount,
        statistics.inferredDependencyCount,
        statistics.packetCount,
        statistics.packetDependencyCount,
        statistics.mergedTaskCount
    );
    StringAppendFormat(
        out,
        ", \"transitionBarrierCount\": {}, \"uavBarrierCount\": {}, \"ownershipReleaseBarrierCount\": {}, "
        "\"ownershipAcquireBarrierCount\": {}, \"stateExportBarrierCount\": {}",
        statistics.transitionBarrierCount,
        statistics.uavBarrierCount,
        statistics.ownershipReleaseBarrierCount,
        statistics.ownershipAcquireBarrierCount,
        statistics.stateExportBarrierCount
    );
    StringAppendFormat(
        out,
        ", \"logicalOwnershipTransferCount\": {}, \"logicalOwnershipTransferSignatureCount\": {}, "
        "\"repeatedOwnershipTransferSignatureCount\": {}, \"concurrentSharingCouldAvoidTransferCount\": {}, "
        "\"concurrentSharingAdviceResourceCount\": {}, \"logicalOwnershipTransferInternalCount\": {}, "
        "\"logicalOwnershipTransferExternalImportCount\": {}, \"logicalOwnershipTransferExternalExportCount\": {}",
        statistics.logicalOwnershipTransferCount,
        statistics.logicalOwnershipTransferSignatureCount,
        statistics.repeatedOwnershipTransferSignatureCount,
        statistics.concurrentSharingCouldAvoidTransferCount,
        statistics.concurrentSharingAdviceResourceCount,
        statistics.logicalOwnershipTransferInternalCount,
        statistics.logicalOwnershipTransferExternalImportCount,
        statistics.logicalOwnershipTransferExternalExportCount
    );
    StringAppendFormat(
        out,
        ", \"resourceSetCount\": {}, \"resourceSetMemberCount\": {}, \"directResourceUseCount\": {}, "
        "\"declaredResourceSetUseCount\": {}, \"expandedResourceSetMemberUseCount\": {}, \"payloadObjectCount\": {}, "
        "\"payloadObjectBytes\": {}, \"uploadBlobCount\": {}, \"uploadBlobBytes\": {}",
        statistics.resourceSetCount,
        statistics.resourceSetMemberCount,
        statistics.directResourceUseCount,
        statistics.declaredResourceSetUseCount,
        statistics.expandedResourceSetMemberUseCount,
        statistics.payloadObjectCount,
        statistics.payloadObjectBytes,
        statistics.uploadBlobCount,
        statistics.uploadBlobBytes
    );
    StringAppendFormat(
        out,
        ", \"declarationSeconds\": {:.9}, \"analysisSeconds\": {:.9}, \"validationSeconds\": {:.9}, "
        "\"dependencyAnalysisSeconds\": {:.9}, \"hazardAnalysisSeconds\": {:.9}, \"topologicalOrderSeconds\": {:.9}",
        statistics.declarationSeconds,
        statistics.analysisSeconds,
        statistics.validationSeconds,
        statistics.dependencyAnalysisSeconds,
        statistics.hazardAnalysisSeconds,
        statistics.topologicalOrderSeconds
    );
    StringAppendFormat(
        out,
        ", \"queueAssignmentSeconds\": {:.9}, \"planningSeconds\": {:.9}, \"packetizationSeconds\": {:.9}, "
        "\"resourceStatePlanningSeconds\": {:.9}, \"packetDependencyPlanningSeconds\": {:.9}, \"totalSeconds\": {:.9}}}",
        statistics.queueAssignmentSeconds,
        statistics.planningSeconds,
        statistics.packetizationSeconds,
        statistics.resourceStatePlanningSeconds,
        statistics.packetDependencyPlanningSeconds,
        statistics.totalSeconds
    );
}

void AppendFrameGraphRecordingRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRecordingRuntimeStatistics& statistics
){
    StringAppendFormat(
        out,
        "{{\"packetCount\": {}, \"taskCount\": {}, \"commandListCount\": {}, \"barrierCount\": {}, "
        "\"workerRoutedPacketCount\": {}, \"parallelPacketCount\": {}",
        statistics.packetCount,
        statistics.taskCount,
        statistics.commandListCount,
        statistics.barrierCount,
        statistics.workerRoutedPacketCount,
        statistics.parallelPacketCount
    );
    StringAppendFormat(
        out,
        ", \"commandListAcquisitionSeconds\": {:.9}, \"graphBarrierRecordingSeconds\": {:.9}, "
        "\"taskRecordSeconds\": {:.9}, \"recordingSeconds\": {:.9}, \"recordingElapsedSeconds\": {:.9}, "
        "\"readyFrontierElapsedSeconds\": {:.9}, \"readyFrontierWorkerBusySeconds\": {:.9}, "
        "\"readyFrontierWorkerCapacitySeconds\": {:.9}}}",
        statistics.commandListAcquisitionSeconds,
        statistics.graphBarrierRecordingSeconds,
        statistics.taskRecordSeconds,
        statistics.recordingSeconds,
        statistics.recordingElapsedSeconds,
        statistics.readyFrontierElapsedSeconds,
        statistics.readyFrontierWorkerBusySeconds,
        statistics.readyFrontierWorkerCapacitySeconds
    );
}

void AppendFrameGraphSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphSubmissionRuntimeStatistics& statistics
){
    StringAppendFormat(
        out,
        "{{\"acceptedPacketCount\": {}, \"acceptedTaskCount\": {}, \"rejectedPacketCount\": {}, "
        "\"rejectedTaskCount\": {}, \"nativeSubmissionCount\": {}, \"rejectedSubmissionCount\": {}, "
        "\"nativeCommandListCount\": {}, \"plannedWaitTokenCount\": {}, \"sameQueueWaitElisionCount\": {}, "
        "\"timelineWaitCount\": {}, \"mergedTimelineWaitCount\": {}, \"acceptedFrontierSubmissionCount\": {}, "
        "\"submissionSeconds\": {:.9}}}",
        statistics.acceptedPacketCount,
        statistics.acceptedTaskCount,
        statistics.rejectedPacketCount,
        statistics.rejectedTaskCount,
        statistics.nativeSubmissionCount,
        statistics.rejectedSubmissionCount,
        statistics.nativeCommandListCount,
        statistics.plannedWaitTokenCount,
        statistics.sameQueueWaitElisionCount,
        statistics.timelineWaitCount,
        statistics.mergedTimelineWaitCount,
        statistics.acceptedFrontierSubmissionCount,
        statistics.submissionSeconds
    );
}

void AppendFrameGraphRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRuntimeStatistics& statistics
){
    if(!statistics.present){
        out += "null";
        return;
    }

    StringAppendFormat(
        out,
        "{{\"graphGeneration\": {}, \"planGeneration\": {}, \"recordingAttemptGeneration\": {}, "
        "\"deviceGeneration\": {}, \"compile\": ",
        statistics.graphGeneration,
        statistics.planGeneration,
        statistics.recordingAttemptGeneration,
        statistics.deviceGeneration
    );
    AppendFrameGraphCompileRuntimeStatisticsJson(out, statistics.compile);
    out += ", \"recording\": ";
    AppendFrameGraphRecordingRuntimeStatisticsJson(out, statistics.recording);
    out += ", \"submission\": ";
    AppendFrameGraphSubmissionRuntimeStatisticsJson(out, statistics.submission);
    out += '}';
}

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
    const Telemetry::FrameGraphRuntimeStatistics& statistics
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
        AppendFrameGraphRuntimeStatisticsDot(out, node.runtimeStatistics);
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

void AppendFrameGraphJson(
    const FrameGraphReportRecord& record,
    const usize graphIndex,
    const bool finalGraph,
    AString<TelemetryArena>& out
){
    const Telemetry::FrameGraphPayload& graph = record.payload;
    out += "      {\n";
    StringAppendFormat(out, "        \"captureIndex\": {},\n", graphIndex);
    StringAppendFormat(out, "        \"frameIndex\": {},\n", graph.frameIndex);
    StringAppendFormat(out, "        \"streamId\": {},\n", record.streamId);
    out += "        \"nodes\": [\n";
    for(usize nodeIndex = 0u; nodeIndex < graph.nodes.size(); ++nodeIndex){
        const Telemetry::FrameGraphNodePayload& node = graph.nodes[nodeIndex];
        char identityText[NameDetail::s_DebugHashTextLength + 1u] = {};
        const AStringView identity = FrameGraphNodeIdentityText(node, identityText);

        StringAppendFormat(out, "          {{\"index\": {}, \"identity\": ", nodeIndex);
        AppendJsonQuotedText(out, identity);
        out += ", \"label\": ";
        AppendJsonQuotedText(out, AStringView(node.label.data(), node.label.size()));
        out += ", \"kind\": ";
        AppendJsonQuotedText(out, AStringView(FrameGraphNodeKindText(node.kind)));
        StringAppendFormat(out, ", \"flags\": {}, \"queueAssignment\": ", static_cast<u32>(node.flags));
        AppendFrameGraphQueueAssignmentJson(out, node.queueAssignment);
        out += ", \"compiledTask\": ";
        AppendFrameGraphCompiledTaskJson(out, node.compiledTask);
        out += ", \"runtimeStatistics\": ";
        AppendFrameGraphRuntimeStatisticsJson(out, node.runtimeStatistics);
        StringAppendFormat(out, "}}{}\n", nodeIndex + 1u == graph.nodes.size() ? "" : ",");
    }
    out += "        ],\n";
    out += "        \"edges\": [\n";
    for(usize edgeIndex = 0u; edgeIndex < graph.edges.size(); ++edgeIndex){
        const Telemetry::FrameGraphEdgePayload& edge = graph.edges[edgeIndex];
        StringAppendFormat(
            out,
            "          {{\"from\": {}, \"to\": {}, \"kind\": ",
            edge.fromNodeIndex,
            edge.toNodeIndex
        );
        AppendJsonQuotedText(out, AStringView(FrameGraphEdgeLabel(edge.kind)));
        StringAppendFormat(
            out,
            ", \"flags\": {}}}{}\n",
            static_cast<u32>(edge.flags),
            edgeIndex + 1u == graph.edges.size() ? "" : ","
        );
    }
    out += "        ]\n";
    StringAppendFormat(out, "      }}{}\n", finalGraph ? "" : ",");
}

void BuildJson(
    const TelemetryReportSummary& summary,
    const FrameGraphReportRecords& graphs,
    AString<TelemetryArena>& out
){
    out.clear();
    out.reserve(EstimateJsonReportReserve(graphs));
    out += "{\n";
    StringAppendFormat(out, "  \"eventCount\": {},\n", summary.eventCount);
    out += "  \"frameRange\": {";
    StringAppendFormat(out, "\"present\": {}", summary.hasFrameRange ? "true" : "false");
    if(summary.hasFrameRange)
        StringAppendFormat(out, ", \"min\": {}, \"max\": {}", summary.minFrameIndex, summary.maxFrameIndex);
    out += "},\n";

    out += "  \"events\": {\n";
    for(usize i = 0u; i < s_TelemetryReportEventKindCount; ++i){
        out += "    ";
        AppendJsonQuotedText(out, EventKindText(static_cast<Telemetry::EventKind::Enum>(i)));
        StringAppendFormat(out, ": {}{}", summary.eventKindCounts[i], i + 1u == s_TelemetryReportEventKindCount ? "\n" : ",\n");
    }
    out += "  },\n";

    out += "  \"perf\": {\n";
    StringAppendFormat(out, "    \"cpuTimingEvents\": {},\n", summary.cpuTimingEventCount);
    StringAppendFormat(out, "    \"cpuTimingSamples\": {},\n", summary.cpuTimingSampleCount);
    StringAppendFormat(out, "    \"cpuTimingSeconds\": {:.9},\n", summary.cpuTimingSeconds);
    StringAppendFormat(out, "    \"maxCpuTimingSeconds\": {:.9},\n", summary.maxCpuTimingSeconds);
    StringAppendFormat(out, "    \"gpuTimingEvents\": {},\n", summary.gpuTimingEventCount);
    StringAppendFormat(out, "    \"gpuTimingSamples\": {},\n", summary.gpuTimingSampleCount);
    StringAppendFormat(out, "    \"gpuTimingSeconds\": {:.9},\n", summary.gpuTimingSeconds);
    StringAppendFormat(out, "    \"maxGpuTimingSeconds\": {:.9},\n", summary.maxGpuTimingSeconds);
    StringAppendFormat(out, "    \"memoryEvents\": {},\n", summary.memoryEventCount);
    StringAppendFormat(out, "    \"maxMemoryUsedBytes\": {},\n", summary.maxMemoryUsedBytes);
    StringAppendFormat(out, "    \"maxMemoryPeakUsedBytes\": {},\n", summary.maxMemoryPeakUsedBytes);
    StringAppendFormat(out, "    \"totalMemoryUsedDeltaBytes\": {}\n", summary.totalMemoryUsedDeltaBytes);
    out += "  },\n";

    out += "  \"frameGraph\": {\n";
    StringAppendFormat(out, "    \"frames\": {},\n", summary.frameGraphFrameCount);
    StringAppendFormat(out, "    \"nodes\": {},\n", summary.frameGraphNodeCount);
    StringAppendFormat(out, "    \"edges\": {},\n", summary.frameGraphEdgeCount);
    StringAppendFormat(out, "    \"maxNodes\": {},\n", summary.maxFrameGraphNodeCount);
    StringAppendFormat(out, "    \"maxEdges\": {},\n", summary.maxFrameGraphEdgeCount);
    out += "    \"records\": [\n";
    for(usize graphIndex = 0u; graphIndex < graphs.size(); ++graphIndex)
        AppendFrameGraphJson(graphs[graphIndex], graphIndex, graphIndex + 1u == graphs.size(), out);
    out += "    ]\n";
    out += "  },\n";
    StringAppendFormat(out, "  \"parseFailures\": {}\n", summary.parseFailureCount);
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
            __hidden_telemetry_report::AddMemory(outReport.summary, payload);
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
    __hidden_telemetry_report::BuildJson(outReport.summary, frameGraphs, outReport.json);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

