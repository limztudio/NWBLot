// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph::RecordThunkClaimGuard final : NoCopy{
public:
    RecordThunkClaimGuard(const GpuTaskGraph& graph, const GpuTaskId& task, const u64 recordingAttemptGeneration, const u64 claimGeneration)noexcept
        : m_graph(graph)
        , m_task(task)
        , m_recordingAttemptGeneration(recordingAttemptGeneration)
        , m_claimGeneration(claimGeneration)
    {}
    ~RecordThunkClaimGuard()noexcept{
        if(!m_active)
            return;

        u16 expectedWord = GpuTaskNode::MakeLifecycleWord(TaskLifecycleState::Recording, true, false);
        const u16 restoredWord = GpuTaskNode::MakeLifecycleWord(TaskLifecycleState::Recording, false, false);
        const b8 claimValid = m_graph.validTask(m_task)
            && m_graph.m_tasks[m_task.index].lifecycleAttemptGeneration == m_recordingAttemptGeneration
            && m_graph.m_tasks[m_task.index].recordingClaimGeneration == m_claimGeneration
            && m_graph.m_tasks[m_task.index].compareExchangeLifecycleWord(expectedWord, restoredWord, MemoryOrder::acq_rel, MemoryOrder::acquire)
        ;
        if(!claimValid)
            FOUNDATION_FATAL_ASSERT_MSG(false, "record thunk unwind must retain its exact graph recording claim");
    }


public:
    void complete()noexcept{
        if(!m_active)
            FOUNDATION_FATAL_ASSERT_MSG(false, "record thunk claim guard may complete exactly once");
        m_active = false;
    }


