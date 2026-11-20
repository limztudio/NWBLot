// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskGraph::packetReadyForSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, const u64 recordingAttemptGeneration)const noexcept{
    if(!planAccess.validFor(compiledGraph) || !planAccess.validPacket(packet))
        return false;
    return packetsReadyForSubmission(compiledGraph, planAccess, GpuSubmissionPacketRange{ packet, 1u }, recordingAttemptGeneration);
}

b8 GpuTaskGraph::packetsReadyForSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketRange range, const u64 recordingAttemptGeneration)const noexcept{
    if(!planAccess.validFor(compiledGraph) || !planAccess.validPacketRange(range) || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_lifecycleMutex);
    if(!matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), recordingAttemptGeneration) || m_activeRecordingPreparationSerial != 0u || m_submissionBindingState == SubmissionBindingState::ExceptionClosing)
        return false;

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return false;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState() != TaskLifecycleState::Recorded || task.lifecycleAttemptGeneration != recordingAttemptGeneration)
                return false;
        }
    }
    return true;
}


b8 GpuTaskGraph::beginPacketSubmission(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packet,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding,
    GpuPacketSubmissionLease& outLease
)const noexcept{
    if(outLease.valid())
        return false;
    GpuPacketSubmissionLease* leasePtr = &outLease;
    return beginPacketSubmissionBatch(compiledGraph, planAccess, Span<const GpuSubmissionPacketId>(&packet, 1u), recordingAttemptGeneration, submissionBinding, &leasePtr) == 1u;
}

usize GpuTaskGraph::beginPacketSubmissionBatch(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const Span<const GpuSubmissionPacketId> packets,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding,
    GpuPacketSubmissionLease* const* const outLeases
)const noexcept{
    if(!planAccess.validFor(compiledGraph) || packets.empty() || !outLeases || recordingAttemptGeneration == 0u || !submissionBinding.valid())
        return 0u;

    NothrowScopedLock lock(m_lifecycleMutex);
    if(
        !matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), recordingAttemptGeneration)
        || m_activeRecordingPreparationSerial != 0u
        || m_activeSubmissionBinding != submissionBinding
        || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
    )
        return 0u;

    usize claimedCount = 0u;
    for(const GpuSubmissionPacketId packet : packets){
        GpuPacketSubmissionLease* const outLease = outLeases[claimedCount];
        if(!outLease || outLease->valid())
            break;
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        if(!packetView.valid())
            break;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;

        b8 packetValid = true;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex])){
                packetValid = false;
                break;
            }
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            const u16 lifecycleWord = task.loadLifecycleWord();
            if(
                GpuTaskNode::DecodeLifecycleState(lifecycleWord) != TaskLifecycleState::Recorded
                || task.lifecycleAttemptGeneration != recordingAttemptGeneration
                || task.recordingClaimGeneration != 0u
                || task.submissionClaimGeneration != 0u
                || GpuTaskNode::DecodeRecordThunkInProgress(lifecycleWord)
            ){
                packetValid = false;
                break;
            }
        }
        if(!packetValid)
            break;

        const u64 claimGeneration = AllocateGeneration();
        FOUNDATION_ASSERT(claimGeneration != 0u);
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.submissionClaimGeneration = claimGeneration;
            task.setLifecycle(TaskLifecycleState::Submitting);
        }
        outLease->m_packet = packet;
        outLease->m_planGeneration = planAccess.planGeneration();
        outLease->m_recordingAttemptGeneration = recordingAttemptGeneration;
        outLease->m_claimGeneration = claimGeneration;
        outLease->m_submissionBinding = submissionBinding;
        ++claimedCount;
    }
    return claimedCount;
}


void GpuTaskGraph::beginPacketSubmissionAcceptance(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, const QueueSubmissionToken& token, GpuPacketSubmissionLease& lease)const noexcept{
    const QueueSubmissionReceipt receipt{ .token = token };
    GpuPacketSubmissionLease* leasePtr = &lease;
    beginPacketSubmissionAcceptanceBatch(compiledGraph, planAccess, Span<const GpuSubmissionPacketId>(&packet, 1u), &receipt, &leasePtr);
}

void GpuTaskGraph::beginPacketSubmissionAcceptanceBatch(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const Span<const GpuSubmissionPacketId> packets, const QueueSubmissionReceipt* const receipts, GpuPacketSubmissionLease* const* const leases)const noexcept{
    const b8 batchInputValid = planAccess.validFor(compiledGraph) && !packets.empty() && receipts != nullptr && leases != nullptr;
    FOUNDATION_FATAL_ASSERT_MSG(batchInputValid, "native-accepted packet batch must retain its exact graph submission leases");

    NothrowScopedLock lock(m_lifecycleMutex);
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packet = packets[packetIndex];
        const QueueSubmissionToken& token = receipts[packetIndex].token;
        GpuPacketSubmissionLease* const leasePtr = leases[packetIndex];
        const b8 inputValid = planAccess.validPacket(packet) && token.valid() && leasePtr != nullptr && leasePtr->valid() && leasePtr->m_packet == packet && leasePtr->m_planGeneration == planAccess.planGeneration();
        FOUNDATION_FATAL_ASSERT_MSG(inputValid, "native-accepted packet must retain its exact graph submission lease");
        const GpuPacketSubmissionLease& lease = *leasePtr;
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        const b8 packetValid = packetView.valid() && packetView.plan->taskCount != 0u;
        FOUNDATION_FATAL_ASSERT_MSG(packetValid, "native-accepted packet must retain its compiled task range");
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;

        const b8 bindingValid = matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), lease.m_recordingAttemptGeneration) && m_activeSubmissionBinding == lease.m_submissionBinding;
        FOUNDATION_FATAL_ASSERT_MSG(bindingValid, "native-accepted packet must retain its graph submission binding");

        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskId taskID = tasks[taskIndex];
            if(!validTask(taskID))
                TerminateInvariant();
            const GpuTaskNode& task = m_tasks[taskID.index];
            const b8 taskValid = task.lifecycleState() == TaskLifecycleState::Submitting && task.lifecycleAttemptGeneration == lease.m_recordingAttemptGeneration && task.submissionClaimGeneration == lease.m_claimGeneration;
            FOUNDATION_FATAL_ASSERT_MSG(taskValid, "native-accepted task must retain its exact submission claim");
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex)
            m_tasks[tasks[taskIndex].index].setLifecycle(TaskLifecycleState::Accepting);
    }
}

