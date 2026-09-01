// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiled_graph.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCompiledGraph::GpuCompiledGraph(GraphicsArena& arena)
    : m_tasks(arena)
    , m_compiledTaskIndexByTask(arena)
    , m_packets(arena)
    , m_packetTasks(arena)
    , m_packetDependencies(arena)
    , m_packetExternalDependencies(arena)
    , m_prologueStateSeeds(arena)
    , m_prologueBarriers(arena)
    , m_epilogueBarriers(arena)
    , m_ownershipTransfers(arena)
    , m_externalResourceExports(arena)
    , m_externalResourceExportSources(arena)
    , m_queueTopology(arena)
{}


GpuCompiledGraph::~GpuCompiledGraph(){
    ScopedLock lock(m_submissionBindingMutex);
    if(m_submissionBindingState == SubmissionBindingState::Active){
        NWB_FATAL_ASSERT_MSG(false, "GpuCompiledGraph destruction requires its active submission owner to resolve first");
        return;
    }
}


bool GpuCompiledGraph::tryReset(){
    ScopedLock lock(m_submissionBindingMutex);
    if(m_submissionBindingState == SubmissionBindingState::Active)
        return false;

    m_tasks.clear();
    m_compiledTaskIndexByTask.clear();
    m_packets.clear();
    m_packetTasks.clear();
    m_packetDependencies.clear();
    m_packetExternalDependencies.clear();
    m_prologueStateSeeds.clear();
    m_prologueBarriers.clear();
    m_epilogueBarriers.clear();
    m_ownershipTransfers.clear();
    m_externalResourceExports.clear();
    m_externalResourceExportSources.clear();
    m_queueTopology.clear();
    m_presentEndpoint = {};
    m_packetTimingEnvelopeRange = {};
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_planGeneration = 0u;
    m_deviceGeneration = 0u;
    m_graphTaskCount = 0u;
    m_compileStatistics = {};
    m_submissionRecordingAttemptGeneration = 0u;
    m_submissionTransactionIdentity = 0u;
    m_submissionTransactionResetGeneration = 0u;
    m_submissionBindingState = SubmissionBindingState::None;
    m_hasPresentEndpoint = false;
    m_valid = false;
    return true;
}


void GpuCompiledGraph::reset(){
    if(!tryReset())
        NWB_ASSERT_MSG(false, "GpuCompiledGraph::reset requires its active submission owner to resolve first");
}

bool GpuCompiledGraph::validFor(const GpuTaskGraph& graph)const noexcept{
    const GpuPresentEndpoint* const graphEndpoint = graph.presentEndpoint();
    return valid()
        && m_generation == graph.generation()
        && m_declarationRevision == graph.declarationRevision()
        && m_graphTaskCount == graph.taskCount()
        && m_tasks.size() == m_graphTaskCount
        && m_compiledTaskIndexByTask.size() == m_graphTaskCount
        && m_packetTasks.size() == m_graphTaskCount
        && (
            (m_graphTaskCount == 0u && m_packets.empty())
            || (m_graphTaskCount > 0u && !m_packets.empty() && m_packets.size() <= m_graphTaskCount)
        )
        && (m_hasPresentEndpoint == (graphEndpoint != nullptr))
        && (
            !graphEndpoint
            || (
                m_presentEndpoint.valid()
                && m_presentEndpoint.producer == graphEndpoint->producer
                && m_presentEndpoint.backBuffer == graphEndpoint->backBuffer
                && m_presentEndpoint.producer.generation == m_generation
                && m_presentEndpoint.backBuffer.generation == m_generation
                && validPacket(m_presentEndpoint.packet)
                && m_presentEndpoint.packet == packetForTask(m_presentEndpoint.producer)
                && queueInfo(m_presentEndpoint.queue) != nullptr
            )
        )
    ;
}