private:
    const GpuTaskGraph& m_graph;
    GpuTaskId m_task;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
    b8 m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph::RecordingAttemptResolutionGuard final : NoCopy{
public:
    RecordingAttemptResolutionGuard(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const u64 recordingAttemptGeneration)noexcept
        : m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_recordingAttemptGeneration(recordingAttemptGeneration)
    {}
    ~RecordingAttemptResolutionGuard()noexcept{
        complete();
    }


public:
    void complete()noexcept{
        if(!m_active)
            return;
        if(!m_graph.resolveRecordingAttemptIfTerminal(m_compiledGraph, m_recordingAttemptGeneration))
            FOUNDATION_FATAL_ASSERT_MSG(false, "terminal packet recording abort must resolve its exact graph attempt");
        m_active = false;
    }


private:
    const GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    u64 m_recordingAttemptGeneration = 0u;
    b8 m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::DiscardNotificationScope::DiscardNotificationScope(const GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{}

GpuTaskGraph::DiscardNotificationScope::~DiscardNotificationScope()noexcept{
    complete();
}

void GpuTaskGraph::DiscardNotificationScope::complete()noexcept{
    if(!m_active)
        return;

    NothrowScopedLock lock(m_graph.m_lifecycleMutex);
    FOUNDATION_FATAL_ASSERT_MSG(m_graph.m_activeDiscardNotificationCount != 0u, "GPU task discard notification ownership must remain balanced");
    --m_graph.m_activeDiscardNotificationCount;
    if(m_ownsPacketRecordingClaim){
        m_graph.releasePacketRecordingClaimWithinLock();
        m_ownsPacketRecordingClaim = false;
    }
    m_active = false;
}

void GpuTaskGraph::DiscardNotificationScope::activateWithinLock()noexcept{
    FOUNDATION_FATAL_ASSERT_MSG(!m_active, "GPU task discard notification ownership cannot be activated twice");
    FOUNDATION_FATAL_ASSERT_MSG(m_graph.m_activeDiscardNotificationCount != Limit<u32>::s_Max, "GPU task discard notification ownership overflowed");
    ++m_graph.m_activeDiscardNotificationCount;
    m_active = true;
}

void GpuTaskGraph::DiscardNotificationScope::adoptPacketRecordingClaimWithinLock()noexcept{
    const b8 adoptionValid = m_active && !m_ownsPacketRecordingClaim && m_graph.m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed) != 0u;
    FOUNDATION_FATAL_ASSERT_MSG(adoptionValid, "packet recording discard notification must adopt one exact live claim");
    m_ownsPacketRecordingClaim = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuCompiledPacketView> GpuTaskGraph::ResolvePacketView(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet)noexcept{
    if(!planAccess.validFor(compiledGraph))
        return MakeUnexpected(Failure{});
    const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
    if(!packetView.valid())
        return MakeUnexpected(Failure{});
    return packetView;
}

Expected<GpuCompiledPacketView> GpuTaskGraph::ResolveLeasedPacketView(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, const GpuPacketRecordingLease& lease)noexcept{
    const auto packetView = ResolvePacketView(compiledGraph, planAccess, packet);
    if(!packetView || !lease.valid() || lease.m_packet != packet || lease.m_planGeneration != planAccess.planGeneration())
        return MakeUnexpected(Failure{});
    return *packetView;
}

Expected<GpuCompiledPacketView> GpuTaskGraph::ResolveAttemptPacketView(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    const b8 leaseValid
)noexcept{
    const auto packetView = ResolvePacketView(compiledGraph, planAccess, packet);
    if(!packetView || recordingAttemptGeneration == 0u || leaseValid)
        return MakeUnexpected(Failure{});
    return *packetView;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, GpuTaskGraph::RecordTaskFailure> GpuTaskGraph::recordTask(const GpuTaskId& taskID, CommandList& commandList, const GpuTaskRecordContext& context, const GpuPacketRecordingLease& lease)const{
    const GpuCompiledTaskView compiledTaskView = context.compiledPlan.findTask(taskID);
    const GpuCompiledTask* const compiledTask = compiledTaskView.plan;
    const u64 planGeneration = context.compiledPlan.planGeneration();
    if(
        !context.declarations.validFor(*this)
        || !context.declarations.validTask(taskID)
        || !context.compiledPlan.validFor(context.declarations)
        || taskID != context.task
        || !lease.valid()
        || !context.compiledPlan.validPacket(context.packet)
        || !compiledTaskView.valid()
        || compiledTask->packet != context.packet
        || compiledTask->queue != context.queue
        || lease.m_packet != context.packet
        || lease.m_planGeneration != planGeneration
        || lease.m_recordingAttemptGeneration != context.recordingAttemptGeneration
        || isTeardownInProgress(MemoryOrder::acquire)
        || m_activeRecordingPlanGeneration.load(MemoryOrder::acquire) != planGeneration
        || m_activeRecordingAttemptGeneration.load(MemoryOrder::acquire) != context.recordingAttemptGeneration
    )
        return MakeUnexpected(RecordTaskFailure{ .recordThunkInvoked = false });

    const GpuTaskNode& task = m_tasks[taskID.index];
    u16 expectedWord = GpuTaskNode::MakeLifecycleWord(TaskLifecycleState::Recording, false, false);
    const u16 inProgressWord = GpuTaskNode::MakeLifecycleWord(TaskLifecycleState::Recording, true, false);
    if(!task.compareExchangeLifecycleWord(expectedWord, inProgressWord, MemoryOrder::acq_rel, MemoryOrder::acquire))
        return MakeUnexpected(RecordTaskFailure{ .recordThunkInvoked = false });

    if(
        isTeardownInProgress(MemoryOrder::acquire)
        || m_activeRecordingPlanGeneration.load(MemoryOrder::acquire) != planGeneration
        || m_activeRecordingAttemptGeneration.load(MemoryOrder::acquire) != context.recordingAttemptGeneration
        || task.lifecycleAttemptGeneration != context.recordingAttemptGeneration
        || task.recordingClaimGeneration != lease.m_claimGeneration
        || !context.compiledPlan.validFor(context.declarations)
    ){
        u16 rollbackWord = inProgressWord;
        if(!task.compareExchangeLifecycleWord(rollbackWord, expectedWord, MemoryOrder::acq_rel, MemoryOrder::acquire))
            TerminateInvariant();
        return MakeUnexpected(RecordTaskFailure{ .recordThunkInvoked = false });
    }

    const void* const payload = task.payload;
    const GpuTaskRecordThunk recordPayload = task.recordPayload;
    RecordThunkClaimGuard recordThunkClaim(*this, taskID, context.recordingAttemptGeneration, lease.m_claimGeneration);

    b8 recorded = false;
    b8 recordThunkInvoked = false;
    if(payload && recordPayload){
        recordThunkInvoked = true;
        recorded = recordPayload(payload, commandList, context);
    }

    const b8 recordingAttemptValid = !isTeardownInProgress(MemoryOrder::acquire)
        && context.compiledPlan.validFor(context.declarations)
        && m_activeRecordingPlanGeneration.load(MemoryOrder::acquire) == planGeneration
        && m_activeRecordingAttemptGeneration.load(MemoryOrder::acquire) == context.recordingAttemptGeneration
    ;
    u16 finishExpectedWord = inProgressWord;
    const u16 finishedWord = GpuTaskNode::MakeLifecycleWord(TaskLifecycleState::Recording, false, recorded && recordingAttemptValid);
    b8 claimValid = false;
    if(
        validTask(taskID)
        && task.lifecycleAttemptGeneration == context.recordingAttemptGeneration
        && task.recordingClaimGeneration == lease.m_claimGeneration
        && task.compareExchangeLifecycleWord(finishExpectedWord, finishedWord, MemoryOrder::acq_rel, MemoryOrder::acquire)
    )
        claimValid = recordingAttemptValid;

    if(claimValid)
        recordThunkClaim.complete();
    if(!recorded || !claimValid)
        return MakeUnexpected(RecordTaskFailure{ .recordThunkInvoked = recordThunkInvoked });
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraph::discardPacketTasksWithinLock(const Span<const GpuTaskId> tasks, const u64 notificationGeneration)const noexcept{
    for(const GpuTaskId taskId : tasks){
        const GpuTaskNode& task = m_tasks[taskId.index];
        task.recordingClaimGeneration = 0u;
        task.submissionClaimGeneration = 0u;
        task.discardNotificationGeneration = notificationGeneration;
        task.setLifecycle(TaskLifecycleState::Discarded);
    }
}

b8 GpuTaskGraph::beginPacketRecording(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    GpuPacketRecordingLease& outLease,
    GpuPacketRecordingAccess* const outAccess
)const noexcept{
    const auto packetView = ResolveAttemptPacketView(compiledGraph, planAccess, packet, recordingAttemptGeneration, outLease.valid());
    if(!packetView)
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView->plan;
    const GpuTaskId* const tasks = packetView->tasks;

    NothrowScopedLock lock(m_lifecycleMutex);
    if(!matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), recordingAttemptGeneration) || m_activeRecordingPreparationSerial != 0u || m_submissionBindingState == SubmissionBindingState::ExceptionClosing)
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.lifecycleState() != TaskLifecycleState::Declared || task.lifecycleAttemptGeneration != recordingAttemptGeneration)
            return false;
    }
    const u64 claimGeneration = AllocateGeneration();
    FOUNDATION_ASSERT(claimGeneration != 0u);
    const u32 activeClaimCount = m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed);
    if(activeClaimCount == Limit<u32>::s_Max)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GPU packet recording claim count overflowed");
    m_activePacketRecordingClaimCount.store(activeClaimCount + 1u, MemoryOrder::release);
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        task.recordingClaimGeneration = claimGeneration;
        task.submissionClaimGeneration = 0u;
        task.setLifecycle(TaskLifecycleState::Recording);
    }
    outLease.m_packet = packet;
    outLease.m_planGeneration = planAccess.planGeneration();
    outLease.m_recordingAttemptGeneration = recordingAttemptGeneration;
    outLease.m_claimGeneration = claimGeneration;
    if(outAccess)
        outAccess->bind(*this, compiledGraph, outLease);
    return true;
}

