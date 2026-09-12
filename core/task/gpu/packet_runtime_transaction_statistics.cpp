// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "scheduler.h"

#include "task_graph.h"

#include <core/common/log.h>
#include <core/graphics/gpu_timing.h>
#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuGraphSubmissionTransaction::hasAcceptedPackets()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_valid && m_acceptedSubmissionCount != 0u;
}


GpuTaskGraphSubmissionStatistics GpuGraphSubmissionTransaction::submissionStatistics()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_submissionStatistics;
}

GpuTaskGraphPacketSubmissionStatistics GpuGraphSubmissionTransaction::packetSubmissionStatistics(
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId& packetID
)const noexcept{
    NothrowScopedLock lock(m_mutex);
    if(
        !validForLocked(planAccess)
        || !planAccess.validPacket(packetID)
        || packetID.index >= m_packets.size()
    )
        return {};

    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    if(!packetView.valid())
        return {};
    const GpuSubmissionPacket& packet = *packetView.plan;
    const GpuPhysicalQueueInfo* const queueInfo = planAccess.queueInfo(packet.queue);
    const PacketRuntime& runtime = m_packets[packetID.index];
    if(
        !queueInfo
        || queueInfo->queueClass >= CommandQueue::kCount
        || runtime.state != PacketRuntimeState::Accepted
        || !runtime.token.valid()
        || runtime.token.queue != queueInfo->queueClass
        || !runtime.token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)
    )
        return {};

    return GpuTaskGraphPacketSubmissionStatistics{
        .graphGeneration = m_generation,
        .planGeneration = m_planGeneration,
        .recordingAttemptGeneration = m_recordingAttemptGeneration,
        .packet = packetID,
        .queue = packet.queue,
        .deviceGeneration = m_deviceGeneration,
        .queueClass = queueInfo->queueClass,
        .taskCount = packet.taskCount,
        .nativeCommandListCount = runtime.nativeCommandListCount,
        .plannedWaitTokenCount = runtime.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = runtime.sameQueueWaitElisionCount,
        .timelineWaitCount = runtime.timelineWaitCount,
        .mergedTimelineWaitCount = runtime.mergedTimelineWaitCount,
        .submissionSeconds = runtime.submissionSeconds,
        .joinsAcceptedQueueFrontier = packet.joinsAcceptedQueueFrontier,
        .isRecoverySubmission = packet.isRecoverySubmission,
    };
}

GpuTaskGraphPhysicalQueueSubmissionStatistics GpuGraphSubmissionTransaction::physicalQueueSubmissionStatistics(
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuPhysicalQueueId& queue
)const noexcept{
    NothrowScopedLock lock(m_mutex);
    if(!validForLocked(planAccess))
        return {};

    const GpuPhysicalQueueInfo* const queueInfo = planAccess.queueInfo(queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::kCount)
        return {};

    GpuTaskGraphPhysicalQueueSubmissionStatistics statistics{
        .graphGeneration = m_generation,
        .planGeneration = m_planGeneration,
        .recordingAttemptGeneration = m_recordingAttemptGeneration,
        .queue = queue,
        .deviceGeneration = m_deviceGeneration,
        .queueClass = queueInfo->queueClass,
    };
    for(usize packetIndex = 0u; packetIndex < m_packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(packetIndex);
        if(!packetID.valid())
            return {};

        const GpuCompiledPacketView packetView = planAccess.packet(packetID);
        if(!packetView.valid())
            return {};
        const GpuSubmissionPacket& packet = *packetView.plan;
        if(packet.queue != queue)
            continue;

        const PacketRuntime& runtime = m_packets[packetIndex];
        if(runtime.nativeSubmissionRejected && runtime.state != PacketRuntimeState::Rejected)
            return {};

        switch(runtime.state){
        case PacketRuntimeState::Accepted:
            ++statistics.acceptedPacketCount;
            statistics.acceptedTaskCount += packet.taskCount;
            ++statistics.nativeSubmissionCount;
            statistics.nativeCommandListCount += runtime.nativeCommandListCount;
            statistics.plannedWaitTokenCount += runtime.plannedWaitTokenCount;
            statistics.sameQueueWaitElisionCount += runtime.sameQueueWaitElisionCount;
            statistics.timelineWaitCount += runtime.timelineWaitCount;
            statistics.mergedTimelineWaitCount += runtime.mergedTimelineWaitCount;
            statistics.submissionSeconds += runtime.submissionSeconds;
            if(packet.joinsAcceptedQueueFrontier)
                ++statistics.acceptedFrontierSubmissionCount;
            if(packet.isRecoverySubmission)
                ++statistics.recoverySubmissionCount;
            break;
        case PacketRuntimeState::Rejected:
            ++statistics.rejectedPacketCount;
            statistics.rejectedTaskCount += packet.taskCount;
            if(runtime.nativeSubmissionRejected)
                ++statistics.rejectedSubmissionCount;
            break;
        default:
            break;
        }
    }
    return statistics;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