bool GpuCompiledGraph::validPacket(const GpuSubmissionPacketId& packetID)const noexcept{
    return valid() && packetID.valid() && packetID.generation == m_planGeneration && packetID.index < m_packets.size();
}

bool GpuCompiledGraph::validPacketRange(const GpuSubmissionPacketRange& range)const noexcept{
    return range.valid()
        && validPacket(range.first)
        && range.packetCount <= m_packets.size() - range.first.index
    ;
}

GpuSubmissionPacketId GpuCompiledGraph::packetIdAt(const usize index)const noexcept{
    return index < m_packets.size()
        ? GpuSubmissionPacketId{ static_cast<u32>(index), m_planGeneration }
        : GpuSubmissionPacketId{}
    ;
}

GpuSubmissionPacketRange GpuCompiledGraph::packetRange(
    const GpuSubmissionPacketId& first,
    const GpuSubmissionPacketId& last
)const noexcept{
    if(!validPacket(first) || !validPacket(last) || last.index < first.index)
        return {};
    return GpuSubmissionPacketRange{
        .first = first,
        .packetCount = static_cast<usize>(last.index) - static_cast<usize>(first.index) + 1u,
    };
}

GpuSubmissionPacketRange GpuCompiledGraph::packetRangeForTasks(
    const GpuTaskId& first,
    const GpuTaskId& last
)const noexcept{
    const GpuSubmissionPacketId firstPacket = packetForTask(first);
    const GpuSubmissionPacketId lastPacket = packetForTask(last);
    return packetRange(firstPacket, lastPacket);
}

GpuSubmissionPacketRange GpuCompiledGraph::allPacketRange()const noexcept{
    return m_packets.empty()
        ? GpuSubmissionPacketRange{}
        : packetRange(packetIdAt(0u), packetIdAt(m_packets.size() - 1u))
    ;
}

const GpuCompiledTask* GpuCompiledGraph::findTask(const GpuTaskId& task)const noexcept{
    if(
        !task.valid()
        || task.generation != m_generation
        || task.index >= m_compiledTaskIndexByTask.size()
    )
        return nullptr;
    const u32 compiledTaskIndex = m_compiledTaskIndexByTask[task.index];
    if(compiledTaskIndex >= m_tasks.size())
        return nullptr;
    const GpuCompiledTask& compiledTask = m_tasks[compiledTaskIndex];
    return compiledTask.task == task ? &compiledTask : nullptr;
}

GpuSubmissionPacketId GpuCompiledGraph::packetForTask(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask ? compiledTask->packet : GpuSubmissionPacketId{};
}

GpuTaskPacketizationDecision::Enum GpuCompiledGraph::packetizationDecisionForTask(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask ? compiledTask->packetizationDecision : GpuTaskPacketizationDecision::Unknown;
}

bool GpuCompiledGraph::tasksSharePacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    const GpuCompiledTask* const firstTask = findTask(first);
    const GpuCompiledTask* const secondTask = findTask(second);
    return firstTask && secondTask && firstTask->packet == secondTask->packet;
}

bool GpuCompiledGraph::taskPrecedesOrSharesPacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    const GpuCompiledTask* const firstTask = findTask(first);
    const GpuCompiledTask* const secondTask = findTask(second);
    return firstTask
        && secondTask
        && firstTask->packet.valid()
        && secondTask->packet.valid()
        && firstTask->packet.index <= secondTask->packet.index
    ;
}

bool GpuCompiledGraph::taskPrecedesInSamePacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    if(first == second)
        return false;
    const GpuCompiledTask* const firstTask = findTask(first);
    const GpuCompiledTask* const secondTask = findTask(second);
    if(
        !firstTask
        || !secondTask
        || firstTask->packet != secondTask->packet
        || !validPacket(firstTask->packet)
    )
        return false;

    const GpuSubmissionPacket& packetPlan = m_packets[firstTask->packet.index];
    if(
        packetPlan.taskCount == 0u
        || packetPlan.taskOffset > m_packetTasks.size()
        || packetPlan.taskCount > m_packetTasks.size() - packetPlan.taskOffset
    )
        return false;

    bool foundFirst = false;
    for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskId task = m_packetTasks[packetPlan.taskOffset + taskIndex];
        if(task == first)
            foundFirst = true;
        else if(task == second)
            return foundFirst;
    }
    return false;
}

