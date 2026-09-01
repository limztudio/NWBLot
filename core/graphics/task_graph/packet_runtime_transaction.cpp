// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_packet_runtime_transaction{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextAcceptanceRevision{ 1u };


[[nodiscard]] static u64 AllocateAcceptanceRevision()noexcept{
    u64 revision = s_NextAcceptanceRevision.fetch_add(1u, MemoryOrder::relaxed);
    if(revision == 0u)
        revision = s_NextAcceptanceRevision.fetch_add(1u, MemoryOrder::relaxed);
    return revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local const GpuGraphSubmissionTransaction::SubmissionOperation* GpuGraphSubmissionTransaction::SubmissionOperation::s_activeOperation = nullptr;


GpuGraphSubmissionTransaction::SubmissionOperation::SubmissionOperation(
    const GpuGraphSubmissionTransaction& transaction,
    const SubmissionOperationMode mode
)noexcept{
    for(const SubmissionOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_transaction == &transaction)
            return;
    }
    if(transaction.m_compositeOperationActive.test(MemoryOrder::acquire))
        return;

    const bool composite = mode == SubmissionOperationMode::CompositeBarrier;
    const bool exclusive = mode != SubmissionOperationMode::OrdinaryPacket;
    // A nested cross-transaction operation must acquire the target as a writer even when it submits an ordinary
    // packet. Acquiring another reader could succeed while the target resolution mutex is held, then form an ABBA
    // cycle with a symmetric callback. Writer try-acquire proves both the target gate and its inner resolution tail
    // are free without waiting while this thread retains its outer transaction.
    const bool nestedOperation = s_activeOperation != nullptr;
    if(nestedOperation){
        if(!m_gateLock.try_acquire(transaction.m_submissionGate, true))
            return;
    }
    else
        m_gateLock.acquire(transaction.m_submissionGate, exclusive);
    if(composite && transaction.m_compositeOperationActive.test_and_set(MemoryOrder::acq_rel)){
        m_gateLock.release();
        return;
    }
    m_transaction = &transaction;
    m_previousOperation = s_activeOperation;
    m_exclusive = nestedOperation || exclusive;
    m_composite = composite;
    s_activeOperation = this;
}

GpuGraphSubmissionTransaction::SubmissionOperation::~SubmissionOperation(){
    if(!m_transaction)
        return;

    NWB_ASSERT(s_activeOperation == this);
    s_activeOperation = m_previousOperation;
    if(m_composite)
        m_transaction->m_compositeOperationActive.clear(MemoryOrder::release);
    m_gateLock.release();
}


GpuGraphSubmissionTransaction::~GpuGraphSubmissionTransaction(){
    SubmissionOperation submissionOperation(*this, SubmissionOperationMode::ExclusiveBarrier);
    if(!submissionOperation.valid()){
        NWB_FATAL_ASSERT_MSG(false, "GpuGraphSubmissionTransaction destruction requires active operations to finish first");
        return;
    }

    ScopedLock lock(m_mutex);
    if(m_activeSubmissionBinding.valid() && (!m_submissionBindingResolved || !allPacketsTerminalLocked())){
        NWB_FATAL_ASSERT_MSG(false, "GpuGraphSubmissionTransaction destruction requires its active graph attempt to resolve first");
        return;
    }
}


bool GpuGraphSubmissionTransaction::validForLocked(const GpuCompiledGraph& compiledGraph)const noexcept{
    return m_valid
        && compiledGraph.valid()
        && m_generation == compiledGraph.generation()
        && m_planGeneration == compiledGraph.planGeneration()
        && m_deviceGeneration == compiledGraph.deviceGeneration()
        && m_packets.size() == compiledGraph.packetCount()
        && m_externalResourceHandoffScratch.size() == compiledGraph.externalResourceExportCount()
    ;
}


bool GpuGraphSubmissionTransaction::waitForSubmissionPublicationAndHasAcceptedPacketsWithinSubmissionOperation()const noexcept{
    if(!SubmissionOperation::activeExclusiveFor(*this))
        return false;

    ScopedLock lock(m_mutex);
    return m_valid && m_acceptedSubmissionCount != 0u;
}


void GpuGraphSubmissionTransaction::reset(const GpuCompiledGraph& compiledGraph){
    if(!tryReset(compiledGraph))
        NWB_ASSERT_MSG(false, "GpuGraphSubmissionTransaction::reset requires every owned packet to resolve first");
}


