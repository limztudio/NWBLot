// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report_dot.h"

#include "memory_report.h"

#include <global/name_hash_text.h>
#include <global/binary.h>
#include <global/json_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryReportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_JsonQueueIndexFormat = "{{\"index\": {}, \"deviceGeneration\": {}}}";
inline constexpr AStringView s_JsonInitialQueueKey = "{\"initialQueue\": ";
inline constexpr AStringView s_JsonPlannedQueueKey = ", \"plannedQueue\": ";
inline constexpr AStringView s_JsonAcceptedQueueKey = ", \"acceptedQueue\": ";
inline constexpr AStringView s_JsonPreviousAcceptedQueueKey = ", \"previousAcceptedQueue\": ";
inline constexpr AStringView s_JsonQueueClassKey = ", \"queueClass\": ";
inline constexpr AStringView s_JsonReasonKey = ", \"reason\": ";
inline constexpr AStringView s_JsonAcceptanceKey = ", \"acceptance\": ";
static constexpr usize s_JsonReportReserveBytes = 1024u;
static constexpr usize s_JsonReportBytesPerGraph = 128u;
static constexpr usize s_JsonReportBytesPerNode = 896u;
static constexpr usize s_JsonReportBytesPerEdge = 96u;
static constexpr usize s_JsonReportRuntimeStatisticsBytes = 4096u;
static constexpr usize s_JsonReportPhysicalQueueRuntimeStatisticsBytes = 2048u;
static constexpr usize s_JsonReportPacketSubmissionStatisticsBytes = 512u;
static constexpr AStringView s_JsonDocumentOpen = "{\n";
static constexpr AStringView s_JsonDocumentClose = "}\n";
static constexpr AStringView s_JsonSectionClose = "  },\n";
static constexpr AStringView s_JsonFieldEnd = ",\n";
static constexpr AStringView s_JsonLastFieldEnd = "\n";
static constexpr AStringView s_JsonRecordingKey = ", \"recording\": ";
static constexpr AStringView s_JsonSubmissionKey = ", \"submission\": ";
static constexpr AStringView s_JsonSecondsFormat = "{:.9}";
static constexpr AStringView s_JsonInheritedTimelineWaitElisionCountFormat = ", \"inheritedTimelineWaitElisionCount\": {}";
static constexpr AStringView s_JsonRecordingCountsFormat = "{{\"packetCount\": {}, \"taskCount\": {}, \"commandListCount\": {}, \"barrierCount\": {}, ";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] usize EstimateJsonReportReserve(const Span<const FrameGraphReportRecord> graphs)noexcept{
    usize reserveBytes = s_JsonReportReserveBytes;
    for(const FrameGraphReportRecord& graph : graphs){
        reserveBytes += s_JsonReportBytesPerGraph;
        reserveBytes += graph.payload.nodes.size() * s_JsonReportBytesPerNode;
        reserveBytes += graph.payload.edges.size() * s_JsonReportBytesPerEdge;
        for(const Core::Telemetry::FrameGraphNodePayload& node : graph.payload.nodes){
            reserveBytes += node.label.size();
            if(node.runtimeStatistics.present)
                reserveBytes += s_JsonReportRuntimeStatisticsBytes;
        }
        reserveBytes += graph.payload.physicalQueueRuntimeStatistics.size() * s_JsonReportPhysicalQueueRuntimeStatisticsBytes;
        reserveBytes += graph.payload.packetSubmissionStatistics.size() * s_JsonReportPacketSubmissionStatisticsBytes;
    }
    return reserveBytes;
}


void AppendFrameGraphPhysicalQueueJson(const Core::Telemetry::FrameGraphPhysicalQueueId& queue, AString<Core::Telemetry::TelemetryArena>& out){
    if(!queue.valid()){
        out += s_JsonNullText;
        return;
    }

    StringAppendFormat(out, s_JsonQueueIndexFormat, queue.index, queue.deviceGeneration);
}

