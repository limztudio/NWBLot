// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::recordTask(
    const GpuTaskId& taskID,
    CommandList& commandList,
    const GpuTaskRecordContext& context,
    const PacketRecordingLease& lease,
    bool& outRecordThunkInvoked
)const{
    outRecordThunkInvoked = false;
    const GpuCompiledTask* const compiledTask = context.graph.findTask(taskID);
    if(
        !validTask(taskID)
        || taskID != context.task
        || !lease.valid()
        || !context.graph.validPacket(context.packet)
        || !compiledTask
        || compiledTask->packet != context.packet
        || compiledTask->queue != context.queue
        || lease.m_packet != context.packet
    )
        return false;

    const void* payload = nullptr;
    GpuTaskRecordThunk recordPayload = nullptr;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(m_teardownInProgress)
            return false;
        const GpuTaskNode& task = m_tasks[taskID.index];
        if(
            task.lifecycleState != GpuTaskLifecycleState::Recording
            || task.lifecycleAttemptGeneration != context.recordingAttemptGeneration
            || context.recordingAttemptGeneration == 0u
            || task.recordingClaimGeneration != lease.m_claimGeneration
            || task.recordThunkInProgress
            || task.recordThunkCompleted
            || !context.graph.validFor(*this)
            || m_activeRecordingPlanGeneration != context.graph.planGeneration()
            || m_activeRecordingAttemptGeneration != context.recordingAttemptGeneration
            || lease.m_planGeneration != context.graph.planGeneration()
            || lease.m_recordingAttemptGeneration != context.recordingAttemptGeneration
            || lease.m_packet != context.packet
        )
            return false;
        task.recordThunkInProgress = true;
        payload = task.payload;
        recordPayload = task.recordPayload;
    }

    bool recorded = false;
    if(payload && recordPayload){
        outRecordThunkInvoked = true;
        recorded = recordPayload(payload, commandList, context);
    }

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || !validTask(taskID)
            || m_activeRecordingPlanGeneration != context.graph.planGeneration()
            || m_activeRecordingAttemptGeneration != context.recordingAttemptGeneration
        )
            return false;

        const GpuTaskNode& task = m_tasks[taskID.index];
        if(
            task.lifecycleState != GpuTaskLifecycleState::Recording
            || task.lifecycleAttemptGeneration != context.recordingAttemptGeneration
            || task.recordingClaimGeneration != lease.m_claimGeneration
            || !task.recordThunkInProgress
        )
            return false;
        task.recordThunkInProgress = false;
        task.recordThunkCompleted = recorded;
    }
    return recorded;
}

bool GpuTaskGraph::beginPacketRecording(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    PacketRecordingLease& outLease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || recordingAttemptGeneration == 0u
        || outLease.valid()
    )
        return false;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != GpuTaskLifecycleState::Declared
            || task.lifecycleAttemptGeneration != recordingAttemptGeneration
        )
            return false;
    }
    const u64 claimGeneration = allocateGeneration();
    if(claimGeneration == 0u)
        return false;
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Recording;
        m_tasks[tasks[taskIndex].index].recordingClaimGeneration = claimGeneration;
        m_tasks[tasks[taskIndex].index].submissionClaimGeneration = 0u;
        m_tasks[tasks[taskIndex].index].recordThunkInProgress = false;
        m_tasks[tasks[taskIndex].index].recordThunkCompleted = false;
    }
    outLease.m_packet = packet;
    outLease.m_planGeneration = compiledGraph.planGeneration();
    outLease.m_recordingAttemptGeneration = recordingAttemptGeneration;
    outLease.m_claimGeneration = claimGeneration;
    return true;
}

bool GpuTaskGraph::completePacketRecording(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    PacketRecordingLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || !lease.valid()
        || lease.m_packet != packet
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return false;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != GpuTaskLifecycleState::Recording
            || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
            || task.recordingClaimGeneration != lease.m_claimGeneration
            || task.recordThunkInProgress
            || (task.recordPayload && !task.recordThunkCompleted)
        )
            return false;
    }
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Recorded;
        m_tasks[tasks[taskIndex].index].recordingClaimGeneration = 0u;
        m_tasks[tasks[taskIndex].index].recordThunkInProgress = false;
        m_tasks[tasks[taskIndex].index].recordThunkCompleted = false;
    }
    lease.reset();
    return true;
}


void GpuTaskGraph::abortPacketRecording(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    PacketRecordingLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || !lease.valid()
        || lease.m_packet != packet
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return;

        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != GpuTaskLifecycleState::Recording
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.recordingClaimGeneration != lease.m_claimGeneration
                || task.recordThunkInProgress
            )
                return;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex)
            m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Discarding;
    }

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != GpuTaskLifecycleState::Discarding
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.recordingClaimGeneration != lease.m_claimGeneration
            )
                return;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Discarded;
            m_tasks[tasks[taskIndex].index].recordingClaimGeneration = 0u;
            m_tasks[tasks[taskIndex].index].recordThunkInProgress = false;
            m_tasks[tasks[taskIndex].index].recordThunkCompleted = false;
        }
    }
    lease.reset();
}


