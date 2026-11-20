// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "packet_runtime_internal.h"
#include "scheduler_submission_bindings.h"
#include "task_graph.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/gpu_timing.h>

#include <global/overflow.h>
#include <global/simd/vector.h>
#include <global/simd/vector_arithmetic.h>
#include <global/termination.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskScheduler::PreparedTimingTicketsUnwindScope final : NoCopy{
private:
    struct Mode{
        enum Enum : u8{
            Rollback,
            Abandon,
            Released,
        };
    };


public:
    PreparedTimingTicketsUnwindScope(const NotNull<GpuTimingSubmissionTicket*>* const tickets, usize& preparedTicketCount)noexcept
        : m_tickets(tickets)
        , m_preparedTicketCount(preparedTicketCount)
    {}
    ~PreparedTimingTicketsUnwindScope()noexcept{
        static_assert(noexcept(static_cast<GpuTimingSubmissionTicket*>(nullptr)->rollbackPreparedSubmission()));
        static_assert(noexcept(static_cast<GpuTimingSubmissionTicket*>(nullptr)->abandonWithoutCallbacks()));

        if(m_mode == Mode::Rollback){
            while(m_preparedTicketCount > 0u)
                m_tickets[--m_preparedTicketCount]->rollbackPreparedSubmission();
        }
        else if(m_mode == Mode::Abandon){
            while(m_preparedTicketCount > 0u)
                m_tickets[--m_preparedTicketCount]->abandonWithoutCallbacks();
        }
    }


public:
    void abandonOnUnwind()noexcept{ m_mode = Mode::Abandon; }

    void release()noexcept{ m_mode = Mode::Released; }


private:
    const NotNull<GpuTimingSubmissionTicket*>* m_tickets = nullptr;
    usize& m_preparedTicketCount;
    Mode::Enum m_mode = Mode::Rollback;
};


class GpuTaskScheduler::SubmittingPacketUnwindScope final : NoCopy{
public:
    SubmittingPacketUnwindScope(GpuTaskGraph& graph, GpuGraphSubmissionTransaction& transaction, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess)noexcept
        : m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_transaction(transaction)
    {}
    ~SubmittingPacketUnwindScope()noexcept{
        if(!m_submissionLease)
            return;

        NothrowScopedLock resolutionLock(m_transaction.m_resolutionMutex);
        m_transaction.abandonSubmittingPacketAfterExceptionWithinSubmissionOperation(m_graph, *m_submissionLease, m_compiledGraph, m_planAccess, m_packet);
    }


public:
    void arm(const GpuSubmissionPacketId packet, GpuPacketSubmissionLease& submissionLease)noexcept{
        if(m_submissionLease)
            FOUNDATION_FATAL_ASSERT_MSG(false, "packet submission unwind scope may arm exactly once");
        m_packet = packet;
        m_submissionLease = &submissionLease;
    }

    void release()noexcept{ m_submissionLease = nullptr; }


private:
    GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuGraphSubmissionTransaction& m_transaction;
    GpuPacketSubmissionLease* m_submissionLease = nullptr;
    GpuSubmissionPacketId m_packet;
};


struct GpuTaskScheduler::BatchEntryState final{
    GpuSubmissionPacketId packet;
    const GpuRecordedPacket* recordedPacket = nullptr;
    usize timingTicketOffset = 0u;
    usize timingTicketCount = 0u;
    usize waitTokenOffset = 0u;
    usize waitTokenCount = 0u;
    usize preparedTimingTicketCount = 0u;
    usize intraBatchSameQueueElisionCount = 0u;
    GpuPacketRuntimeDetail::PacketWaitStatistics waitStatistics;
    Array<Optional<CommandList::GraphSubmissionOwnership>, GpuRecordedPacket::s_MaxCommandLists> graphSubmissionOwnerships;
    GpuPacketSubmissionLease submissionLease;
    GpuGraphSubmissionTransaction::NativeSubmissionInfo nativeSubmissionInfo;
};

class GpuTaskScheduler::BatchEntryBuffer final : NoCopy{
    static_assert(IsNothrowConstructible_V<BatchEntryState>);


public:
    BatchEntryBuffer(Alloc::ScratchArena& scratchArena, const usize count)noexcept
        : m_scratchArena(scratchArena)
    {
        if(count == 0u || MultiplyOverflows<usize>(count, sizeof(BatchEntryState)))
            return;

        const usize byteSize = count * sizeof(BatchEntryState);
        void* const storage = m_scratchArena.allocate(alignof(BatchEntryState), byteSize);
        if(!storage)
            return;

        m_entries = static_cast<BatchEntryState*>(storage);
        m_byteSize = byteSize;
        for(usize index = 0u; index < count; ++index)
            new(m_entries + index) BatchEntryState();
        m_count = count;
    }
    ~BatchEntryBuffer()noexcept{
        if(!m_entries)
            return;

        for(usize index = m_count; index > 0u; --index)
            m_entries[index - 1u].~BatchEntryState();
        m_scratchArena.deallocate(m_entries, alignof(BatchEntryState), m_byteSize);
    }


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_entries != nullptr; }
    [[nodiscard]] BatchEntryState* data()noexcept{ return m_entries; }
    [[nodiscard]] BatchEntryState& operator[](const usize index)noexcept{ return m_entries[index]; }


