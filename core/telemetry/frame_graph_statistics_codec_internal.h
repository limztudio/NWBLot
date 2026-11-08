// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph.h"

#include <core/alloc/scratch.h>
#include <core/telemetry/frame_graph_types.adhd.h>
#include <core/telemetry/local_values.adhd.h>

#include <global/binary.h>
#include <global/overflow.h>
#include <global/simd/vector.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphStatisticsCodecDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline EncodedFrameGraphPhysicalQueueId EncodeQueue(const FrameGraphPhysicalQueueId& queue)noexcept{
    return EncodedFrameGraphPhysicalQueueId{ .index = queue.index, .deviceGeneration = queue.deviceGeneration, };
}

[[nodiscard]] inline FrameGraphPhysicalQueueId DecodeQueue(const EncodedFrameGraphPhysicalQueueId& queue)noexcept{
    return FrameGraphPhysicalQueueId{ .index = queue.index, .deviceGeneration = queue.deviceGeneration, };
}

template<typename OutputT, typename InputT>
[[nodiscard]] inline OutputT ConvertSubmissionRuntimeStatistics(const InputT& statistics)noexcept(
    noexcept(OutputT{})
    && noexcept(DeclVal<void(*)(OutputT)noexcept>()(DeclVal<OutputT&&>()))
    && noexcept(DeclVal<OutputT&>().acceptedPacketCount = DeclVal<const InputT&>().acceptedPacketCount)
    && noexcept(DeclVal<OutputT&>().acceptedTaskCount = DeclVal<const InputT&>().acceptedTaskCount)
    && noexcept(DeclVal<OutputT&>().rejectedPacketCount = DeclVal<const InputT&>().rejectedPacketCount)
    && noexcept(DeclVal<OutputT&>().rejectedTaskCount = DeclVal<const InputT&>().rejectedTaskCount)
    && noexcept(DeclVal<OutputT&>().nativeSubmissionCount = DeclVal<const InputT&>().nativeSubmissionCount)
    && noexcept(DeclVal<OutputT&>().rejectedSubmissionCount = DeclVal<const InputT&>().rejectedSubmissionCount)
    && noexcept(DeclVal<OutputT&>().nativeCommandListCount = DeclVal<const InputT&>().nativeCommandListCount)
    && noexcept(DeclVal<OutputT&>().plannedWaitTokenCount = DeclVal<const InputT&>().plannedWaitTokenCount)
    && noexcept(DeclVal<OutputT&>().sameQueueWaitElisionCount = DeclVal<const InputT&>().sameQueueWaitElisionCount)
    && noexcept(DeclVal<OutputT&>().timelineWaitCount = DeclVal<const InputT&>().timelineWaitCount)
    && noexcept(DeclVal<OutputT&>().mergedTimelineWaitCount = DeclVal<const InputT&>().mergedTimelineWaitCount)
    && noexcept(DeclVal<OutputT&>().inheritedTimelineWaitElisionCount = DeclVal<const InputT&>().inheritedTimelineWaitElisionCount)
    && noexcept(DeclVal<OutputT&>().acceptedFrontierSubmissionCount = DeclVal<const InputT&>().acceptedFrontierSubmissionCount)
    && noexcept(DeclVal<OutputT&>().submissionSeconds = DeclVal<const InputT&>().submissionSeconds)
    && noexcept(DeclVal<OutputT&>().recoverySubmissionCount = DeclVal<const InputT&>().recoverySubmissionCount)
){
    OutputT converted;
    converted.acceptedPacketCount = statistics.acceptedPacketCount;
    converted.acceptedTaskCount = statistics.acceptedTaskCount;
    converted.rejectedPacketCount = statistics.rejectedPacketCount;
    converted.rejectedTaskCount = statistics.rejectedTaskCount;
    converted.nativeSubmissionCount = statistics.nativeSubmissionCount;
    converted.rejectedSubmissionCount = statistics.rejectedSubmissionCount;
    converted.nativeCommandListCount = statistics.nativeCommandListCount;
    converted.plannedWaitTokenCount = statistics.plannedWaitTokenCount;
    converted.sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount;
    converted.timelineWaitCount = statistics.timelineWaitCount;
    converted.mergedTimelineWaitCount = statistics.mergedTimelineWaitCount;
    converted.inheritedTimelineWaitElisionCount = statistics.inheritedTimelineWaitElisionCount;
    converted.acceptedFrontierSubmissionCount = statistics.acceptedFrontierSubmissionCount;
    converted.submissionSeconds = statistics.submissionSeconds;
    converted.recoverySubmissionCount = statistics.recoverySubmissionCount;
    return converted;
}

