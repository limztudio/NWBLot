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
    const GpuTaskPacketRecordingLease& lease
)const{
    if(!validTask(taskID) || !lease.valid())
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
        )
            return false;
        task.recordThunkInProgress = true;
        payload = task.payload;
        recordPayload = task.recordPayload;
    }

    const bool recorded = payload && recordPayload && recordPayload(payload, commandList, context);

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
    const GpuTaskId* const tasks,
    const usize taskCount,
    const u64 recordingAttemptGeneration,
    GpuTaskPacketRecordingLease& outLease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || recordingAttemptGeneration == 0u
        || outLease.valid()
    )
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Recording;
        m_tasks[tasks[taskIndex].index].recordingClaimGeneration = claimGeneration;
        m_tasks[tasks[taskIndex].index].submissionClaimGeneration = 0u;
        m_tasks[tasks[taskIndex].index].recordThunkInProgress = false;
        m_tasks[tasks[taskIndex].index].recordThunkCompleted = false;
    }
    outLease.m_planGeneration = compiledGraph.planGeneration();
    outLease.m_recordingAttemptGeneration = recordingAttemptGeneration;
    outLease.m_claimGeneration = claimGeneration;
    return true;
}

bool GpuTaskGraph::completePacketRecording(
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId* const tasks,
    const usize taskCount,
    GpuTaskPacketRecordingLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || !lease.valid()
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
    const GpuTaskId* const tasks,
    const usize taskCount,
    GpuTaskPacketRecordingLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || !lease.valid()
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return;

        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
            m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Discarding;
    }

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
    const GpuTaskId* const tasks,
    const usize taskCount,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || recordingAttemptGeneration == 0u
    )
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
    const GpuTaskId* const tasks,
    const usize taskCount,
    const u64 recordingAttemptGeneration,
    GpuTaskPacketSubmissionLease& outLease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || recordingAttemptGeneration == 0u
        || outLease.valid()
    )
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        task.lifecycleState = GpuTaskLifecycleState::Submitting;
        task.submissionClaimGeneration = claimGeneration;
    }
    outLease.m_planGeneration = compiledGraph.planGeneration();
    outLease.m_recordingAttemptGeneration = recordingAttemptGeneration;
    outLease.m_claimGeneration = claimGeneration;
    return true;
}


bool GpuTaskGraph::completePacketSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId* const tasks,
    const usize taskCount,
    const QueueSubmissionToken& token,
    GpuTaskPacketSubmissionLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || !token.valid()
        || !lease.valid()
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return false;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return false;

        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = GpuTaskLifecycleState::Accepting;
        }
    }

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
    const GpuTaskId* const tasks,
    const usize taskCount,
    GpuTaskPacketSubmissionLease& lease
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || !lease.valid()
        || lease.m_planGeneration != compiledGraph.planGeneration()
    )
        return;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != lease.m_recordingAttemptGeneration
        )
            return;

        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
            m_tasks[tasks[taskIndex].index].lifecycleState = GpuTaskLifecycleState::Discarding;
    }

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = GpuTaskLifecycleState::Discarded;
            task.submissionClaimGeneration = 0u;
        }
    }
    lease.reset();
}


bool GpuTaskGraph::discardUnacceptedPacket(
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId* const tasks,
    const usize taskCount,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(
        !compiledGraph.validFor(*this)
        || !tasks
        || taskCount == 0u
        || recordingAttemptGeneration == 0u
    )
        return false;

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeRecordingPlanGeneration != compiledGraph.planGeneration()
            || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
        )
            return false;

        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState != GpuTaskLifecycleState::Discarded)
                task.lifecycleState = GpuTaskLifecycleState::Discarding;
        }
    }

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
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
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState == GpuTaskLifecycleState::Discarding){
                task.lifecycleState = GpuTaskLifecycleState::Discarded;
                task.recordingClaimGeneration = 0u;
            }
        }
    }
    return true;
}


void GpuTaskGraph::acceptTask(const GpuTaskId& taskID, const QueueSubmissionToken& token)noexcept{
    // Native graph work must carry its attempt explicitly. This compatibility entry point is only for declaration
    // lifecycle probes before compilation, so a delayed caller cannot accept a later retry by observing its state.
    u64 recordingAttemptGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(m_teardownInProgress || m_activeRecordingPlanGeneration != 0u)
            return;
        recordingAttemptGeneration = m_activeRecordingAttemptGeneration;
    }
    acceptTask(taskID, token, recordingAttemptGeneration);
}

void GpuTaskGraph::acceptTask(
    const GpuTaskId& taskID,
    const QueueSubmissionToken& token,
    const u64 recordingAttemptGeneration
)noexcept{
    void* payload = nullptr;
    GpuTaskAcceptedThunk acceptPayload = nullptr;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || !validTask(taskID)
            || !token.valid()
            || recordingAttemptGeneration == 0u
            || recordingAttemptGeneration != m_activeRecordingAttemptGeneration
        )
            return;

        const GpuTaskNode& task = m_tasks[taskID.index];
        const GpuTaskLifecycleState::Enum requiredLifecycleState = m_activeRecordingPlanGeneration == 0u
            ? GpuTaskLifecycleState::Declared
            : GpuTaskLifecycleState::Recorded
        ;
        if(task.lifecycleState != requiredLifecycleState || task.lifecycleAttemptGeneration != recordingAttemptGeneration)
            return;
        task.lifecycleState = GpuTaskLifecycleState::Accepting;
        payload = task.payload;
        acceptPayload = task.acceptPayload;
    }
    if(payload && acceptPayload)
        acceptPayload(payload, token);

    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            validTask(taskID)
            && recordingAttemptGeneration == m_activeRecordingAttemptGeneration
        ){
            const GpuTaskNode& task = m_tasks[taskID.index];
            if(
                task.lifecycleState == GpuTaskLifecycleState::Accepting
                && task.lifecycleAttemptGeneration == recordingAttemptGeneration
            )
                task.lifecycleState = GpuTaskLifecycleState::Accepted;
        }
    }
}

void GpuTaskGraph::discardTask(const GpuTaskId& taskID)const noexcept{
    // See acceptTask(): compiled graph work must retain the originating recording-attempt generation.
    u64 recordingAttemptGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(m_teardownInProgress || m_activeRecordingPlanGeneration != 0u)
            return;
        recordingAttemptGeneration = m_activeRecordingAttemptGeneration;
    }
    discardTask(taskID, recordingAttemptGeneration);
}

void GpuTaskGraph::discardTask(
    const GpuTaskId& taskID,
    const u64 recordingAttemptGeneration
)const noexcept{
    void* payload = nullptr;
    GpuTaskDiscardedThunk discardPayload = nullptr;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || !validTask(taskID)
            || recordingAttemptGeneration == 0u
            || recordingAttemptGeneration != m_activeRecordingAttemptGeneration
        )
            return;

        const GpuTaskNode& task = m_tasks[taskID.index];
        if(
            (
                task.lifecycleState != GpuTaskLifecycleState::Declared
                && task.lifecycleState != GpuTaskLifecycleState::Recorded
            )
            || task.lifecycleAttemptGeneration != recordingAttemptGeneration
        )
            return;
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

