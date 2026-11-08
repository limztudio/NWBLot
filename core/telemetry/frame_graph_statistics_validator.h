// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph.h"

#include <core/alloc/scratch.h>
#include <core/telemetry/frame_graph_types.adhd.h>
#include <core/telemetry/local_values.adhd.h>

#include <global/overflow.h>
#include <global/simd/vector.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameGraphPhysicalQueueRuntimeStatisticsAccumulator{
    FrameGraphPhysicalQueueCompileRuntimeStatistics compile;
    FrameGraphPhysicalQueueRecordingRuntimeStatistics recording;
    FrameGraphPhysicalQueueSubmissionRuntimeStatistics submission;
    u64 barrierCount = 0u;
};

using FrameGraphPacketSubmissionStatisticsAccumulator = ::NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsAccumulatorValue;

class FrameGraphStatisticsValidator final{
private:
    static constexpr f64 s_DoublePrecisionEpsilon = ::NWB::Core::Telemetry::s_TelemetryFrameGraphStatisticsCodecDoublePrecisionEpsilon;
    static constexpr f64 s_SubmissionRoundingLimit = ::NWB::Core::Telemetry::s_TelemetryFrameGraphStatisticsCodecSubmissionRoundingLimit;
    static constexpr f64 s_SubmissionRoundingScale = ::NWB::Core::Telemetry::s_TelemetryFrameGraphStatisticsCodecSubmissionRoundingScale;

public:
    [[nodiscard]] static b8 PacketSubmissionStatisticsRecordLess(const FrameGraphPacketSubmissionStatisticsRecord& lhs, const FrameGraphPacketSubmissionStatisticsRecord& rhs)noexcept;
    [[nodiscard]] static b8 PhysicalQueueRuntimeStatisticsRecordLess(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& lhs, const FrameGraphPhysicalQueueRuntimeStatisticsRecord& rhs)noexcept;
    [[nodiscard]] static b8 AccumulatePhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatistics& statistics, const FrameGraphRuntimeStatistics& ownerStatistics, FrameGraphPhysicalQueueRuntimeStatisticsAccumulator& total)noexcept;
    [[nodiscard]] static b8 ValidatePhysicalQueueRuntimeStatisticsOwner(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record, usize nodeCount, FrameGraphNodeKind::Enum ownerKind, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept;
    [[nodiscard]] static b8 AccumulatePacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics, const FrameGraphSubmissionRuntimeStatistics& ownerStatistics, FrameGraphPacketSubmissionStatisticsAccumulator& total)noexcept;
    [[nodiscard]] static b8 PacketSubmissionDurationSumsMatch(f64 lhs, f64 rhs, u64 submissionCount)noexcept;

    [[nodiscard]] static b8 ValidatePacketSubmissionStatisticsOwner(const FrameGraphPacketSubmissionStatisticsRecord& statistics, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
        return
            IsValidFrameGraphRuntimeStatistics(ownerStatistics)
            && statistics.packetGeneration == ownerStatistics.planGeneration
            && static_cast<u64>(statistics.packetIndex) < ownerStatistics.compile.packetCount
            && statistics.queue.deviceGeneration == ownerStatistics.deviceGeneration
            && statistics.taskCount <= ownerStatistics.compile.taskCount
            && statistics.commandListCount <= ownerStatistics.recording.commandListCount
        ;
    }


    template<typename SubmissionStatistics>
    [[nodiscard]] static b8 PacketSubmissionStatisticsAccumulatorMatches(const FrameGraphPacketSubmissionStatisticsAccumulator& total, const SubmissionStatistics& statistics)noexcept(
        noexcept(static_cast<b8>(
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
        && noexcept(DeclVal<void (*)(b8)noexcept>()(DeclVal<u64>() <= DeclVal<const SubmissionStatistics&>().acceptedTaskCount - DeclVal<const FrameGraphPacketSubmissionStatisticsAccumulator&>().taskCount))
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

    template<typename NodeT>
    [[nodiscard]] static b8 ValidatePacketSubmissionStatisticsTable(
        Alloc::ScratchArena& scratchArena,
        const Span<const NodeT> nodes,
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