private:
    Alloc::ScratchArena& m_scratchArena;
    BatchEntryState* m_entries = nullptr;
    usize m_count = 0u;
    usize m_byteSize = 0u;
};

class GpuTaskScheduler::BatchPreparedTimingTicketsUnwindScope final : NoCopy{
private:
    struct Mode{
        enum Enum : u8{
            Rollback,
            Abandon,
            Released,
        };
    };


public:
    BatchPreparedTimingTicketsUnwindScope(
        BatchEntryState* const entries,
        const Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena>& tickets,
        const usize& entryCount
    )noexcept
        : m_entries(entries)
        , m_tickets(tickets)
        , m_entryCount(entryCount)
    {}
    ~BatchPreparedTimingTicketsUnwindScope()noexcept{
        if(m_mode == Mode::Released || !m_entries)
            return;

        for(usize entryIndex = m_beginIndex; entryIndex < m_entryCount; ++entryIndex){
            BatchEntryState& entry = m_entries[entryIndex];
            while(entry.preparedTimingTicketCount > 0u){
                const usize ticketIndex = entry.timingTicketOffset + --entry.preparedTimingTicketCount;
                if(m_mode == Mode::Rollback)
                    m_tickets[ticketIndex]->rollbackPreparedSubmission();
                else
                    m_tickets[ticketIndex]->abandonWithoutCallbacks();
            }
        }
    }


public:
    void abandonOnUnwind()noexcept{ m_mode = Mode::Abandon; }
    void advanceTo(const usize beginIndex)noexcept{ m_beginIndex = beginIndex; }
    void release()noexcept{ m_mode = Mode::Released; }


private:
    BatchEntryState* m_entries = nullptr;
    const Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena>& m_tickets;
    usize m_beginIndex = 0u;
    const usize& m_entryCount;
    Mode::Enum m_mode = Mode::Rollback;
};

class GpuTaskScheduler::BatchSubmittingPacketsUnwindScope final : NoCopy{
public:
    BatchSubmittingPacketsUnwindScope(GpuTaskGraph& graph, GpuGraphSubmissionTransaction& transaction, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, BatchEntryState* const entries)noexcept
        : m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_transaction(transaction)
        , m_entries(entries)
    {}
    ~BatchSubmittingPacketsUnwindScope()noexcept{
        if(!m_entries || m_beginIndex >= m_endIndex)
            return;

        NothrowScopedLock resolutionLock(m_transaction.m_resolutionMutex);
        for(usize entryIndex = m_beginIndex; entryIndex < m_endIndex; ++entryIndex){
            BatchEntryState& entry = m_entries[entryIndex];
            if(entry.submissionLease.valid())
                m_transaction.abandonSubmittingPacketAfterExceptionWithinSubmissionOperation(m_graph, entry.submissionLease, m_compiledGraph, m_planAccess, entry.packet);
        }
    }


public:
    void arm(const usize endIndex)noexcept{ m_endIndex = endIndex; }
    void advanceTo(const usize beginIndex)noexcept{ m_beginIndex = beginIndex; }
    void release()noexcept{ m_endIndex = 0u; }


private:
    GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuGraphSubmissionTransaction& m_transaction;
    BatchEntryState* m_entries = nullptr;
    usize m_beginIndex = 0u;
    usize m_endIndex = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskScheduler::submitPacketWithinSubmissionOperation(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph& recordedGraph,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuSubmissionPacketId& packetID,
    const Span<const NotNull<GpuTimingSubmissionTicket*>> timingTickets,
    const QueueSubmissionPreSubmitHook* const preSubmitHook,
    const GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback>& taskAcceptedCallbacks,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuGraphSubmissionTransaction& transaction
)const{
    if(!declarationAccess.validFor(graph) || !planAccess.validFor(compiledGraph) || !artifactAccess.validFor(recordedGraph) || !GpuGraphSubmissionTransaction::SubmissionOperation::ActiveFor(transaction))
        return false;

    const usize timingTicketCount = timingTickets.size();
    if(
        device().getDeviceGeneration() != planAccess.deviceGeneration()
        || !recordedGraph.validForWithinArtifactOperation(graph, declarationAccess, compiledGraph, planAccess, artifactAccess)
        || !transaction.validFor(planAccess)
        || (timingTicketCount != 0u && !timingTickets.data())
        || (preSubmitHook && !preSubmitHook->valid())
    )
        return false;

    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packet = *packetView.plan;
    GpuTimingSubmissionTicket* const ownedTimingTicket = recordedGraph.packetTimingTicket(packetID, artifactAccess);
    if(packet.recordsTiming != static_cast<b8>(ownedTimingTicket))
        return false;
    Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena> submissionTimingTickets(scratchArena);
    submissionTimingTickets.reserve(timingTicketCount + (ownedTimingTicket ? 1u : 0u));
    if(ownedTimingTicket)
        submissionTimingTickets.push_back(MakeNotNull(ownedTimingTicket));
    for(usize timingTicketIndex = 0u; timingTicketIndex < timingTicketCount; ++timingTicketIndex){
        if(timingTickets[timingTicketIndex].get() != ownedTimingTicket)
            submissionTimingTickets.push_back(timingTickets[timingTicketIndex]);
    }
    if(!graph.packetReadyForSubmission(compiledGraph, planAccess, packetID, recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactAccess)))
        return false;