b8 GpuTaskGraph::beginPacketRecordingBatch(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const Span<const GpuSubmissionPacketId> packets,
    const u64 recordingAttemptGeneration,
    GpuPacketRecordingLease* const outLeases,
    GpuPacketRecordingAccess* const outAccesses
)const noexcept{
    if(!planAccess.validFor(compiledGraph) || packets.empty() || !outLeases || !outAccesses || recordingAttemptGeneration == 0u)
        return false;
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        if(outLeases[packetIndex].valid())
            return false;
    }

    NothrowScopedLock lock(m_lifecycleMutex);
    const u64 planGeneration = planAccess.planGeneration();
    if(!matchesPacketAttemptWithinLock(compiledGraph, planGeneration, recordingAttemptGeneration) || m_activeRecordingPreparationSerial != 0u || m_submissionBindingState == SubmissionBindingState::ExceptionClosing)
        return false;

    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packets[packetIndex]);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState() != TaskLifecycleState::Declared || task.lifecycleAttemptGeneration != recordingAttemptGeneration)
                return false;
        }
    }

    const u32 activeClaimCount = m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed);
    if(packets.size() > static_cast<usize>(Limit<u32>::s_Max) - activeClaimCount)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GPU packet recording claim count overflowed");
    m_activePacketRecordingClaimCount.store(activeClaimCount + static_cast<u32>(packets.size()), MemoryOrder::release);

    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packet = packets[packetIndex];
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;
        const u64 claimGeneration = AllocateGeneration();
        FOUNDATION_ASSERT(claimGeneration != 0u);
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.recordingClaimGeneration = claimGeneration;
            task.submissionClaimGeneration = 0u;
            task.setLifecycle(TaskLifecycleState::Recording);
        }
        GpuPacketRecordingLease& lease = outLeases[packetIndex];
        lease.m_packet = packet;
        lease.m_planGeneration = planGeneration;
        lease.m_recordingAttemptGeneration = recordingAttemptGeneration;
        lease.m_claimGeneration = claimGeneration;
        outAccesses[packetIndex].bind(*this, compiledGraph, lease);
    }
    return true;
}