bool GpuCompiledGraph::tasksFormContiguousPacketSequence(
    const GpuTaskId* const tasks,
    const usize taskCount
)const noexcept{
    if(!tasks || taskCount == 0u)
        return false;
    const GpuCompiledTask* const firstTask = findTask(tasks[0u]);
    if(!firstTask || !validPacket(firstTask->packet))
        return false;

    const GpuSubmissionPacket& packetPlan = m_packets[firstTask->packet.index];
    if(
        taskCount > packetPlan.taskCount
        || packetPlan.taskOffset > m_packetTasks.size()
        || packetPlan.taskCount > m_packetTasks.size() - packetPlan.taskOffset
    )
        return false;

    for(u32 firstTaskIndex = 0u;
        static_cast<usize>(firstTaskIndex) + taskCount <= packetPlan.taskCount;
        ++firstTaskIndex
    ){
        bool matches = true;
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            if(m_packetTasks[packetPlan.taskOffset + firstTaskIndex + taskIndex] == tasks[taskIndex])
                continue;
            matches = false;
            break;
        }
        if(matches)
            return true;
    }
    return false;
}

bool GpuCompiledGraph::taskJoinsAcceptedQueueFrontier(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask
        && validPacket(compiledTask->packet)
        && m_packets[compiledTask->packet.index].joinsAcceptedQueueFrontier
    ;
}

const GpuPhysicalQueueInfo* GpuCompiledGraph::queueInfoForTask(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask ? queueInfo(compiledTask->queue) : nullptr;
}

const GpuSubmissionPacket& GpuCompiledGraph::packet(const GpuSubmissionPacketId& packetID)const noexcept{
    NWB_ASSERT(validPacket(packetID));
    return m_packets[packetID.index];
}

const GpuTaskId* GpuCompiledGraph::packetTasks(const GpuSubmissionPacketId& packetID)const noexcept{
    const GpuSubmissionPacket& packetPlan = packet(packetID);
    return packetPlan.taskCount > 0u ? m_packetTasks.data() + packetPlan.taskOffset : nullptr;
}

const GpuPacketDependency* GpuCompiledGraph::packetDependencies(const GpuSubmissionPacketId& packetID)const noexcept{
    const GpuSubmissionPacket& packetPlan = packet(packetID);
    return packetPlan.dependencyCount > 0u ? m_packetDependencies.data() + packetPlan.dependencyOffset : nullptr;
}

const GpuExternalCompletionId* GpuCompiledGraph::packetExternalDependencies(
    const GpuSubmissionPacketId& packetID
)const noexcept{
    const GpuSubmissionPacket& packetPlan = packet(packetID);
    return packetPlan.externalDependencyCount > 0u
        ? m_packetExternalDependencies.data() + packetPlan.externalDependencyOffset
        : nullptr
    ;
}

const GpuPacketStateSeed* GpuCompiledGraph::taskPrologueStateSeeds(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    if(
        !compiledTask
        || compiledTask->prologueStateSeedCount == 0u
        || compiledTask->prologueStateSeedOffset > m_prologueStateSeeds.size()
        || compiledTask->prologueStateSeedCount > m_prologueStateSeeds.size() - compiledTask->prologueStateSeedOffset
    )
        return nullptr;
    return m_prologueStateSeeds.data() + compiledTask->prologueStateSeedOffset;
}

