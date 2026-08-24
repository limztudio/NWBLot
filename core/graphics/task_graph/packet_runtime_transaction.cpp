// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local const GpuGraphSubmissionTransaction::SubmissionOperation*
    GpuGraphSubmissionTransaction::SubmissionOperation::s_activeOperation = nullptr;


GpuGraphSubmissionTransaction::SubmissionOperation::SubmissionOperation(
    const GpuGraphSubmissionTransaction& transaction
)noexcept{
    for(const SubmissionOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_transaction == &transaction)
            return;
    }

    // Top-level callers wait for the transaction publication boundary. Nested work on another transaction may use
    // an uncontended gate, but never waits while retaining its outer gate and therefore cannot form an ABBA cycle.
    if(s_activeOperation){
        if(!transaction.m_submissionMutex.try_lock())
            return;
    }
    else if(!transaction.m_submissionMutex.try_lock()){
#if !defined(NWB_FINAL)
        transaction.m_submissionWaiterCount.fetch_add(1u, MemoryOrder::release);
#endif
        transaction.m_submissionMutex.lock();
#if !defined(NWB_FINAL)
        transaction.m_submissionWaiterCount.fetch_sub(1u, MemoryOrder::release);
#endif
    }
    m_transaction = &transaction;
    m_previousOperation = s_activeOperation;
    s_activeOperation = this;
}