bool GpuGraphSubmissionTransaction::tryReset(const GpuCompiledGraph& compiledGraph){
    SubmissionOperation submissionOperation(*this, SubmissionOperationMode::ExclusiveBarrier);
    if(!submissionOperation.valid())
        return false;

    ScopedLock lock(m_mutex);
    if(m_activeSubmissionBinding.valid() && (!m_submissionBindingResolved || !allPacketsTerminalLocked()))
        return false;
    const GpuPhysicalQueueTopology queueTopology = compiledGraph.queueTopology();
    // Allocate the exact physical-frontier bound before changing the prior transaction so an allocation failure
    // cannot leave reset half-applied and no accepted native submission can grow this storage later.
    m_latestAcceptedQueueTokens.reserve(queueTopology.queueCount);
    m_packets.clear();
    m_latestAcceptedQueueTokens.clear();
    m_externalResourceHandoffScratch.clear();
    m_generation = compiledGraph.generation();
    m_planGeneration = compiledGraph.planGeneration();
    m_recordingAttemptGeneration = 0u;
    m_resetGeneration = GpuTaskGraph::allocateGeneration();
    m_activeSubmissionBinding = {};
    m_submissionBindingResolved = false;
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_acceptedSubmissionCount = 0u;
    m_acceptanceRevision = 0u;
    m_submissionStatistics = {};
    m_valid = compiledGraph.valid();
    if(!m_valid)
        return true;
    m_packets.resize(compiledGraph.packetCount());
    m_externalResourceHandoffScratch.reserve(compiledGraph.externalResourceExportCount());
    for(usize exportIndex = 0u; exportIndex < compiledGraph.externalResourceExportCount(); ++exportIndex){
        const GpuCompiledExternalResourceExport* const exportInfo = compiledGraph.externalResourceExportAt(exportIndex);
        if(!exportInfo || !exportInfo->resource.valid()){
            m_packets.clear();
            m_externalResourceHandoffScratch.clear();
            m_valid = false;
            return true;
        }
        ExternalResourceHandoffScratch& scratch = m_externalResourceHandoffScratch.emplace_back(m_arena);
        scratch.resource = exportInfo->resource;
    }
    m_submissionStatistics.graphGeneration = m_generation;
    m_submissionStatistics.planGeneration = m_planGeneration;
    m_submissionStatistics.deviceGeneration = m_deviceGeneration;
    m_acceptanceRevision = __hidden_packet_runtime_transaction::AllocateAcceptanceRevision();
    return true;
}

bool GpuGraphSubmissionTransaction::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    ScopedLock lock(m_mutex);
    return validForLocked(compiledGraph);
}


bool GpuGraphSubmissionTransaction::hasAcceptedPackets()const noexcept{
    ScopedLock lock(m_mutex);
    return m_valid && m_acceptedSubmissionCount != 0u;
}


GpuTaskGraphSubmissionStatistics GpuGraphSubmissionTransaction::submissionStatistics()const noexcept{
    ScopedLock lock(m_mutex);
    return m_submissionStatistics;
}

GpuTaskGraphPacketSubmissionStatistics GpuGraphSubmissionTransaction::packetSubmissionStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID
)const noexcept{
    ScopedLock lock(m_mutex);
    if(
        !validForLocked(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || packetID.index >= m_packets.size()
    )
        return {};

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuPhysicalQueueInfo* const queueInfo = compiledGraph.queueInfo(packet.queue);
    const PacketRuntime& runtime = m_packets[packetID.index];
    if(
        !queueInfo
        || queueInfo->queueClass >= CommandQueue::kCount
        || runtime.state != PacketRuntimeState::Accepted
        || !runtime.token.valid()
        || runtime.token.queue != queueInfo->queueClass
        || !runtime.token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)
    )
        return {};

    return GpuTaskGraphPacketSubmissionStatistics{
        .graphGeneration = m_generation,
        .planGeneration = m_planGeneration,
        .recordingAttemptGeneration = m_recordingAttemptGeneration,
        .deviceGeneration = m_deviceGeneration,
        .packet = packetID,
        .queue = packet.queue,
        .queueClass = queueInfo->queueClass,
        .taskCount = packet.taskCount,
        .nativeCommandListCount = runtime.nativeCommandListCount,
        .plannedWaitTokenCount = runtime.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = runtime.sameQueueWaitElisionCount,
        .timelineWaitCount = runtime.timelineWaitCount,
        .mergedTimelineWaitCount = runtime.mergedTimelineWaitCount,
        .submissionSeconds = runtime.submissionSeconds,
        .joinsAcceptedQueueFrontier = packet.joinsAcceptedQueueFrontier,
        .isRecoverySubmission = packet.isRecoverySubmission,
    };
}