template<typename OutputT, typename InputT>
[[nodiscard]] inline OutputT ConvertPhysicalQueueCompileRuntimeStatistics(const InputT& statistics)noexcept(
    noexcept(OutputT{})
    && noexcept(DeclVal<void(*)(OutputT)noexcept>()(DeclVal<OutputT&&>()))
    && noexcept(DeclVal<OutputT&>().taskCount = DeclVal<const InputT&>().taskCount)
    && noexcept(DeclVal<OutputT&>().packetCount = DeclVal<const InputT&>().packetCount)
    && noexcept(DeclVal<OutputT&>().mergedTaskCount = DeclVal<const InputT&>().mergedTaskCount)
    && noexcept(DeclVal<OutputT&>().prologueBarrierCount = DeclVal<const InputT&>().prologueBarrierCount)
    && noexcept(DeclVal<OutputT&>().epilogueBarrierCount = DeclVal<const InputT&>().epilogueBarrierCount)
    && noexcept(DeclVal<OutputT&>().ownershipReleaseBarrierCount = DeclVal<const InputT&>().ownershipReleaseBarrierCount)
    && noexcept(DeclVal<OutputT&>().ownershipAcquireBarrierCount = DeclVal<const InputT&>().ownershipAcquireBarrierCount)
    && noexcept(DeclVal<OutputT&>().incomingLogicalOwnershipTransferCount = DeclVal<const InputT&>().incomingLogicalOwnershipTransferCount)
    && noexcept(DeclVal<OutputT&>().outgoingLogicalOwnershipTransferCount = DeclVal<const InputT&>().outgoingLogicalOwnershipTransferCount)
    && noexcept(DeclVal<OutputT&>().incomingLogicalOwnershipTransferSignatureCount = DeclVal<const InputT&>().incomingLogicalOwnershipTransferSignatureCount)
    && noexcept(DeclVal<OutputT&>().outgoingLogicalOwnershipTransferSignatureCount = DeclVal<const InputT&>().outgoingLogicalOwnershipTransferSignatureCount)
    && noexcept(DeclVal<OutputT&>().incomingRepeatedOwnershipTransferSignatureCount = DeclVal<const InputT&>().incomingRepeatedOwnershipTransferSignatureCount)
    && noexcept(DeclVal<OutputT&>().outgoingRepeatedOwnershipTransferSignatureCount = DeclVal<const InputT&>().outgoingRepeatedOwnershipTransferSignatureCount)
    && noexcept(DeclVal<OutputT&>().concurrentSharingAdviceResourceCount = DeclVal<const InputT&>().concurrentSharingAdviceResourceCount)
){
    OutputT converted;
    converted.taskCount = statistics.taskCount;
    converted.packetCount = statistics.packetCount;
    converted.mergedTaskCount = statistics.mergedTaskCount;
    converted.prologueBarrierCount = statistics.prologueBarrierCount;
    converted.epilogueBarrierCount = statistics.epilogueBarrierCount;
    converted.ownershipReleaseBarrierCount = statistics.ownershipReleaseBarrierCount;
    converted.ownershipAcquireBarrierCount = statistics.ownershipAcquireBarrierCount;
    converted.incomingLogicalOwnershipTransferCount = statistics.incomingLogicalOwnershipTransferCount;
    converted.outgoingLogicalOwnershipTransferCount = statistics.outgoingLogicalOwnershipTransferCount;
    converted.incomingLogicalOwnershipTransferSignatureCount = statistics.incomingLogicalOwnershipTransferSignatureCount;
    converted.outgoingLogicalOwnershipTransferSignatureCount = statistics.outgoingLogicalOwnershipTransferSignatureCount;
    converted.incomingRepeatedOwnershipTransferSignatureCount = statistics.incomingRepeatedOwnershipTransferSignatureCount;
    converted.outgoingRepeatedOwnershipTransferSignatureCount = statistics.outgoingRepeatedOwnershipTransferSignatureCount;
    converted.concurrentSharingAdviceResourceCount = statistics.concurrentSharingAdviceResourceCount;
    return converted;
}

