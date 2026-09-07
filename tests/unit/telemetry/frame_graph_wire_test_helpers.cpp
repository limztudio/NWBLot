// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_wire_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Telemetry::EncodedFrameGraphRuntimeStatistics EncodeTestFrameGraphRuntimeStatistics(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved
){
    return Telemetry::EncodedFrameGraphRuntimeStatistics{
        .nodeIndex = nodeIndex,
        .deviceGeneration = statistics.deviceGeneration,
        .reserved = reserved,
        .graphGeneration = statistics.graphGeneration,
        .planGeneration = statistics.planGeneration,
        .recordingAttemptGeneration = statistics.recordingAttemptGeneration,
        .compile = {
            .taskCount = statistics.compile.taskCount,
            .resourceCount = statistics.compile.resourceCount,
            .resourceUseCount = statistics.compile.resourceUseCount,
            .explicitDependencyCount = statistics.compile.explicitDependencyCount,
            .inferredDependencyCount = statistics.compile.inferredDependencyCount,
            .packetCount = statistics.compile.packetCount,
            .packetDependencyCount = statistics.compile.packetDependencyCount,
            .mergedTaskCount = statistics.compile.mergedTaskCount,
            .transitionBarrierCount = statistics.compile.transitionBarrierCount,
            .uavBarrierCount = statistics.compile.uavBarrierCount,
            .ownershipReleaseBarrierCount = statistics.compile.ownershipReleaseBarrierCount,
            .ownershipAcquireBarrierCount = statistics.compile.ownershipAcquireBarrierCount,
            .stateExportBarrierCount = statistics.compile.stateExportBarrierCount,
            .logicalOwnershipTransferCount = statistics.compile.logicalOwnershipTransferCount,
            .logicalOwnershipTransferSignatureCount = statistics.compile.logicalOwnershipTransferSignatureCount,
            .repeatedOwnershipTransferSignatureCount = statistics.compile.repeatedOwnershipTransferSignatureCount,
            .concurrentSharingCouldAvoidTransferCount = statistics.compile.concurrentSharingCouldAvoidTransferCount,
            .concurrentSharingAdviceResourceCount = statistics.compile.concurrentSharingAdviceResourceCount,
            .logicalOwnershipTransferInternalCount = statistics.compile.logicalOwnershipTransferInternalCount,
            .logicalOwnershipTransferExternalImportCount = statistics.compile.logicalOwnershipTransferExternalImportCount,
            .logicalOwnershipTransferExternalExportCount = statistics.compile.logicalOwnershipTransferExternalExportCount,
            .resourceSetCount = statistics.compile.resourceSetCount,
            .resourceSetMemberCount = statistics.compile.resourceSetMemberCount,
            .directResourceUseCount = statistics.compile.directResourceUseCount,
            .declaredResourceSetUseCount = statistics.compile.declaredResourceSetUseCount,
            .expandedResourceSetMemberUseCount = statistics.compile.expandedResourceSetMemberUseCount,
            .payloadObjectCount = statistics.compile.payloadObjectCount,
            .payloadObjectBytes = statistics.compile.payloadObjectBytes,
            .uploadBlobCount = statistics.compile.uploadBlobCount,
            .uploadBlobBytes = statistics.compile.uploadBlobBytes,
            .declarationSeconds = statistics.compile.declarationSeconds,
            .analysisSeconds = statistics.compile.analysisSeconds,
            .validationSeconds = statistics.compile.validationSeconds,
            .dependencyAnalysisSeconds = statistics.compile.dependencyAnalysisSeconds,
            .hazardAnalysisSeconds = statistics.compile.hazardAnalysisSeconds,
            .topologicalOrderSeconds = statistics.compile.topologicalOrderSeconds,
            .queueAssignmentSeconds = statistics.compile.queueAssignmentSeconds,
            .planningSeconds = statistics.compile.planningSeconds,
            .packetizationSeconds = statistics.compile.packetizationSeconds,
            .resourceStatePlanningSeconds = statistics.compile.resourceStatePlanningSeconds,
            .packetDependencyPlanningSeconds = statistics.compile.packetDependencyPlanningSeconds,
            .totalSeconds = statistics.compile.totalSeconds,
        },
        .recording = {
            .packetCount = statistics.recording.packetCount,
            .taskCount = statistics.recording.taskCount,
            .commandListCount = statistics.recording.commandListCount,
            .barrierCount = statistics.recording.barrierCount,
            .workerRoutedPacketCount = statistics.recording.workerRoutedPacketCount,
            .parallelPacketCount = statistics.recording.parallelPacketCount,
            .commandListAcquisitionSeconds = statistics.recording.commandListAcquisitionSeconds,
            .graphBarrierRecordingSeconds = statistics.recording.graphBarrierRecordingSeconds,
            .taskRecordSeconds = statistics.recording.taskRecordSeconds,
            .recordingSeconds = statistics.recording.recordingSeconds,
            .recordingElapsedSeconds = statistics.recording.recordingElapsedSeconds,
            .readyFrontierElapsedSeconds = statistics.recording.readyFrontierElapsedSeconds,
            .readyFrontierWorkerBusySeconds = statistics.recording.readyFrontierWorkerBusySeconds,
            .readyFrontierWorkerCapacitySeconds = statistics.recording.readyFrontierWorkerCapacitySeconds,
        },
        .submission = {
            .acceptedPacketCount = statistics.submission.acceptedPacketCount,
            .acceptedTaskCount = statistics.submission.acceptedTaskCount,
            .rejectedPacketCount = statistics.submission.rejectedPacketCount,
            .rejectedTaskCount = statistics.submission.rejectedTaskCount,
            .nativeSubmissionCount = statistics.submission.nativeSubmissionCount,
            .rejectedSubmissionCount = statistics.submission.rejectedSubmissionCount,
            .nativeCommandListCount = statistics.submission.nativeCommandListCount,
            .plannedWaitTokenCount = statistics.submission.plannedWaitTokenCount,
            .sameQueueWaitElisionCount = statistics.submission.sameQueueWaitElisionCount,
            .timelineWaitCount = statistics.submission.timelineWaitCount,
            .mergedTimelineWaitCount = statistics.submission.mergedTimelineWaitCount,
            .acceptedFrontierSubmissionCount = statistics.submission.acceptedFrontierSubmissionCount,
            .submissionSeconds = statistics.submission.submissionSeconds,
        },
    };
}

