// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"
#include "scheduler.h"

#include "task_graph.h"

#include <core/common/log.h>
#include <core/graphics/gpu_timing.h>
#include <global/exception.h>
#include <global/termination.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_packet_runtime_transaction{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextAcceptanceRevision{ 1u };


[[nodiscard]] static u64 AllocateAcceptanceRevision()noexcept{
    u64 nextRevision = s_NextAcceptanceRevision.load(MemoryOrder::relaxed);
    while(true){
        if(nextRevision == 0u || nextRevision == Limit<u64>::s_Max){
            NWB_FATAL_ASSERT_MSG(false, "GPU task graph acceptance revision identity exhausted");
            TerminateInvariant();
        }
        if(s_NextAcceptanceRevision.compare_exchange_weak(
            nextRevision,
            nextRevision + 1u,
            MemoryOrder::relaxed,
            MemoryOrder::relaxed
        ))
            return nextRevision;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuGraphSubmissionTransaction::SubmissionWriterReservation::~SubmissionWriterReservation()noexcept{
    reset();
}


bool GpuGraphSubmissionTransaction::SubmissionWriterReservation::acquire(
    const GpuGraphSubmissionTransaction& transaction,
    const bool tryOnly
)noexcept{
    if(m_transaction)
        TerminateInvariant();
    if(tryOnly){
        u32 expectedWriterCount = 0u;
        if(!transaction.m_submissionGateWriterCount.compare_exchange_strong(
            expectedWriterCount,
            1u,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            return false;
    }
    else{
        u32 writerCount = transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire);
        while(true){
            if(writerCount == Limit<u32>::s_Max){
                NWB_FATAL_ASSERT_MSG(false, "GPU graph submission writer ownership overflowed");
                TerminateInvariant();
            }
            if(transaction.m_submissionGateWriterCount.compare_exchange_weak(
                writerCount,
                writerCount + 1u,
                MemoryOrder::acq_rel,
                MemoryOrder::acquire
            ))
                break;
        }
    }
    m_transaction = &transaction;
    return true;
}

void GpuGraphSubmissionTransaction::SubmissionWriterReservation::reset()noexcept{
    if(!m_transaction)
        return;
    const u32 previousWriterCount = m_transaction->m_submissionGateWriterCount.fetch_sub(1u, MemoryOrder::release);
    if(previousWriterCount == 0u){
        NWB_FATAL_ASSERT_MSG(false, "GPU graph submission writer ownership underflowed");
        TerminateInvariant();
    }
    if(previousWriterCount == 1u)
        m_transaction->m_submissionGateWriterCount.notify_all();
    m_transaction = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuGraphSubmissionTransaction::SubmissionOperation* GpuGraphSubmissionTransaction::SubmissionOperation::s_activeOperation = nullptr;


GpuGraphSubmissionTransaction::SubmissionOperation::SubmissionOperation(
    const GpuGraphSubmissionTransaction& transaction,
    const SubmissionOperationMode mode,
    const GpuRecordedGraph::ArtifactOperation* const borrowedArtifact
)noexcept{
    constexpr u32 writerBit = 1u << 31u;
    constexpr u32 readerMask = writerBit - 1u;
    for(const SubmissionOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_transaction == &transaction)
            return;
    }
    // Cross-transaction reentry is a prompt rejection regardless of target availability. An exception finalizer can
    // therefore run after its local gates unwind without retaining an unrelated transaction gate on this thread.
    if(s_activeOperation)
        return;
    if(
        GpuRecordedGraph::ArtifactOperation::active()
        && (
            !borrowedArtifact
            || !GpuRecordedGraph::ArtifactOperation::activeScopeIs(*borrowedArtifact)
        )
    )
        return;
    if(borrowedArtifact && !GpuRecordedGraph::ArtifactOperation::activeScopeIs(*borrowedArtifact))
        return;
    if(transaction.m_compositeOperationActive.test(MemoryOrder::acquire))
        return;

    const bool composite = mode == SubmissionOperationMode::CompositeBarrier;
    const bool tryExclusive = mode == SubmissionOperationMode::TryExclusiveBarrier;
    const bool exceptionFinalizer = mode == SubmissionOperationMode::ExceptionFinalizer;
    const bool exceptionClosing = transaction.m_submissionExceptionClosing.test(MemoryOrder::acquire);
    if(exceptionClosing != exceptionFinalizer)
        return;
    const bool exclusive = mode != SubmissionOperationMode::OrdinaryPacket;
    const bool tryWriter = tryExclusive;
    if(exclusive){
        if(!m_writerReservation.acquire(transaction, tryWriter))
            return;

        u32 expectedState = 0u;
        while(!transaction.m_submissionGateState.compare_exchange_strong(
            expectedState,
            writerBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        )){
            if(tryWriter){
                m_writerReservation.reset();
                return;
            }
            transaction.m_submissionGateState.wait(expectedState, MemoryOrder::acquire);
            expectedState = 0u;
        }
    }
    else{
        while(true){
            const u32 writerCount = transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire);
            if(writerCount != 0u){
                transaction.m_submissionGateWriterCount.wait(writerCount, MemoryOrder::acquire);
                continue;
            }
            u32 operationState = transaction.m_submissionGateState.load(MemoryOrder::acquire);
            if((operationState & writerBit) != 0u){
                transaction.m_submissionGateState.wait(operationState, MemoryOrder::acquire);
                continue;
            }
            if((operationState & readerMask) == readerMask){
                NWB_FATAL_ASSERT_MSG(false, "GPU graph submission reader ownership overflowed");
                TerminateInvariant();
            }
            if(!transaction.m_submissionGateState.compare_exchange_weak(
                operationState,
                operationState + 1u,
                MemoryOrder::acq_rel,
                MemoryOrder::acquire
            ))
                continue;
            if(transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire) == 0u)
                break;
            const u32 previousState = transaction.m_submissionGateState.fetch_sub(1u, MemoryOrder::release);
            if((previousState & readerMask) == 1u)
                transaction.m_submissionGateState.notify_all();
        }
    }
    const bool closingAfterAdmission = transaction.m_submissionExceptionClosing.test(MemoryOrder::acquire);
    if(closingAfterAdmission != exceptionFinalizer){
        if(exclusive){
            transaction.m_submissionGateState.store(0u, MemoryOrder::release);
            transaction.m_submissionGateState.notify_all();
            m_writerReservation.reset();
        }
        else{
            const u32 previousState = transaction.m_submissionGateState.fetch_sub(1u, MemoryOrder::release);
            if((previousState & readerMask) == 0u)
                TerminateInvariant();
            if((previousState & readerMask) == 1u)
                transaction.m_submissionGateState.notify_all();
        }
        return;
    }
    if(composite && transaction.m_compositeOperationActive.test_and_set(MemoryOrder::acq_rel)){
        if(exclusive){
            transaction.m_submissionGateState.store(0u, MemoryOrder::release);
            transaction.m_submissionGateState.notify_all();
            m_writerReservation.reset();
        }
        return;
    }
    m_transaction = &transaction;
    m_previousOperation = s_activeOperation;
    m_exclusive = exclusive;
    m_composite = composite;
    s_activeOperation = this;
}

GpuGraphSubmissionTransaction::SubmissionOperation::~SubmissionOperation()noexcept{
    if(!m_transaction)
        return;

    if(s_activeOperation != this){
        NWB_FATAL_ASSERT_MSG(false, "GPU graph submission operations must unwind in lexical order");
        TerminateInvariant();
    }
    s_activeOperation = m_previousOperation;
    if(m_composite)
        m_transaction->m_compositeOperationActive.clear(MemoryOrder::release);
    if(m_exclusive){
        if(m_transaction->m_submissionGateState.load(MemoryOrder::acquire) != (1u << 31u)){
            NWB_FATAL_ASSERT_MSG(false, "GPU graph submission writer operation lost its exact gate claim");
            TerminateInvariant();
        }
        m_transaction->m_submissionGateState.store(0u, MemoryOrder::release);
        m_transaction->m_submissionGateState.notify_all();
        m_writerReservation.reset();
    }
    else{
        const u32 previousState = m_transaction->m_submissionGateState.fetch_sub(1u, MemoryOrder::release);
        if((previousState & ((1u << 31u) - 1u)) == 0u){
            NWB_FATAL_ASSERT_MSG(false, "GPU graph submission reader ownership underflowed");
            TerminateInvariant();
        }
        if((previousState & ((1u << 31u) - 1u)) == 1u)
            m_transaction->m_submissionGateState.notify_all();
    }
}


class GpuGraphSubmissionTransaction::AcceptedPacketPublicationGuard final : NoCopy{
public:
    AcceptedPacketPublicationGuard(
        GpuGraphSubmissionTransaction& transaction,
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketId packet,
        const QueueSubmissionToken& token,
        GpuTaskGraph::PacketSubmissionLease& lease,
        const NativeSubmissionInfo& nativeSubmissionInfo,
        GpuTimingSubmissionTicket* const* const timingTickets,
        const usize timingTicketCount
    )noexcept
        : m_transaction(transaction)
        , m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_lease(lease)
        , m_token(token)
        , m_nativeSubmissionInfo(nativeSubmissionInfo)
        , m_timingTickets(timingTickets)
        , m_packet(packet)
        , m_timingTicketCount(timingTicketCount)
    {}
    ~AcceptedPacketPublicationGuard()noexcept{
        if(!m_active)
            return;
        m_transaction.abandonTimingTicketsWithoutCallbacks(m_timingTickets, m_timingTicketCount);
        publish();
    }

public:
    void complete()noexcept{
        if(!m_active){
            NWB_FATAL_ASSERT_MSG(false, "accepted packet publication guard may complete exactly once");
            TerminateInvariant();
        }
        publish();
    }

private:
    void publish()noexcept{
        // Graph lifecycle completion deliberately precedes the transaction-token commit: the last transaction
        // packet resolves its graph binding only after every graph task is terminal. Graph lifecycle is private;
        // public transaction queries serialize on m_mutex and therefore observe either Submitting or the complete
        // accepted token/statistics publication, never a partially written transaction record.
        m_graph.completePacketSubmissionAcceptance(m_compiledGraph, m_planAccess, m_packet, m_lease);
        m_transaction.commitAcceptedPacket(m_graph, m_compiledGraph, m_packet, m_token, m_nativeSubmissionInfo);
        m_active = false;
    }

private:
    GpuGraphSubmissionTransaction& m_transaction;
    GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuTaskGraph::PacketSubmissionLease& m_lease;
    const QueueSubmissionToken m_token;
    const NativeSubmissionInfo m_nativeSubmissionInfo;
    GpuTimingSubmissionTicket* const* const m_timingTickets = nullptr;
    const GpuSubmissionPacketId m_packet;
    const usize m_timingTicketCount = 0u;
    bool m_active = true;
};


GpuGraphSubmissionTransaction::~GpuGraphSubmissionTransaction()noexcept{
    SubmissionOperation submissionOperation(*this, SubmissionOperationMode::WaitExclusiveBarrier);
    if(!submissionOperation.valid()){
        NWB_FATAL_ASSERT_MSG(false, "GpuGraphSubmissionTransaction destruction requires active operations to finish first");
        TerminateInvariant();
    }

    NothrowScopedLock lock(m_mutex);
    if(m_activeSubmissionBinding.valid() && (!m_submissionBindingResolved || !allPacketsTerminalLocked())){
        NWB_FATAL_ASSERT_MSG(false, "GpuGraphSubmissionTransaction destruction requires its active graph attempt to resolve first");
        TerminateInvariant();
    }
}


bool GpuGraphSubmissionTransaction::validForLocked(const GpuCompiledGraph::ReadView& planAccess)const noexcept{
    return m_valid
        && planAccess.valid()
        && m_generation == planAccess.generation()
        && m_planGeneration == planAccess.planGeneration()
        && m_deviceGeneration == planAccess.deviceGeneration()
        && m_packets.size() == planAccess.packetCount()
    ;
}


bool GpuGraphSubmissionTransaction::hasUnresolvedSubmissionBinding(
    const GpuCompiledGraph& compiledGraph
)const noexcept{
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    NothrowScopedLock lock(m_mutex);
    return validForLocked(planAccess)
        && m_recordingAttemptGeneration != 0u
        && m_activeSubmissionBinding.valid()
        && !m_submissionBindingResolved
    ;
}


bool GpuGraphSubmissionTransaction::waitForSubmissionPublicationAndHasAcceptedPacketsWithinSubmissionOperation()const noexcept{
    if(!SubmissionOperation::activeExclusiveFor(*this))
        return false;

    NothrowScopedLock lock(m_mutex);
    return m_valid && m_acceptedSubmissionCount != 0u;
}


void GpuGraphSubmissionTransaction::reset(const GpuCompiledGraph& compiledGraph){
    if(tryReset(compiledGraph))
        return;

    NWB_FATAL_ASSERT_MSG(false, "GpuGraphSubmissionTransaction::reset requires every owned packet to resolve first");
    TerminateInvariant();
}


bool GpuGraphSubmissionTransaction::tryReset(const GpuCompiledGraph& compiledGraph){
    SubmissionOperation submissionOperation(*this, SubmissionOperationMode::TryExclusiveBarrier);
    if(!submissionOperation.valid())
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;

    {
        NothrowScopedLock lock(m_mutex);
        if(m_activeSubmissionBinding.valid() && (!m_submissionBindingResolved || !allPacketsTerminalLocked()))
            return false;
    }

    const GpuPhysicalQueueTopology queueTopology = planAccess.queueTopology();
    GraphicsVector<PacketRuntime> nextPackets(m_arena);
    GraphicsVector<LatestAcceptedQueueToken> nextLatestAcceptedQueueTokens(m_arena);
    bool nextValid = planAccess.valid();
    if(nextValid){
        nextPackets.resize(planAccess.packetCount());
        nextLatestAcceptedQueueTokens.reserve(queueTopology.queueCount);
    }

    const u64 nextGeneration = planAccess.generation();
    const u64 nextPlanGeneration = planAccess.planGeneration();
    const u64 nextResetGeneration = GpuTaskGraph::allocateGeneration();
    const u16 nextDeviceGeneration = planAccess.deviceGeneration();
    const u64 nextAcceptanceRevision = nextValid
        ? __hidden_packet_runtime_transaction::AllocateAcceptanceRevision()
        : 0u
    ;
    GpuTaskGraphSubmissionStatistics nextSubmissionStatistics;
    if(nextValid){
        nextSubmissionStatistics.graphGeneration = nextGeneration;
        nextSubmissionStatistics.planGeneration = nextPlanGeneration;
        nextSubmissionStatistics.deviceGeneration = nextDeviceGeneration;
    }

    static_assert(noexcept(m_packets = Move(nextPackets)), "packet reset publication must be non-throwing");
    static_assert(
        noexcept(m_latestAcceptedQueueTokens = Move(nextLatestAcceptedQueueTokens)),
        "accepted queue-frontier reset publication must be non-throwing"
    );
    static_assert(IsNothrowMoveConstructible_V<LatestAcceptedQueueToken>);
    static_assert(IsNothrowDestructible_V<PacketRuntime>);

    NothrowScopedLock lock(m_mutex);
    m_packets = Move(nextPackets);
    m_latestAcceptedQueueTokens = Move(nextLatestAcceptedQueueTokens);
    m_generation = nextGeneration;
    m_planGeneration = nextPlanGeneration;
    m_recordingAttemptGeneration = 0u;
    m_resetGeneration = nextResetGeneration;
    m_activeSubmissionBinding = {};
    m_submissionBindingResolved = false;
    m_deviceGeneration = nextDeviceGeneration;
    m_acceptedSubmissionCount = 0u;
    m_acceptanceRevision = nextAcceptanceRevision;
    m_submissionStatistics = nextSubmissionStatistics;
    m_valid = nextValid;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuGraphSubmissionTransaction::validFor(const GpuCompiledGraph::ReadView& planAccess)const noexcept{
    NothrowScopedLock lock(m_mutex);
    return validForLocked(planAccess);
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


bool GpuGraphSubmissionTransaction::beginSubmissionExceptionClosingWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    u64& outRecordingAttemptGeneration,
    GpuGraphSubmissionBinding& outSubmissionBinding
)noexcept{
    outRecordingAttemptGeneration = 0u;
    outSubmissionBinding = {};
    if(!SubmissionOperation::activeFor(*this))
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;

    NothrowScopedLock lock(m_mutex);
    if(
        !validForLocked(planAccess)
        || m_recordingAttemptGeneration == 0u
        || !m_activeSubmissionBinding.valid()
        || m_submissionBindingResolved
    )
        return false;
    if(m_submissionExceptionClosing.test(MemoryOrder::acquire)){
        if(
            m_exceptionClosingRecordingAttemptGeneration != m_recordingAttemptGeneration
            || m_exceptionClosingBinding != m_activeSubmissionBinding
        )
            TerminateInvariant();
        outRecordingAttemptGeneration = m_exceptionClosingRecordingAttemptGeneration;
        outSubmissionBinding = m_exceptionClosingBinding;
        return true;
    }

    m_exceptionClosingRecordingAttemptGeneration = m_recordingAttemptGeneration;
    m_exceptionClosingBinding = m_activeSubmissionBinding;
    if(m_submissionExceptionClosing.test_and_set(MemoryOrder::release))
        TerminateInvariant();
    if(!graph.beginSubmissionExceptionClosing(
        compiledGraph,
        m_exceptionClosingRecordingAttemptGeneration,
        m_exceptionClosingBinding
    )){
        m_submissionExceptionClosing.clear(MemoryOrder::release);
        m_submissionExceptionClosing.notify_all();
        m_exceptionClosingRecordingAttemptGeneration = 0u;
        m_exceptionClosingBinding = {};
        return false;
    }
    outRecordingAttemptGeneration = m_exceptionClosingRecordingAttemptGeneration;
    outSubmissionBinding = m_exceptionClosingBinding;
    return true;
}

bool GpuGraphSubmissionTransaction::submissionExceptionClosingResolved(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)noexcept{
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    NothrowScopedLock lock(m_mutex);
    if(
        !m_submissionExceptionClosing.test(MemoryOrder::acquire)
        || m_exceptionClosingRecordingAttemptGeneration != recordingAttemptGeneration
        || m_exceptionClosingBinding != submissionBinding
    )
        return true;
    if(
        m_recordingAttemptGeneration != recordingAttemptGeneration
        || m_activeSubmissionBinding != submissionBinding
    )
        TerminateInvariant();
    if(!m_submissionBindingResolved){
        if(!validForLocked(planAccess))
            TerminateInvariant();
        return false;
    }

    m_exceptionClosingRecordingAttemptGeneration = 0u;
    m_exceptionClosingBinding = {};
    m_submissionExceptionClosing.clear(MemoryOrder::release);
    m_submissionExceptionClosing.notify_all();
    return true;
}

void GpuGraphSubmissionTransaction::completeSubmissionExceptionClosingWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)noexcept{
    if(!SubmissionOperation::activeExclusiveFor(*this))
        TerminateInvariant();
    {
        NothrowScopedLock lock(m_mutex);
        const bool closureValid = m_submissionExceptionClosing.test(MemoryOrder::acquire)
            && planAccess.validFor(compiledGraph)
            && validForLocked(planAccess)
            && m_recordingAttemptGeneration == recordingAttemptGeneration
            && m_activeSubmissionBinding == submissionBinding
            && m_exceptionClosingRecordingAttemptGeneration == recordingAttemptGeneration
            && m_exceptionClosingBinding == submissionBinding
        ;
        if(!closureValid)
            TerminateInvariant();
        if(m_submissionBindingResolved){
            m_exceptionClosingRecordingAttemptGeneration = 0u;
            m_exceptionClosingBinding = {};
            m_submissionExceptionClosing.clear(MemoryOrder::release);
            m_submissionExceptionClosing.notify_all();
            return;
        }
    }

    abandonUnacceptedPacketsAfterExceptionWithinSubmissionOperation(graph, compiledGraph, planAccess);
    NothrowScopedLock lock(m_mutex);
    if(!m_submissionBindingResolved)
        TerminateInvariant();
    m_exceptionClosingRecordingAttemptGeneration = 0u;
    m_exceptionClosingBinding = {};
    m_submissionExceptionClosing.clear(MemoryOrder::release);
    m_submissionExceptionClosing.notify_all();
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

    // The exact bound plan and terminal transaction packets prove every graph task was accepted or discarded. Keep
    // terminal resolution declaration-storage free because the last packet lease may have released its read claim.
    m_submissionBindingResolved = graph.resolveSubmissionTransaction(
        compiledGraph,
        m_recordingAttemptGeneration,
        m_activeSubmissionBinding
    );
    if(!m_submissionBindingResolved){
        NWB_FATAL_ASSERT_MSG(false, "terminal transaction packets must resolve their exact graph binding");
        TerminateInvariant();
    }
}

bool GpuGraphSubmissionTransaction::beginPacketSubmission(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    const u64 recordingAttemptGeneration,
    GpuTaskGraph::PacketSubmissionLease& outLease
)noexcept{
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(
        !planAccess.valid()
        || !planAccess.validPacket(packetID)
        || outLease.valid()
        || !graph.packetReadyForSubmission(
            compiledGraph,
            planAccess,
            packetID,
            recordingAttemptGeneration
        )
        || !bindRecordingAttemptWithinSubmissionOperation(graph, compiledGraph, recordingAttemptGeneration)
    )
        return false;

    GpuGraphSubmissionBinding submissionBinding;
    {
        NothrowScopedLock lock(m_mutex);
        if(
            !validForLocked(planAccess)
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
        planAccess,
        packetID,
        recordingAttemptGeneration,
        submissionBinding,
        outLease
    )){
        NothrowScopedLock lock(m_mutex);
        if(validForLocked(planAccess) && packetID.index < m_packets.size()){
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
    GpuTimingSubmissionTicket* const* const timingTickets,
    const usize timingTicketCount,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount
){
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    const bool submissionValid =
        !planAccess.valid()
        ? false
        : packetView.valid()
            && validFor(planAccess)
            && token.valid()
            && lease.valid()
            && lease.m_packet == packetID
            && lease.m_planGeneration == planAccess.planGeneration()
            && matchesRecordingAttemptBinding(
            graph,
            compiledGraph,
            lease.m_recordingAttemptGeneration,
            lease.m_submissionBinding
        )
    ;
    if(!submissionValid){
        NWB_FATAL_ASSERT_MSG(false, "native-accepted packet must retain its transaction submission lease");
        TerminateInvariant();
    }

    const GpuSubmissionPacket& packet = *packetView.plan;
    const GpuPhysicalQueueInfo* const queueInfo = planAccess.queueInfo(packet.queue);
    const bool tokenValid = queueInfo
        && queueInfo->queueClass < CommandQueue::kCount
        && token.queue == queueInfo->queueClass
        && token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)
    ;
    if(!tokenValid){
        NWB_FATAL_ASSERT_MSG(false, "native-accepted packet token must match its exact compiled physical queue");
        TerminateInvariant();
    }
    if((timingTicketCount != 0u && !timingTickets) || (taskAcceptedCallbackCount != 0u && !taskAcceptedCallbacks)){
        NWB_FATAL_ASSERT_MSG(false, "native-accepted packet observer arrays must remain valid through publication");
        TerminateInvariant();
    }
    for(usize timingTicketIndex = 0u; timingTicketIndex < timingTicketCount; ++timingTicketIndex){
        if(!timingTickets[timingTicketIndex]){
            NWB_FATAL_ASSERT_MSG(false, "native-accepted packet timing tickets must remain valid through publication");
            TerminateInvariant();
        }
    }
    const GpuTaskId* const tasks = packetView.tasks;
    if(packet.taskCount != 0u && !tasks){
        NWB_FATAL_ASSERT_MSG(false, "native-accepted packet tasks must remain available through observer publication");
        TerminateInvariant();
    }

    graph.beginPacketSubmissionAcceptance(
        compiledGraph,
        planAccess,
        packetID,
        token,
        lease
    );
    AcceptedPacketPublicationGuard publicationGuard(
        *this,
        graph,
        compiledGraph,
        planAccess,
        packetID,
        token,
        lease,
        nativeSubmissionInfo,
        timingTickets,
        timingTicketCount
    );

    bool callbacksAccepted = true;
    bool timingResolved = true;
    for(usize timingTicketIndex = 0u; timingTicketIndex < timingTicketCount; ++timingTicketIndex){
        if(!timingTickets[timingTicketIndex]->resolveSubmission(token))
            timingResolved = false;
    }
    if(!timingResolved)
        NWB_LOGGER_ERROR(NWB_TEXT("GPU task graph: Accepted packet quarantined invalid timing query ownership"));

    // Native acceptance remains hidden while synchronous typed and compatibility observers publish. If an
    // observer throws, the publication guard commits that irreversible acceptance while the exception unwinds.
    graph.notifyPacketSubmissionAccepted(compiledGraph, planAccess, packetID, token, lease);
    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        for(usize callbackIndex = 0u; callbackIndex < taskAcceptedCallbackCount; ++callbackIndex){
            const GpuTaskGraphTaskAcceptedCallback& callback = taskAcceptedCallbacks[callbackIndex];
            if(callback.task == tasks[taskIndex] && !callback.invoke(callback.context, token))
                callbacksAccepted = false;
        }
    }

    publicationGuard.complete();
    return callbacksAccepted;
}

void GpuGraphSubmissionTransaction::commitAcceptedPacket(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    const QueueSubmissionToken& token,
    const NativeSubmissionInfo& nativeSubmissionInfo
)noexcept{
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    NothrowScopedLock lock(m_mutex);
    if(!validForLocked(planAccess) || !packetView.valid() || packetID.index >= m_packets.size()){
        NWB_FATAL_ASSERT_MSG(false, "accepted packet commit must retain its exact transaction and compiled packet");
        TerminateInvariant();
    }
    const GpuSubmissionPacket& packet = *packetView.plan;
    const GpuPhysicalQueueInfo* const queueInfo = planAccess.queueInfo(packet.queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::kCount){
        NWB_FATAL_ASSERT_MSG(false, "accepted packet commit must retain its compiled physical queue");
        TerminateInvariant();
    }
    // reset() and cancellation cannot cross a graph-owned submission lease. Once the graph
    // publishes accepted callbacks, this transaction resolution is therefore an invariant rather than a second
    // failure point.
    PacketRuntime& runtime = m_packets[packetID.index];
    if(runtime.state != PacketRuntimeState::Submitting){
        NWB_FATAL_ASSERT_MSG(false, "accepted packet commit requires the exact submitting transaction packet");
        TerminateInvariant();
    }
    runtime.state = PacketRuntimeState::Accepted;
    runtime.token = token;
    runtime.nativeCommandListCount = nativeSubmissionInfo.commandListCount;
    runtime.plannedWaitTokenCount = nativeSubmissionInfo.plannedWaitTokenCount;
    runtime.sameQueueWaitElisionCount = nativeSubmissionInfo.sameQueueWaitElisionCount;
    runtime.timelineWaitCount = nativeSubmissionInfo.timelineWaitCount;
    runtime.mergedTimelineWaitCount = nativeSubmissionInfo.mergedTimelineWaitCount;
    runtime.submissionSeconds = nativeSubmissionInfo.submissionSeconds;

    if(m_acceptedSubmissionCount >= m_packets.size()){
        NWB_FATAL_ASSERT_MSG(false, "accepted packet count cannot exceed the transaction packet count");
        TerminateInvariant();
    }
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
    if(queueClassIndex >= GpuTaskGraphSubmissionStatistics::s_QueueClassCount){
        NWB_FATAL_ASSERT_MSG(false, "accepted packet queue class must fit transaction statistics storage");
        TerminateInvariant();
    }
    ++m_submissionStatistics.nativeSubmissionCountByQueueClass[queueClassIndex];
    m_submissionStatistics.nativeCommandListCountByQueueClass[queueClassIndex] += nativeSubmissionInfo.commandListCount;
    m_submissionStatistics.timelineWaitCountByQueueClass[queueClassIndex] += nativeSubmissionInfo.timelineWaitCount;

    bool foundLatestQueue = false;
    for(LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == packet.queue){
            const bool latestMatchesQueue = latest.token.valid()
                && latest.token.queue == token.queue
                && latest.token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)
            ;
            if(!latestMatchesQueue){
                NWB_FATAL_ASSERT_MSG(false, "accepted queue frontier token must preserve its exact physical queue identity");
                TerminateInvariant();
            }
            if(token.value > latest.token.value)
                latest.token = token;
            foundLatestQueue = true;
            break;
        }
    }
    if(!foundLatestQueue){
        if(m_latestAcceptedQueueTokens.size() >= m_latestAcceptedQueueTokens.capacity()){
            NWB_FATAL_ASSERT_MSG(false, "accepted queue frontier storage must be reserved before native submission");
            TerminateInvariant();
        }
        static_assert(IsNothrowMoveConstructible_V<LatestAcceptedQueueToken>);
        m_latestAcceptedQueueTokens.push_back(LatestAcceptedQueueToken{
            .queue = packet.queue,
            .token = token,
        });
    }
    resolveSubmissionBindingIfTerminalLocked(graph, compiledGraph);
}

void GpuGraphSubmissionTransaction::abandonTimingTicketsWithoutCallbacks(
    GpuTimingSubmissionTicket* const* const timingTickets,
    const usize timingTicketCount
)noexcept{
    if(timingTicketCount != 0u && !timingTickets){
        NWB_FATAL_ASSERT_MSG(false, "accepted packet cleanup requires its timing ticket array");
        TerminateInvariant();
    }
    for(usize timingTicketIndex = 0u; timingTicketIndex < timingTicketCount; ++timingTicketIndex){
        if(!timingTickets[timingTicketIndex]){
            NWB_FATAL_ASSERT_MSG(false, "accepted packet cleanup requires every timing ticket");
            TerminateInvariant();
        }
        timingTickets[timingTicketIndex]->abandonWithoutCallbacks();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