template<typename OutputT, typename InputT>
[[nodiscard]] inline OutputT ConvertPhysicalQueueRecordingRuntimeStatistics(const InputT& statistics)noexcept(
    noexcept(OutputT{})
    && noexcept(DeclVal<void(*)(OutputT)noexcept>()(DeclVal<OutputT&&>()))
    && noexcept(DeclVal<OutputT&>().packetCount = DeclVal<const InputT&>().packetCount)
    && noexcept(DeclVal<OutputT&>().taskCount = DeclVal<const InputT&>().taskCount)
    && noexcept(DeclVal<OutputT&>().commandListCount = DeclVal<const InputT&>().commandListCount)
    && noexcept(DeclVal<OutputT&>().barrierCount = DeclVal<const InputT&>().barrierCount)
    && noexcept(DeclVal<OutputT&>().workerRoutedPacketCount = DeclVal<const InputT&>().workerRoutedPacketCount)
    && noexcept(DeclVal<OutputT&>().parallelPacketCount = DeclVal<const InputT&>().parallelPacketCount)
    && noexcept(DeclVal<OutputT&>().commandListAcquisitionSeconds = DeclVal<const InputT&>().commandListAcquisitionSeconds)
    && noexcept(DeclVal<OutputT&>().graphBarrierRecordingSeconds = DeclVal<const InputT&>().graphBarrierRecordingSeconds)
    && noexcept(DeclVal<OutputT&>().taskRecordSeconds = DeclVal<const InputT&>().taskRecordSeconds)
    && noexcept(DeclVal<OutputT&>().recordingSeconds = DeclVal<const InputT&>().recordingSeconds)
){
    OutputT converted;
    converted.packetCount = statistics.packetCount;
    converted.taskCount = statistics.taskCount;
    converted.commandListCount = statistics.commandListCount;
    converted.barrierCount = statistics.barrierCount;
    converted.workerRoutedPacketCount = statistics.workerRoutedPacketCount;
    converted.parallelPacketCount = statistics.parallelPacketCount;
    converted.commandListAcquisitionSeconds = statistics.commandListAcquisitionSeconds;
    converted.graphBarrierRecordingSeconds = statistics.graphBarrierRecordingSeconds;
    converted.taskRecordSeconds = statistics.taskRecordSeconds;
    converted.recordingSeconds = statistics.recordingSeconds;
    return converted;
}

[[nodiscard]] EncodedFrameGraphPhysicalQueueRuntimeStatistics EncodePhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record)noexcept;
[[nodiscard]] Expected<FrameGraphPhysicalQueueRuntimeStatisticsRecord> DecodePhysicalQueueRuntimeStatistics(const EncodedFrameGraphPhysicalQueueRuntimeStatistics& encoded, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept;
[[nodiscard]] EncodedFrameGraphPacketSubmissionStatistics EncodePacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics)noexcept;
[[nodiscard]] Expected<FrameGraphPacketSubmissionStatisticsRecord> DecodePacketSubmissionStatistics(const EncodedFrameGraphPacketSubmissionStatistics& encoded)noexcept;

[[nodiscard]] EncodedFrameGraphRuntimeStatistics EncodeRuntimeStatistics(u32 nodeIndex, const FrameGraphRuntimeStatistics& statistics)noexcept;
[[nodiscard]] Expected<FrameGraphRuntimeStatistics> DecodeRuntimeStatistics(const EncodedFrameGraphRuntimeStatistics& encoded)noexcept;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