const GpuCompiledBarrier* GpuCompiledGraph::taskPrologueBarriers(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    if(
        !compiledTask
        || compiledTask->prologueBarrierCount == 0u
        || compiledTask->prologueBarrierOffset > m_prologueBarriers.size()
        || compiledTask->prologueBarrierCount > m_prologueBarriers.size() - compiledTask->prologueBarrierOffset
    )
        return nullptr;
    return m_prologueBarriers.data() + compiledTask->prologueBarrierOffset;
}

const GpuCompiledBarrier* GpuCompiledGraph::taskEpilogueBarriers(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    if(
        !compiledTask
        || compiledTask->epilogueBarrierCount == 0u
        || compiledTask->epilogueBarrierOffset > m_epilogueBarriers.size()
        || compiledTask->epilogueBarrierCount > m_epilogueBarriers.size() - compiledTask->epilogueBarrierOffset
    )
        return nullptr;
    return m_epilogueBarriers.data() + compiledTask->epilogueBarrierOffset;
}

const GpuCompiledOwnershipTransfer* GpuCompiledGraph::logicalOwnershipTransfers()const noexcept{
    return valid() && !m_ownershipTransfers.empty() ? m_ownershipTransfers.data() : nullptr;
}

const GpuCompiledOwnershipTransfer* GpuCompiledGraph::logicalOwnershipTransferAt(const usize index)const noexcept{
    return valid() && index < m_ownershipTransfers.size() ? &m_ownershipTransfers[index] : nullptr;
}

const GpuCompiledExternalResourceExport* GpuCompiledGraph::externalResourceExport(
    const GpuGraphResourceId& resource
)const noexcept{
    if(!resource.valid() || resource.generation != m_generation)
        return nullptr;
    for(const GpuCompiledExternalResourceExport& exportInfo : m_externalResourceExports){
        if(exportInfo.resource == resource)
            return &exportInfo;
    }
    return nullptr;
}

const GpuCompiledExternalResourceExport* GpuCompiledGraph::externalResourceExportAt(
    const usize index
)const noexcept{
    return index < m_externalResourceExports.size() ? &m_externalResourceExports[index] : nullptr;
}

const GpuCompiledExternalResourceExportSource* GpuCompiledGraph::externalResourceExportSources(
    const GpuCompiledExternalResourceExport& exportInfo
)const noexcept{
    if(
        exportInfo.sourceCount == 0u
        || exportInfo.sourceOffset > m_externalResourceExportSources.size()
        || exportInfo.sourceCount > m_externalResourceExportSources.size() - exportInfo.sourceOffset
    )
        return nullptr;
    return m_externalResourceExportSources.data() + exportInfo.sourceOffset;
}