bool GpuTaskGraph::packetReadyForSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || recordingAttemptGeneration == 0u
    )
        return false;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != GpuTaskLifecycleState::Recorded
            || task.lifecycleAttemptGeneration != recordingAttemptGeneration
        )
            return false;
    }
    return true;
}


bool GpuTaskGraph::beginPacketSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    GpuTaskPacketSubmissionLease& outLease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || recordingAttemptGeneration == 0u
        || outLease.valid()
    )
        return false;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != GpuTaskLifecycleState::Recorded
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
        task.lifecycleState = GpuTaskLifecycleState::Submitting;
        task.submissionClaimGeneration = claimGeneration;
    }
    outLease.m_packet = packet;
    outLease.m_planGeneration = compiledGraph.planGeneration();
    outLease.m_recordingAttemptGeneration = recordingAttemptGeneration;
    outLease.m_claimGeneration = claimGeneration;
    return true;
}


bool GpuTaskGraph::completePacketSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const QueueSubmissionToken& token,
    GpuTaskPacketSubmissionLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || !token.valid()
        || !lease.valid()
        || lease.m_packet != packet
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return false;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return false;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return false;

        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != GpuTaskLifecycleState::Submitting
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.submissionClaimGeneration != lease.m_claimGeneration
            )
                return false;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = GpuTaskLifecycleState::Accepting;
        }
    }

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.payload && task.acceptPayload)
            task.acceptPayload(task.payload, token);
    }

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return false;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != GpuTaskLifecycleState::Accepting
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.submissionClaimGeneration != lease.m_claimGeneration
            )
                return false;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = GpuTaskLifecycleState::Accepted;
            task.submissionClaimGeneration = 0u;
        }
    }
    lease.reset();
    return true;
}


void GpuTaskGraph::abortPacketSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    GpuTaskPacketSubmissionLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || !lease.valid()
        || lease.m_packet != packet
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return;

        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != GpuTaskLifecycleState::Submitting
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.submissionClaimGeneration != lease.m_claimGeneration
            )
                return;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex)
            m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Discarding;
    }

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != GpuTaskLifecycleState::Discarding
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.submissionClaimGeneration != lease.m_claimGeneration
            )
                return;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = GpuTaskLifecycleState::Discarded;
            task.submissionClaimGeneration = 0u;
        }
    }
    lease.reset();
}


bool GpuTaskGraph::discardUnacceptedPacket(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !compiledGraph.validPacket(packet)
        || recordingAttemptGeneration == 0u
    )
        return false;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return false;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
        )
            return false;

        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                (
                    task.lifecycleState != GpuTaskLifecycleState::Declared
                    && task.lifecycleState != GpuTaskLifecycleState::Recorded
                    && task.lifecycleState != GpuTaskLifecycleState::Discarded
                )
                || task.lifecycleAttemptGeneration != recordingAttemptGeneration
            )
                return false;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState != GpuTaskLifecycleState::Discarded)
                task.lifecycleState = GpuTaskLifecycleState::Discarding;
        }
    }

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.lifecycleState != GpuTaskLifecycleState::Discarding)
            continue;
        if(task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
        )
            return false;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != GpuTaskLifecycleState::Discarding
                && task.lifecycleState != GpuTaskLifecycleState::Discarded
            )
                return false;
            if(task.lifecycleAttemptGeneration != recordingAttemptGeneration)
                return false;
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState == GpuTaskLifecycleState::Discarding){
                task.lifecycleState = GpuTaskLifecycleState::Discarded;
                task.recordingClaimGeneration = 0u;
            }
        }
    }
    return true;
}


void GpuTaskGraph::discardTask(const GpuTaskId& taskID)const noexcept{
    void* payload = nullptr;
    GpuTaskDiscardedThunk discardPayload = nullptr;
    u64 recordingAttemptGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != 0u
            || !validTask(taskID)
        )
            return;

        const GpuTaskNode& task = m_tasks[taskID.index];
        if(
            task.lifecycleState != GpuTaskLifecycleState::Declared
            || task.lifecycleAttemptGeneration != m_activeRecordingAttemptGeneration
        )
            return;
        recordingAttemptGeneration = m_activeRecordingAttemptGeneration;
        task.lifecycleState = GpuTaskLifecycleState::Discarding;
        payload = task.payload;
        discardPayload = task.discardPayload;
    }
    if(payload && discardPayload)
        discardPayload(payload);

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            validTask(taskID)
            && m_activeRecordingPlanGeneration == 0u
            && recordingAttemptGeneration == m_activeRecordingAttemptGeneration
        ){
            const GpuTaskNode& task = m_tasks[taskID.index];
            if(
                task.lifecycleState == GpuTaskLifecycleState::Discarding
                && task.lifecycleAttemptGeneration == recordingAttemptGeneration
            ){
                task.lifecycleState = GpuTaskLifecycleState::Discarded;
                task.recordingClaimGeneration = 0u;
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