Telemetry::EncodedFrameGraphRuntimeStatisticsV6 EncodeTestFrameGraphRuntimeStatisticsV6(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved
){
    const Telemetry::EncodedFrameGraphRuntimeStatistics legacy = EncodeTestFrameGraphRuntimeStatistics(
        statistics,
        nodeIndex,
        reserved
    );
    return Telemetry::EncodedFrameGraphRuntimeStatisticsV6{
        .nodeIndex = legacy.nodeIndex,
        .deviceGeneration = legacy.deviceGeneration,
        .reserved = legacy.reserved,
        .graphGeneration = legacy.graphGeneration,
        .planGeneration = legacy.planGeneration,
        .recordingAttemptGeneration = legacy.recordingAttemptGeneration,
        .compile = legacy.compile,
        .recording = legacy.recording,
        .submission = {
            .acceptedPacketCount = legacy.submission.acceptedPacketCount,
            .acceptedTaskCount = legacy.submission.acceptedTaskCount,
            .rejectedPacketCount = legacy.submission.rejectedPacketCount,
            .rejectedTaskCount = legacy.submission.rejectedTaskCount,
            .nativeSubmissionCount = legacy.submission.nativeSubmissionCount,
            .rejectedSubmissionCount = legacy.submission.rejectedSubmissionCount,
            .nativeCommandListCount = legacy.submission.nativeCommandListCount,
            .plannedWaitTokenCount = legacy.submission.plannedWaitTokenCount,
            .sameQueueWaitElisionCount = legacy.submission.sameQueueWaitElisionCount,
            .timelineWaitCount = legacy.submission.timelineWaitCount,
            .mergedTimelineWaitCount = legacy.submission.mergedTimelineWaitCount,
            .acceptedFrontierSubmissionCount = legacy.submission.acceptedFrontierSubmissionCount,
            .submissionSeconds = legacy.submission.submissionSeconds,
            .recoverySubmissionCount = statistics.submission.recoverySubmissionCount,
        },
    };
}