void GpuTaskGraph::notifyPacketSubmissionAccepted(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, const QueueSubmissionToken& token, const GpuPacketSubmissionLease& lease)const{
    const b8 inputValid = planAccess.validFor(compiledGraph) && planAccess.validPacket(packet) && token.valid() && lease.valid();
    FOUNDATION_FATAL_ASSERT_MSG(inputValid, "native-accepted callback publication must retain its exact graph submission lease");

    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    FOUNDATION_FATAL_ASSERT_MSG(packetView.valid(), "native-accepted callback publication must retain its compiled task range");
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

void GpuTaskGraph::completePacketSubmissionAcceptance(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const noexcept{
    GpuPacketSubmissionLease* leasePtr = &lease;
    completePacketSubmissionAcceptanceBatch(compiledGraph, planAccess, Span<const GpuSubmissionPacketId>(&packet, 1u), &leasePtr);
}

void GpuTaskGraph::completePacketSubmissionAcceptanceBatch(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const Span<const GpuSubmissionPacketId> packets, GpuPacketSubmissionLease* const* const leases)const noexcept{
    const b8 batchInputValid = planAccess.validFor(compiledGraph) && !packets.empty() && leases != nullptr;
    FOUNDATION_FATAL_ASSERT_MSG(batchInputValid, "native-accepted completion batch must retain its exact graph submission leases");

    NothrowScopedLock lock(m_lifecycleMutex);
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packet = packets[packetIndex];
        GpuPacketSubmissionLease* const leasePtr = leases[packetIndex];
        const b8 inputValid = planAccess.validPacket(packet) && leasePtr != nullptr && leasePtr->valid();
        FOUNDATION_FATAL_ASSERT_MSG(inputValid, "native-accepted completion must retain its exact graph submission lease");
        GpuPacketSubmissionLease& lease = *leasePtr;

        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        FOUNDATION_FATAL_ASSERT_MSG(packetView.valid(), "native-accepted completion must retain its compiled task range");
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;

        const b8 bindingValid = matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), lease.m_recordingAttemptGeneration) && m_activeSubmissionBinding == lease.m_submissionBinding;
        FOUNDATION_FATAL_ASSERT_MSG(bindingValid, "native-accepted completion must retain its graph submission binding");
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskId taskID = tasks[taskIndex];
            if(!validTask(taskID))
                TerminateInvariant();
            const GpuTaskNode& task = m_tasks[taskID.index];
            const b8 taskValid = task.lifecycleState() == TaskLifecycleState::Accepting && task.lifecycleAttemptGeneration == lease.m_recordingAttemptGeneration && task.submissionClaimGeneration == lease.m_claimGeneration;
            FOUNDATION_FATAL_ASSERT_MSG(taskValid, "native-accepted task must retain its exact accepting claim");
        }
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.submissionClaimGeneration = 0u;
            task.setLifecycle(TaskLifecycleState::Accepted);
        }
        lease.reset();
    }
}


void GpuTaskGraph::abortPacketSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const{
    if(!planAccess.validFor(compiledGraph) || !lease.valid() || lease.m_packet != packet || lease.m_planGeneration != planAccess.planGeneration())
        return;
    const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
    if(!packetView.valid())
        return;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    DiscardNotificationScope notification(*this);
    const u64 notificationGeneration = AllocateGeneration();
    {
        ScopedLock lock(m_lifecycleMutex);
        if(!matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), lease.m_recordingAttemptGeneration) || m_activeSubmissionBinding != lease.m_submissionBinding)
            return;

        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            if(!validTask(tasks[taskIndex]))
                return;
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleState() != TaskLifecycleState::Submitting || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration || task.submissionClaimGeneration != lease.m_claimGeneration)
                return;
        }
        notification.activateWithinLock();
        discardPacketTasksWithinLock(Span<const GpuTaskId>(tasks, packetPlan.taskCount), notificationGeneration);
        lease.reset();
    }

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.discardNotificationGeneration == notificationGeneration && task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }
}

b8 GpuTaskGraph::abandonPacketSubmissionWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const noexcept{
    Expected<GpuCompiledPacketView> packetView = MakeUnexpected(Failure{});
    if(!(packetView = ResolvePacketView(compiledGraph, planAccess, packet)) || !lease.valid() || lease.m_packet != packet || lease.m_planGeneration != planAccess.planGeneration())
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView->plan;
    const GpuTaskId* const tasks = packetView->tasks;

    NothrowScopedLock lock(m_lifecycleMutex);
    if(!matchesPacketAttemptWithinLock(compiledGraph, planAccess.planGeneration(), lease.m_recordingAttemptGeneration) || m_activeSubmissionBinding != lease.m_submissionBinding)
        return false;
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.lifecycleState() != TaskLifecycleState::Submitting || task.lifecycleAttemptGeneration != lease.m_recordingAttemptGeneration || task.submissionClaimGeneration != lease.m_claimGeneration)
            return false;
    }
    discardPacketTasksWithinLock(Span<const GpuTaskId>(tasks, packetPlan.taskCount));
    lease.reset();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