b8 GpuTaskGraph::completePacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const noexcept{
    GpuPacketRecordingLease* leasePtr = &lease;
    return completePacketRecordingBatch(compiledGraph, planAccess, Span<const GpuSubmissionPacketId>(&packet, 1u), &leasePtr);
}

b8 GpuTaskGraph::completePacketRecordingBatch(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const Span<const GpuSubmissionPacketId> packets,
    GpuPacketRecordingLease* const* const leases
)const noexcept{
    if(!planAccess.validFor(compiledGraph) || packets.empty() || !leases)
        return false;

    const u64 planGeneration = planAccess.planGeneration();
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const GpuPacketRecordingLease* const leasePtr = leases[packetIndex];
        if(!leasePtr || !leasePtr->valid() || leasePtr->m_packet != packets[packetIndex] || leasePtr->m_planGeneration != planGeneration)
            return false;
    }

    NothrowScopedLock lock(m_lifecycleMutex);
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packet = packets[packetIndex];
        const GpuPacketRecordingLease& lease = *leases[packetIndex];
        if(!matchesPacketAttemptWithinLock(compiledGraph, planGeneration, lease.m_recordingAttemptGeneration))
            return false;
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            const u16 lifecycleWord = task.loadLifecycleWord();
            if(
                GpuTaskNode::DecodeLifecycleState(lifecycleWord) != TaskLifecycleState::Recording
                || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration
                || task.recordingClaimGeneration != lease.m_claimGeneration
                || GpuTaskNode::DecodeRecordThunkInProgress(lifecycleWord)
                || (task.recordPayload && !GpuTaskNode::DecodeRecordThunkCompleted(lifecycleWord))
            )
                return false;
        }
    }

    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packet = packets[packetIndex];
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.recordingClaimGeneration = 0u;
            task.setLifecycle(TaskLifecycleState::Recorded);
        }
        leases[packetIndex]->reset();
    }
    releasePacketRecordingClaimWithinLock(static_cast<u32>(packets.size()));
    return true;
}

void GpuTaskGraph::rollbackUnstartedPacketRecordingBatch(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const Span<const GpuSubmissionPacketId> packets,
    GpuPacketRecordingLease* const* const leases
)const noexcept{
    if(!planAccess.validFor(compiledGraph) || packets.empty() || !leases)
        return;

    const u64 planGeneration = planAccess.planGeneration();
    NothrowScopedLock lock(m_lifecycleMutex);
    u32 releasedCount = 0u;
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        GpuPacketRecordingLease* const leasePtr = leases[packetIndex];
        if(!leasePtr || !leasePtr->valid())
            continue;
        const GpuSubmissionPacketId packet = packets[packetIndex];
        const b8 leaseMatches = leasePtr->m_packet == packet
            && leasePtr->m_planGeneration == planGeneration
            && matchesPacketAttemptWithinLock(compiledGraph, planGeneration, leasePtr->m_recordingAttemptGeneration)
        ;
        FOUNDATION_FATAL_ASSERT_MSG(leaseMatches, "unstarted packet recording rollback must retain its exact lease");
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        FOUNDATION_FATAL_ASSERT_MSG(packetView.valid(), "unstarted packet recording rollback must retain its compiled packet");
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                TerminateInvariant();
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            const u16 lifecycleWord = task.loadLifecycleWord();
            const b8 taskUnstarted = GpuTaskNode::DecodeLifecycleState(lifecycleWord) == TaskLifecycleState::Recording
                && task.lifecycleAttemptGeneration == leasePtr->m_recordingAttemptGeneration
                && task.recordingClaimGeneration == leasePtr->m_claimGeneration
                && !GpuTaskNode::DecodeRecordThunkInProgress(lifecycleWord)
                && !GpuTaskNode::DecodeRecordThunkCompleted(lifecycleWord)
            ;
            FOUNDATION_FATAL_ASSERT_MSG(taskUnstarted, "unstarted packet recording rollback requires unexecuted recording tasks");
            task.recordingClaimGeneration = 0u;
            task.setLifecycle(TaskLifecycleState::Declared);
        }
        leasePtr->reset();
        ++releasedCount;
    }
    if(releasedCount != 0u)
        releasePacketRecordingClaimWithinLock(releasedCount);
}