Telemetry::EncodedFrameGraphRuntimeStatisticsV8 EncodeTestFrameGraphRuntimeStatisticsV8(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved
){
    const Telemetry::EncodedFrameGraphRuntimeStatisticsV6 legacy = EncodeTestFrameGraphRuntimeStatisticsV6(
        statistics,
        nodeIndex,
        reserved
    );
    Telemetry::EncodedFrameGraphRuntimeStatisticsV8 encoded;
    encoded.nodeIndex = legacy.nodeIndex;
    encoded.deviceGeneration = legacy.deviceGeneration;
    encoded.reserved = legacy.reserved;
    encoded.graphGeneration = legacy.graphGeneration;
    encoded.planGeneration = legacy.planGeneration;
    encoded.recordingAttemptGeneration = legacy.recordingAttemptGeneration;
    NWB_MEMCPY(&encoded.compile, sizeof(encoded.compile), &legacy.compile, sizeof(legacy.compile));
    encoded.compile.resourceVersionCount = statistics.compile.resourceVersionCount;
    encoded.compile.resourceVersionEdgeCount = statistics.compile.resourceVersionEdgeCount;
    encoded.recording = legacy.recording;
    encoded.submission = legacy.submission;
    return encoded;
}

Telemetry::EncodedFrameGraphRuntimeStatisticsV6 DowngradeFrameGraphRuntimeStatisticsV8(
    const Telemetry::EncodedFrameGraphRuntimeStatisticsV8& statistics
){
    Telemetry::EncodedFrameGraphRuntimeStatisticsV6 legacy;
    legacy.nodeIndex = statistics.nodeIndex;
    legacy.deviceGeneration = statistics.deviceGeneration;
    legacy.reserved = statistics.reserved;
    legacy.graphGeneration = statistics.graphGeneration;
    legacy.planGeneration = statistics.planGeneration;
    legacy.recordingAttemptGeneration = statistics.recordingAttemptGeneration;
    NWB_MEMCPY(&legacy.compile, sizeof(legacy.compile), &statistics.compile, sizeof(legacy.compile));
    legacy.recording = statistics.recording;
    legacy.submission = statistics.submission;
    return legacy;
}

Telemetry::EncodedFrameGraphRuntimeStatistics DowngradeFrameGraphRuntimeStatisticsV6(
    const Telemetry::EncodedFrameGraphRuntimeStatisticsV6& statistics
){
    return Telemetry::EncodedFrameGraphRuntimeStatistics{
        .nodeIndex = statistics.nodeIndex,
        .deviceGeneration = statistics.deviceGeneration,
        .reserved = statistics.reserved,
        .graphGeneration = statistics.graphGeneration,
        .planGeneration = statistics.planGeneration,
        .recordingAttemptGeneration = statistics.recordingAttemptGeneration,
        .compile = statistics.compile,
        .recording = statistics.recording,
        .submission = {
            .acceptedPacketCount = statistics.submission.acceptedPacketCount,
            .acceptedTaskCount = statistics.submission.acceptedTaskCount,
            .rejectedPacketCount = statistics.submission.rejectedPacketCount,
            .rejectedTaskCount = statistics.submission.rejectedTaskCount,
            .nativeSubmissionCount = statistics.submission.nativeSubmissionCount,
            .rejectedSubmissionCount = statistics.submission.rejectedSubmissionCount,
            .nativeCommandListCount = statistics.submission.nativeCommandListCount,
            .plannedWaitTokenCount = statistics.submission.plannedWaitTokenCount,
            .sameQueueWaitElisionCount = statistics.submission.sameQueueWaitElisionCount,
            .timelineWaitCount = statistics.submission.timelineWaitCount,
            .mergedTimelineWaitCount = statistics.submission.mergedTimelineWaitCount,
            .acceptedFrontierSubmissionCount = statistics.submission.acceptedFrontierSubmissionCount,
            .submissionSeconds = statistics.submission.submissionSeconds,
        },
    };
}

Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics
DowngradeFrameGraphPhysicalQueueRuntimeStatisticsV6(
    const Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics
){
    return Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics{
        .ownerNodeIndex = statistics.ownerNodeIndex,
        .queue = statistics.queue,
        .queueClass = statistics.queueClass,
        .reserved = {
            statistics.reserved[0u],
            statistics.reserved[1u],
            statistics.reserved[2u],
            statistics.reserved[3u],
            statistics.reserved[4u],
            statistics.reserved[5u],
            statistics.reserved[6u],
        },
        .compile = statistics.compile,
        .recording = statistics.recording,
        .submission = {
            .acceptedPacketCount = statistics.submission.acceptedPacketCount,
            .acceptedTaskCount = statistics.submission.acceptedTaskCount,
            .rejectedPacketCount = statistics.submission.rejectedPacketCount,
            .rejectedTaskCount = statistics.submission.rejectedTaskCount,
            .nativeSubmissionCount = statistics.submission.nativeSubmissionCount,
            .rejectedSubmissionCount = statistics.submission.rejectedSubmissionCount,
            .nativeCommandListCount = statistics.submission.nativeCommandListCount,
            .plannedWaitTokenCount = statistics.submission.plannedWaitTokenCount,
            .sameQueueWaitElisionCount = statistics.submission.sameQueueWaitElisionCount,
            .timelineWaitCount = statistics.submission.timelineWaitCount,
            .mergedTimelineWaitCount = statistics.submission.mergedTimelineWaitCount,
            .acceptedFrontierSubmissionCount = statistics.submission.acceptedFrontierSubmissionCount,
            .submissionSeconds = statistics.submission.submissionSeconds,
        },
    };
}