    const GpuRecordedPacket* const recordedPacket = recordedGraph.findWithinArtifactOperation(packetID, artifactAccess);
    const GpuPhysicalQueueInfo* const queue = planAccess.queueInfo(packet.queue);
    if(!recordedPacket || recordedPacket->commandListCount == 0u || recordedPacket->commandListCount > GpuRecordedPacket::s_MaxCommandLists || !queue || !device().matchesPhysicalQueueIdentity(packet.queue)){
        return false;
    }
    for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex){
        CommandList* const commandList = recordedPacket->commandLists[commandListIndex];
        if(!commandList)
            return false;
    }

    Vector<QueueSubmissionToken, Alloc::ScratchArena> waitTokens(scratchArena);
    waitTokens.reserve(packet.dependencyCount + packet.externalDependencyCount + (packet.joinsAcceptedQueueFrontier ? planAccess.packetCount() : 0u));
    const GpuPacketDependency* const dependencies = packetView.dependencies;
    for(u32 dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
        const QueueSubmissionToken token = transaction.packetToken(dependencies[dependencyIndex].producer);
        if(!token.valid())
            return false;
        waitTokens.push_back(token);
    }

    const GpuExternalCompletionId* const externalDependencies = packetView.externalDependencies;
    for(u32 dependencyIndex = 0u; dependencyIndex < packet.externalDependencyCount; ++dependencyIndex){
        const GpuExternalCompletionId completion = externalDependencies[dependencyIndex];
        const QueueSubmissionToken* const token = declarationAccess.externalCompletionToken(completion);
        if(!token)
            return false;
        const GpuPhysicalQueueInfo* const externalQueue = device().getPhysicalQueueInfo(GpuPhysicalQueueId{ .index = token->physicalQueueIndex, .deviceGeneration = token->deviceGeneration, });
        if(!externalQueue || externalQueue->queueClass != token->queue)
            return false;
        waitTokens.push_back(*token);
    }

    if(packet.externalDependencyCount != 0u && !GpuPacketRuntimeDetail::ValidateInitialOwnershipCompletions(declarationAccess, planAccess, packetID))
        return false;

    if(packet.joinsAcceptedQueueFrontier && !transaction.appendAcceptedQueueFrontierWaitTokens(packet.queue, waitTokens))
        return false;

    SubmittingPacketUnwindScope submittingPacketUnwind(graph, transaction, compiledGraph, planAccess);
    usize preparedTimingTicketCount = 0u;
    PreparedTimingTicketsUnwindScope preparedTimingTicketsUnwind(submissionTimingTickets.data(), preparedTimingTicketCount);
    for(; preparedTimingTicketCount < submissionTimingTickets.size(); ++preparedTimingTicketCount){
        if(submissionTimingTickets[preparedTimingTicketCount]->prepareSubmissionAfterCommandListValidation(Span<CommandList* const>(recordedPacket->commandLists.data(), recordedPacket->commandListCount), waitTokens))
            continue;
        return false;
    }

    Array<Optional<CommandList::GraphSubmissionOwnership>, GpuRecordedPacket::s_MaxCommandLists> graphSubmissionOwnerships;
    for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex){
        graphSubmissionOwnerships[commandListIndex].emplace(*recordedPacket->commandLists[commandListIndex]);
        if(graphSubmissionOwnerships[commandListIndex].value().m_acquired)
            continue;
        return false;
    }

    for(const QueueSubmissionToken& waitToken : waitTokens){
        if(device().validateSubmissionWaitToken(waitToken))
            continue;
        return false;
    }

    GpuGraphSubmissionTransaction::NativeSubmissionInfo nativeSubmissionInfo;
    nativeSubmissionInfo.commandListCount = recordedPacket->commandListCount;
    const GpuPacketRuntimeDetail::PacketWaitStatistics waitStatistics = GpuPacketRuntimeDetail::CountPacketWaitStatistics(packet.queue, waitTokens, scratchArena);
    nativeSubmissionInfo.plannedWaitTokenCount = waitStatistics.plannedWaitTokenCount;
    nativeSubmissionInfo.sameQueueWaitElisionCount = waitStatistics.sameQueueWaitElisionCount;
    nativeSubmissionInfo.mergedTimelineWaitCount = waitStatistics.mergedTimelineWaitCount;

    GpuPacketSubmissionLease submissionLease;
    if(!transaction.beginPacketSubmission(graph, compiledGraph, packetID, recordedGraph.recordingAttemptGeneration(), submissionLease))
        return false;
    preparedTimingTicketsUnwind.abandonOnUnwind();
    submittingPacketUnwind.arm(packetID, submissionLease);

    QueueSubmissionDesc submitDesc;
    if(!waitTokens.empty())
        submitDesc.setWaitTokens(waitTokens);
    if(preSubmitHook)
        submitDesc.setPreSubmitHook(*preSubmitHook);
    const Timer submissionBegin = TimerNow();
    const auto submission = device().executeGraphCommandLists(recordedPacket->commandLists.data(), recordedPacket->commandListCount, packet.queue, submitDesc);
    const QueueSubmissionToken token = submission ? submission->token : QueueSubmissionToken{};
    const usize emittedTimelineWaitCount = submission ? submission->timelineWaitCount : 0u;
    if(token.valid()){
        for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex){
            graphSubmissionOwnerships[commandListIndex].value().accept();
            graphSubmissionOwnerships[commandListIndex].reset();
        }
    }
    else{
        for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex)
            graphSubmissionOwnerships[commandListIndex].reset();
    }
    SubmittingPacketUnwindScope resolutionSubmittingPacketUnwind(graph, transaction, compiledGraph, planAccess);
    PreparedTimingTicketsUnwindScope resolutionPreparedTimingTicketsUnwind(submissionTimingTickets.data(), preparedTimingTicketCount);
    ScopedLock resolutionLock(transaction.m_resolutionMutex);
    resolutionPreparedTimingTicketsUnwind.abandonOnUnwind();
    resolutionSubmittingPacketUnwind.arm(packetID, submissionLease);
    preparedTimingTicketsUnwind.release();
    submittingPacketUnwind.release();

    if(!token.valid()){
        b8 timingResolved = true;
        for(const auto timingTicket : submissionTimingTickets){
            if(!timingTicket->resolveSubmission({}))
                timingResolved = false;
        }
        resolutionPreparedTimingTicketsUnwind.release();
        resolutionSubmittingPacketUnwind.release();
        transaction.rejectSubmittingPacket(graph, submissionLease, compiledGraph, planAccess, packetID);
        if(submissionLease.valid())
            FOUNDATION_FATAL_ASSERT_MSG(false, "rejected native submission must consume its exact packet lease");
        if(!timingResolved)
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("GPU task graph: Rejected packet failed to discard prepared timing ownership"));
        return false;
    }

    FOUNDATION_ASSERT(token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration));
    if(emittedTimelineWaitCount > waitStatistics.timelineWaitCount)
        TerminateInvariant();
    nativeSubmissionInfo.timelineWaitCount = emittedTimelineWaitCount;
    nativeSubmissionInfo.inheritedTimelineWaitElisionCount = waitStatistics.timelineWaitCount - emittedTimelineWaitCount;
    nativeSubmissionInfo.submissionSeconds = DurationInSeconds<f64>(TimerNow(), submissionBegin);
    resolutionPreparedTimingTicketsUnwind.release();
    resolutionSubmittingPacketUnwind.release();
    return transaction.acceptSubmittingPacket(graph, submissionLease, compiledGraph, packetID, token, nativeSubmissionInfo, submissionTimingTickets.data(), submissionTimingTickets.size(), taskAcceptedCallbacks);
}