GpuTaskGraphPhysicalQueueSubmissionStatistics GpuGraphSubmissionTransaction::physicalQueueSubmissionStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuPhysicalQueueId& queue
)const noexcept{
    ScopedLock lock(m_mutex);
    if(!validForLocked(compiledGraph))
        return {};

    const GpuPhysicalQueueInfo* const queueInfo = compiledGraph.queueInfo(queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::kCount)
        return {};

    GpuTaskGraphPhysicalQueueSubmissionStatistics statistics{
        .graphGeneration = m_generation,
        .planGeneration = m_planGeneration,
        .recordingAttemptGeneration = m_recordingAttemptGeneration,
        .deviceGeneration = m_deviceGeneration,
        .queue = queue,
        .queueClass = queueInfo->queueClass,
    };
    for(usize packetIndex = 0u; packetIndex < m_packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(packetIndex);
        if(!packetID.valid())
            return {};

        const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
        if(packet.queue != queue)
            continue;

        const PacketRuntime& runtime = m_packets[packetIndex];
        if(runtime.nativeSubmissionRejected && runtime.state != PacketRuntimeState::Rejected)
            return {};

        switch(runtime.state){
        case PacketRuntimeState::Accepted:
            ++statistics.acceptedPacketCount;
            statistics.acceptedTaskCount += packet.taskCount;
            ++statistics.nativeSubmissionCount;
            statistics.nativeCommandListCount += runtime.nativeCommandListCount;
            statistics.plannedWaitTokenCount += runtime.plannedWaitTokenCount;
            statistics.sameQueueWaitElisionCount += runtime.sameQueueWaitElisionCount;
            statistics.timelineWaitCount += runtime.timelineWaitCount;
            statistics.mergedTimelineWaitCount += runtime.mergedTimelineWaitCount;
            statistics.submissionSeconds += runtime.submissionSeconds;
            if(packet.joinsAcceptedQueueFrontier)
                ++statistics.acceptedFrontierSubmissionCount;
            if(packet.isRecoverySubmission)
                ++statistics.recoverySubmissionCount;
            break;
        case PacketRuntimeState::Rejected:
            ++statistics.rejectedPacketCount;
            statistics.rejectedTaskCount += packet.taskCount;
            if(runtime.nativeSubmissionRejected)
                ++statistics.rejectedSubmissionCount;
            break;
        default:
            break;
        }
    }
    return statistics;
}

bool GpuGraphSubmissionTransaction::allPacketsTerminalLocked()const noexcept{
    if(m_packets.empty())
        return false;
    for(const PacketRuntime& runtime : m_packets){
        if(
            runtime.state != PacketRuntimeState::Accepted
            && runtime.state != PacketRuntimeState::Rejected
        )
            return false;
    }
    return true;
}


bool GpuGraphSubmissionTransaction::bindRecordingAttemptWithinSubmissionOperation(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
)noexcept{
    if(!SubmissionOperation::activeFor(*this) || recordingAttemptGeneration == 0u)
        return false;

    ScopedLock lock(m_mutex);
    const GpuGraphSubmissionBinding submissionBinding(m_transactionIdentity, m_resetGeneration);
    if(
        !validForLocked(compiledGraph)
        || !submissionBinding.valid()
        || (
            m_recordingAttemptGeneration != 0u
            && m_recordingAttemptGeneration != recordingAttemptGeneration
        )
        || (m_activeSubmissionBinding.valid() && m_activeSubmissionBinding != submissionBinding)
        || !graph.bindSubmissionTransaction(compiledGraph, recordingAttemptGeneration, submissionBinding)
    )
        return false;
    if(!m_activeSubmissionBinding.valid())
        m_submissionBindingResolved = false;
    m_recordingAttemptGeneration = recordingAttemptGeneration;
    m_activeSubmissionBinding = submissionBinding;
    m_submissionStatistics.recordingAttemptGeneration = recordingAttemptGeneration;
    return true;
}

