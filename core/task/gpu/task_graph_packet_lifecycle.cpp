// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::DiscardNotificationScope::DiscardNotificationScope(const GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{}

GpuTaskGraph::DiscardNotificationScope::~DiscardNotificationScope(){
    complete();
}

void GpuTaskGraph::DiscardNotificationScope::complete()noexcept{
    if(!m_active)
        return;

    NothrowScopedLock lock(m_graph.m_lifecycleMutex);
    NWB_FATAL_ASSERT_MSG(
        m_graph.m_activeDiscardNotificationCount != 0u,
        "GPU task discard notification ownership must remain balanced"
    );
    if(m_graph.m_activeDiscardNotificationCount == 0u)
        TerminateInvariant();
    --m_graph.m_activeDiscardNotificationCount;
    if(m_ownsPacketRecordingClaim){
        m_graph.releasePacketRecordingClaimWithinLock();
        m_ownsPacketRecordingClaim = false;
    }
    m_active = false;
}

void GpuTaskGraph::DiscardNotificationScope::activateWithinLock()noexcept{
    NWB_FATAL_ASSERT_MSG(!m_active, "GPU task discard notification ownership cannot be activated twice");
    NWB_FATAL_ASSERT_MSG(
        m_graph.m_activeDiscardNotificationCount != Limit<u32>::s_Max,
        "GPU task discard notification ownership overflowed"
    );
    if(m_active || m_graph.m_activeDiscardNotificationCount == Limit<u32>::s_Max)
        TerminateInvariant();
    ++m_graph.m_activeDiscardNotificationCount;
    m_active = true;
}

void GpuTaskGraph::DiscardNotificationScope::adoptPacketRecordingClaimWithinLock()noexcept{
    const bool adoptionValid = m_active
        && !m_ownsPacketRecordingClaim
        && m_graph.m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed) != 0u
    ;
    NWB_FATAL_ASSERT_MSG(adoptionValid, "packet recording discard notification must adopt one exact live claim");
    if(!adoptionValid)
        TerminateInvariant();
    m_ownsPacketRecordingClaim = true;
}


void GpuTaskGraph::releasePacketRecordingClaimWithinLock()const noexcept{
    const u32 activeClaimCount = m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed);
    NWB_FATAL_ASSERT_MSG(activeClaimCount != 0u, "GPU packet recording claim ownership must remain balanced");
    if(activeClaimCount == 0u)
        TerminateInvariant();
    m_activePacketRecordingClaimCount.store(activeClaimCount - 1u, MemoryOrder::release);
    if(activeClaimCount == 1u)
        m_activePacketRecordingClaimCount.notify_all();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::PacketRecordingAbort::PacketRecordingAbort(PacketRecordingAbort&& other)noexcept
    : m_packet(other.m_packet)
    , m_planGeneration(other.m_planGeneration)
    , m_recordingAttemptGeneration(other.m_recordingAttemptGeneration)
    , m_claimGeneration(other.m_claimGeneration)
{
    other.reset();
}

