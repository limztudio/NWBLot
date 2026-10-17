// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph.h"

#include <core/alloc/scratch.h>

#include <global/binary.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphStatisticsCodecDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f64 s_DoublePrecisionEpsilon = 2.2204460492503130808472633361816e-16;
inline constexpr f64 s_SubmissionRoundingLimit = 0.5;
inline constexpr f64 s_SubmissionRoundingScale = 2.0;


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

[[nodiscard]] inline bool AccumulateBoundedCount(const u64 value, const u64 maximum, u64& total)noexcept{
    if(total > maximum || value > maximum - total)
        return false;
    total += value;
    return true;
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

[[nodiscard]] inline EncodedFrameGraphPhysicalQueueRuntimeStatistics EncodePhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record)noexcept{
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics = record.statistics;
    EncodedFrameGraphPhysicalQueueRuntimeStatistics encoded;
    encoded.ownerNodeIndex = record.ownerNodeIndex;
    encoded.queue = EncodeQueue(statistics.queue);
    encoded.queueClass = statistics.queueClass;
    encoded.compile = ConvertPhysicalQueueCompileRuntimeStatistics<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>(statistics.compile);
    encoded.recording = ConvertPhysicalQueueRecordingRuntimeStatistics<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>(statistics.recording);
    encoded.submission = ConvertSubmissionRuntimeStatistics<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>(statistics.submission);
    return encoded;
}

[[nodiscard]] inline Expected<FrameGraphPhysicalQueueRuntimeStatisticsRecord> DecodePhysicalQueueRuntimeStatistics(const EncodedFrameGraphPhysicalQueueRuntimeStatistics& encoded, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
    FrameGraphPhysicalQueueRuntimeStatisticsRecord decoded;
    if(encoded.reserved[0u] != 0u || encoded.reserved[1u] != 0u || encoded.reserved[2u] != 0u || encoded.reserved[3u] != 0u || encoded.reserved[4u] != 0u || encoded.reserved[5u] != 0u || encoded.reserved[6u] != 0u)
        return MakeUnexpected(Failure{});

    decoded = {
        .ownerNodeIndex = encoded.ownerNodeIndex,
        .statistics = {
            .graphGeneration = ownerStatistics.graphGeneration,
            .planGeneration = ownerStatistics.planGeneration,
            .recordingAttemptGeneration = ownerStatistics.recordingAttemptGeneration,
            .queue = DecodeQueue(encoded.queue),
            .deviceGeneration = ownerStatistics.deviceGeneration,
            .queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass),
            .compile = ConvertPhysicalQueueCompileRuntimeStatistics<FrameGraphPhysicalQueueCompileRuntimeStatistics>(encoded.compile),
            .recording = ConvertPhysicalQueueRecordingRuntimeStatistics<FrameGraphPhysicalQueueRecordingRuntimeStatistics>(encoded.recording),
            .submission = ConvertSubmissionRuntimeStatistics<FrameGraphPhysicalQueueSubmissionRuntimeStatistics>(encoded.submission),
        },
    };
    if(!IsValidFrameGraphPhysicalQueueRuntimeStatistics(decoded.statistics))
        return MakeUnexpected(Failure{});
    return decoded;
}

[[nodiscard]] inline bool PacketSubmissionStatisticsRecordLess(const FrameGraphPacketSubmissionStatisticsRecord& lhs, const FrameGraphPacketSubmissionStatisticsRecord& rhs)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    return lhs.packetIndex < rhs.packetIndex;
}

[[nodiscard]] inline EncodedFrameGraphPacketSubmissionStatistics EncodePacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics)noexcept{
    return EncodedFrameGraphPacketSubmissionStatistics{
        .ownerNodeIndex = statistics.ownerNodeIndex,
        .packetIndex = statistics.packetIndex,
        .packetGeneration = statistics.packetGeneration,
        .queue = EncodeQueue(statistics.queue),
        .queueClass = statistics.queueClass,
        .joinsAcceptedQueueFrontier = static_cast<u8>(statistics.joinsAcceptedQueueFrontier ? 1u : 0u),
        .recoverySubmission = static_cast<u8>(statistics.recoverySubmission ? 1u : 0u),
        .taskCount = statistics.taskCount,
        .commandListCount = statistics.commandListCount,
        .plannedWaitTokenCount = statistics.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount,
        .timelineWaitCount = statistics.timelineWaitCount,
        .mergedTimelineWaitCount = statistics.mergedTimelineWaitCount,
        .inheritedTimelineWaitElisionCount = statistics.inheritedTimelineWaitElisionCount,
        .submissionSeconds = statistics.submissionSeconds,
    };
}