bool GpuGraphSubmissionTransaction::matchesRecordingAttemptBinding(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    ScopedLock lock(m_mutex);
    return recordingAttemptGeneration != 0u
        && validForLocked(compiledGraph)
        && m_recordingAttemptGeneration == recordingAttemptGeneration
        && m_activeSubmissionBinding == submissionBinding
        && graph.matchesSubmissionTransaction(
            compiledGraph,
            recordingAttemptGeneration,
            submissionBinding
        )
    ;
}


void GpuGraphSubmissionTransaction::resolveSubmissionBindingIfTerminalLocked(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph
)noexcept{
    if(
        !m_activeSubmissionBinding.valid()
        || m_submissionBindingResolved
        || !allPacketsTerminalLocked()
    )
        return;
    m_submissionBindingResolved = graph.resolveSubmissionTransaction(
        compiledGraph,
        m_recordingAttemptGeneration,
        m_activeSubmissionBinding
    );
    NWB_ASSERT_MSG(m_submissionBindingResolved, "terminal transaction packets must resolve their exact graph binding");
}

bool GpuGraphSubmissionTransaction::beginPacketSubmission(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    const u64 recordingAttemptGeneration,
    GpuTaskGraph::PacketSubmissionLease& outLease
)noexcept{
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || outLease.valid()
        || !graph.packetReadyForSubmission(
            compiledGraph,
            packetID,
            recordingAttemptGeneration
        )
        || !bindRecordingAttemptWithinSubmissionOperation(graph, compiledGraph, recordingAttemptGeneration)
    )
        return false;

    GpuGraphSubmissionBinding submissionBinding;
    {
        ScopedLock lock(m_mutex);
        if(
            !validForLocked(compiledGraph)
            || m_recordingAttemptGeneration != recordingAttemptGeneration
            || packetID.index >= m_packets.size()
        )
            return false;
        PacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state != PacketRuntimeState::Declared)
            return false;
        runtime.state = PacketRuntimeState::Submitting;
        submissionBinding = m_activeSubmissionBinding;
    }

    if(!graph.beginPacketSubmission(
        compiledGraph,
        packetID,
        recordingAttemptGeneration,
        submissionBinding,
        outLease
    )){
        ScopedLock lock(m_mutex);
        if(validForLocked(compiledGraph) && packetID.index < m_packets.size()){
            PacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == PacketRuntimeState::Submitting)
                runtime.state = PacketRuntimeState::Declared;
        }
        return false;
    }
    return true;
}

