// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuGraphSubmissionTransaction::rejectTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId task,
    const u64 recordingAttemptGeneration
)noexcept{
    if(!validFor(compiledGraph))
        return;
    rejectPacket(graph, compiledGraph, compiledGraph.packetForTask(task), recordingAttemptGeneration);
}

bool GpuGraphSubmissionTransaction::discardUnaccepted(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
)noexcept{
    if(
        !validFor(compiledGraph)
        || recordingAttemptGeneration == 0u
        || !bindRecordingAttempt(graph, compiledGraph, recordingAttemptGeneration)
    )
        return false;

    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex){
        {
            ScopedLock lock(m_mutex);
            if(
                !validForLocked(compiledGraph)
                || m_recordingAttemptGeneration != recordingAttemptGeneration
                || packetIndex >= m_packets.size()
            )
                return false;
            const PacketRuntimeState state = m_packets[packetIndex].state;
            if(state == PacketRuntimeState::Accepted || state == PacketRuntimeState::Rejected)
                continue;
            if(
                state == PacketRuntimeState::Submitting
                || state == PacketRuntimeState::Rejecting
            )
                return false;
        }

        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        if(!packet.valid())
            return false;
        rejectPacket(graph, compiledGraph, packet, recordingAttemptGeneration);

        ScopedLock lock(m_mutex);
        if(
            !validForLocked(compiledGraph)
            || packetIndex >= m_packets.size()
            || m_packets[packetIndex].state != PacketRuntimeState::Rejected
        )
            return false;
    }
    return true;
}


void GpuGraphSubmissionTransaction::rejectPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const u64 recordingAttemptGeneration
)noexcept{
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || recordingAttemptGeneration == 0u
        || !bindRecordingAttempt(graph, compiledGraph, recordingAttemptGeneration)
    )
        return;
    {
        ScopedLock lock(m_mutex);
        if(
            !validForLocked(compiledGraph)
            || m_recordingAttemptGeneration != recordingAttemptGeneration
            || packetID.index >= m_packets.size()
        )
            return;
        PacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state != PacketRuntimeState::Declared)
            return;
        runtime.state = PacketRuntimeState::Rejecting;
    }

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    if(!graph.discardUnacceptedPacket(
        compiledGraph,
        packetID,
        recordingAttemptGeneration
    ))
    {
        ScopedLock lock(m_mutex);
        if(validForLocked(compiledGraph) && packetID.index < m_packets.size()){
            PacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == PacketRuntimeState::Rejecting)
                runtime.state = PacketRuntimeState::Declared;
        }
        return;
    }
    {
        ScopedLock lock(m_mutex);
        if(validForLocked(compiledGraph) && packetID.index < m_packets.size()){
            PacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == PacketRuntimeState::Rejecting){
                runtime.state = PacketRuntimeState::Rejected;
                ++m_submissionStatistics.rejectedPacketCount;
                m_submissionStatistics.rejectedTaskCount += packet.taskCount;
            }
        }
    }
}

void GpuGraphSubmissionTransaction::rejectSubmittingPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    GpuTaskGraph::PacketSubmissionLease& lease
)noexcept{
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || !lease.valid()
        || lease.m_packet != packetID
        || lease.m_planGeneration != compiledGraph.planGeneration()
        || !bindRecordingAttempt(graph, compiledGraph, lease.m_recordingAttemptGeneration)
    )
        return;

    {
        ScopedLock lock(m_mutex);
        if(
            !validForLocked(compiledGraph)
            || m_recordingAttemptGeneration != lease.m_recordingAttemptGeneration
            || packetID.index >= m_packets.size()
        )
            return;
        PacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state != PacketRuntimeState::Submitting)
            return;
    }

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    graph.abortPacketSubmission(
        compiledGraph,
        packetID,
        lease
    );
    if(lease.valid())
        return;

    ScopedLock lock(m_mutex);
    if(validForLocked(compiledGraph) && packetID.index < m_packets.size()){
        PacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state == PacketRuntimeState::Submitting){
            runtime.state = PacketRuntimeState::Rejected;
            runtime.nativeSubmissionRejected = true;
            ++m_submissionStatistics.rejectedPacketCount;
            m_submissionStatistics.rejectedTaskCount += packet.taskCount;
            ++m_submissionStatistics.rejectedSubmissionCount;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