GpuTaskGraphPhysicalQueueCompileStatistics GpuCompiledGraph::physicalQueueCompileStatistics(
    const GpuPhysicalQueueId& queue
)const noexcept{
    if(!valid())
        return {};

    const GpuPhysicalQueueInfo* const queueInfo = this->queueInfo(queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::kCount)
        return {};

    GpuTaskGraphPhysicalQueueCompileStatistics statistics{
        .graphGeneration = m_generation,
        .planGeneration = m_planGeneration,
        .deviceGeneration = m_deviceGeneration,
        .queue = queue,
        .queueClass = queueInfo->queueClass,
    };
    const auto countOwnershipBarriers = [&statistics](
        const GraphicsVector<GpuCompiledBarrier>& barriers,
        const u32 barrierOffset,
        const u32 barrierCount
    ){
        if(
            barrierCount == 0u
            || barrierOffset > barriers.size()
            || barrierCount > barriers.size() - barrierOffset
        )
            return;

        const GpuCompiledBarrier* const taskBarriers = barriers.data() + barrierOffset;
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            switch(taskBarriers[barrierIndex].type){
            case GpuCompiledBarrierType::TextureOwnershipRelease:
            case GpuCompiledBarrierType::BufferOwnershipRelease:
            case GpuCompiledBarrierType::AccelStructOwnershipRelease:
                ++statistics.ownershipReleaseBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipAcquire:
            case GpuCompiledBarrierType::BufferOwnershipAcquire:
            case GpuCompiledBarrierType::AccelStructOwnershipAcquire:
                ++statistics.ownershipAcquireBarrierCount;
                break;
            default:
                break;
            }
        }
    };
    for(const GpuCompiledTask& task : m_tasks){
        if(task.queue != queue)
            continue;

        ++statistics.taskCount;
        statistics.prologueBarrierCount += task.prologueBarrierCount;
        statistics.epilogueBarrierCount += task.epilogueBarrierCount;
        countOwnershipBarriers(m_prologueBarriers, task.prologueBarrierOffset, task.prologueBarrierCount);
        countOwnershipBarriers(m_epilogueBarriers, task.epilogueBarrierOffset, task.epilogueBarrierCount);
    }
    for(const GpuSubmissionPacket& packet : m_packets){
        if(packet.queue != queue)
            continue;

        ++statistics.packetCount;
        if(packet.taskCount > 1u)
            statistics.mergedTaskCount += packet.taskCount - 1u;
    }
    const auto sameTransferSignature = [](const GpuCompiledOwnershipTransfer& lhs, const GpuCompiledOwnershipTransfer& rhs){
        return lhs.resource == rhs.resource
            && lhs.route == rhs.route
            && lhs.sourceTask == rhs.sourceTask
            && lhs.destinationTask == rhs.destinationTask
            && lhs.sourceQueue == rhs.sourceQueue
            && lhs.destinationQueue == rhs.destinationQueue
        ;
    };
    for(usize transferIndex = 0u; transferIndex < m_ownershipTransfers.size(); ++transferIndex){
        const GpuCompiledOwnershipTransfer& transfer = m_ownershipTransfers[transferIndex];
        if(transfer.sourceQueue == queue)
            ++statistics.outgoingLogicalOwnershipTransferCount;
        if(transfer.destinationQueue == queue)
            ++statistics.incomingLogicalOwnershipTransferCount;

        bool signatureAlreadyCounted = false;
        bool hasEarlierDistinctSignature = false;
        for(usize previousIndex = 0u; previousIndex < transferIndex; ++previousIndex){
            const GpuCompiledOwnershipTransfer& previous = m_ownershipTransfers[previousIndex];
            if(sameTransferSignature(transfer, previous)){
                signatureAlreadyCounted = true;
                break;
            }
            if(previous.resource == transfer.resource)
                hasEarlierDistinctSignature = true;
        }
        if(signatureAlreadyCounted)
            continue;

        if(transfer.sourceQueue == queue){
            ++statistics.outgoingLogicalOwnershipTransferSignatureCount;
            if(hasEarlierDistinctSignature)
                ++statistics.outgoingRepeatedOwnershipTransferSignatureCount;
        }
        if(transfer.destinationQueue == queue){
            ++statistics.incomingLogicalOwnershipTransferSignatureCount;
            if(hasEarlierDistinctSignature)
                ++statistics.incomingRepeatedOwnershipTransferSignatureCount;
        }
    }
    for(usize transferIndex = 0u; transferIndex < m_ownershipTransfers.size(); ++transferIndex){
        const GpuCompiledOwnershipTransfer& transfer = m_ownershipTransfers[transferIndex];
        if(
            !transfer.concurrentSharingCouldAvoid
            || (transfer.sourceQueue != queue && transfer.destinationQueue != queue)
        )
            continue;

        bool resourceAlreadyCounted = false;
        for(usize previousIndex = 0u; previousIndex < transferIndex; ++previousIndex){
            const GpuCompiledOwnershipTransfer& previous = m_ownershipTransfers[previousIndex];
            if(
                previous.resource == transfer.resource
                && previous.concurrentSharingCouldAvoid
                && (previous.sourceQueue == queue || previous.destinationQueue == queue)
            ){
                resourceAlreadyCounted = true;
                break;
            }
        }
        if(resourceAlreadyCounted)
            continue;

        usize distinctSignatureCount = 0u;
        for(usize candidateIndex = 0u; candidateIndex < m_ownershipTransfers.size(); ++candidateIndex){
            const GpuCompiledOwnershipTransfer& candidate = m_ownershipTransfers[candidateIndex];
            if(candidate.resource != transfer.resource || !candidate.concurrentSharingCouldAvoid)
                continue;

            bool signatureAlreadyCounted = false;
            for(usize previousIndex = 0u; previousIndex < candidateIndex; ++previousIndex){
                if(sameTransferSignature(candidate, m_ownershipTransfers[previousIndex])){
                    signatureAlreadyCounted = true;
                    break;
                }
            }
            if(!signatureAlreadyCounted)
                ++distinctSignatureCount;
        }
        if(distinctSignatureCount > 1u)
            ++statistics.concurrentSharingAdviceResourceCount;
    }
    return statistics;
}