bool GpuGraphSubmissionTransaction::acceptSubmittingPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    const QueueSubmissionToken& token,
    GpuTaskGraph::PacketSubmissionLease& lease,
    const NativeSubmissionInfo& nativeSubmissionInfo,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount
)noexcept{
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || !token.valid()
        || !lease.valid()
        || lease.m_packet != packetID
        || lease.m_planGeneration != compiledGraph.planGeneration()
        || !matchesRecordingAttemptBinding(
            graph,
            compiledGraph,
            lease.m_recordingAttemptGeneration,
            lease.m_submissionBinding
        )
    )
        return false;

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuPhysicalQueueInfo* const queueInfo = compiledGraph.queueInfo(packet.queue);
    if(
        !queueInfo
        || queueInfo->queueClass >= CommandQueue::kCount
        || token.queue != queueInfo->queueClass
        || !token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)
    )
        return false;

    if(!graph.completePacketSubmission(
        compiledGraph,
        packetID,
        token,
        lease
    ))
        return false;

    // Compatibility callbacks are synchronous publication obligations. Graph typed accepted hooks and the final
    // Accepted lifecycle transition have completed, while the transaction token/frontier remains hidden. Invoke
    // every matching task callback in compiled order even after an earlier false result; publication below is
    // unconditional because the native submission has already accepted.
    bool callbacksAccepted = true;
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        for(usize callbackIndex = 0u; callbackIndex < taskAcceptedCallbackCount; ++callbackIndex){
            const GpuTaskGraphTaskAcceptedCallback& callback = taskAcceptedCallbacks[callbackIndex];
            if(callback.task == tasks[taskIndex] && !callback.invoke(callback.context, token))
                callbacksAccepted = false;
        }
    }

    ScopedLock lock(m_mutex);
    // reset() and cancellation cannot cross a graph-owned submission lease. Once the graph
    // publishes accepted callbacks, this transaction resolution is therefore an invariant rather than a second
    // failure point.
    NWB_ASSERT(validForLocked(compiledGraph));
    NWB_ASSERT(packetID.index < m_packets.size());
    PacketRuntime& runtime = m_packets[packetID.index];
    NWB_ASSERT(runtime.state == PacketRuntimeState::Submitting);
    runtime.state = PacketRuntimeState::Accepted;
    runtime.token = token;
    runtime.nativeCommandListCount = nativeSubmissionInfo.commandListCount;
    runtime.plannedWaitTokenCount = nativeSubmissionInfo.plannedWaitTokenCount;
    runtime.sameQueueWaitElisionCount = nativeSubmissionInfo.sameQueueWaitElisionCount;
    runtime.timelineWaitCount = nativeSubmissionInfo.timelineWaitCount;
    runtime.mergedTimelineWaitCount = nativeSubmissionInfo.mergedTimelineWaitCount;
    runtime.submissionSeconds = nativeSubmissionInfo.submissionSeconds;

    ++m_acceptedSubmissionCount;
    if(m_acceptedSubmissionCount == 0u)
        ++m_acceptedSubmissionCount;
    m_acceptanceRevision = __hidden_packet_runtime_transaction::AllocateAcceptanceRevision();

    ++m_submissionStatistics.acceptedPacketCount;
    m_submissionStatistics.acceptedTaskCount += packet.taskCount;
    ++m_submissionStatistics.nativeSubmissionCount;
    m_submissionStatistics.nativeCommandListCount += nativeSubmissionInfo.commandListCount;
    m_submissionStatistics.plannedWaitTokenCount += nativeSubmissionInfo.plannedWaitTokenCount;
    m_submissionStatistics.sameQueueWaitElisionCount += nativeSubmissionInfo.sameQueueWaitElisionCount;
    m_submissionStatistics.timelineWaitCount += nativeSubmissionInfo.timelineWaitCount;
    m_submissionStatistics.mergedTimelineWaitCount += nativeSubmissionInfo.mergedTimelineWaitCount;
    m_submissionStatistics.submissionSeconds += nativeSubmissionInfo.submissionSeconds;
    if(packet.joinsAcceptedQueueFrontier)
        ++m_submissionStatistics.acceptedFrontierSubmissionCount;
    if(packet.isRecoverySubmission)
        ++m_submissionStatistics.recoverySubmissionCount;

    const usize queueClassIndex = static_cast<usize>(queueInfo->queueClass);
    NWB_ASSERT(queueClassIndex < GpuTaskGraphSubmissionStatistics::s_QueueClassCount);
    if(queueClassIndex < GpuTaskGraphSubmissionStatistics::s_QueueClassCount){
        ++m_submissionStatistics.nativeSubmissionCountByQueueClass[queueClassIndex];
        m_submissionStatistics.nativeCommandListCountByQueueClass[queueClassIndex] += nativeSubmissionInfo.commandListCount;
        m_submissionStatistics.timelineWaitCountByQueueClass[queueClassIndex] += nativeSubmissionInfo.timelineWaitCount;
    }

    bool foundLatestQueue = false;
    for(LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == packet.queue){
            const bool latestMatchesQueue = latest.token.valid()
                && latest.token.queue == token.queue
                && latest.token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)
            ;
            NWB_ASSERT_MSG(latestMatchesQueue, "accepted queue frontier token must preserve its exact physical queue identity");
            if(!latestMatchesQueue || token.value > latest.token.value)
                latest.token = token;
            foundLatestQueue = true;
            break;
        }
    }
    if(!foundLatestQueue){
        NWB_ASSERT(m_latestAcceptedQueueTokens.size() < m_latestAcceptedQueueTokens.capacity());
        m_latestAcceptedQueueTokens.push_back(LatestAcceptedQueueToken{
            .queue = packet.queue,
            .token = token,
        });
    }
    resolveSubmissionBindingIfTerminalLocked(graph, compiledGraph);
    return callbacksAccepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