GpuTaskGraph::PacketRecordingAbort::~PacketRecordingAbort(){
    if(valid()){
        NWB_FATAL_ASSERT_MSG(false, "GPU task packet recording abort must be consumed before destruction");
        TerminateInvariant();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph::RecordThunkClaimGuard final : NoCopy{
public:
    RecordThunkClaimGuard(
        const GpuTaskGraph& graph,
        const GpuTaskId& task,
        const u64 recordingAttemptGeneration,
        const u64 claimGeneration
    )noexcept
        : m_graph(graph)
        , m_task(task)
        , m_recordingAttemptGeneration(recordingAttemptGeneration)
        , m_claimGeneration(claimGeneration)
    {}
    ~RecordThunkClaimGuard()noexcept{
        if(!m_active)
            return;

        NothrowScopedLock lock(m_graph.m_lifecycleMutex);
        const bool claimValid = m_graph.validTask(m_task)
            && m_graph.m_tasks[m_task.index].lifecycleState == TaskLifecycleState::Recording
            && m_graph.m_tasks[m_task.index].lifecycleAttemptGeneration == m_recordingAttemptGeneration
            && m_graph.m_tasks[m_task.index].recordingClaimGeneration == m_claimGeneration
            && m_graph.m_tasks[m_task.index].recordThunkInProgress
        ;
        if(!claimValid){
            NWB_FATAL_ASSERT_MSG(false, "record thunk unwind must retain its exact graph recording claim");
            TerminateInvariant();
        }
        m_graph.m_tasks[m_task.index].recordThunkInProgress = false;
    }


public:
    void complete()noexcept{
        if(!m_active){
            NWB_FATAL_ASSERT_MSG(false, "record thunk claim guard may complete exactly once");
            TerminateInvariant();
        }
        m_active = false;
    }


private:
    const GpuTaskGraph& m_graph;
    GpuTaskId m_task;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
    bool m_active = true;
};


class GpuTaskGraph::RecordingAttemptResolutionGuard final : NoCopy{
public:
    RecordingAttemptResolutionGuard(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const u64 recordingAttemptGeneration
    )noexcept
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
        if(!m_graph.resolveRecordingAttemptIfTerminal(m_compiledGraph, m_recordingAttemptGeneration)){
            NWB_FATAL_ASSERT_MSG(false, "terminal packet recording abort must resolve its exact graph attempt");
            TerminateInvariant();
        }
        m_active = false;
    }


private:
    const GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    u64 m_recordingAttemptGeneration = 0u;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::PacketRecordingLease::validFor(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet
)const noexcept{
    if(
        !valid()
        || !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || m_packet != packet
        || m_planGeneration != planAccess.planGeneration()
    )
        return false;

    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    NothrowScopedLock lock(graph.m_lifecycleMutex);
    if(
        graph.m_teardownInProgress
        || graph.m_activeCompiledGraph != &compiledGraph
        || graph.m_activeRecordingPlanGeneration != m_planGeneration
        || graph.m_activeRecordingAttemptGeneration != m_recordingAttemptGeneration
        || graph.m_activeRecordingPreparationSerial != 0u
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!graph.validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = graph.m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != TaskLifecycleState::Recording
            || task.lifecycleAttemptGeneration != m_recordingAttemptGeneration
            || task.recordingClaimGeneration != m_claimGeneration
        )
            return false;
    }
    return true;
}


GpuTaskGraph::PacketRecordingAccess::PacketRecordingAccess(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const PacketRecordingLease& lease
)noexcept{
    if(!lease.validFor(graph, compiledGraph, planAccess, lease.m_packet))
        return;

    m_graph = &graph;
    m_compiledGraph = &compiledGraph;
    m_lease = &lease;
    m_packet = lease.m_packet;
    m_planGeneration = lease.m_planGeneration;
    m_recordingAttemptGeneration = lease.m_recordingAttemptGeneration;
    m_claimGeneration = lease.m_claimGeneration;
}

bool GpuTaskGraph::PacketRecordingAccess::validFor(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet
)const noexcept{
    return m_graph == &graph
        && m_compiledGraph == &compiledGraph
        && m_lease
        && m_lease->m_packet == m_packet
        && m_lease->m_planGeneration == m_planGeneration
        && m_lease->m_recordingAttemptGeneration == m_recordingAttemptGeneration
        && m_lease->m_claimGeneration == m_claimGeneration
        && planAccess.validFor(compiledGraph)
        && m_packet == packet
        && m_planGeneration == planAccess.planGeneration()
        && m_recordingAttemptGeneration != 0u
        && m_claimGeneration != 0u
    ;
}

bool GpuTaskGraph::PacketRecordingAccess::validForTask(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskId& task
)const noexcept{
    if(!validFor(graph, compiledGraph, planAccess, m_packet))
        return false;
    const GpuCompiledTaskView compiledTask = planAccess.findTask(task);
    return compiledTask.valid() && compiledTask.plan->packet == m_packet;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::recordTask(
    const GpuTaskId& taskID,
    CommandList& commandList,
    const GpuTaskRecordContext& context,
    const PacketRecordingLease& lease,
    bool& outRecordThunkInvoked
)const{
    outRecordThunkInvoked = false;
    const GpuCompiledTaskView compiledTaskView = context.compiledPlan.findTask(taskID);
    const GpuCompiledTask* const compiledTask = compiledTaskView.plan;
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
            task.lifecycleState != TaskLifecycleState::Recording
            || task.lifecycleAttemptGeneration != context.recordingAttemptGeneration
            || context.recordingAttemptGeneration == 0u
            || task.recordingClaimGeneration != lease.m_claimGeneration
            || task.recordThunkInProgress
            || task.recordThunkCompleted
            || !context.compiledPlan.validFor(context.declarations)
            || m_activeRecordingPlanGeneration != context.compiledPlan.planGeneration()
            || m_activeRecordingAttemptGeneration != context.recordingAttemptGeneration
            || lease.m_planGeneration != context.compiledPlan.planGeneration()
            || lease.m_recordingAttemptGeneration != context.recordingAttemptGeneration
            || lease.m_packet != context.packet
        )
            return false;
        task.recordThunkInProgress = true;
        payload = task.payload;
        recordPayload = task.recordPayload;
    }
    RecordThunkClaimGuard recordThunkClaim(*this, taskID, context.recordingAttemptGeneration, lease.m_claimGeneration);

    bool recorded = false;
    if(payload && recordPayload){
        outRecordThunkInvoked = true;
        recorded = recordPayload(payload, commandList, context);
    }

    bool claimValid = false;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(validTask(taskID)){
            const GpuTaskNode& task = m_tasks[taskID.index];
            claimValid = task.lifecycleState == TaskLifecycleState::Recording
                && task.lifecycleAttemptGeneration == context.recordingAttemptGeneration
                && task.recordingClaimGeneration == lease.m_claimGeneration
                && task.recordThunkInProgress
            ;
            if(claimValid){
                const bool recordingAttemptValid = !m_teardownInProgress
                    && context.compiledPlan.validFor(context.declarations)
                    && m_activeRecordingPlanGeneration == context.compiledPlan.planGeneration()
                    && m_activeRecordingAttemptGeneration == context.recordingAttemptGeneration
                ;
                task.recordThunkInProgress = false;
                task.recordThunkCompleted = recorded && recordingAttemptValid;
                claimValid = recordingAttemptValid;
            }
        }
    }
    if(claimValid)
        recordThunkClaim.complete();
    return recorded && claimValid;
}