void AppendFrameGraphQueueAssignmentJson(const Core::Telemetry::FrameGraphQueueAssignment& assignment, AString<Core::Telemetry::TelemetryArena>& out){
    if(!assignment.present){
        out += s_JsonNullText;
        return;
    }

    out += s_JsonInitialQueueKey;
    AppendFrameGraphPhysicalQueueJson(assignment.initialQueue, out);
    out += s_JsonPlannedQueueKey;
    AppendFrameGraphPhysicalQueueJson(assignment.plannedQueue, out);
    out += s_JsonAcceptedQueueKey;
    AppendFrameGraphPhysicalQueueJson(assignment.acceptedQueue, out);
    out += s_JsonPreviousAcceptedQueueKey;
    AppendFrameGraphPhysicalQueueJson(assignment.previousAcceptedQueue, out);
    out += s_JsonQueueClassKey;
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueClassText(assignment.queueClass)));
    out += s_JsonReasonKey;
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueAssignmentReasonText(assignment.reason)));
    StringAppendFormat(out, ", \"modifierMask\": {}", static_cast<u32>(assignment.modifiers));
    out += s_JsonAcceptanceKey;
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueAssignmentAcceptanceText(assignment.acceptance)));
    StringAppendFormat(out, ", \"dedicated\": {}", assignment.dedicated ? s_TrueText : s_FalseText);
    StringAppendFormat(
        out,
        ", \"score\": {{\"overlap\": {}, \"queueLoad\": {}, \"incomingCrossings\": {}, "
        "\"outgoingCrossings\": {}, \"ownershipTransfers\": {}, \"total\": {}}}}}",
        assignment.score.overlap,
        assignment.score.queueLoad,
        assignment.score.incomingCrossings,
        assignment.score.outgoingCrossings,
        assignment.score.ownershipTransfers,
        assignment.score.total
    );
}

void AppendFrameGraphCompiledTaskJson(const Core::Telemetry::FrameGraphCompiledTask& compiledTask, AString<Core::Telemetry::TelemetryArena>& out){
    if(!compiledTask.present){
        out += s_JsonNullText;
        return;
    }

    StringAppendFormat(out, "{{\"planGeneration\": {}, \"packetIndex\": {}, \"packetizationDecision\": ", compiledTask.planGeneration, compiledTask.packetIndex);
    AppendJsonQuotedText(out, AStringView(FrameGraphTaskPacketizationDecisionText(compiledTask.packetizationDecision)));
    out += '}';
}