const GpuPhysicalQueueInfo* GpuCompiledGraph::queueInfo(const GpuPhysicalQueueId& queue)const noexcept{
    if(!queue.valid() || queue.deviceGeneration != m_deviceGeneration)
        return nullptr;
    for(const GpuPhysicalQueueInfo& info : m_queueTopology){
        if(info.id == queue)
            return &info;
    }
    return nullptr;
}

GpuPhysicalQueueTopology GpuCompiledGraph::queueTopology()const noexcept{
    if(!valid())
        return {};
    return GpuPhysicalQueueTopology{
        .queues = m_queueTopology.empty() ? nullptr : m_queueTopology.data(),
        .queueCount = m_queueTopology.size(),
    };
}


bool GpuCompiledGraph::bindSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    ScopedLock lock(m_submissionBindingMutex);
    if(!validFor(graph) || m_planGeneration != expectedPlanGeneration)
        return false;
    if(m_submissionBindingState == SubmissionBindingState::Active){
        return m_submissionRecordingAttemptGeneration == recordingAttemptGeneration
            && m_submissionTransactionIdentity == submissionBinding.m_transactionIdentity
            && m_submissionTransactionResetGeneration == submissionBinding.m_resetGeneration
        ;
    }
    if(
        m_submissionBindingState != SubmissionBindingState::None
        && (
            m_submissionBindingState != SubmissionBindingState::Resolved
            || m_submissionRecordingAttemptGeneration == recordingAttemptGeneration
        )
    )
        return false;
    m_submissionRecordingAttemptGeneration = recordingAttemptGeneration;
    m_submissionTransactionIdentity = submissionBinding.m_transactionIdentity;
    m_submissionTransactionResetGeneration = submissionBinding.m_resetGeneration;
    m_submissionBindingState = SubmissionBindingState::Active;
    return true;
}


bool GpuCompiledGraph::matchesSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    ScopedLock lock(m_submissionBindingMutex);
    return validFor(graph)
        && m_planGeneration == expectedPlanGeneration
        && m_submissionBindingState != SubmissionBindingState::None
        && m_submissionRecordingAttemptGeneration == recordingAttemptGeneration
        && m_submissionTransactionIdentity == submissionBinding.m_transactionIdentity
        && m_submissionTransactionResetGeneration == submissionBinding.m_resetGeneration
    ;
}


bool GpuCompiledGraph::resolveSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    ScopedLock lock(m_submissionBindingMutex);
    if(
        !validFor(graph)
        || m_planGeneration != expectedPlanGeneration
        || m_submissionRecordingAttemptGeneration != recordingAttemptGeneration
        || m_submissionTransactionIdentity != submissionBinding.m_transactionIdentity
        || m_submissionTransactionResetGeneration != submissionBinding.m_resetGeneration
    )
        return false;
    if(m_submissionBindingState == SubmissionBindingState::Resolved)
        return true;
    if(m_submissionBindingState != SubmissionBindingState::Active)
        return false;
    m_submissionBindingState = SubmissionBindingState::Resolved;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

