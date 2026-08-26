// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuGraphSubmissionTransaction::rejectPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID
)noexcept{
    u64 recordingAttemptGeneration = 0u;
    {
        ScopedLock lock(m_mutex);
        recordingAttemptGeneration = m_recordingAttemptGeneration;
    }
    rejectPacket(graph, compiledGraph, packetID, recordingAttemptGeneration);
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
    GpuPacketRuntimeState::Enum previousState = GpuPacketRuntimeState::Declared;
    {
        ScopedLock lock(m_mutex);
        if(
            !validForLocked(compiledGraph)
            || m_recordingAttemptGeneration != recordingAttemptGeneration
            || packetID.index >= m_packets.size()
        )
            return;
        GpuPacketRuntime& runtime = m_packets[packetID.index];
        if(
            runtime.state == GpuPacketRuntimeState::Accepted
            || runtime.state == GpuPacketRuntimeState::Rejected
            || runtime.state == GpuPacketRuntimeState::Submitting
            || runtime.state == GpuPacketRuntimeState::Rejecting
        )
            return;
        if(
            runtime.state != GpuPacketRuntimeState::Declared
            && runtime.state != GpuPacketRuntimeState::Recorded
        )
            return;
        previousState = runtime.state;
        runtime.state = GpuPacketRuntimeState::Rejecting;
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
            GpuPacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == GpuPacketRuntimeState::Rejecting)
                runtime.state = previousState;
        }
        return;
    }
    {
        ScopedLock lock(m_mutex);
        if(validForLocked(compiledGraph) && packetID.index < m_packets.size()){
            GpuPacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == GpuPacketRuntimeState::Rejecting){
                runtime.state = GpuPacketRuntimeState::Rejected;
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
    GpuTaskPacketSubmissionLease& lease
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
        GpuPacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state != GpuPacketRuntimeState::Submitting)
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
        GpuPacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state == GpuPacketRuntimeState::Submitting){
            runtime.state = GpuPacketRuntimeState::Rejected;
            runtime.nativeSubmissionRejected = true;
            ++m_submissionStatistics.rejectedPacketCount;
            m_submissionStatistics.rejectedTaskCount += packet.taskCount;
            ++m_submissionStatistics.rejectedSubmissionCount;
        }
    }
}

void GpuGraphSubmissionTransaction::rejectTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId task
)noexcept{
    u64 recordingAttemptGeneration = 0u;
    {
        ScopedLock lock(m_mutex);
        recordingAttemptGeneration = m_recordingAttemptGeneration;
    }
    rejectTask(graph, compiledGraph, task, recordingAttemptGeneration);
}

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
    const GpuCompiledGraph& compiledGraph
)noexcept{
    u64 recordingAttemptGeneration = 0u;
    {
        ScopedLock lock(m_mutex);
        recordingAttemptGeneration = m_recordingAttemptGeneration;
    }
    return discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration);
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
            const GpuPacketRuntimeState::Enum state = m_packets[packetIndex].state;
            if(state == GpuPacketRuntimeState::Accepted || state == GpuPacketRuntimeState::Rejected)
                continue;
            if(
                state == GpuPacketRuntimeState::Submitting
                || state == GpuPacketRuntimeState::Rejecting
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
            || m_packets[packetIndex].state != GpuPacketRuntimeState::Rejected
        )
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