b8 GpuTaskGraph::deferPacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease, GpuPacketRecordingAbort& outAbort)const noexcept{
    if(!planAccess.validFor(compiledGraph) || !planAccess.validPacket(packet) || !lease.valid() || outAbort.valid() || lease.m_packet != packet || lease.m_planGeneration != planAccess.planGeneration())
        return false;
    outAbort.m_packet = lease.m_packet;
    outAbort.m_planGeneration = lease.m_planGeneration;
    outAbort.m_recordingAttemptGeneration = lease.m_recordingAttemptGeneration;
    outAbort.m_claimGeneration = lease.m_claimGeneration;
    lease.reset();
    return true;
}


b8 GpuTaskGraph::completePacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuPacketRecordingAbort& abort)const{
    if(!planAccess.validFor(compiledGraph) || !abort.valid() || abort.m_planGeneration != planAccess.planGeneration())
        return false;
    const GpuCompiledPacketView packetView = planAccess.packetWithTasks(abort.m_packet);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    DiscardNotificationScope notification(*this);
    const u64 notificationGeneration = AllocateGeneration();
    {
        ScopedLock lock(m_lifecycleMutex);
        if(!matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), abort.m_recordingAttemptGeneration))
            return false;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            const u16 lifecycleWord = task.loadLifecycleWord();
            if(GpuTaskNode::DecodeLifecycleState(lifecycleWord) != TaskLifecycleState::Recording || task.lifecycleAttemptGeneration != abort.m_recordingAttemptGeneration || task.recordingClaimGeneration != abort.m_claimGeneration || GpuTaskNode::DecodeRecordThunkInProgress(lifecycleWord))
                return false;
        }
        notification.activateWithinLock();
        notification.adoptPacketRecordingClaimWithinLock();
        discardPacketTasksWithinLock(Span<const GpuTaskId>(tasks, packetPlan.taskCount), notificationGeneration);
        abort.reset();
    }

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.discardNotificationGeneration == notificationGeneration && task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }
    return true;
}


void GpuTaskGraph::abortPacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const{
    const u64 recordingAttemptGeneration = lease.m_recordingAttemptGeneration;
    GpuPacketRecordingAbort abort;
    if(!deferPacketRecordingAbort(compiledGraph, planAccess, packet, lease, abort))
        FOUNDATION_FATAL_ASSERT_MSG(false, "active packet recording abort must retain its exact lease");
    RecordingAttemptResolutionGuard resolution(*this, compiledGraph, recordingAttemptGeneration);
    if(!completePacketRecordingAbort(compiledGraph, planAccess, abort))
        FOUNDATION_FATAL_ASSERT_MSG(false, "active packet recording abort must complete before attempt resolution");
    resolution.complete();
}


void GpuTaskGraph::releasePacketRecordingClaimWithinLock(const u32 releasedCount)const noexcept{
    const u32 activeClaimCount = m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed);
    FOUNDATION_FATAL_ASSERT_MSG(releasedCount != 0u && activeClaimCount >= releasedCount, "GPU packet recording claim ownership must remain balanced");
    const u32 remaining = activeClaimCount - releasedCount;
    m_activePacketRecordingClaimCount.store(remaining, MemoryOrder::release);
    if(remaining == 0u && m_exceptionRecordingClaimWaiters != 0u)
        m_activePacketRecordingClaimCount.notify_all();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

