// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::packetReadyForSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || recordingAttemptGeneration == 0u
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
        || m_activeRecordingPreparationSerial != 0u
        || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != TaskLifecycleState::Recorded
            || task.lifecycleAttemptGeneration != recordingAttemptGeneration
        )
            return false;
    }
    return true;
}


bool GpuTaskGraph::beginPacketSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding,
    PacketSubmissionLease& outLease
)const noexcept{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || recordingAttemptGeneration == 0u
        || !submissionBinding.valid()
        || outLease.valid()
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
        || m_activeRecordingPreparationSerial != 0u
        || m_activeSubmissionBinding != submissionBinding
        || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != TaskLifecycleState::Recorded
            || task.lifecycleAttemptGeneration != recordingAttemptGeneration
            || task.recordingClaimGeneration != 0u
            || task.submissionClaimGeneration != 0u
            || task.recordThunkInProgress
        )
            return false;
    }

    const u64 claimGeneration = allocateGeneration();
    if(claimGeneration == 0u)
        return false;
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        task.lifecycleState = TaskLifecycleState::Submitting;
        task.submissionClaimGeneration = claimGeneration;
    }
    outLease.m_packet = packet;
    outLease.m_planGeneration = planAccess.planGeneration();
    outLease.m_recordingAttemptGeneration = recordingAttemptGeneration;
    outLease.m_claimGeneration = claimGeneration;
    outLease.m_submissionBinding = submissionBinding;
    return true;
}


void GpuTaskGraph::beginPacketSubmissionAcceptance(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const QueueSubmissionToken& token,
    PacketSubmissionLease& lease
)const noexcept{
    const bool inputValid =
        !planAccess.validFor(compiledGraph)
        ? false
        : planAccess.validPacket(packet)
            && token.valid()
            && lease.valid()
            && lease.m_packet == packet
            && lease.m_planGeneration == planAccess.planGeneration()
    ;
    NWB_FATAL_ASSERT_MSG(inputValid, "native-accepted packet must retain its exact graph submission lease");
    if(!inputValid)
        TerminateInvariant();
    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    const bool packetValid = packetView.valid() && packetView.plan->taskCount != 0u;
    NWB_FATAL_ASSERT_MSG(packetValid, "native-accepted packet must retain its compiled task range");
    if(!packetValid)
        TerminateInvariant();
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    NothrowScopedLock lock(m_lifecycleMutex);
    const bool bindingValid = !m_teardownInProgress
        && m_activeCompiledGraph == &compiledGraph
        && m_activeRecordingPlanGeneration == planAccess.planGeneration()
        && m_activeRecordingAttemptGeneration == lease.m_recordingAttemptGeneration
        && m_activeSubmissionBinding == lease.m_submissionBinding
    ;
    NWB_FATAL_ASSERT_MSG(bindingValid, "native-accepted packet must retain its graph submission binding");
    if(!bindingValid)
        TerminateInvariant();

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskId taskID = tasks[taskIndex];
        if(!validTask(taskID))
            TerminateInvariant();
        const GpuTaskNode& task = m_tasks[taskID.index];
        const bool taskValid = task.lifecycleState == TaskLifecycleState::Submitting
            && task.lifecycleAttemptGeneration == lease.m_recordingAttemptGeneration
            && task.submissionClaimGeneration == lease.m_claimGeneration
        ;
        NWB_FATAL_ASSERT_MSG(taskValid, "native-accepted task must retain its exact submission claim");
        if(!taskValid)
            TerminateInvariant();
    }
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex)
        m_tasks[tasks[taskIndex].index].lifecycleState = TaskLifecycleState::Accepting;
}