bool GpuTaskGraph::beginPacketRecording(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    PacketRecordingLease& outLease
)const noexcept{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || recordingAttemptGeneration == 0u
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
        || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(
            task.lifecycleState != TaskLifecycleState::Declared
            || task.lifecycleAttemptGeneration != recordingAttemptGeneration
        )
            return false;
    }
    const u64 claimGeneration = allocateGeneration();
    if(claimGeneration == 0u)
        return false;
    const u32 activeClaimCount = m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed);
    if(activeClaimCount == Limit<u32>::s_Max){
        NWB_FATAL_ASSERT_MSG(false, "GPU packet recording claim count overflowed");
        TerminateInvariant();
    }
    m_activePacketRecordingClaimCount.store(activeClaimCount + 1u, MemoryOrder::release);
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        m_tasks[tasks[taskIndex].index].lifecycleState = TaskLifecycleState::Recording;
        m_tasks[tasks[taskIndex].index].recordingClaimGeneration = claimGeneration;
        m_tasks[tasks[taskIndex].index].submissionClaimGeneration = 0u;
        m_tasks[tasks[taskIndex].index].recordThunkInProgress = false;
        m_tasks[tasks[taskIndex].index].recordThunkCompleted = false;
    }
    outLease.m_packet = packet;
    outLease.m_planGeneration = planAccess.planGeneration();
    outLease.m_recordingAttemptGeneration = recordingAttemptGeneration;
    outLease.m_claimGeneration = claimGeneration;
    return true;
}

bool GpuTaskGraph::completePacketRecording(
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
            || (task.recordPayload && !task.recordThunkCompleted)
        )
            return false;
    }
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        m_tasks[tasks[taskIndex].index].lifecycleState = TaskLifecycleState::Recorded;
        m_tasks[tasks[taskIndex].index].recordingClaimGeneration = 0u;
        m_tasks[tasks[taskIndex].index].recordThunkInProgress = false;
        m_tasks[tasks[taskIndex].index].recordThunkCompleted = false;
    }
    lease.reset();
    releasePacketRecordingClaimWithinLock();
    return true;
}


bool GpuTaskGraph::deferPacketRecordingAbort(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    PacketRecordingLease& lease,
    PacketRecordingAbort& outAbort
)const noexcept{
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.validPacket(packet)
        || !lease.valid()
        || outAbort.valid()
        || lease.m_packet != packet
        || lease.m_planGeneration != planAccess.planGeneration()
    )
        return false;
    outAbort.m_packet = lease.m_packet;
    outAbort.m_planGeneration = lease.m_planGeneration;
    outAbort.m_recordingAttemptGeneration = lease.m_recordingAttemptGeneration;
    outAbort.m_claimGeneration = lease.m_claimGeneration;
    lease.reset();
    return true;
}


bool GpuTaskGraph::completePacketRecordingAbort(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    PacketRecordingAbort& abort
)const{
    if(
        !planAccess.validFor(compiledGraph)
        || !abort.valid()
        || !planAccess.validPacket(abort.m_packet)
        || abort.m_planGeneration != planAccess.planGeneration()
    )
        return false;
    const GpuCompiledPacketView packetView = planAccess.packet(abort.m_packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return false;
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
            || m_activeRecordingAttemptGeneration != abort.m_recordingAttemptGeneration
        )
            return false;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(
                task.lifecycleState != TaskLifecycleState::Recording
                || task.lifecycleAttemptGeneration != abort.m_recordingAttemptGeneration
                || task.recordingClaimGeneration != abort.m_claimGeneration
                || task.recordThunkInProgress
            )
                return false;
        }
        notification.activateWithinLock();
        notification.adoptPacketRecordingClaimWithinLock();
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = TaskLifecycleState::Discarded;
            task.recordingClaimGeneration = 0u;
            task.submissionClaimGeneration = 0u;
            task.discardNotificationGeneration = notificationGeneration;
            task.recordThunkInProgress = false;
            task.recordThunkCompleted = false;
        }
        abort.reset();
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


void GpuTaskGraph::abortPacketRecording(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    PacketRecordingLease& lease
)const{
    const u64 recordingAttemptGeneration = lease.m_recordingAttemptGeneration;
    PacketRecordingAbort abort;
    if(!deferPacketRecordingAbort(compiledGraph, planAccess, packet, lease, abort)){
        NWB_FATAL_ASSERT_MSG(false, "active packet recording abort must retain its exact lease");
        TerminateInvariant();
    }
    RecordingAttemptResolutionGuard resolution(*this, compiledGraph, recordingAttemptGeneration);
    if(!completePacketRecordingAbort(compiledGraph, planAccess, abort)){
        NWB_FATAL_ASSERT_MSG(false, "active packet recording abort must complete before attempt resolution");
        TerminateInvariant();
    }
    resolution.complete();
}


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

