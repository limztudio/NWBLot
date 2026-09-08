// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recorded_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PacketRecordingIntervalEntry{
    u64 beginNanoseconds = 0u;
    u64 endNanoseconds = 0u;
    u32 packetIndex = 0u;
};

[[nodiscard]] bool LessPacketRecordingIntervalEntry(
    const PacketRecordingIntervalEntry& left,
    const PacketRecordingIntervalEntry& right
)noexcept{
    return left.beginNanoseconds < right.beginNanoseconds
        || (
            left.beginNanoseconds == right.beginNanoseconds
            && (
                left.endNanoseconds < right.endNanoseconds
                || (left.endNanoseconds == right.endNanoseconds && left.packetIndex < right.packetIndex)
            )
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuRecordedGraph::ArtifactStorage final : NoCopy{
    GraphicsVector<GpuRecordedPacket> packets;
    GraphicsVector<u8> packetRecordingOverlaps;
    GraphicsVector<GlobalUniquePtr<GpuTimingSubmissionTicket>> packetTimingTickets;
    GraphicsVector<CommandListResourceStateHandoff> packetStateSeeds;
    PacketRecordingScratch serialRecordingScratch;
    GraphicsVector<PacketRecordingScratch> packetRecordingScratch;
    GpuTimingRecorder* timingRecorder = nullptr;
    const GpuTaskGraph* graphIdentity = nullptr;
    f64 recordingElapsedSeconds = 0.0;
    f64 readyFrontierElapsedSeconds = 0.0;
    f64 readyFrontierWorkerBusySeconds = 0.0;
    f64 readyFrontierWorkerCapacitySeconds = 0.0;
    usize packetCount = 0u;
    u64 compiledObjectIdentity = 0u;
    u64 generation = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    bool valid = false;


    explicit ArtifactStorage(GraphicsArena& arena)
        : packets(arena)
        , packetRecordingOverlaps(arena)
        , packetTimingTickets(arena)
        , packetStateSeeds(arena)
        , serialRecordingScratch(arena)
        , packetRecordingScratch(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuRecordedGraph::ArtifactOperation* GpuRecordedGraph::ArtifactOperation::s_activeOperation = nullptr;


bool GpuRecordedGraph::ArtifactOperation::activeFor(const GpuRecordedGraph& recordedGraph)noexcept{
    for(const ArtifactOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_recordedGraph == &recordedGraph)
            return true;
    }
    return false;
}

bool GpuRecordedGraph::ArtifactOperation::activeExclusiveFor(const GpuRecordedGraph& recordedGraph)noexcept{
    for(const ArtifactOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_recordedGraph == &recordedGraph)
            return operation->m_exclusive;
    }
    return false;
}


GpuRecordedGraph::ArtifactOperation::ArtifactOperation(
    const GpuRecordedGraph& recordedGraph,
    const ArtifactOperationMode mode
)noexcept{
    const bool exclusive = mode == ArtifactOperationMode::Exclusive;
    const bool waitRead = mode == ArtifactOperationMode::WaitRead;
    for(const ArtifactOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_recordedGraph != &recordedGraph)
            continue;
        if(exclusive && !operation->m_exclusive)
            return;

        m_recordedGraph = &recordedGraph;
        m_previousOperation = s_activeOperation;
        m_exclusive = operation->m_exclusive;
        s_activeOperation = this;
        return;
    }

    // Mutating cross-artifact/transaction reentry is rejected even when the target happens to be idle. This keeps
    // blocking cleanup outside every unrelated scheduler gate and removes the symmetric ABBA shape entirely.
    if(s_activeOperation || GpuGraphSubmissionTransaction::SubmissionOperation::active())
        return;
    const bool acquireExclusive = exclusive;
    if(acquireExclusive){
        u32 expectedState = 0u;
        if(!recordedGraph.m_operationState.compare_exchange_strong(
            expectedState,
            GpuRecordedGraph::s_ArtifactOperationWriterBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            return;
    }
    else{
        u32 operationState = recordedGraph.m_operationState.load(MemoryOrder::acquire);
        while(true){
            if((operationState & GpuRecordedGraph::s_ArtifactOperationWriterBit) != 0u){
                if(!waitRead)
                    return;
                recordedGraph.m_operationState.wait(operationState, MemoryOrder::acquire);
                operationState = recordedGraph.m_operationState.load(MemoryOrder::acquire);
                continue;
            }
            if(
                (operationState & GpuRecordedGraph::s_ArtifactOperationReaderMask)
                == GpuRecordedGraph::s_ArtifactOperationReaderMask
            ){
                NWB_FATAL_ASSERT_MSG(false, "GpuRecordedGraph artifact reader ownership overflowed");
                TerminateInvariant();
            }

            if(recordedGraph.m_operationState.compare_exchange_weak(
                operationState,
                operationState + 1u,
                MemoryOrder::acq_rel,
                MemoryOrder::acquire
            ))
                break;
        }
    }
    m_exclusive = acquireExclusive;
    m_recordedGraph = &recordedGraph;
    m_previousOperation = s_activeOperation;
    m_ownsAdmission = true;
    s_activeOperation = this;
}
GpuRecordedGraph::ArtifactOperation::~ArtifactOperation()noexcept{
    if(!m_recordedGraph)
        return;

    if(s_activeOperation != this){
        NWB_FATAL_ASSERT_MSG(false, "GpuRecordedGraph artifact operations must unwind in lexical order");
        TerminateInvariant();
    }
    s_activeOperation = m_previousOperation;
    if(!m_ownsAdmission)
        return;

    if(m_exclusive){
        if(m_recordedGraph->m_operationState.load(MemoryOrder::relaxed) != GpuRecordedGraph::s_ArtifactOperationWriterBit){
            NWB_FATAL_ASSERT_MSG(false, "GpuRecordedGraph exclusive artifact operation must retain its writer claim");
            TerminateInvariant();
        }
        m_recordedGraph->m_operationState.store(0u, MemoryOrder::release);
        m_recordedGraph->m_operationState.notify_all();
    }
    else{
        u32 operationState = m_recordedGraph->m_operationState.load(MemoryOrder::acquire);
        while(true){
            if((operationState & GpuRecordedGraph::s_ArtifactOperationReaderMask) == 0u){
                NWB_FATAL_ASSERT_MSG(false, "GpuRecordedGraph shared artifact operation must retain its reader claim");
                TerminateInvariant();
            }
            if(m_recordedGraph->m_operationState.compare_exchange_weak(
                operationState,
                operationState - 1u,
                MemoryOrder::release,
                MemoryOrder::relaxed
            )){
                if((operationState & GpuRecordedGraph::s_ArtifactOperationReaderMask) == 1u)
                    m_recordedGraph->m_operationState.notify_all();
                break;
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRecordedGraph::PacketRecordingScratch::ensureValid(GraphicsArena& arena){
    if(!stateFanInScratchArena){
        stateFanInScratchArena = MakeGlobalUnique<Alloc::ScratchArena>(
            arena,
            Name("core/task/gpu/packet_state_fan_in")
        );
    }
    return stateFanInScratchArena != nullptr;
}


void GpuRecordedGraph::PacketRecordingScratch::reset()noexcept{
    initialStateSeed.reset();
    stateSubsetScratch.reset();
    stateMergeScratch.reset();
    externalBaseStateSeed.reset();
    externalMergedStateSeed.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuRecordedGraph::GpuRecordedGraph(GraphicsArena& arena)
    : m_arena(arena)
    , m_activeStorage(MakeGlobalUnique<ArtifactStorage>(arena, arena))
    , m_candidateStorage(MakeGlobalUnique<ArtifactStorage>(arena, arena))
{}
GpuRecordedGraph::~GpuRecordedGraph(){
    if(ArtifactOperation::activeFor(*this)){
        NWB_FATAL_ASSERT_MSG(false, "GpuRecordedGraph destruction requires active artifact operations to finish first");
        TerminateInvariant();
    }

    // Destruction normally joins existing readers. A callback that already owns another artifact or transaction
    // cannot wait here without permitting a symmetric cross-object destruction deadlock.
    if(ArtifactOperation::active() || GpuGraphSubmissionTransaction::SubmissionOperation::active()){
        ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Exclusive);
        if(!artifactOperation.valid()){
            NWB_FATAL_ASSERT_MSG(false, "nested GpuRecordedGraph destruction cannot wait for active artifact operations");
            TerminateInvariant();
        }

        if(m_activeStorage)
            revokeCommandListPublicationsWithoutCallbacks(*m_activeStorage);
        return;
    }

    u32 operationState = m_operationState.load(MemoryOrder::acquire);
    while(true){
        if((operationState & s_ArtifactOperationWriterBit) != 0u){
            m_operationState.wait(operationState, MemoryOrder::acquire);
            operationState = m_operationState.load(MemoryOrder::acquire);
            continue;
        }
        if(m_operationState.compare_exchange_weak(
            operationState,
            operationState | s_ArtifactOperationWriterBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        )){
            operationState |= s_ArtifactOperationWriterBit;
            break;
        }
    }
    while((operationState & s_ArtifactOperationReaderMask) != 0u){
        m_operationState.wait(operationState, MemoryOrder::acquire);
        operationState = m_operationState.load(MemoryOrder::acquire);
    }
    NWB_ASSERT(operationState == s_ArtifactOperationWriterBit);

    if(m_activeStorage)
        revokeCommandListPublicationsWithoutCallbacks(*m_activeStorage);
    m_operationState.store(0u, MemoryOrder::release);
    m_operationState.notify_all();
}


bool GpuRecordedGraph::tryReset(const GpuCompiledGraph& compiledGraph){
    if(ArtifactOperation::activeFor(*this))
        return false;

    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Exclusive);
    if(!artifactOperation.valid())
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    if(!m_activeStorage || !m_candidateStorage)
        return false;

    if(m_activeStorage->recordingAttemptGeneration != 0u){
        if(m_activeStorage->compiledObjectIdentity != planAccess.objectIdentity())
            return false;
        if(compiledGraph.matchesActiveAttemptIdentity(
            m_activeStorage->graphIdentity,
            m_activeStorage->compiledObjectIdentity,
            m_activeStorage->planGeneration,
            m_activeStorage->recordingAttemptGeneration,
            planAccess
        ))
            return false;
    }
    if(!prepareResetStorageCandidate(compiledGraph, planAccess))
        return false;
    publishStorageCandidate(nullptr, compiledGraph, planAccess, 0u, artifactOperation);
    return true;
}


void GpuRecordedGraph::reset(const GpuCompiledGraph& compiledGraph){
    if(tryReset(compiledGraph))
        return;

    NWB_FATAL_ASSERT_MSG(
        false,
        "GpuRecordedGraph::reset requires a valid matching plan and a resolved recording attempt"
    );
    TerminateInvariant();
}


void GpuRecordedGraph::revokeCommandListPublicationsWithoutCallbacks(ArtifactStorage& storage)noexcept{
    const usize packetCount = Min(storage.packetCount, storage.packets.size());
    for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex){
        GpuRecordedPacket& packet = storage.packets[packetIndex];
        for(usize commandListIndex = 0u; commandListIndex < GpuRecordedPacket::s_MaxCommandLists; ++commandListIndex){
            CommandList* const commandList = packet.ownedCommandLists[commandListIndex].get();
            if(commandList){
                commandList->revokeGraphRecordingPublication(
                    packet.commandListRecordingLeaseSerials[commandListIndex]
                );
            }
        }
    }
}

void GpuRecordedGraph::retireStorageWithoutCallbacks(ArtifactStorage& storage)noexcept{
    static_assert(IsNothrowDestructible_V<GpuRecordedPacket>);
    static_assert(IsNothrowDestructible_V<GpuTimingSubmissionTicket>);
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    static_assert(IsNothrowDestructible_V<PacketRecordingScratch>);

    for(GpuRecordedPacket& packet : storage.packets){
        static_assert(noexcept(packet = GpuRecordedPacket{}));
        packet = {};
    }
    for(u8& overlaps : storage.packetRecordingOverlaps)
        overlaps = 0u;
    for(GlobalUniquePtr<GpuTimingSubmissionTicket>& ticket : storage.packetTimingTickets){
        static_assert(noexcept(ticket->abandonWithoutCallbacks()));
        if(ticket)
            ticket->abandonWithoutCallbacks();
    }
    for(CommandListResourceStateHandoff& stateSeed : storage.packetStateSeeds){
        static_assert(noexcept(stateSeed.reset()));
        stateSeed.reset();
    }
    static_assert(noexcept(storage.serialRecordingScratch.reset()));
    storage.serialRecordingScratch.reset();
    for(PacketRecordingScratch& scratch : storage.packetRecordingScratch){
        static_assert(noexcept(scratch.reset()));
        scratch.reset();
    }
    storage.timingRecorder = nullptr;
    storage.graphIdentity = nullptr;
    storage.recordingElapsedSeconds = 0.0;
    storage.readyFrontierElapsedSeconds = 0.0;
    storage.readyFrontierWorkerBusySeconds = 0.0;
    storage.readyFrontierWorkerCapacitySeconds = 0.0;
    storage.packetCount = 0u;
    storage.compiledObjectIdentity = 0u;
    storage.generation = 0u;
    storage.planGeneration = 0u;
    storage.recordingAttemptGeneration = 0u;
    storage.deviceGeneration = 0u;
    storage.valid = false;
}

bool GpuRecordedGraph::prepareStorageCandidateLayout(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
){
    if(
        !planAccess.validFor(compiledGraph)
        || !planAccess.valid()
        || !m_candidateStorage
    )
        return false;

    ArtifactStorage& candidate = *m_candidateStorage;
    retireStorageWithoutCallbacks(candidate);

    const usize packetCount = planAccess.packetCount();
    if(candidate.packets.size() < packetCount)
        candidate.packets.resize(packetCount);
    if(candidate.packetRecordingOverlaps.size() < packetCount)
        candidate.packetRecordingOverlaps.resize(packetCount);
    if(candidate.packetTimingTickets.size() < packetCount)
        candidate.packetTimingTickets.resize(packetCount);
    while(candidate.packetStateSeeds.size() < packetCount)
        candidate.packetStateSeeds.emplace_back(m_arena);
    while(candidate.packetRecordingScratch.size() < packetCount)
        candidate.packetRecordingScratch.emplace_back(m_arena);
    if(!candidate.serialRecordingScratch.ensureValid(m_arena))
        return false;

    for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex){
        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(packetIndex);
        if(!planAccess.validPacket(packetID))
            return false;

        candidate.packets[packetIndex].packet = packetID;
        candidate.packetRecordingOverlaps[packetIndex] = 0u;
        candidate.packetStateSeeds[packetIndex].reset();
        if(!candidate.packetRecordingScratch[packetIndex].ensureValid(m_arena))
            return false;
    }

    candidate.timingRecorder = nullptr;
    candidate.packetCount = packetCount;
    candidate.compiledObjectIdentity = planAccess.objectIdentity();
    candidate.generation = planAccess.generation();
    candidate.planGeneration = planAccess.planGeneration();
    candidate.deviceGeneration = planAccess.deviceGeneration();
    return true;
}

bool GpuRecordedGraph::prepareResetStorageCandidate(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
){
    if(!prepareStorageCandidateLayout(compiledGraph, planAccess))
        return false;

    ArtifactStorage& candidate = *m_candidateStorage;
    for(GlobalUniquePtr<GpuTimingSubmissionTicket>& ticket : candidate.packetTimingTickets){
        static_assert(noexcept(ticket.reset()));
        ticket.reset();
    }
    candidate.valid = true;
    return true;
}

bool GpuRecordedGraph::prepareRecordingStorageCandidate(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    GpuTimingRecorder* const timingRecorder
){
    if(!prepareStorageCandidateLayout(compiledGraph, planAccess))
        return false;

    ArtifactStorage& candidate = *m_candidateStorage;
    bool recordsTiming = false;
    for(usize packetIndex = 0u; packetIndex < candidate.packetCount; ++packetIndex){
        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packetID);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        recordsTiming = recordsTiming || packetPlan.recordsTiming;
        if(packetPlan.recordsTiming && !timingRecorder)
            return false;
        if(packetPlan.recordsTiming){
            GlobalUniquePtr<GpuTimingSubmissionTicket>& ticket = candidate.packetTimingTickets[packetIndex];
            if(ticket && !ticket->resetForRecordingReuse(*timingRecorder))
                ticket.reset();
            if(!ticket)
                ticket = MakeGlobalUnique<GpuTimingSubmissionTicket>(m_arena, *timingRecorder);
            if(!candidate.packetTimingTickets[packetIndex])
                return false;
        }
        else{
            GlobalUniquePtr<GpuTimingSubmissionTicket>& ticket = candidate.packetTimingTickets[packetIndex];
            static_assert(noexcept(ticket.reset()));
            ticket.reset();
        }
    }
    for(usize packetIndex = candidate.packetCount; packetIndex < candidate.packetTimingTickets.size(); ++packetIndex){
        GlobalUniquePtr<GpuTimingSubmissionTicket>& ticket = candidate.packetTimingTickets[packetIndex];
        static_assert(noexcept(ticket.reset()));
        ticket.reset();
    }

    candidate.timingRecorder = recordsTiming ? timingRecorder : nullptr;
    candidate.valid = true;
    return true;
}

void GpuRecordedGraph::publishStorageCandidate(
    const GpuTaskGraph* const graphIdentity,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const u64 recordingAttemptGeneration,
    const ArtifactOperation& artifactAccess
)noexcept{
    const bool candidateValid = planAccess.validFor(compiledGraph)
        && ArtifactOperation::activeExclusiveFor(*this)
        && artifactAccess.exclusiveFor(*this)
        && m_activeStorage
        && m_candidateStorage
        && m_candidateStorage->valid
        && m_candidateStorage->compiledObjectIdentity == planAccess.objectIdentity()
        && m_candidateStorage->generation == planAccess.generation()
        && m_candidateStorage->planGeneration == planAccess.planGeneration()
        && m_candidateStorage->deviceGeneration == planAccess.deviceGeneration()
    ;
    NWB_FATAL_ASSERT_MSG(candidateValid, "recorded artifact publication requires one complete exact-plan candidate");
    if(!candidateValid)
        TerminateInvariant();

    m_candidateStorage->graphIdentity = graphIdentity;
    m_candidateStorage->recordingAttemptGeneration = recordingAttemptGeneration;
    revokeCommandListPublicationsWithoutCallbacks(*m_activeStorage);
    static_assert(noexcept(Swap(m_activeStorage, m_candidateStorage)), "artifact owner publication must be non-throwing");
    Swap(m_activeStorage, m_candidateStorage);
    retireStorageWithoutCallbacks(*m_candidateStorage);
}

bool GpuRecordedGraph::validForWithinArtifactOperation(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const ArtifactOperation& artifactAccess
)const noexcept{
    const ArtifactStorage* const storage = m_activeStorage.get();
    return planAccess.validFor(compiledGraph)
        && artifactAccess.validFor(*this)
        && storage
        && storage->valid
        && planAccess.valid()
        && storage->compiledObjectIdentity == planAccess.objectIdentity()
        && storage->generation == planAccess.generation()
        && storage->planGeneration == planAccess.planGeneration()
        && storage->deviceGeneration == planAccess.deviceGeneration()
        && storage->packetCount == planAccess.packetCount()
        && storage->packets.size() >= storage->packetCount
        && storage->packetRecordingOverlaps.size() >= storage->packetCount
        && storage->packetTimingTickets.size() >= storage->packetCount
        && storage->packetStateSeeds.size() >= storage->packetCount
        && storage->packetRecordingScratch.size() >= storage->packetCount
    ;
}

bool GpuRecordedGraph::validForWithinArtifactOperation(
    const GpuTaskGraph& graph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const ArtifactOperation& artifactAccess
)const noexcept{
    const ArtifactStorage* const storage = m_activeStorage.get();
    return declarationAccess.validFor(graph)
        && planAccess.validFor(declarationAccess)
        && validForWithinArtifactOperation(compiledGraph, planAccess, artifactAccess)
        && storage->graphIdentity == &graph
        && storage->recordingAttemptGeneration != 0u
        // A terminal accepted/discarded artifact remains a valid immutable result after the graph releases its
        // active attempt lease. Declaration admission plus the exact compiled-plan identity proves that the source
        // graph has not mutated; active-attempt matching is required only by claim/reset paths.
    ;
}

bool GpuRecordedGraph::validFor(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
)const noexcept{
    if(!planAccess.validFor(compiledGraph))
        return false;
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return false;
    return validForWithinArtifactOperation(compiledGraph, planAccess, artifactOperation);
}

bool GpuRecordedGraph::validFor(
    const GpuTaskGraph& graph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
)const noexcept{
    if(!declarationAccess.validFor(graph) || !planAccess.validFor(declarationAccess))
        return false;
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return false;
    return validForWithinArtifactOperation(
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        artifactOperation
    );
}


u64 GpuRecordedGraph::recordingAttemptGeneration()const noexcept{
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    return artifactOperation.valid() && m_activeStorage ? m_activeStorage->recordingAttemptGeneration : 0u;
}


GpuTaskGraphRecordingStatistics GpuRecordedGraph::recordingStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
)const noexcept{
    GpuTaskGraphRecordingStatistics statistics;
    if(!planAccess.validFor(compiledGraph))
        return statistics;
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return statistics;
    if(!validForWithinArtifactOperation(compiledGraph, planAccess, artifactOperation))
        return statistics;

    const ArtifactStorage& storage = *m_activeStorage;
    statistics.graphGeneration = storage.generation;
    statistics.planGeneration = storage.planGeneration;
    statistics.recordingAttemptGeneration = storage.recordingAttemptGeneration;
    statistics.deviceGeneration = storage.deviceGeneration;
    statistics.recordingElapsedSeconds = storage.recordingElapsedSeconds;
    statistics.readyFrontierElapsedSeconds = storage.readyFrontierElapsedSeconds;
    statistics.readyFrontierWorkerBusySeconds = storage.readyFrontierWorkerBusySeconds;
    statistics.readyFrontierWorkerCapacitySeconds = storage.readyFrontierWorkerCapacitySeconds;
    for(usize packetIndex = 0u; packetIndex < storage.packetCount; ++packetIndex){
        const GpuRecordedPacket& recordedPacket = storage.packets[packetIndex];
        if(
            recordedPacket.commandListCount == 0u
            || recordedPacket.packet != planAccess.packetIdAt(packetIndex)
        )
            continue;

        ++statistics.packetCount;
        statistics.taskCount += recordedPacket.taskCount;
        statistics.commandListCount += recordedPacket.commandListCount;
        statistics.barrierCount += recordedPacket.barrierCount;
        statistics.commandListAcquisitionSeconds += recordedPacket.commandListAcquisitionSeconds;
        statistics.graphBarrierRecordingSeconds += recordedPacket.graphBarrierRecordingSeconds;
        statistics.taskRecordSeconds += recordedPacket.taskRecordSeconds;
        statistics.recordingSeconds += recordedPacket.recordingSeconds;
        if(recordedPacket.recordingWorkerIndex != 0u)
            ++statistics.workerRoutedPacketCount;
        if(storage.packetRecordingOverlaps[packetIndex] != 0u)
            ++statistics.parallelPacketCount;
    }
    return statistics;
}

GpuTaskGraphPhysicalQueueRecordingStatistics GpuRecordedGraph::physicalQueueRecordingStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuPhysicalQueueId& queue
)const noexcept{
    if(!planAccess.validFor(compiledGraph))
        return {};
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return {};
    if(!validForWithinArtifactOperation(compiledGraph, planAccess, artifactOperation))
        return {};

    const ArtifactStorage& storage = *m_activeStorage;
    const GpuPhysicalQueueInfo* const queueInfo = planAccess.queueInfo(queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::kCount)
        return {};

    GpuTaskGraphPhysicalQueueRecordingStatistics statistics{
        .graphGeneration = storage.generation,
        .planGeneration = storage.planGeneration,
        .recordingAttemptGeneration = storage.recordingAttemptGeneration,
        .deviceGeneration = storage.deviceGeneration,
        .queue = queue,
        .queueClass = queueInfo->queueClass,
    };
    for(usize packetIndex = 0u; packetIndex < storage.packetCount; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuRecordedPacket& recordedPacket = storage.packets[packetIndex];
        if(
            recordedPacket.commandListCount == 0u
            || recordedPacket.packet != packet
        )
            continue;

        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return {};
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        if(packetPlan.queue != queue)
            continue;

        ++statistics.packetCount;
        statistics.taskCount += recordedPacket.taskCount;
        statistics.commandListCount += recordedPacket.commandListCount;
        statistics.barrierCount += recordedPacket.barrierCount;
        statistics.commandListAcquisitionSeconds += recordedPacket.commandListAcquisitionSeconds;
        statistics.graphBarrierRecordingSeconds += recordedPacket.graphBarrierRecordingSeconds;
        statistics.taskRecordSeconds += recordedPacket.taskRecordSeconds;
        statistics.recordingSeconds += recordedPacket.recordingSeconds;
        if(recordedPacket.recordingWorkerIndex != 0u)
            ++statistics.workerRoutedPacketCount;
        // The cached flag is graph-wide. A packet on this queue retains overlap with a published packet on another
        // physical queue, preserving the aggregate's cross-queue recording semantics.
        if(storage.packetRecordingOverlaps[packetIndex] != 0u)
            ++statistics.parallelPacketCount;
    }
    return statistics;
}


bool GpuTaskGraphExternalCompletionToken::validFor(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
)const noexcept{
    if(
        !planAccess.validFor(compiledGraph)
        || !completion.valid()
        || completion.generation != planAccess.generation()
        || !token.valid()
        || !token.hasPhysicalQueueIdentity()
    )
        return false;

    // A metadata-only compatibility binding may originate on a current-device queue omitted from the assignment topology.
    // Graph-owned tokens instead require complete-topology validation during compile; this fallback validates device
    // lifetime here and leaves concrete queue validation to the submitting Device.
    return token.deviceGeneration == planAccess.deviceGeneration();
}

bool GpuTaskGraphExternalCompletionToken::validFallbackFor(
    const GpuTaskGraph& graph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
)const noexcept{
    return declarationAccess.validFor(graph)
        && planAccess.validFor(declarationAccess)
        && declarationAccess.validExternalCompletion(completion)
        && !declarationAccess.externalCompletionToken(completion)
        && validFor(compiledGraph, planAccess)
    ;
}

Optional<GpuRecordedPacket> GpuRecordedGraph::packetSnapshot(const GpuSubmissionPacketId& packet)const noexcept{
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return {};
    const GpuRecordedPacket* const recordedPacket = findWithinArtifactOperation(packet, artifactOperation);
    static_assert(noexcept(GpuRecordedPacket(*recordedPacket)));
    return recordedPacket ? Optional<GpuRecordedPacket>(*recordedPacket) : Optional<GpuRecordedPacket>();
}

bool GpuRecordedGraph::hasTaskFinalStateSeed(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskId task
)const noexcept{
    if(!planAccess.validFor(compiledGraph))
        return false;
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return false;
    if(
        !validForWithinArtifactOperation(compiledGraph, planAccess, artifactOperation)
        || !planAccess.findTask(task).valid()
    )
        return false;

    const GpuSubmissionPacketId packet = planAccess.packetForTask(task);
    const CommandListResourceStateHandoff* const stateSeed = packetStateSeed(packet, artifactOperation);
    return findWithinArtifactOperation(packet, artifactOperation)
        && stateSeed
        && stateSeed->validForDeviceGeneration(m_activeStorage->deviceGeneration)
    ;
}

bool GpuRecordedGraph::copyTaskFinalStateSeed(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskId task,
    CommandListResourceStateHandoff& outStateSeed
)const{
    if(!planAccess.validFor(compiledGraph))
        return false;
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return false;
    if(
        !validForWithinArtifactOperation(compiledGraph, planAccess, artifactOperation)
        || !planAccess.findTask(task).valid()
    )
        return false;

    const GpuSubmissionPacketId packet = planAccess.packetForTask(task);
    const CommandListResourceStateHandoff* const stateSeed = packetStateSeed(packet, artifactOperation);
    return findWithinArtifactOperation(packet, artifactOperation)
        && stateSeed
        && stateSeed->validForDeviceGeneration(m_activeStorage->deviceGeneration)
        && outStateSeed.copyFrom(*stateSeed)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const GpuRecordedPacket* GpuRecordedGraph::findWithinArtifactOperation(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)const noexcept{
    const ArtifactStorage* const storage = m_activeStorage.get();
    if(
        !artifactAccess.validFor(*this)
        || !storage
        || !storage->valid
        || !packet.valid()
        || packet.generation != storage->planGeneration
        || packet.index >= storage->packetCount
    )
        return nullptr;
    const GpuRecordedPacket& recordedPacket = storage->packets[packet.index];
    return recordedPacket.packet == packet && recordedPacket.commandListCount != 0u
        ? &recordedPacket
        : nullptr
    ;
}

void GpuRecordedGraph::discardPacketTimingTicket(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
){
    GpuTimingSubmissionTicket* const ticket = packetTimingTicket(packet, artifactAccess);
    if(!ticket)
        return;
    ticket->discard();
    m_activeStorage->packetTimingTickets[packet.index].reset();
}

void GpuRecordedGraph::abandonPacketTimingTicketWithoutCallbacks(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)noexcept{
    GpuTimingSubmissionTicket* const ticket = packetTimingTicket(packet, artifactAccess);
    if(!ticket)
        return;
    ticket->abandonWithoutCallbacks();
    m_activeStorage->packetTimingTickets[packet.index].reset();
}

GpuTimingSubmissionTicket* GpuRecordedGraph::packetTimingTicket(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)const noexcept{
    const ArtifactStorage* const storage = m_activeStorage.get();
    if(
        !artifactAccess.validFor(*this)
        || !storage
        || !storage->valid
        || !packet.valid()
        || packet.generation != storage->planGeneration
        || packet.index >= storage->packetCount
    )
        return nullptr;
    return storage->packetTimingTickets[packet.index].get();
}

CommandListResourceStateHandoff* GpuRecordedGraph::packetStateSeed(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)noexcept{
    ArtifactStorage* const storage = m_activeStorage.get();
    if(
        !artifactAccess.validFor(*this)
        || !storage
        || !storage->valid
        || !packet.valid()
        || packet.generation != storage->planGeneration
        || packet.index >= storage->packetCount
    )
        return nullptr;
    return &storage->packetStateSeeds[packet.index];
}

const CommandListResourceStateHandoff* GpuRecordedGraph::packetStateSeed(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)const noexcept{
    const ArtifactStorage* const storage = m_activeStorage.get();
    if(
        !artifactAccess.validFor(*this)
        || !storage
        || !storage->valid
        || !packet.valid()
        || packet.generation != storage->planGeneration
        || packet.index >= storage->packetCount
    )
        return nullptr;
    return &storage->packetStateSeeds[packet.index];
}

GpuRecordedGraph::PacketRecordingScratch* GpuRecordedGraph::packetRecordingScratch(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)noexcept{
    ArtifactStorage* const storage = m_activeStorage.get();
    if(
        !artifactAccess.exclusiveFor(*this)
        || !storage
        || !storage->valid
        || !packet.valid()
        || packet.generation != storage->planGeneration
        || packet.index >= storage->packetCount
    )
        return nullptr;
    return &storage->packetRecordingScratch[packet.index];
}

GpuRecordedGraph::PacketRecordingScratch* GpuRecordedGraph::serialRecordingScratch(
    const ArtifactOperation& artifactAccess
)noexcept{
    return artifactAccess.exclusiveFor(*this) && m_activeStorage && m_activeStorage->valid
        ? &m_activeStorage->serialRecordingScratch
        : nullptr
    ;
}

GpuRecordedPacket* GpuRecordedGraph::packetStorage(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)noexcept{
    ArtifactStorage* const storage = m_activeStorage.get();
    if(
        !artifactAccess.exclusiveFor(*this)
        || !storage
        || !storage->valid
        || !packet.valid()
        || packet.generation != storage->planGeneration
        || packet.index >= storage->packetCount
    )
        return nullptr;
    return &storage->packets[packet.index];
}

void GpuRecordedGraph::clearPacketPublicationWithoutCallbacks(
    const GpuSubmissionPacketId& packet,
    const ArtifactOperation& artifactAccess
)noexcept{
    GpuRecordedPacket* const recordedPacket = packetStorage(packet, artifactAccess);
    CommandListResourceStateHandoff* const stateSeed = packetStateSeed(packet, artifactAccess);
    const bool publicationValid = recordedPacket && stateSeed && artifactAccess.exclusiveFor(*this);
    NWB_FATAL_ASSERT_MSG(publicationValid, "recorded packet rollback requires its exact artifact writer");
    if(!publicationValid)
        TerminateInvariant();

    for(usize commandListIndex = 0u; commandListIndex < GpuRecordedPacket::s_MaxCommandLists; ++commandListIndex){
        CommandList* const commandList = recordedPacket->ownedCommandLists[commandListIndex].get();
        if(commandList){
            commandList->revokeGraphRecordingPublication(
                recordedPacket->commandListRecordingLeaseSerials[commandListIndex]
            );
        }
    }
    static_assert(noexcept(*recordedPacket = GpuRecordedPacket{}));
    static_assert(noexcept(stateSeed->reset()));
    *recordedPacket = {};
    stateSeed->reset();
}

GpuTimingRecorder* GpuRecordedGraph::timingRecorderWithinArtifactOperation(
    const ArtifactOperation& artifactAccess
)const noexcept{
    return artifactAccess.validFor(*this) && m_activeStorage && m_activeStorage->valid
        ? m_activeStorage->timingRecorder
        : nullptr
    ;
}

u64 GpuRecordedGraph::recordingAttemptGenerationWithinArtifactOperation(
    const ArtifactOperation& artifactAccess
)const noexcept{
    return artifactAccess.validFor(*this) && m_activeStorage && m_activeStorage->valid
        ? m_activeStorage->recordingAttemptGeneration
        : 0u
    ;
}

void GpuRecordedGraph::addRecordingElapsedSeconds(
    const f64 elapsedSeconds,
    const ArtifactOperation& artifactAccess
)noexcept{
    if(artifactAccess.exclusiveFor(*this) && m_activeStorage && m_activeStorage->valid)
        m_activeStorage->recordingElapsedSeconds += elapsedSeconds;
}

void GpuRecordedGraph::addReadyFrontierStatistics(
    const f64 elapsedSeconds,
    const f64 workerBusySeconds,
    const f64 workerCapacitySeconds,
    const ArtifactOperation& artifactAccess
)noexcept{
    if(!artifactAccess.exclusiveFor(*this) || !m_activeStorage || !m_activeStorage->valid)
        return;
    m_activeStorage->readyFrontierElapsedSeconds += elapsedSeconds;
    m_activeStorage->readyFrontierWorkerBusySeconds += workerBusySeconds;
    m_activeStorage->readyFrontierWorkerCapacitySeconds += workerCapacitySeconds;
}

void GpuRecordedGraph::cachePacketRecordingOverlaps(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const Vector<u32, Alloc::ScratchArena>& packetIndices,
    Alloc::ScratchArena& scratchArena,
    const ArtifactOperation& artifactAccess
){
    using IntervalEntry = __hidden_gpu_packet_runtime_recorded_graph::PacketRecordingIntervalEntry;
    const bool artifactAccessValid = planAccess.validFor(compiledGraph)
        && artifactAccess.exclusiveFor(*this)
        && m_activeStorage
        && m_activeStorage->valid
    ;
    NWB_FATAL_ASSERT_MSG(artifactAccessValid, "Packet overlap caching requires its exact valid artifact writer");
    if(!artifactAccessValid)
        TerminateInvariant();
    ArtifactStorage& storage = *m_activeStorage;

    Vector<IntervalEntry, Alloc::ScratchArena> intervalEntries(scratchArena);
    intervalEntries.reserve(packetIndices.size());
    for(const u32 packetIndex : packetIndices){
        if(
            packetIndex >= storage.packetCount
        )
            continue;
        const GpuRecordedPacket& recordedPacket = storage.packets[packetIndex];
        if(
            recordedPacket.commandListCount == 0u
            || recordedPacket.packet != planAccess.packetIdAt(packetIndex)
            || recordedPacket.recordingBeginNanoseconds >= recordedPacket.recordingEndNanoseconds
        )
            continue;
        intervalEntries.push_back(IntervalEntry{
            .beginNanoseconds = recordedPacket.recordingBeginNanoseconds,
            .endNanoseconds = recordedPacket.recordingEndNanoseconds,
            .packetIndex = packetIndex,
        });
    }
    Sort(
        intervalEntries.begin(),
        intervalEntries.end(),
        __hidden_gpu_packet_runtime_recorded_graph::LessPacketRecordingIntervalEntry
    );

    u64 maximumPreviousEndNanoseconds = 0u;
    for(usize intervalIndex = 0u; intervalIndex < intervalEntries.size(); ++intervalIndex){
        const IntervalEntry& interval = intervalEntries[intervalIndex];
        const bool overlapsPrevious = intervalIndex != 0u
            && interval.beginNanoseconds < maximumPreviousEndNanoseconds
        ;
        const bool overlapsNext = intervalIndex + 1u < intervalEntries.size()
            && intervalEntries[intervalIndex + 1u].beginNanoseconds < interval.endNanoseconds
        ;
        if(overlapsPrevious || overlapsNext)
            storage.packetRecordingOverlaps[interval.packetIndex] = 1u;
        maximumPreviousEndNanoseconds = Max(maximumPreviousEndNanoseconds, interval.endNanoseconds);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