b8 GpuTaskScheduler::submitPacketBatchWithinSubmissionOperation(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph& recordedGraph,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const usize batchStart,
    const usize batchEnd,
    const GpuPhysicalQueueId& batchQueue,
    const GpuTaskSubmissionDetail::TaskSubmissionBindings& bindings,
    const GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback>& taskAcceptedCallbacks,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuGraphSubmissionTransaction& transaction,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(!declarationAccess.validFor(graph) || !planAccess.validFor(compiledGraph) || !artifactAccess.validFor(recordedGraph) || !GpuGraphSubmissionTransaction::SubmissionOperation::ActiveFor(transaction))
        return false;
    if(
        device().getDeviceGeneration() != planAccess.deviceGeneration()
        || !recordedGraph.validForWithinArtifactOperation(graph, declarationAccess, compiledGraph, planAccess, artifactAccess)
        || !transaction.validFor(planAccess)
        || !device().matchesPhysicalQueueIdentity(batchQueue)
    )
        return false;
    const GpuPhysicalQueueInfo* const batchQueueInfo = planAccess.queueInfo(batchQueue);
    if(!batchQueueInfo || batchStart >= batchEnd)
        return false;

    const usize initialBatchCount = batchEnd - batchStart;
    BatchEntryBuffer entries(scratchArena, initialBatchCount);
    if(!entries.valid())
        return false;
    Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena> allTimingTickets(scratchArena);
    allTimingTickets.reserve(bindings.timingTickets.size() + initialBatchCount);
    Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena> collectedTimingTickets(scratchArena);
    collectedTimingTickets.reserve(bindings.timingTickets.size());
    Vector<QueueSubmissionToken, Alloc::ScratchArena> allWaitTokens(scratchArena);
    Vector<QueueSubmissionToken, Alloc::ScratchArena> entryWaitTokens(scratchArena);
    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactAccess);
    const b8 allBatchPacketsReady = graph.packetsReadyForSubmission(compiledGraph, planAccess, GpuSubmissionPacketRange{ planAccess.packetIdAt(batchStart), initialBatchCount }, recordingAttemptGeneration);
    const auto truncateTimingTickets = [&](const usize targetSize)noexcept{
        while(allTimingTickets.size() > targetSize)
            allTimingTickets.pop_back();
    };

    usize activeBatchCount = 0u;
    BatchSubmittingPacketsUnwindScope batchSubmittingUnwind(graph, transaction, compiledGraph, planAccess, entries.data());
    BatchPreparedTimingTicketsUnwindScope batchTimingUnwind(entries.data(), allTimingTickets, activeBatchCount);
    GpuSubmissionPacketId trailingSinglePacket;
    const auto prepareEntry = [&](const usize entryIndex) -> b8{
        BatchEntryState& entry = entries[entryIndex];
        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(batchStart + entryIndex);
        entry.packet = packetID;

        const GpuCompiledPacketView packetView = planAccess.packet(packetID);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packet = *packetView.plan;
        if(packet.queue != batchQueue)
            return false;

        GpuTimingSubmissionTicket* const ownedTimingTicket = recordedGraph.packetTimingTicket(packetID, artifactAccess);
        if(packet.recordsTiming != static_cast<b8>(ownedTimingTicket))
            return false;
        if(!allBatchPacketsReady && !graph.packetReadyForSubmission(compiledGraph, planAccess, packetID, recordingAttemptGeneration))
            return false;

        const GpuRecordedPacket* const recordedPacket = recordedGraph.findWithinArtifactOperation(packetID, artifactAccess);
        if(!recordedPacket || recordedPacket->commandListCount == 0u || recordedPacket->commandListCount > GpuRecordedPacket::s_MaxCommandLists)
            return false;
        for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex){
            CommandList* const commandList = recordedPacket->commandLists[commandListIndex];
            if(!commandList)
                return false;
            for(usize earlierEntry = 0u; earlierEntry < entryIndex; ++earlierEntry){
                const GpuRecordedPacket* const earlierPacket = entries[earlierEntry].recordedPacket;
                for(u8 earlierCmdIndex = 0u; earlierCmdIndex < earlierPacket->commandListCount; ++earlierCmdIndex){
                    if(earlierPacket->commandLists[earlierCmdIndex] == commandList)
                        return false;
                }
            }
        }
        entry.recordedPacket = recordedPacket;

        if(bindings.collectPacket(packetID, collectedTimingTickets) != nullptr)
            return false;
        entry.timingTicketOffset = allTimingTickets.size();
        if(ownedTimingTicket)
            allTimingTickets.push_back(MakeNotNull(ownedTimingTicket));
        for(const NotNull<GpuTimingSubmissionTicket*> ticket : collectedTimingTickets){
            if(ticket.get() != ownedTimingTicket)
                allTimingTickets.push_back(ticket);
        }
        entry.timingTicketCount = allTimingTickets.size() - entry.timingTicketOffset;

        entryWaitTokens.clear();
        entryWaitTokens.reserve(packet.dependencyCount + packet.externalDependencyCount + (packet.joinsAcceptedQueueFrontier ? planAccess.packetCount() : 0u));
        const GpuPacketDependency* const dependencies = packetView.dependencies;
        for(u32 dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            const GpuSubmissionPacketId producer = dependencies[dependencyIndex].producer;
            if(producer.generation == planAccess.planGeneration() && producer.index >= batchStart && producer.index < batchStart + entryIndex){
                ++entry.intraBatchSameQueueElisionCount;
                continue;
            }
            const QueueSubmissionToken token = transaction.packetToken(producer);
            if(!token.valid()){
                truncateTimingTickets(entry.timingTicketOffset);
                return false;
            }
            entryWaitTokens.push_back(token);
        }

        const GpuExternalCompletionId* const externalDependencies = packetView.externalDependencies;
        for(u32 dependencyIndex = 0u; dependencyIndex < packet.externalDependencyCount; ++dependencyIndex){
            const GpuExternalCompletionId completion = externalDependencies[dependencyIndex];
            const QueueSubmissionToken* const token = declarationAccess.externalCompletionToken(completion);
            if(!token){
                truncateTimingTickets(entry.timingTicketOffset);
                return false;
            }
            const GpuPhysicalQueueInfo* const externalQueue = device().getPhysicalQueueInfo(GpuPhysicalQueueId{ .index = token->physicalQueueIndex, .deviceGeneration = token->deviceGeneration, });
            if(!externalQueue || externalQueue->queueClass != token->queue){
                truncateTimingTickets(entry.timingTicketOffset);
                return false;
            }
            entryWaitTokens.push_back(*token);
        }

        if(packet.externalDependencyCount != 0u && !GpuPacketRuntimeDetail::ValidateInitialOwnershipCompletions(declarationAccess, planAccess, packetID)){
            truncateTimingTickets(entry.timingTicketOffset);
            return false;
        }
        if(packet.joinsAcceptedQueueFrontier && !transaction.appendAcceptedQueueFrontierWaitTokens(batchQueue, entryWaitTokens)){
            truncateTimingTickets(entry.timingTicketOffset);
            return false;
        }

        for(usize ticketOffset = 0u; ticketOffset < entry.timingTicketCount; ++ticketOffset){
            if(allTimingTickets[entry.timingTicketOffset + ticketOffset]->prepareSubmissionAfterCommandListValidation(Span<CommandList* const>(recordedPacket->commandLists.data(), recordedPacket->commandListCount), entryWaitTokens)){
                ++entry.preparedTimingTicketCount;
                continue;
            }
            while(entry.preparedTimingTicketCount > 0u)
                allTimingTickets[entry.timingTicketOffset + --entry.preparedTimingTicketCount]->rollbackPreparedSubmission();
            truncateTimingTickets(entry.timingTicketOffset);
            return false;
        }

        for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex){
            entry.graphSubmissionOwnerships[commandListIndex].emplace(*recordedPacket->commandLists[commandListIndex]);
            if(entry.graphSubmissionOwnerships[commandListIndex].value().m_acquired)
                continue;
            for(u8 resetIndex = 0u; resetIndex <= commandListIndex; ++resetIndex)
                entry.graphSubmissionOwnerships[resetIndex].reset();
            while(entry.preparedTimingTicketCount > 0u)
                allTimingTickets[entry.timingTicketOffset + --entry.preparedTimingTicketCount]->rollbackPreparedSubmission();
            truncateTimingTickets(entry.timingTicketOffset);
            return false;
        }

        for(const QueueSubmissionToken& waitToken : entryWaitTokens){
            if(device().validateSubmissionWaitToken(waitToken))
                continue;
            for(u8 resetIndex = 0u; resetIndex < recordedPacket->commandListCount; ++resetIndex)
                entry.graphSubmissionOwnerships[resetIndex].reset();
            while(entry.preparedTimingTicketCount > 0u)
                allTimingTickets[entry.timingTicketOffset + --entry.preparedTimingTicketCount]->rollbackPreparedSubmission();
            truncateTimingTickets(entry.timingTicketOffset);
            return false;
        }

        entry.waitStatistics = GpuPacketRuntimeDetail::CountPacketWaitStatistics(batchQueue, entryWaitTokens, scratchArena);
        entry.waitTokenOffset = allWaitTokens.size();
        entry.waitTokenCount = entryWaitTokens.size();
        allWaitTokens.insert(allWaitTokens.end(), entryWaitTokens.begin(), entryWaitTokens.end());
        entry.nativeSubmissionInfo.commandListCount = recordedPacket->commandListCount;
        entry.nativeSubmissionInfo.plannedWaitTokenCount = entry.waitStatistics.plannedWaitTokenCount + entry.intraBatchSameQueueElisionCount;
        entry.nativeSubmissionInfo.sameQueueWaitElisionCount = entry.waitStatistics.sameQueueWaitElisionCount + entry.intraBatchSameQueueElisionCount;
        entry.nativeSubmissionInfo.mergedTimelineWaitCount = entry.waitStatistics.mergedTimelineWaitCount;
        return true;
    };

    for(usize entryIndex = 0u; entryIndex < initialBatchCount; ++entryIndex){
        if(!prepareEntry(entryIndex)){
            if(entryIndex == 0u){
                if(outFailedPacket)
                    *outFailedPacket = entries[0u].packet;
                return false;
            }
            trailingSinglePacket = entries[entryIndex].packet;
            break;
        }
        ++activeBatchCount;
    }

    const usize preparedBatchCount = activeBatchCount;
    Vector<GpuSubmissionPacketId, Alloc::ScratchArena> batchPackets(scratchArena);
    batchPackets.resize(preparedBatchCount);
    Vector<GpuPacketSubmissionLease*, Alloc::ScratchArena> batchLeases(scratchArena);
    batchLeases.resize(preparedBatchCount);
    for(usize entryIndex = 0u; entryIndex < preparedBatchCount; ++entryIndex){
        batchPackets[entryIndex] = entries[entryIndex].packet;
        batchLeases[entryIndex] = &entries[entryIndex].submissionLease;
    }
    const usize claimedBatchCount = transaction.beginPacketSubmissionBatch(
        graph,
        compiledGraph,
        Span<const GpuSubmissionPacketId>(batchPackets.data(), preparedBatchCount),
        recordingAttemptGeneration,
        batchLeases.data()
    );
    if(claimedBatchCount == 0u){
        if(outFailedPacket)
            *outFailedPacket = entries[0u].packet;
        return false;
    }
    if(claimedBatchCount < preparedBatchCount){
        for(usize cleanupIndex = claimedBatchCount; cleanupIndex < preparedBatchCount; ++cleanupIndex){
            BatchEntryState& cleanupEntry = entries[cleanupIndex];
            for(u8 resetIndex = 0u; resetIndex < cleanupEntry.recordedPacket->commandListCount; ++resetIndex)
                cleanupEntry.graphSubmissionOwnerships[resetIndex].reset();
            while(cleanupEntry.preparedTimingTicketCount > 0u)
                allTimingTickets[cleanupEntry.timingTicketOffset + --cleanupEntry.preparedTimingTicketCount]->rollbackPreparedSubmission();
        }
        trailingSinglePacket = entries[claimedBatchCount].packet;
        activeBatchCount = claimedBatchCount;
    }
    batchTimingUnwind.abandonOnUnwind();
    batchSubmittingUnwind.arm(activeBatchCount);

    Vector<Device::GraphCommandListBatchEntry, Alloc::ScratchArena> deviceEntries(scratchArena);
    deviceEntries.resize(activeBatchCount);
    for(usize entryIndex = 0u; entryIndex < activeBatchCount; ++entryIndex){
        const BatchEntryState& entry = entries[entryIndex];
        deviceEntries[entryIndex].commandLists = entry.recordedPacket->commandLists.data();
        deviceEntries[entryIndex].numCommandLists = entry.recordedPacket->commandListCount;
        if(entry.waitTokenCount != 0u)
            deviceEntries[entryIndex].submitDesc.setWaitTokens(Span<const QueueSubmissionToken>(allWaitTokens.data() + entry.waitTokenOffset, entry.waitTokenCount));
    }
    Vector<QueueSubmissionReceipt, Alloc::ScratchArena> receipts(scratchArena);
    receipts.resize(activeBatchCount);

    const Timer submissionBegin = TimerNow();
    const Device::GraphCommandListBatchResult batchResult = device().executeGraphCommandListsBatch(
        Span<const Device::GraphCommandListBatchEntry>(deviceEntries.data(), deviceEntries.size()),
        batchQueue,
        Span<QueueSubmissionReceipt>(receipts.data(), receipts.size())
    );

    if(batchResult.status == Device::GraphCommandListBatchResult::Status::Accepted){
        const f64 batchSeconds = DurationInSeconds<f64>(TimerNow(), submissionBegin);
        const f64 perPacketSeconds = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(batchSeconds), VectorDoubleReplicate(static_cast<f64>(activeBatchCount))));
        Vector<GpuGraphSubmissionTransaction::NativeSubmissionInfo, Alloc::ScratchArena> batchNativeInfos(scratchArena);
        batchNativeInfos.resize(activeBatchCount);
        Vector<usize, Alloc::ScratchArena> batchTimingOffsets(scratchArena);
        batchTimingOffsets.resize(activeBatchCount);
        Vector<usize, Alloc::ScratchArena> batchTimingCounts(scratchArena);
        batchTimingCounts.resize(activeBatchCount);
        for(usize entryIndex = 0u; entryIndex < activeBatchCount; ++entryIndex){
            BatchEntryState& entry = entries[entryIndex];
            for(u8 commandListIndex = 0u; commandListIndex < entry.recordedPacket->commandListCount; ++commandListIndex){
                entry.graphSubmissionOwnerships[commandListIndex].value().accept();
                entry.graphSubmissionOwnerships[commandListIndex].reset();
            }
            FOUNDATION_ASSERT(receipts[entryIndex].token.valid() && receipts[entryIndex].token.matchesPhysicalQueue(batchQueue.index, batchQueue.deviceGeneration));
            if(receipts[entryIndex].timelineWaitCount > entry.waitStatistics.timelineWaitCount)
                TerminateInvariant();
            entry.nativeSubmissionInfo.timelineWaitCount = receipts[entryIndex].timelineWaitCount;
            entry.nativeSubmissionInfo.inheritedTimelineWaitElisionCount = entry.waitStatistics.timelineWaitCount - receipts[entryIndex].timelineWaitCount;
            entry.nativeSubmissionInfo.submissionSeconds = perPacketSeconds;
            batchNativeInfos[entryIndex] = entry.nativeSubmissionInfo;
            batchTimingOffsets[entryIndex] = entry.timingTicketOffset;
            batchTimingCounts[entryIndex] = entry.timingTicketCount;
        }

        ScopedLock resolutionLock(transaction.m_resolutionMutex);
        batchTimingUnwind.release();
        batchSubmittingUnwind.release();
        if(!transaction.acceptSubmittingPacketBatch(
            graph,
            compiledGraph,
            Span<const GpuSubmissionPacketId>(batchPackets.data(), activeBatchCount),
            receipts.data(),
            batchLeases.data(),
            batchNativeInfos.data(),
            allTimingTickets.data(),
            batchTimingOffsets.data(),
            batchTimingCounts.data(),
            taskAcceptedCallbacks,
            outFailedPacket
        ))
            return false;
    }
    else if(batchResult.status == Device::GraphCommandListBatchResult::Status::NativeSubmitFailed){
        for(usize entryIndex = 0u; entryIndex < activeBatchCount; ++entryIndex){
            for(u8 commandListIndex = 0u; commandListIndex < entries[entryIndex].recordedPacket->commandListCount; ++commandListIndex)
                entries[entryIndex].graphSubmissionOwnerships[commandListIndex].reset();
        }
        ScopedLock resolutionLock(transaction.m_resolutionMutex);
        batchTimingUnwind.release();
        batchSubmittingUnwind.release();
        b8 timingResolved = true;
        for(usize entryIndex = 0u; entryIndex < activeBatchCount; ++entryIndex){
            BatchEntryState& entry = entries[entryIndex];
            for(usize ticketOffset = 0u; ticketOffset < entry.timingTicketCount; ++ticketOffset){
                if(!allTimingTickets[entry.timingTicketOffset + ticketOffset]->resolveSubmission({}))
                    timingResolved = false;
            }
            transaction.rejectSubmittingPacket(graph, entry.submissionLease, compiledGraph, planAccess, entry.packet);
            if(entry.submissionLease.valid())
                FOUNDATION_FATAL_ASSERT_MSG(false, "rejected native submission must consume its exact packet lease");
        }
        if(!timingResolved)
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("GPU task graph: Rejected packet failed to discard prepared timing ownership"));
        if(outFailedPacket)
            *outFailedPacket = entries[0u].packet;
        return false;
    }
    else{
        for(usize entryIndex = 0u; entryIndex < activeBatchCount; ++entryIndex){
            BatchEntryState& entry = entries[entryIndex];
            const Timer entryBegin = TimerNow();
            const auto submission = device().executeGraphCommandLists(entry.recordedPacket->commandLists.data(), entry.recordedPacket->commandListCount, batchQueue, deviceEntries[entryIndex].submitDesc);
            const QueueSubmissionToken token = submission ? submission->token : QueueSubmissionToken{};
            const usize emittedTimelineWaitCount = submission ? submission->timelineWaitCount : 0u;
            if(!token.valid()){
                for(usize rejectIndex = entryIndex; rejectIndex < activeBatchCount; ++rejectIndex){
                    for(u8 commandListIndex = 0u; commandListIndex < entries[rejectIndex].recordedPacket->commandListCount; ++commandListIndex)
                        entries[rejectIndex].graphSubmissionOwnerships[commandListIndex].reset();
                }
                ScopedLock resolutionLock(transaction.m_resolutionMutex);
                batchTimingUnwind.release();
                batchSubmittingUnwind.release();
                b8 timingResolved = true;
                for(usize rejectIndex = entryIndex; rejectIndex < activeBatchCount; ++rejectIndex){
                    BatchEntryState& rejectEntry = entries[rejectIndex];
                    for(usize ticketOffset = 0u; ticketOffset < rejectEntry.timingTicketCount; ++ticketOffset){
                        if(!allTimingTickets[rejectEntry.timingTicketOffset + ticketOffset]->resolveSubmission({}))
                            timingResolved = false;
                    }
                    transaction.rejectSubmittingPacket(graph, rejectEntry.submissionLease, compiledGraph, planAccess, rejectEntry.packet);
                    if(rejectEntry.submissionLease.valid())
                        FOUNDATION_FATAL_ASSERT_MSG(false, "rejected native submission must consume its exact packet lease");
                }
                if(!timingResolved)
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("GPU task graph: Rejected packet failed to discard prepared timing ownership"));
                if(outFailedPacket)
                    *outFailedPacket = entry.packet;
                return false;
            }

            for(u8 commandListIndex = 0u; commandListIndex < entry.recordedPacket->commandListCount; ++commandListIndex){
                entry.graphSubmissionOwnerships[commandListIndex].value().accept();
                entry.graphSubmissionOwnerships[commandListIndex].reset();
            }
            FOUNDATION_ASSERT(token.matchesPhysicalQueue(batchQueue.index, batchQueue.deviceGeneration));
            if(emittedTimelineWaitCount > entry.waitStatistics.timelineWaitCount)
                TerminateInvariant();
            entry.nativeSubmissionInfo.timelineWaitCount = emittedTimelineWaitCount;
            entry.nativeSubmissionInfo.inheritedTimelineWaitElisionCount = entry.waitStatistics.timelineWaitCount - emittedTimelineWaitCount;
            entry.nativeSubmissionInfo.submissionSeconds = DurationInSeconds<f64>(TimerNow(), entryBegin);

            ScopedLock resolutionLock(transaction.m_resolutionMutex);
            batchTimingUnwind.advanceTo(entryIndex + 1u);
            batchSubmittingUnwind.advanceTo(entryIndex + 1u);
            const b8 accepted = transaction.acceptSubmittingPacket(
                graph,
                entry.submissionLease,
                compiledGraph,
                entry.packet,
                token,
                entry.nativeSubmissionInfo,
                entry.timingTicketCount != 0u ? allTimingTickets.data() + entry.timingTicketOffset : nullptr,
                entry.timingTicketCount,
                taskAcceptedCallbacks
            );
            if(!accepted){
                for(usize rejectIndex = entryIndex + 1u; rejectIndex < activeBatchCount; ++rejectIndex){
                    for(u8 commandListIndex = 0u; commandListIndex < entries[rejectIndex].recordedPacket->commandListCount; ++commandListIndex)
                        entries[rejectIndex].graphSubmissionOwnerships[commandListIndex].reset();
                }
                batchTimingUnwind.release();
                batchSubmittingUnwind.release();
                b8 timingResolved = true;
                for(usize rejectIndex = entryIndex + 1u; rejectIndex < activeBatchCount; ++rejectIndex){
                    BatchEntryState& rejectEntry = entries[rejectIndex];
                    for(usize ticketOffset = 0u; ticketOffset < rejectEntry.timingTicketCount; ++ticketOffset){
                        if(!allTimingTickets[rejectEntry.timingTicketOffset + ticketOffset]->resolveSubmission({}))
                            timingResolved = false;
                    }
                    transaction.rejectSubmittingPacket(graph, rejectEntry.submissionLease, compiledGraph, planAccess, rejectEntry.packet);
                }
                if(!timingResolved)
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("GPU task graph: Rejected packet failed to discard prepared timing ownership"));
                if(outFailedPacket)
                    *outFailedPacket = entry.packet;
                return false;
            }
        }
        batchTimingUnwind.release();
        batchSubmittingUnwind.release();
    }

    if(trailingSinglePacket.valid()){
        const QueueSubmissionPreSubmitHook* const trailingHook = bindings.collectPacket(trailingSinglePacket, collectedTimingTickets);
        if(!submitPacketWithinSubmissionOperation(
            declarationAccess,
            compiledGraph,
            planAccess,
            recordedGraph,
            artifactAccess,
            trailingSinglePacket,
            Span<const NotNull<GpuTimingSubmissionTicket*>>(collectedTimingTickets.data(), collectedTimingTickets.size()),
            trailingHook,
            taskAcceptedCallbacks,
            scratchArena,
            graph,
            transaction
        )){
            if(outFailedPacket)
                *outFailedPacket = trailingSinglePacket;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