[[nodiscard]] inline Expected<FrameGraphPacketSubmissionStatisticsRecord> DecodePacketSubmissionStatistics(const EncodedFrameGraphPacketSubmissionStatistics& encoded)noexcept{
    FrameGraphPacketSubmissionStatisticsRecord decoded;
    if(encoded.joinsAcceptedQueueFrontier > 1u || encoded.recoverySubmission > 1u || encoded.reserved != 0u)
        return MakeUnexpected(Failure{});

    decoded = {
        .packetGeneration = encoded.packetGeneration,
        .taskCount = encoded.taskCount,
        .commandListCount = encoded.commandListCount,
        .ownerNodeIndex = encoded.ownerNodeIndex,
        .packetIndex = encoded.packetIndex,
        .queue = DecodeQueue(encoded.queue),
        .queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass),
        .joinsAcceptedQueueFrontier = encoded.joinsAcceptedQueueFrontier != 0u,
        .recoverySubmission = encoded.recoverySubmission != 0u,
        .plannedWaitTokenCount = encoded.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = encoded.sameQueueWaitElisionCount,
        .timelineWaitCount = encoded.timelineWaitCount,
        .mergedTimelineWaitCount = encoded.mergedTimelineWaitCount,
        .inheritedTimelineWaitElisionCount = encoded.inheritedTimelineWaitElisionCount,
        .submissionSeconds = encoded.submissionSeconds,
    };
    if(!IsValidFrameGraphPacketSubmissionStatistics(decoded))
        return MakeUnexpected(Failure{});
    return decoded;
}

struct FrameGraphPacketSubmissionStatisticsAccumulator{
    u64 nativeSubmissionCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 inheritedTimelineWaitElisionCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    u64 recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

[[nodiscard]] inline bool ValidatePacketSubmissionStatisticsOwner(const FrameGraphPacketSubmissionStatisticsRecord& statistics, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
    return
        IsValidFrameGraphRuntimeStatistics(ownerStatistics)
        && statistics.packetGeneration == ownerStatistics.planGeneration
        && static_cast<u64>(statistics.packetIndex) < ownerStatistics.compile.packetCount
        && statistics.queue.deviceGeneration == ownerStatistics.deviceGeneration
        && statistics.taskCount <= ownerStatistics.compile.taskCount
        && statistics.commandListCount <= ownerStatistics.recording.commandListCount
    ;
}

[[nodiscard]] inline bool AccumulatePacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics, const FrameGraphSubmissionRuntimeStatistics& ownerStatistics, FrameGraphPacketSubmissionStatisticsAccumulator& total)noexcept{
    if(!(
        AccumulateBoundedCount(1u, ownerStatistics.nativeSubmissionCount, total.nativeSubmissionCount)
        && AccumulateBoundedCount(statistics.taskCount, ownerStatistics.acceptedTaskCount, total.taskCount)
        && AccumulateBoundedCount(statistics.commandListCount, ownerStatistics.nativeCommandListCount, total.commandListCount)
        && AccumulateBoundedCount(statistics.plannedWaitTokenCount, ownerStatistics.plannedWaitTokenCount, total.plannedWaitTokenCount)
        && AccumulateBoundedCount(statistics.sameQueueWaitElisionCount, ownerStatistics.sameQueueWaitElisionCount, total.sameQueueWaitElisionCount)
        && AccumulateBoundedCount(statistics.timelineWaitCount, ownerStatistics.timelineWaitCount, total.timelineWaitCount)
        && AccumulateBoundedCount(statistics.mergedTimelineWaitCount, ownerStatistics.mergedTimelineWaitCount, total.mergedTimelineWaitCount)
        && AccumulateBoundedCount(statistics.inheritedTimelineWaitElisionCount, ownerStatistics.inheritedTimelineWaitElisionCount, total.inheritedTimelineWaitElisionCount)
        && AccumulateBoundedCount(statistics.joinsAcceptedQueueFrontier ? 1u : 0u, ownerStatistics.acceptedFrontierSubmissionCount, total.acceptedFrontierSubmissionCount)
        && AccumulateBoundedCount(statistics.recoverySubmission ? 1u : 0u, ownerStatistics.recoverySubmissionCount, total.recoverySubmissionCount)
    ))
        return false;

    total.submissionSeconds += statistics.submissionSeconds;
    return IsFinite(total.submissionSeconds);
}