bool ConvertFrameGraphPayloadV8ToLegacy(
    const Telemetry::TelemetryBytes& source,
    const u16 legacyVersion,
    Telemetry::TelemetryBytes& outPayload
){
    if(
        source.size() < sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        || (
            legacyVersion != Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion
            && legacyVersion != Telemetry::s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion
            && legacyVersion != Telemetry::s_FrameGraphRecoverySubmissionCountPayloadVersion
            && legacyVersion != Telemetry::s_FrameGraphPacketSubmissionStatisticsPayloadVersion
        )
    )
        return false;

    Telemetry::EncodedFrameGraphPayloadHeaderV8 sourceHeader;
    NWB_MEMCPY(&sourceHeader, sizeof(sourceHeader), source.data(), sizeof(sourceHeader));
    if(
        sourceHeader.version != Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion
        || sourceHeader.packetSubmissionStatisticsCount != 0u
        || (
            legacyVersion == Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion
            && sourceHeader.physicalQueueRuntimeStatisticsCount != 0u
        )
    )
        return false;

    usize runtimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8);
    if(
        !AddBinaryRepeatedReserveBytes(
            runtimeStatisticsOffset,
            sourceHeader.nodeCount,
            sizeof(Telemetry::EncodedFrameGraphNode)
        )
        || !AddBinaryRepeatedReserveBytes(
            runtimeStatisticsOffset,
            sourceHeader.edgeCount,
            sizeof(Telemetry::EncodedFrameGraphEdge)
        )
        || !AddBinaryRepeatedReserveBytes(
            runtimeStatisticsOffset,
            sourceHeader.queueAssignmentCount,
            sizeof(Telemetry::EncodedFrameGraphQueueAssignment)
        )
        || !AddBinaryRepeatedReserveBytes(
            runtimeStatisticsOffset,
            sourceHeader.compiledTaskCount,
            sizeof(Telemetry::EncodedFrameGraphCompiledTask)
        )
    )
        return false;

    usize physicalQueueRuntimeStatisticsOffset = runtimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(
        physicalQueueRuntimeStatisticsOffset,
        sourceHeader.runtimeStatisticsCount,
        sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8)
    ))
        return false;
    usize stringTableOffset = physicalQueueRuntimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(
        stringTableOffset,
        sourceHeader.physicalQueueRuntimeStatisticsCount,
        sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6)
    ))
        return false;
    usize expectedSourceBytes = stringTableOffset;
    if(
        !AddBinaryReserveBytes(expectedSourceBytes, sourceHeader.stringTableBytes)
        || expectedSourceBytes != source.size()
    )
        return false;

    outPayload.clear();
    outPayload.reserve(source.size());
    if(legacyVersion == Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion){
        Telemetry::EncodedFrameGraphPayloadHeaderV4 header;
        header.frameIndex = sourceHeader.frameIndex;
        header.nodeCount = sourceHeader.nodeCount;
        header.edgeCount = sourceHeader.edgeCount;
        header.stringTableBytes = sourceHeader.stringTableBytes;
        header.queueAssignmentCount = sourceHeader.queueAssignmentCount;
        header.compiledTaskCount = sourceHeader.compiledTaskCount;
        header.runtimeStatisticsCount = sourceHeader.runtimeStatisticsCount;
        AppendPOD(outPayload, header);
    }
    else if(legacyVersion == Telemetry::s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion){
        Telemetry::EncodedFrameGraphPayloadHeaderV5 header;
        header.frameIndex = sourceHeader.frameIndex;
        header.nodeCount = sourceHeader.nodeCount;
        header.edgeCount = sourceHeader.edgeCount;
        header.stringTableBytes = sourceHeader.stringTableBytes;
        header.queueAssignmentCount = sourceHeader.queueAssignmentCount;
        header.compiledTaskCount = sourceHeader.compiledTaskCount;
        header.runtimeStatisticsCount = sourceHeader.runtimeStatisticsCount;
        header.physicalQueueRuntimeStatisticsCount = sourceHeader.physicalQueueRuntimeStatisticsCount;
        AppendPOD(outPayload, header);
    }
    else if(legacyVersion == Telemetry::s_FrameGraphRecoverySubmissionCountPayloadVersion){
        Telemetry::EncodedFrameGraphPayloadHeaderV6 header;
        header.frameIndex = sourceHeader.frameIndex;
        header.nodeCount = sourceHeader.nodeCount;
        header.edgeCount = sourceHeader.edgeCount;
        header.stringTableBytes = sourceHeader.stringTableBytes;
        header.queueAssignmentCount = sourceHeader.queueAssignmentCount;
        header.compiledTaskCount = sourceHeader.compiledTaskCount;
        header.runtimeStatisticsCount = sourceHeader.runtimeStatisticsCount;
        header.physicalQueueRuntimeStatisticsCount = sourceHeader.physicalQueueRuntimeStatisticsCount;
        AppendPOD(outPayload, header);
    }
    else{
        Telemetry::EncodedFrameGraphPayloadHeaderV7 header;
        header.frameIndex = sourceHeader.frameIndex;
        header.nodeCount = sourceHeader.nodeCount;
        header.edgeCount = sourceHeader.edgeCount;
        header.stringTableBytes = sourceHeader.stringTableBytes;
        header.queueAssignmentCount = sourceHeader.queueAssignmentCount;
        header.compiledTaskCount = sourceHeader.compiledTaskCount;
        header.runtimeStatisticsCount = sourceHeader.runtimeStatisticsCount;
        header.physicalQueueRuntimeStatisticsCount = sourceHeader.physicalQueueRuntimeStatisticsCount;
        header.packetSubmissionStatisticsCount = 0u;
        AppendPOD(outPayload, header);
    }
    BinaryDetail::AppendBytesNoReserveUnchecked(
        outPayload,
        source.data() + sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8),
        runtimeStatisticsOffset - sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
    );

    for(u32 statisticsIndex = 0u; statisticsIndex < sourceHeader.runtimeStatisticsCount; ++statisticsIndex){
        Telemetry::EncodedFrameGraphRuntimeStatisticsV8 statistics;
        const usize statisticsOffset = runtimeStatisticsOffset
            + sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8) * statisticsIndex
        ;
        NWB_MEMCPY(
            &statistics,
            sizeof(statistics),
            source.data() + statisticsOffset,
            sizeof(statistics)
        );
        const Telemetry::EncodedFrameGraphRuntimeStatisticsV6 legacyStatistics =
            DowngradeFrameGraphRuntimeStatisticsV8(statistics)
        ;
        if(legacyVersion >= Telemetry::s_FrameGraphRecoverySubmissionCountPayloadVersion)
            AppendPOD(outPayload, legacyStatistics);
        else
            AppendPOD(outPayload, DowngradeFrameGraphRuntimeStatisticsV6(legacyStatistics));
    }
    if(legacyVersion != Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion){
        for(
            u32 statisticsIndex = 0u;
            statisticsIndex < sourceHeader.physicalQueueRuntimeStatisticsCount;
            ++statisticsIndex
        ){
            Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 statistics;
            const usize statisticsOffset = physicalQueueRuntimeStatisticsOffset
                + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) * statisticsIndex
            ;
            NWB_MEMCPY(
                &statistics,
                sizeof(statistics),
                source.data() + statisticsOffset,
                sizeof(statistics)
            );
            if(legacyVersion >= Telemetry::s_FrameGraphRecoverySubmissionCountPayloadVersion)
                AppendPOD(outPayload, statistics);
            else
                AppendPOD(outPayload, DowngradeFrameGraphPhysicalQueueRuntimeStatisticsV6(statistics));
        }
    }
    BinaryDetail::AppendBytesNoReserveUnchecked(
        outPayload,
        source.data() + stringTableOffset,
        sourceHeader.stringTableBytes
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