GpuGraphSubmissionTransaction::SubmissionOperation::~SubmissionOperation(){
    if(!m_transaction)
        return;

    NWB_ASSERT(s_activeOperation == this);
    s_activeOperation = m_previousOperation;
    m_transaction->m_submissionMutex.unlock();
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


bool GpuGraphSubmissionTransaction::waitForSubmissionPublicationAndHasAcceptedPackets()const noexcept{
    SubmissionOperation submissionOperation(*this);
    if(!submissionOperation.valid())
        return false;

    ScopedLock lock(m_mutex);
    return m_valid && m_acceptedSubmissionCount != 0u;
}


void GpuGraphSubmissionTransaction::reset(const GpuCompiledGraph& compiledGraph){
    SubmissionOperation submissionOperation(*this);
    if(!submissionOperation.valid())
        return;

    ScopedLock lock(m_mutex);
    for(const GpuPacketRuntime& runtime : m_packets){
        if(
            runtime.state == GpuPacketRuntimeState::Submitting
            || runtime.state == GpuPacketRuntimeState::Rejecting
        ){
            NWB_ASSERT_MSG(false, "GpuGraphSubmissionTransaction::reset requires every native submission/cancellation to resolve first");
            return;
        }
    }
    m_packets.clear();
    m_latestAcceptedQueueTokens.clear();
    m_externalResourceHandoffScratch.clear();
    m_generation = compiledGraph.generation();
    m_planGeneration = compiledGraph.planGeneration();
    m_recordingAttemptGeneration = 0u;
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_acceptedSubmissionCount = 0u;
    m_submissionStatistics = {};
    m_valid = compiledGraph.valid();
    if(!m_valid)
        return;
    m_packets.resize(compiledGraph.packetCount());
    m_externalResourceHandoffScratch.reserve(compiledGraph.externalResourceExportCount());
    for(usize exportIndex = 0u; exportIndex < compiledGraph.externalResourceExportCount(); ++exportIndex){
        const GpuCompiledExternalResourceExport* const exportInfo = compiledGraph.externalResourceExportAt(exportIndex);
        if(!exportInfo || !exportInfo->resource.valid()){
            m_packets.clear();
            m_externalResourceHandoffScratch.clear();
            m_valid = false;
            return;
        }
        ExternalResourceHandoffScratch& scratch = m_externalResourceHandoffScratch.emplace_back(m_arena);
        scratch.resource = exportInfo->resource;
    }
    m_submissionStatistics.graphGeneration = m_generation;
    m_submissionStatistics.planGeneration = m_planGeneration;
    m_submissionStatistics.deviceGeneration = m_deviceGeneration;
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

        const GpuPacketRuntime& runtime = m_packets[packetIndex];
        if(
            (runtime.hasNativeSubmission && runtime.state != GpuPacketRuntimeState::Accepted)
            || (runtime.nativeSubmissionRejected && runtime.state != GpuPacketRuntimeState::Rejected)
        )
            return {};

        switch(runtime.state){
        case GpuPacketRuntimeState::Accepted:
            ++statistics.acceptedPacketCount;
            statistics.acceptedTaskCount += packet.taskCount;
            if(!runtime.hasNativeSubmission)
                break;

            ++statistics.nativeSubmissionCount;
            statistics.nativeCommandListCount += runtime.nativeCommandListCount;
            statistics.plannedWaitTokenCount += runtime.plannedWaitTokenCount;
            statistics.sameQueueWaitElisionCount += runtime.sameQueueWaitElisionCount;
            statistics.timelineWaitCount += runtime.timelineWaitCount;
            statistics.mergedTimelineWaitCount += runtime.mergedTimelineWaitCount;
            statistics.submissionSeconds += runtime.submissionSeconds;
            if(packet.joinsAcceptedQueueFrontier)
                ++statistics.acceptedFrontierSubmissionCount;
            break;
        case GpuPacketRuntimeState::Rejected:
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

bool GpuGraphSubmissionTransaction::bindRecordingAttempt(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
)noexcept{
    if(
        recordingAttemptGeneration == 0u
        || !graph.matchesRecordingAttempt(compiledGraph, recordingAttemptGeneration)
    )
        return false;

    ScopedLock lock(m_mutex);
    if(
        !validForLocked(compiledGraph)
        || (
            m_recordingAttemptGeneration != 0u
            && m_recordingAttemptGeneration != recordingAttemptGeneration
        )
    )
        return false;
    m_recordingAttemptGeneration = recordingAttemptGeneration;
    m_submissionStatistics.recordingAttemptGeneration = recordingAttemptGeneration;
    return true;
}

bool GpuGraphSubmissionTransaction::matchesRecordedAttempt(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph
)const noexcept{
    ScopedLock lock(m_mutex);
    return m_recordingAttemptGeneration != 0u
        && recordedGraph.validFor(graph, compiledGraph)
        && m_recordingAttemptGeneration == recordedGraph.recordingAttemptGeneration()
    ;
}

bool GpuGraphSubmissionTransaction::markPacketRecorded(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packet,
    const u64 recordingAttemptGeneration
)noexcept{
    if(
        !compiledGraph.validPacket(packet)
        || !graph.packetReadyForSubmission(
            compiledGraph,
            compiledGraph.packetTasks(packet),
            compiledGraph.packet(packet).taskCount,
            recordingAttemptGeneration
        )
        || !bindRecordingAttempt(graph, compiledGraph, recordingAttemptGeneration)
    )
        return false;

    ScopedLock lock(m_mutex);
    if(
        !validForLocked(compiledGraph)
        || !packet.valid()
        || packet.generation != m_planGeneration
        || packet.index >= m_packets.size()
        || m_recordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;
    GpuPacketRuntime& runtime = m_packets[packet.index];
    if(runtime.state != GpuPacketRuntimeState::Declared)
        return false;
    runtime.state = GpuPacketRuntimeState::Recorded;
    return true;
}


bool GpuGraphSubmissionTransaction::beginPacketSubmission(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    const u64 recordingAttemptGeneration,
    GpuTaskPacketSubmissionLease& outLease
)noexcept{
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || outLease.valid()
        || !graph.packetReadyForSubmission(
            compiledGraph,
            compiledGraph.packetTasks(packetID),
            compiledGraph.packet(packetID).taskCount,
            recordingAttemptGeneration
        )
        || !bindRecordingAttempt(graph, compiledGraph, recordingAttemptGeneration)
    )
        return false;

    GpuPacketRuntimeState::Enum previousState = GpuPacketRuntimeState::Declared;
    {
        ScopedLock lock(m_mutex);
        if(
            !validForLocked(compiledGraph)
            || m_recordingAttemptGeneration != recordingAttemptGeneration
            || packetID.index >= m_packets.size()
        )
            return false;
        GpuPacketRuntime& runtime = m_packets[packetID.index];
        if(
            runtime.state != GpuPacketRuntimeState::Declared
            && runtime.state != GpuPacketRuntimeState::Recorded
        )
            return false;
        previousState = runtime.state;
        runtime.state = GpuPacketRuntimeState::Submitting;
    }

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    if(!graph.beginPacketSubmission(
        compiledGraph,
        compiledGraph.packetTasks(packetID),
        packet.taskCount,
        recordingAttemptGeneration,
        outLease
    )){
        ScopedLock lock(m_mutex);
        if(validForLocked(compiledGraph) && packetID.index < m_packets.size()){
            GpuPacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == GpuPacketRuntimeState::Submitting)
                runtime.state = previousState;
        }
        return false;
    }
    return true;
}

bool GpuGraphSubmissionTransaction::acceptPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const QueueSubmissionToken& token
)noexcept{
    SubmissionOperation submissionOperation(*this);
    if(!submissionOperation.valid())
        return false;

    u64 recordingAttemptGeneration = 0u;
    {
        ScopedLock lock(m_mutex);
        recordingAttemptGeneration = m_recordingAttemptGeneration;
    }
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || !token.valid()
        || recordingAttemptGeneration == 0u
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
    GpuTaskPacketSubmissionLease lease;
    if(!beginPacketSubmission(graph, compiledGraph, packetID, recordingAttemptGeneration, lease))
        return false;
    return acceptSubmittingPacket(graph, compiledGraph, packetID, token, lease);
}


bool GpuGraphSubmissionTransaction::acceptSubmittingPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    const QueueSubmissionToken& token,
    GpuTaskPacketSubmissionLease& lease,
    const NativeSubmissionInfo* const nativeSubmissionInfo,
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount
)noexcept{
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || !token.valid()
        || !lease.valid()
        || lease.m_planGeneration != compiledGraph.planGeneration()
        || !bindRecordingAttempt(graph, compiledGraph, lease.m_recordingAttemptGeneration)
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
        compiledGraph.packetTasks(packetID),
        packet.taskCount,
        token,
        lease
    ))
        return false;

    // Compatibility callbacks are synchronous publication obligations. Graph typed accepted hooks and the final
    // Accepted lifecycle transition have completed, while the transaction token/frontier remains hidden. Invoke
    // every matching task callback in compiled order even after an earlier false result; publication below is
    // unconditional because the native submission has already accepted.
    bool callbacksAccepted = true;
    if(acceptedCallback && !acceptedCallback->invoke(acceptedCallback->context, packetID, token))
        callbacksAccepted = false;
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        for(usize callbackIndex = 0u; callbackIndex < taskAcceptedCallbackCount; ++callbackIndex){
            const GpuTaskGraphTaskAcceptedCallback& callback = taskAcceptedCallbacks[callbackIndex];
            if(callback.task == tasks[taskIndex] && !callback.invoke(callback.context, token))
                callbacksAccepted = false;
        }
    }

    ScopedLock lock(m_mutex);
    // reset(), public acceptance, and cancellation cannot cross a graph-owned submission lease. Once the graph
    // publishes accepted callbacks, this transaction resolution is therefore an invariant rather than a second
    // failure point.
    NWB_ASSERT(validForLocked(compiledGraph));
    NWB_ASSERT(packetID.index < m_packets.size());
    GpuPacketRuntime& runtime = m_packets[packetID.index];
    NWB_ASSERT(runtime.state == GpuPacketRuntimeState::Submitting);
    runtime.state = GpuPacketRuntimeState::Accepted;
    runtime.token = token;
    if(nativeSubmissionInfo){
        runtime.nativeCommandListCount = nativeSubmissionInfo->commandListCount;
        runtime.plannedWaitTokenCount = nativeSubmissionInfo->plannedWaitTokenCount;
        runtime.sameQueueWaitElisionCount = nativeSubmissionInfo->sameQueueWaitElisionCount;
        runtime.timelineWaitCount = nativeSubmissionInfo->timelineWaitCount;
        runtime.mergedTimelineWaitCount = nativeSubmissionInfo->mergedTimelineWaitCount;
        runtime.submissionSeconds = nativeSubmissionInfo->submissionSeconds;
        runtime.hasNativeSubmission = true;
    }

    ++m_acceptedSubmissionCount;
    if(m_acceptedSubmissionCount == 0u)
        ++m_acceptedSubmissionCount;

    ++m_submissionStatistics.acceptedPacketCount;
    m_submissionStatistics.acceptedTaskCount += packet.taskCount;
    if(nativeSubmissionInfo){
        ++m_submissionStatistics.nativeSubmissionCount;
        m_submissionStatistics.nativeCommandListCount += nativeSubmissionInfo->commandListCount;
        m_submissionStatistics.plannedWaitTokenCount += nativeSubmissionInfo->plannedWaitTokenCount;
        m_submissionStatistics.sameQueueWaitElisionCount += nativeSubmissionInfo->sameQueueWaitElisionCount;
        m_submissionStatistics.timelineWaitCount += nativeSubmissionInfo->timelineWaitCount;
        m_submissionStatistics.mergedTimelineWaitCount += nativeSubmissionInfo->mergedTimelineWaitCount;
        m_submissionStatistics.submissionSeconds += nativeSubmissionInfo->submissionSeconds;
        if(packet.joinsAcceptedQueueFrontier)
            ++m_submissionStatistics.acceptedFrontierSubmissionCount;

        const usize queueClassIndex = static_cast<usize>(queueInfo->queueClass);
        NWB_ASSERT(queueClassIndex < GpuTaskGraphSubmissionStatistics::s_QueueClassCount);
        if(queueClassIndex < GpuTaskGraphSubmissionStatistics::s_QueueClassCount){
            ++m_submissionStatistics.nativeSubmissionCountByQueueClass[queueClassIndex];
            m_submissionStatistics.nativeCommandListCountByQueueClass[queueClassIndex] += nativeSubmissionInfo->commandListCount;
            m_submissionStatistics.timelineWaitCountByQueueClass[queueClassIndex] += nativeSubmissionInfo->timelineWaitCount;
        }
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
        m_latestAcceptedQueueTokens.push_back(LatestAcceptedQueueToken{
            .queue = packet.queue,
            .token = token,
        });
    }
    return callbacksAccepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