[[nodiscard]] inline bool PacketSubmissionDurationSumsMatch(const f64 lhs, const f64 rhs, const u64 submissionCount)noexcept{
    if(lhs == rhs)
        return true;
    if(submissionCount == 0u)
        return false;

    const f64 roundingFactor = static_cast<f64>(submissionCount) * s_DoublePrecisionEpsilon;
    if(roundingFactor >= s_SubmissionRoundingLimit)
        return false;
    const f64 magnitude = lhs > rhs ? lhs : rhs;
    const f64 difference = lhs > rhs ? lhs - rhs : rhs - lhs;
    return difference / magnitude <= (s_SubmissionRoundingScale * roundingFactor) / (1.0 - roundingFactor);
}

template<typename SubmissionStatistics>
[[nodiscard]] inline bool PacketSubmissionStatisticsAccumulatorMatches(const FrameGraphPacketSubmissionStatisticsAccumulator& total, const SubmissionStatistics& statistics)noexcept(
    noexcept(static_cast<bool>(
        DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().nativeSubmissionCount != DeclVal<const SubmissionStatistics&>().nativeSubmissionCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().commandListCount != DeclVal<const SubmissionStatistics&>().nativeCommandListCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().plannedWaitTokenCount != DeclVal<const SubmissionStatistics&>().plannedWaitTokenCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().sameQueueWaitElisionCount != DeclVal<const SubmissionStatistics&>().sameQueueWaitElisionCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().timelineWaitCount != DeclVal<const SubmissionStatistics&>().timelineWaitCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().mergedTimelineWaitCount != DeclVal<const SubmissionStatistics&>().mergedTimelineWaitCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().inheritedTimelineWaitElisionCount != DeclVal<const SubmissionStatistics&>().inheritedTimelineWaitElisionCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().acceptedFrontierSubmissionCount != DeclVal<const SubmissionStatistics&>().acceptedFrontierSubmissionCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().recoverySubmissionCount != DeclVal<const SubmissionStatistics&>().recoverySubmissionCount
        || DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().taskCount > DeclVal<const SubmissionStatistics&>().acceptedTaskCount
        || !PacketSubmissionDurationSumsMatch(
            DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().submissionSeconds,
            DeclVal<const SubmissionStatistics&>().submissionSeconds,
            DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().nativeSubmissionCount
        )
    ))
    && noexcept(DeclVal<void (*)(u64)noexcept>()(DeclVal<const SubmissionStatistics&>().acceptedPacketCount - DeclVal<const SubmissionStatistics&>().nativeSubmissionCount))
    && noexcept(DeclVal<void (*)(bool)noexcept>()(DeclVal<u64>() <= DeclVal<const SubmissionStatistics&>().acceptedTaskCount - DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().taskCount))
){
    if(
        total.nativeSubmissionCount != statistics.nativeSubmissionCount
        || total.commandListCount != statistics.nativeCommandListCount
        || total.plannedWaitTokenCount != statistics.plannedWaitTokenCount
        || total.sameQueueWaitElisionCount != statistics.sameQueueWaitElisionCount
        || total.timelineWaitCount != statistics.timelineWaitCount
        || total.mergedTimelineWaitCount != statistics.mergedTimelineWaitCount
        || total.inheritedTimelineWaitElisionCount != statistics.inheritedTimelineWaitElisionCount
        || total.acceptedFrontierSubmissionCount != statistics.acceptedFrontierSubmissionCount
        || total.recoverySubmissionCount != statistics.recoverySubmissionCount
        || total.taskCount > statistics.acceptedTaskCount
        || !PacketSubmissionDurationSumsMatch(total.submissionSeconds, statistics.submissionSeconds, total.nativeSubmissionCount)
    )
        return false;

    const u64 manualAcceptedPacketCount = statistics.acceptedPacketCount - statistics.nativeSubmissionCount;
    return manualAcceptedPacketCount <= statistics.acceptedTaskCount - total.taskCount;
}

