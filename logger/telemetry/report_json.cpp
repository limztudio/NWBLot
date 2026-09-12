// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "report_json.h"

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


static constexpr usize s_JsonReportReserveBytes = 1024u;
static constexpr usize s_JsonReportBytesPerGraph = 128u;
static constexpr usize s_JsonReportBytesPerNode = 896u;
static constexpr usize s_JsonReportBytesPerEdge = 96u;
static constexpr usize s_JsonReportRuntimeStatisticsBytes = 4096u;
static constexpr usize s_JsonReportPhysicalQueueRuntimeStatisticsBytes = 2048u;
static constexpr usize s_JsonReportPacketSubmissionStatisticsBytes = 512u;


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
        reserveBytes += graph.payload.physicalQueueRuntimeStatistics.size()
            * s_JsonReportPhysicalQueueRuntimeStatisticsBytes
        ;
        reserveBytes += graph.payload.packetSubmissionStatistics.size()
            * s_JsonReportPacketSubmissionStatisticsBytes
        ;
    }
    return reserveBytes;
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
    const Telemetry::FrameGraphCompileRuntimeStatistics& statistics,
    const bool resourceVersionStatisticsPresent
){
    StringAppendFormat(
        out,
        "{{\"taskCount\": {}, \"resourceCount\": {}, \"resourceVersionCount\": ",
        statistics.taskCount,
        statistics.resourceCount
    );
    if(resourceVersionStatisticsPresent){
        StringAppendFormat(
            out,
            "{}, \"resourceVersionEdgeCount\": {}",
            statistics.resourceVersionCount,
            statistics.resourceVersionEdgeCount
        );
    }
    else
        out += "null, \"resourceVersionEdgeCount\": null";
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

template<typename Statistics>
void AppendSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Statistics& statistics,
    const bool recoverySubmissionCountPresent
){
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
    if(recoverySubmissionCountPresent)
        StringAppendFormat(out, ", \"recoverySubmissionCount\": {}", statistics.recoverySubmissionCount);
    else
        out += ", \"recoverySubmissionCount\": null";
    StringAppendFormat(out, ", \"submissionSeconds\": {:.9}}}", statistics.submissionSeconds);
}

void AppendFrameGraphSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphSubmissionRuntimeStatistics& statistics,
    const bool recoverySubmissionCountPresent
){
    AppendSubmissionRuntimeStatisticsJson(out, statistics, recoverySubmissionCountPresent);
}

void AppendFrameGraphPhysicalQueueCompileRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueCompileRuntimeStatistics& statistics
){
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

void AppendFrameGraphPhysicalQueueRecordingRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueRecordingRuntimeStatistics& statistics
){
    StringAppendFormat(
        out,
        "{{\"packetCount\": {}, \"taskCount\": {}, \"commandListCount\": {}, \"barrierCount\": {}, "
        "\"workerRoutedPacketCount\": {}, \"parallelPacketCount\": {}, "
        "\"commandListAcquisitionSeconds\": {:.9}, \"graphBarrierRecordingSeconds\": {:.9}, "
        "\"taskRecordSeconds\": {:.9}, \"recordingSeconds\": {:.9}}}",
        statistics.packetCount,
        statistics.taskCount,
        statistics.commandListCount,
        statistics.barrierCount,
        statistics.workerRoutedPacketCount,
        statistics.parallelPacketCount,
        statistics.commandListAcquisitionSeconds,
        statistics.graphBarrierRecordingSeconds,
        statistics.taskRecordSeconds,
        statistics.recordingSeconds
    );
}

void AppendFrameGraphPhysicalQueueSubmissionRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueSubmissionRuntimeStatistics& statistics,
    const bool recoverySubmissionCountPresent
){
    AppendSubmissionRuntimeStatisticsJson(out, statistics, recoverySubmissionCountPresent);
}

void AppendFrameGraphPhysicalQueueRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& statistics,
    const bool recoverySubmissionCountPresent
){
    out += "{\"queue\": ";
    AppendFrameGraphPhysicalQueueJson(out, statistics.queue);
    out += ", \"queueClass\": ";
    AppendJsonQuotedText(out, AStringView(FrameGraphQueueClassText(statistics.queueClass)));
    out += ", \"compile\": ";
    AppendFrameGraphPhysicalQueueCompileRuntimeStatisticsJson(out, statistics.compile);
    out += ", \"recording\": ";
    AppendFrameGraphPhysicalQueueRecordingRuntimeStatisticsJson(out, statistics.recording);
    out += ", \"submission\": ";
    AppendFrameGraphPhysicalQueueSubmissionRuntimeStatisticsJson(
        out,
        statistics.submission,
        recoverySubmissionCountPresent
    );
    out += '}';
}

void AppendFrameGraphPacketSubmissionStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord& statistics
){
    StringAppendFormat(
        out,
        "{{\"packet\": {{\"index\": {}, \"generation\": {}}}, \"queue\": ",
        statistics.packetIndex,
        statistics.packetGeneration
    );
    AppendFrameGraphPhysicalQueueJson(out, statistics.queue);
    out += ", \"queueClass\": ";
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
    StringAppendFormat(
        out,
        ", \"joinsAcceptedQueueFrontier\": {}, \"recoverySubmission\": {}, \"submissionSeconds\": {:.9}}}",
        statistics.joinsAcceptedQueueFrontier ? "true" : "false",
        statistics.recoverySubmission ? "true" : "false",
        statistics.submissionSeconds
    );
}

void AppendFrameGraphRuntimeStatisticsJson(
    AString<TelemetryArena>& out,
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    const FrameGraphOwnerStatisticsRange& ownerRange,
    const bool physicalQueueRuntimeStatisticsPresent,
    const bool packetSubmissionStatisticsPresent,
    const bool recoverySubmissionCountPresent,
    const bool resourceVersionStatisticsPresent
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
    AppendFrameGraphCompileRuntimeStatisticsJson(out, statistics.compile, resourceVersionStatisticsPresent);
    out += ", \"recording\": ";
    AppendFrameGraphRecordingRuntimeStatisticsJson(out, statistics.recording);
    out += ", \"submission\": ";
    AppendFrameGraphSubmissionRuntimeStatisticsJson(out, statistics.submission, recoverySubmissionCountPresent);
    out += ", \"physicalQueues\": ";
    if(!physicalQueueRuntimeStatisticsPresent)
        out += "null";
    else{
        out += '[';
        for(usize index = ownerRange.physicalQueueBegin; index < ownerRange.physicalQueueEnd; ++index){
            if(index != ownerRange.physicalQueueBegin)
                out += ", ";
            const Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord& record = physicalQueueRuntimeStatistics[index];
            AppendFrameGraphPhysicalQueueRuntimeStatisticsJson(out, record.statistics, recoverySubmissionCountPresent);
        }
        out += ']';
    }

    out += ", \"packetSubmissions\": ";
    if(!packetSubmissionStatisticsPresent)
        out += "null";
    else{
        out += '[';
        for(usize index = ownerRange.packetSubmissionBegin; index < ownerRange.packetSubmissionEnd; ++index){
            if(index != ownerRange.packetSubmissionBegin)
                out += ", ";
            AppendFrameGraphPacketSubmissionStatisticsJson(out, packetSubmissionStatistics[index]);
        }
        out += ']';
    }
    out += '}';
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
    FrameGraphOwnerStatisticsRange ownerRange;
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
        ownerRange.advance(graph, static_cast<u32>(nodeIndex));
        const usize physicalQueueRuntimeStatisticsCount = ownerRange.physicalQueueEnd - ownerRange.physicalQueueBegin;
        AppendFrameGraphRuntimeStatisticsJson(
            out,
            node.runtimeStatistics,
            graph.physicalQueueRuntimeStatistics,
            graph.packetSubmissionStatistics,
            ownerRange,
            graph.physicalQueueRuntimeStatisticsPresent && physicalQueueRuntimeStatisticsCount != 0u,
            graph.packetSubmissionStatisticsPresent,
            graph.wireVersion >= Telemetry::s_FrameGraphRecoverySubmissionCountPayloadVersion,
            graph.wireVersion >= Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion
        );
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
    const AStringView memoryRecords,
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
    AppendTelemetryMemorySourcesJson(out, summary);
    out += "    \"memoryRecords\": [\n";
    out.append(memoryRecords.data(), memoryRecords.size());
    out += "\n    ]\n";
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


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

