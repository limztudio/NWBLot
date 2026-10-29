// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuGraphSubmissionAcceptanceSnapshot> GpuGraphSubmissionTransaction::copyAcceptedPacketTokens(const GpuCompiledGraph::ReadView& compiledPlan, Alloc::ScratchArena& scratchArena)const{
    if(!compiledPlan.valid())
        return MakeUnexpected(Failure{});
    GpuGraphSubmissionAcceptanceSnapshot snapshot{ .packetTokens = Vector<QueueSubmissionToken, Alloc::ScratchArena>(compiledPlan.packetCount(), scratchArena), };

    NothrowScopedLock lock(m_mutex);
    if(!m_valid || m_generation != compiledPlan.generation() || m_planGeneration != compiledPlan.planGeneration() || m_deviceGeneration != compiledPlan.deviceGeneration() || m_packets.size() != compiledPlan.packetCount() || m_acceptanceRevision == 0u)
        return MakeUnexpected(Failure{});

    for(usize packetIndex = 0u; packetIndex < snapshot.packetTokens.size(); ++packetIndex){
        const PacketRuntime& runtime = m_packets[packetIndex];
        snapshot.packetTokens[packetIndex] = runtime.state == PacketRuntimeState::Accepted ? runtime.token : QueueSubmissionToken{};
    }
    snapshot.recordingAttemptGeneration = m_recordingAttemptGeneration;
    snapshot.acceptanceRevision = m_acceptanceRevision;
    return snapshot;
}

QueueSubmissionToken GpuGraphSubmissionTransaction::packetToken(const GpuSubmissionPacketId& packet)const noexcept{
    NothrowScopedLock lock(m_mutex);
    return packetTokenLocked(packet);
}


QueueSubmissionToken GpuGraphSubmissionTransaction::taskToken(const GpuCompiledGraph::ReadView& planAccess, const GpuTaskId task)const noexcept{
    if(!planAccess.valid())
        return {};
    NothrowScopedLock lock(m_mutex);
    if(!validForLocked(planAccess))
        return {};
    return packetTokenLocked(planAccess.packetForTask(task));
}

b8 GpuGraphSubmissionTransaction::appendAcceptedQueueFrontierWaitTokens(const GpuPhysicalQueueId& destinationQueue, Vector<QueueSubmissionToken, Alloc::ScratchArena>& outTokens)const{
    ScopedLock lock(m_mutex);
    if(!m_valid || !destinationQueue.valid() || destinationQueue.deviceGeneration != m_deviceGeneration)
        return false;

    for(const LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == destinationQueue)
            continue;
        if(!latest.queue.valid() || latest.queue.deviceGeneration != m_deviceGeneration || !latest.token.valid() || !latest.token.matchesPhysicalQueue(latest.queue.index, latest.queue.deviceGeneration))
            return false;
        // One newest token per queue, so no producer duplicates even after several accepts before the recovery tail arms.
        outTokens.push_back(latest.token);
    }
    return true;
}

QueueSubmissionToken GpuGraphSubmissionTransaction::packetTokenLocked(const GpuSubmissionPacketId& packet)const noexcept{
    if(!m_valid || !packet.valid() || packet.generation != m_planGeneration || packet.index >= m_packets.size())
        return {};
    const PacketRuntime& runtime = m_packets[packet.index];
    return runtime.state == PacketRuntimeState::Accepted ? runtime.token : QueueSubmissionToken{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