template<typename NodeContainer>
[[nodiscard]] inline bool ValidatePacketSubmissionStatisticsTable(
    Alloc::ScratchArena& scratchArena,
    const NodeContainer& nodes,
    const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const Span<const FrameGraphPacketSubmissionStatisticsRecord> packetSubmissionStatistics
){
    for(usize statisticsIndex = 0u; statisticsIndex < packetSubmissionStatistics.size(); ++statisticsIndex){
        const FrameGraphPacketSubmissionStatisticsRecord& statistics = packetSubmissionStatistics[statisticsIndex];
        if(
            statistics.ownerNodeIndex >= nodes.size()
            || nodes[statistics.ownerNodeIndex].kind != FrameGraphNodeKind::Pass
            || !ValidatePacketSubmissionStatisticsOwner(statistics, nodes[statistics.ownerNodeIndex].runtimeStatistics)
            || (statisticsIndex != 0u && !PacketSubmissionStatisticsRecordLess(packetSubmissionStatistics[statisticsIndex - 1u], statistics))
        )
            return false;
    }

    Vector<FrameGraphPacketSubmissionStatisticsAccumulator, Alloc::ScratchArena> queueTotals(scratchArena);
    queueTotals.resize(physicalQueueRuntimeStatistics.size());
    usize statisticsIndex = 0u;
    usize queueBegin = 0u;
    for(usize nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex){
        usize queueEnd = queueBegin;
        while(queueEnd < physicalQueueRuntimeStatistics.size() && physicalQueueRuntimeStatistics[queueEnd].ownerNodeIndex == nodeIndex)
            ++queueEnd;

        FrameGraphPacketSubmissionStatisticsAccumulator total;
        while(statisticsIndex < packetSubmissionStatistics.size() && packetSubmissionStatistics[statisticsIndex].ownerNodeIndex == nodeIndex){
            const auto& statistics = packetSubmissionStatistics[statisticsIndex];
            const auto& ownerSubmission = nodes[nodeIndex].runtimeStatistics.submission;
            if(!AccumulatePacketSubmissionStatistics(statistics, ownerSubmission, total))
                return false;

            usize first = queueBegin;
            usize last = queueEnd;
            while(first < last){
                const usize middle = first + (last - first) / 2u;
                const auto queue = physicalQueueRuntimeStatistics[middle].statistics.queue;
                if(queue.index < statistics.queue.index || (queue.index == statistics.queue.index && queue.deviceGeneration < statistics.queue.deviceGeneration))
                    first = middle + 1u;
                else
                    last = middle;
            }
            if(first < queueEnd && physicalQueueRuntimeStatistics[first].statistics.queue == statistics.queue){
                if(physicalQueueRuntimeStatistics[first].statistics.queueClass != statistics.queueClass || !AccumulatePacketSubmissionStatistics(statistics, ownerSubmission, queueTotals[first]))
                    return false;
            }
            ++statisticsIndex;
        }
        if(nodes[nodeIndex].runtimeStatistics.present && !PacketSubmissionStatisticsAccumulatorMatches(total, nodes[nodeIndex].runtimeStatistics.submission))
            return false;
        queueBegin = queueEnd;
    }
    if(statisticsIndex != packetSubmissionStatistics.size())
        return false;

    for(usize queueIndex = 0u; queueIndex < physicalQueueRuntimeStatistics.size(); ++queueIndex){
        if(!PacketSubmissionStatisticsAccumulatorMatches(queueTotals[queueIndex], physicalQueueRuntimeStatistics[queueIndex].statistics.submission))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