void GpuTaskGraph::notifyPacketSubmissionAccepted(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const QueueSubmissionToken& token,
    const PacketSubmissionLease& lease
)const{
    const bool inputValid =
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || !token.valid()
        || !lease.valid()
        ? false
        : true
    ;
    NWB_FATAL_ASSERT_MSG(inputValid, "native-accepted callback publication must retain its exact graph submission lease");
    if(!inputValid)
        TerminateInvariant();

    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    NWB_FATAL_ASSERT_MSG(packetView.valid(), "native-accepted callback publication must retain its compiled task range");
    if(!packetView.valid())
        TerminateInvariant();
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskId taskID = tasks[taskIndex];
        if(!validTask(taskID))
            TerminateInvariant();
        const GpuTaskNode& task = m_tasks[taskID.index];
        if(task.payload && task.acceptPayload)
            task.acceptPayload(task.payload, token);
    }
}

void GpuTaskGraph::completePacketSubmissionAcceptance(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    PacketSubmissionLease& lease
)const noexcept{
    const bool inputValid = planAccess.validFor(compiledGraph) && planAccess.validPacket(packet) && lease.valid();
    NWB_FATAL_ASSERT_MSG(inputValid, "native-accepted completion must retain its exact graph submission lease");
    if(!inputValid)
        TerminateInvariant();

    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    NWB_FATAL_ASSERT_MSG(packetView.valid(), "native-accepted completion must retain its compiled task range");
    if(!packetView.valid())
        TerminateInvariant();
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    NothrowScopedLock lock(m_lifecycleMutex);
    const bool bindingValid = !m_teardownInProgress
        && m_activeCompiledGraph == &compiledGraph
        && m_activeRecordingPlanGeneration == planAccess.planGeneration()
        && m_activeRecordingAttemptGeneration == lease.m_recordingAttemptGeneration
        && m_activeSubmissionBinding == lease.m_submissionBinding
    ;
    NWB_FATAL_ASSERT_MSG(bindingValid, "native-accepted completion must retain its graph submission binding");
    if(!bindingValid)
        TerminateInvariant();
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskId taskID = tasks[taskIndex];
        if(!validTask(taskID))
            TerminateInvariant();
        const GpuTaskNode& task = m_tasks[taskID.index];
        const bool taskValid = task.lifecycleState == TaskLifecycleState::Accepting
            && task.lifecycleAttemptGeneration == lease.m_recordingAttemptGeneration
            && task.submissionClaimGeneration == lease.m_claimGeneration
        ;
        NWB_FATAL_ASSERT_MSG(taskValid, "native-accepted task must retain its exact accepting claim");
        if(!taskValid)
            TerminateInvariant();
    }
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        task.lifecycleState = TaskLifecycleState::Accepted;
        task.submissionClaimGeneration = 0u;
    }
    lease.reset();
}


void GpuTaskGraph::abortPacketSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    PacketSubmissionLease& lease
)const{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || !lease.valid()
        || lease.m_packet != packet
        || lease.m_planGeneration != planAccess.planGeneration()
    )
        return;
    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    DiscardNotificationScope notification(*this);
    const u64 notificationGeneration = allocateGeneration();
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeCompiledGraph != &compiledGraph
            || m_activeRecordingPlanGeneration != planAccess.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
            || m_activeSubmissionBinding != lease.m_submissionBinding
        )
            return;

        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != TaskLifecycleState::Submitting
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.submissionClaimGeneration != lease.m_claimGeneration
            )
                return;
        }
        notification.activateWithinLock();
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = TaskLifecycleState::Discarded;
            task.recordingClaimGeneration = 0u;
            task.submissionClaimGeneration = 0u;
            task.discardNotificationGeneration = notificationGeneration;
            task.recordThunkInProgress = false;
            task.recordThunkCompleted = false;
        }
        lease.reset();
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
}

bool GpuTaskGraph::abandonPacketSubmissionWithoutCallbacks(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    PacketSubmissionLease& lease
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
        || m_activeSubmissionBinding != lease.m_submissionBinding
    )
        return false;
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != TaskLifecycleState::Submitting
            || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
            || task.submissionClaimGeneration != lease.m_claimGeneration
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
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

