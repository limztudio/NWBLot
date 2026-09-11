// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::abandonPacketRecordingWithoutCallbacks(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    PacketRecordingLease& lease
)const noexcept{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || !lease.valid()
        || lease.m_packet != packet
        || lease.m_planGeneration != planAccess.planGeneration()
    )
        return false;
    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    NothrowScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeCompiledGraph != &compiledGraph
        || m_activeRecordingPlanGeneration != planAccess.planGeneration()
        || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
    )
        return false;
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != TaskLifecycleState::Recording
            || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
            || task.recordingClaimGeneration != lease.m_claimGeneration
            || task.recordThunkInProgress
        )
            return false;
    }
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        task.lifecycleState = TaskLifecycleState::Discarded;
        task.recordingClaimGeneration = 0u;
        task.submissionClaimGeneration = 0u;
        task.discardNotificationGeneration = 0u;
        task.recordThunkInProgress = false;
        task.recordThunkCompleted = false;
    }
    lease.reset();
    releasePacketRecordingClaimWithinLock();
    return true;
}

bool GpuTaskGraph::abandonPacketRecordingAbortWithoutCallbacks(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    PacketRecordingAbort& abort
)const noexcept{
    if(!abort.valid())
        return false;

    PacketRecordingLease lease;
    lease.m_packet = abort.m_packet;
    lease.m_planGeneration = abort.m_planGeneration;
    lease.m_recordingAttemptGeneration = abort.m_recordingAttemptGeneration;
    lease.m_claimGeneration = abort.m_claimGeneration;
    if(!abandonPacketRecordingWithoutCallbacks(compiledGraph, planAccess, abort.m_packet, lease))
        return false;
    abort.reset();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