void AppendFrameGraphCompileRuntimeStatisticsJson(const Core::Telemetry::FrameGraphCompileRuntimeStatistics& statistics, AString<Core::Telemetry::TelemetryArena>& out){
    StringAppendFormat(out, "{{\"taskCount\": {}, \"resourceCount\": {}, \"resourceVersionCount\": ", statistics.taskCount, statistics.resourceCount);
    StringAppendFormat(out, "{}, \"resourceVersionEdgeCount\": {}", statistics.resourceVersionCount, statistics.resourceVersionEdgeCount);
    StringAppendFormat(
        out,
        ", \"resourceUseCount\": {}, \"explicitDependencyCount\": {}, "
        "\"inferredDependencyCount\": {}, \"packetCount\": {}, \"packetDependencyCount\": {}, \"mergedTaskCount\": {}",
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

void AppendFrameGraphRecordingRuntimeStatisticsJson(const Core::Telemetry::FrameGraphRecordingRuntimeStatistics& statistics, AString<Core::Telemetry::TelemetryArena>& out){
    StringAppendFormat(out, s_JsonRecordingCountsFormat, statistics.packetCount, statistics.taskCount, statistics.commandListCount, statistics.barrierCount);
    StringAppendFormat(out, "\"workerRoutedPacketCount\": {}, \"parallelPacketCount\": {}", statistics.workerRoutedPacketCount, statistics.parallelPacketCount);
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

template<typename Statistics>
void AppendSubmissionRuntimeStatisticsJson(const Statistics& statistics, AString<Core::Telemetry::TelemetryArena>& out){
    StringAppendFormat(
        out,
        "{{\"acceptedPacketCount\": {}, \"acceptedTaskCount\": {}, \"rejectedPacketCount\": {}, "
        "\"rejectedTaskCount\": {}, \"nativeSubmissionCount\": {}, \"rejectedSubmissionCount\": {}, "
        "\"nativeCommandListCount\": {}, \"plannedWaitTokenCount\": {}, \"sameQueueWaitElisionCount\": {}, "
        "\"timelineWaitCount\": {}, \"mergedTimelineWaitCount\": {}, \"acceptedFrontierSubmissionCount\": {}",
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
        statistics.acceptedFrontierSubmissionCount
    );
    StringAppendFormat(out, s_JsonInheritedTimelineWaitElisionCountFormat, statistics.inheritedTimelineWaitElisionCount);
    StringAppendFormat(out, ", \"recoverySubmissionCount\": {}", statistics.recoverySubmissionCount);
    StringAppendFormat(out, ", \"submissionSeconds\": {:.9}}}", statistics.submissionSeconds);
}

void AppendFrameGraphPhysicalQueueCompileRuntimeStatisticsJson(const Core::Telemetry::FrameGraphPhysicalQueueCompileRuntimeStatistics& statistics, AString<Core::Telemetry::TelemetryArena>& out){
    StringAppendFormat(
        out,
        "{{\"taskCount\": {}, \"packetCount\": {}, \"mergedTaskCount\": {}, "
        "\"prologueBarrierCount\": {}, \"epilogueBarrierCount\": {}, "
        "\"ownershipReleaseBarrierCount\": {}, \"ownershipAcquireBarrierCount\": {}",
        statistics.taskCount,
        statistics.packetCount,
        statistics.mergedTaskCount,
        statistics.prologueBarrierCount,
        statistics.epilogueBarrierCount,
        statistics.ownershipReleaseBarrierCount,
        statistics.ownershipAcquireBarrierCount
    );
    StringAppendFormat(
        out,
        ", \"incomingLogicalOwnershipTransferCount\": {}, \"outgoingLogicalOwnershipTransferCount\": {}, "
        "\"incomingLogicalOwnershipTransferSignatureCount\": {}, "
        "\"outgoingLogicalOwnershipTransferSignatureCount\": {}, "
        "\"incomingRepeatedOwnershipTransferSignatureCount\": {}, "
        "\"outgoingRepeatedOwnershipTransferSignatureCount\": {}, "
        "\"concurrentSharingAdviceResourceCount\": {}}}",
        statistics.incomingLogicalOwnershipTransferCount,
        statistics.outgoingLogicalOwnershipTransferCount,
        statistics.incomingLogicalOwnershipTransferSignatureCount,
        statistics.outgoingLogicalOwnershipTransferSignatureCount,
        statistics.incomingRepeatedOwnershipTransferSignatureCount,
        statistics.outgoingRepeatedOwnershipTransferSignatureCount,
        statistics.concurrentSharingAdviceResourceCount
    );
}

void AppendFrameGraphPhysicalQueueRecordingRuntimeStatisticsJson(const Core::Telemetry::FrameGraphPhysicalQueueRecordingRuntimeStatistics& statistics, AString<Core::Telemetry::TelemetryArena>& out){
    StringAppendFormat(out, s_JsonRecordingCountsFormat, statistics.packetCount, statistics.taskCount, statistics.commandListCount, statistics.barrierCount);
    StringAppendFormat(
        out,
        "\"workerRoutedPacketCount\": {}, \"parallelPacketCount\": {}, "
        "\"commandListAcquisitionSeconds\": {:.9}, \"graphBarrierRecordingSeconds\": {:.9}, "
        "\"taskRecordSeconds\": {:.9}, \"recordingSeconds\": {:.9}}}",
        statistics.workerRoutedPacketCount,
        statistics.parallelPacketCount,
        statistics.commandListAcquisitionSeconds,
        statistics.graphBarrierRecordingSeconds,
        statistics.taskRecordSeconds,
        statistics.recordingSeconds
    );
}

void AppendFrameGraphPhysicalQueueRuntimeStatisticsJson(const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& statistics, AString<Core::Telemetry::TelemetryArena>& out){
    out += "{\"queue\": ";
    AppendFrameGraphPhysicalQueueJson(statistics.queue, out);
    out += s_JsonQueueClassKey;
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueClassText(statistics.queueClass)));
    out += ", \"compile\": ";
    AppendFrameGraphPhysicalQueueCompileRuntimeStatisticsJson(statistics.compile, out);
    out += s_JsonRecordingKey;
    AppendFrameGraphPhysicalQueueRecordingRuntimeStatisticsJson(statistics.recording, out);
    out += s_JsonSubmissionKey;
    AppendSubmissionRuntimeStatisticsJson(statistics.submission, out);
    out += '}';
}

void AppendFrameGraphPacketSubmissionStatisticsJson(const Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord& statistics, AString<Core::Telemetry::TelemetryArena>& out){
    StringAppendFormat(out, "{{\"packet\": {{\"index\": {}, \"generation\": {}}}, \"queue\": ", statistics.packetIndex, statistics.packetGeneration);
    AppendFrameGraphPhysicalQueueJson(statistics.queue, out);
    out += s_JsonQueueClassKey;
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueClassText(statistics.queueClass)));
    StringAppendFormat(
        out,
        ", \"taskCount\": {}, \"commandListCount\": {}, \"plannedWaitTokenCount\": {}, "
        "\"sameQueueWaitElisionCount\": {}, \"timelineWaitCount\": {}, \"mergedTimelineWaitCount\": {}",
        statistics.taskCount,
        statistics.commandListCount,
        statistics.plannedWaitTokenCount,
        statistics.sameQueueWaitElisionCount,
        statistics.timelineWaitCount,
        statistics.mergedTimelineWaitCount
    );
    StringAppendFormat(out, s_JsonInheritedTimelineWaitElisionCountFormat, statistics.inheritedTimelineWaitElisionCount);
    StringAppendFormat(
        out,
        ", \"joinsAcceptedQueueFrontier\": {}, \"recoverySubmission\": {}, \"submissionSeconds\": {:.9}}}",
        statistics.joinsAcceptedQueueFrontier ? s_TrueText : s_FalseText,
        statistics.recoverySubmission ? s_TrueText : s_FalseText,
        statistics.submissionSeconds
    );
}

