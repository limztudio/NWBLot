// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::discardUnacceptedPacket(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || recordingAttemptGeneration == 0u
        || !submissionBinding.valid()
    )
        return false;
    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    DiscardNotificationScope notification(*this);
    u64 notificationGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeCompiledGraph != &compiledGraph
            || m_activeRecordingPlanGeneration != planAccess.planGeneration()
            || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
            || m_activeSubmissionBinding != submissionBinding
            || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
        )
            return false;

        bool hasUnacceptedTask = false;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                (
                    task.lifecycleState != TaskLifecycleState::Declared
                    && task.lifecycleState != TaskLifecycleState::Recorded
                    && task.lifecycleState != TaskLifecycleState::Discarded
                )
                || task.lifecycleAttemptGeneration != recordingAttemptGeneration
            )
                return false;
            if(task.lifecycleState != TaskLifecycleState::Discarded)
                hasUnacceptedTask = true;
        }
        if(!hasUnacceptedTask)
            return true;

        notificationGeneration = allocateGeneration();
        notification.activateWithinLock();
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState != TaskLifecycleState::Discarded){
                task.lifecycleState = TaskLifecycleState::Discarded;
                task.recordingClaimGeneration = 0u;
                task.submissionClaimGeneration = 0u;
                task.discardNotificationGeneration = notificationGeneration;
                task.recordThunkInProgress = false;
                task.recordThunkCompleted = false;
            }
        }
    }

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.discardNotificationGeneration == notificationGeneration
            && task.payload
            && task.discardPayload
        )
            task.discardPayload(task.payload);
    }
    return true;
}



bool GpuTaskGraph::abandonUnacceptedPacketWithoutCallbacks(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || recordingAttemptGeneration == 0u
        || !submissionBinding.valid()
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
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
        || m_activeSubmissionBinding != submissionBinding
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            (
                task.lifecycleState != TaskLifecycleState::Declared
                && task.lifecycleState != TaskLifecycleState::Recorded
                && task.lifecycleState != TaskLifecycleState::Discarded
            )
            || task.lifecycleAttemptGeneration != recordingAttemptGeneration
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
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