void AppendFrameGraphRuntimeStatisticsJson(
    const Core::Telemetry::FrameGraphRuntimeStatistics& statistics,
    const Span<const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const Span<const Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord> packetSubmissionStatistics,
    const FrameGraphOwnerStatisticsRange& ownerRange,
    const b8 physicalQueueRuntimeStatisticsPresent,
    const b8 packetSubmissionStatisticsPresent,
    AString<Core::Telemetry::TelemetryArena>& out
){
    if(!statistics.present){
        out += s_JsonNullText;
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
    AppendFrameGraphCompileRuntimeStatisticsJson(statistics.compile, out);
    out += s_JsonRecordingKey;
    AppendFrameGraphRecordingRuntimeStatisticsJson(statistics.recording, out);
    out += s_JsonSubmissionKey;
    AppendSubmissionRuntimeStatisticsJson(statistics.submission, out);
    out += ", \"physicalQueues\": ";
    if(!physicalQueueRuntimeStatisticsPresent)
        out += s_JsonNullText;
    else{
        out += '[';
        for(usize index = ownerRange.physicalQueueBegin; index < ownerRange.physicalQueueEnd; ++index){
            if(index != ownerRange.physicalQueueBegin)
                out += ", ";
            const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord& record = physicalQueueRuntimeStatistics[index];
            AppendFrameGraphPhysicalQueueRuntimeStatisticsJson(record.statistics, out);
        }
        out += ']';
    }

    out += ", \"packetSubmissions\": ";
    if(!packetSubmissionStatisticsPresent)
        out += s_JsonNullText;
    else{
        out += '[';
        for(usize index = ownerRange.packetSubmissionBegin; index < ownerRange.packetSubmissionEnd; ++index){
            if(index != ownerRange.packetSubmissionBegin)
                out += ", ";
            AppendFrameGraphPacketSubmissionStatisticsJson(packetSubmissionStatistics[index], out);
        }
        out += ']';
    }
    out += '}';
}


void AppendFrameGraphJson(const FrameGraphReportRecord& record, const usize graphIndex, const b8 finalGraph, AString<Core::Telemetry::TelemetryArena>& out){
    const Core::Telemetry::FrameGraphPayload& graph = record.payload;
    out += "      {\n";
    StringAppendFormat(out, "        \"captureIndex\": {},\n", graphIndex);
    StringAppendFormat(out, "        \"frameIndex\": {},\n", graph.frameIndex);
    StringAppendFormat(out, "        \"streamId\": {},\n", record.streamId);
    out += "        \"nodes\": [\n";
    FrameGraphOwnerStatisticsRange ownerRange;
    for(usize nodeIndex = 0u; nodeIndex < graph.nodes.size(); ++nodeIndex){
        const Core::Telemetry::FrameGraphNodePayload& node = graph.nodes[nodeIndex];
        Array<char, NameHashTextCodec::s_TextLength + 1u> identityText;
        const AStringView identity = FormatNameIdentityText(node.name, identityText);

        StringAppendFormat(out, "          {{\"index\": {}, \"identity\": ", nodeIndex);
        AppendJsonQuotedText(out, identity);
        out += ", \"label\": ";
        AppendJsonQuotedText(out, node.label);
        out += ", \"kind\": ";
        AppendJsonQuotedText(out, AStringView(FrameGraphNodeKindText(node.kind)));
        StringAppendFormat(out, ", \"flags\": {}, \"queueAssignment\": ", static_cast<u32>(node.flags));
        AppendFrameGraphQueueAssignmentJson(node.queueAssignment, out);
        out += ", \"compiledTask\": ";
        AppendFrameGraphCompiledTaskJson(node.compiledTask, out);
        out += ", \"runtimeStatistics\": ";
        ownerRange.advance(graph, static_cast<u32>(nodeIndex));
        const usize physicalQueueRuntimeStatisticsCount = ownerRange.physicalQueueEnd - ownerRange.physicalQueueBegin;
        AppendFrameGraphRuntimeStatisticsJson(node.runtimeStatistics, graph.physicalQueueRuntimeStatistics, graph.packetSubmissionStatistics, ownerRange, physicalQueueRuntimeStatisticsCount != 0u, graph.packetSubmissionStatisticsPresent, out);
        StringAppendFormat(out, "}}{}\n", nodeIndex + 1u == graph.nodes.size() ? "" : ",");
    }
    out += "        ],\n";
    out += "        \"edges\": [\n";
    for(usize edgeIndex = 0u; edgeIndex < graph.edges.size(); ++edgeIndex){
        const Core::Telemetry::FrameGraphEdgePayload& edge = graph.edges[edgeIndex];
        StringAppendFormat(out, "          {{\"from\": {}, \"to\": {}, \"kind\": ", edge.fromNodeIndex, edge.toNodeIndex);
        AppendJsonQuotedText(out, AStringView(FrameGraphEdgeLabel(edge.kind)));
        StringAppendFormat(out, ", \"flags\": {}}}{}\n", static_cast<u32>(edge.flags), edgeIndex + 1u == graph.edges.size() ? "" : ",");
    }
    out += "        ]\n";
    StringAppendFormat(out, "      }}{}\n", finalGraph ? "" : ",");
}

void BuildJson(const TelemetryReportSummary& summary, const Span<const FrameGraphReportRecord> graphs, const AStringView memoryRecords, AString<Core::Telemetry::TelemetryArena>& out){
    out.clear();
    usize reserveBytes = EstimateJsonReportReserve(graphs);
    if(!AddBinaryReserveBytes(reserveBytes, memoryRecords.size()))
        reserveBytes = Max(reserveBytes, memoryRecords.size());
    out.reserve(reserveBytes);
    out += s_JsonDocumentOpen;
    StringAppendFormat(out, "  \"eventCount\": {},\n", summary.eventCount);
    out += "  \"frameRange\": {";
    StringAppendFormat(out, "\"present\": {}", summary.hasFrameRange ? s_TrueText : s_FalseText);
    if(summary.hasFrameRange)
        StringAppendFormat(out, ", \"min\": {}, \"max\": {}", summary.minFrameIndex, summary.maxFrameIndex);
    out += "},\n";

    out += "  \"events\": {\n";
    for(usize i = 0u; i < s_TelemetryReportEventKindCount; ++i){
        out += "    ";
        AppendJsonQuotedText(out, EventKindText(static_cast<Core::Telemetry::EventKind::Enum>(i)));
        StringAppendFormat(out, ": {}{}", summary.eventKindCounts[i], i + 1u == s_TelemetryReportEventKindCount ? s_JsonLastFieldEnd : s_JsonFieldEnd);
    }
    out += s_JsonSectionClose;

    out += "  \"perf\": {\n";
    StringAppendFormat(out, "    \"cpuTimingEvents\": {},\n", summary.cpuTimingEventCount);
    StringAppendFormat(out, "    \"cpuTimingSamples\": {},\n", summary.cpuTimingSampleCount);
    out += "    \"cpuTimingSeconds\": ";
    if(summary.cpuTimingSecondsValid)
        StringAppendFormat(out, s_JsonSecondsFormat, summary.cpuTimingSeconds);
    else
        out += s_JsonNullText;
    out += s_JsonFieldEnd;
    StringAppendFormat(out, "    \"maxCpuTimingSeconds\": {:.9},\n", summary.maxCpuTimingSeconds);
    StringAppendFormat(out, "    \"gpuTimingEvents\": {},\n", summary.gpuTimingEventCount);
    StringAppendFormat(out, "    \"gpuTimingSamples\": {},\n", summary.gpuTimingSampleCount);
    out += "    \"gpuTimingSeconds\": ";
    if(summary.gpuTimingSecondsValid)
        StringAppendFormat(out, s_JsonSecondsFormat, summary.gpuTimingSeconds);
    else
        out += s_JsonNullText;
    out += s_JsonFieldEnd;
    StringAppendFormat(out, "    \"maxGpuTimingSeconds\": {:.9},\n", summary.maxGpuTimingSeconds);
    StringAppendFormat(out, "    \"memoryEvents\": {},\n", summary.memoryEventCount);
    AppendTelemetryMemorySourcesJson(summary, out);
    out += "    \"memoryRecords\": [\n";
    out.append(memoryRecords.data(), memoryRecords.size());
    out += "\n    ]\n";
    out += s_JsonSectionClose;

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
    out += s_JsonSectionClose;
    StringAppendFormat(out, "  \"parseFailures\": {}\n", summary.parseFailureCount);
    out += s_JsonDocumentClose;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

