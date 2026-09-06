// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"
#include "packet_runtime_internal.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>

#include <global/termination.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_submission{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Ordinary external completions may originate on any current-device queue. A completion paired with an imported
// ownership acquire is narrower: it must prove the exact physical source queue that released the resource, or the
// consumer could wait an unrelated timeline and race the Vulkan acquire.
[[nodiscard]] bool ValidateInitialOwnershipCompletion(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId& packetID,
    const GpuExternalCompletionId& completion,
    const QueueSubmissionToken& token
){
    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packet = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuCompiledTaskView compiledTaskView = planAccess.findTask(tasks[taskIndex]);
        if(!compiledTaskView.valid())
            return false;
        const GpuCompiledTask& compiledTask = *compiledTaskView.plan;
        const GpuCompiledBarrier* const barriers = compiledTaskView.prologueBarriers;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask.prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(!barrier.isInitialOwnerHandoff)
                continue;
            if(
                barrier.type != GpuCompiledBarrierType::TextureOwnershipAcquire
                && barrier.type != GpuCompiledBarrierType::BufferOwnershipAcquire
                && barrier.type != GpuCompiledBarrierType::AccelStructOwnershipAcquire
            )
                return false;

            const GpuTaskGraphResourceView resource = declarationAccess.resourceAt(barrier.resource.index);
            const GpuTaskGraphInitialOwnerHandoffSourceView* const multiSource = GpuPacketRuntimeDetail::FindInitialOwnerHandoffSource(resource, barrier);
            if(resource.initialOwnerHandoffSourceCount != 0u && !multiSource)
                return false;
            if(multiSource && multiSource->completion != completion)
                continue;
            if(!multiSource && resource.initialOwnerCompletion != completion)
                continue;
            const GpuPhysicalQueueInfo* const sourceQueue = planAccess.queueInfo(barrier.sourceQueue);
            if(
                !sourceQueue
                || (
                    multiSource
                        ? (
                            multiSource->sourceQueue != barrier.sourceQueue
                            || multiSource->destinationQueue != barrier.destinationQueue
                            || !multiSource->minimumCompletionToken.valid()
                            || !multiSource->minimumCompletionToken.matchesPhysicalQueue(
                                barrier.sourceQueue.index,
                                barrier.sourceQueue.deviceGeneration
                            )
                            || token.value < multiSource->minimumCompletionToken.value
                            || !multiSource->stateSource
                            || !multiSource->stateSource->validForDeviceGeneration(planAccess.deviceGeneration())
                        )
                        : (
                            resource.initialOwnerQueue != barrier.sourceQueue
                            || resource.initialOwnerReleaseDestinationQueue != barrier.destinationQueue
                            || !resource.initialOwnerMinimumCompletionToken.valid()
                            || resource.initialOwnerMinimumCompletionToken.queue != sourceQueue->queueClass
                            || !resource.initialOwnerMinimumCompletionToken.matchesPhysicalQueue(
                                barrier.sourceQueue.index,
                                barrier.sourceQueue.deviceGeneration
                            )
                            || token.value < resource.initialOwnerMinimumCompletionToken.value
                        )
                )
                || token.queue != sourceQueue->queueClass
                || !token.matchesPhysicalQueue(barrier.sourceQueue.index, barrier.sourceQueue.deviceGeneration)
            )
                return false;
        }
    }
    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphSubmitter::PreparedTimingTicketsUnwindScope final : NoCopy{
private:
    enum class Mode : u8{
        Rollback,
        Abandon,
        Released,
    };


public:
    PreparedTimingTicketsUnwindScope(
        GpuTimingSubmissionTicket* const* const tickets,
        usize& preparedTicketCount
    )noexcept
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
    GpuTimingSubmissionTicket* const* m_tickets = nullptr;
    usize& m_preparedTicketCount;
    Mode m_mode = Mode::Rollback;
};


class GpuTaskGraphSubmitter::SubmittingPacketUnwindScope final : NoCopy{
public:
    SubmittingPacketUnwindScope(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuGraphSubmissionTransaction& transaction
    )noexcept
        : m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_transaction(transaction)
    {}
    ~SubmittingPacketUnwindScope()noexcept{
        if(!m_submissionLease)
            return;

        NothrowScopedLock resolutionLock(m_transaction.m_resolutionMutex);
        m_transaction.abandonSubmittingPacketAfterExceptionWithinSubmissionOperation(
            m_graph,
            m_compiledGraph,
            m_planAccess,
            m_packet,
            *m_submissionLease
        );
    }


public:
    void arm(const GpuSubmissionPacketId packet, GpuTaskGraph::PacketSubmissionLease& submissionLease)noexcept{
        if(m_submissionLease){
            NWB_FATAL_ASSERT_MSG(false, "packet submission unwind scope may arm exactly once");
            TerminateInvariant();
        }
        m_packet = packet;
        m_submissionLease = &submissionLease;
    }
    void release()noexcept{ m_submissionLease = nullptr; }


private:
    GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuGraphSubmissionTransaction& m_transaction;
    GpuTaskGraph::PacketSubmissionLease* m_submissionLease = nullptr;
    GpuSubmissionPacketId m_packet;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphSubmitter::submitPacketWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph& recordedGraph,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuSubmissionPacketId& packetID,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuTimingSubmissionTicket* const* const timingTickets,
    const usize timingTicketCount,
    const QueueSubmissionPreSubmitHook* const preSubmitHook,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount
)const{
    if(
        !planAccess.validFor(compiledGraph)
        || !artifactAccess.validFor(recordedGraph)
        || !GpuGraphSubmissionTransaction::SubmissionOperation::activeFor(transaction)
    )
        return false;
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
    if(!declarationAccess.valid())
        return false;

    if(
        !planAccess.validFor(declarationAccess)
        || !declarationAccess.validForDeviceGeneration(planAccess.deviceGeneration())
        || m_device.getDeviceGeneration() != planAccess.deviceGeneration()
        || !planAccess.validPacket(packetID)
        || !recordedGraph.validForWithinArtifactOperation(
            graph,
            declarationAccess,
            compiledGraph,
            planAccess,
            artifactAccess
        )
        || !transaction.validFor(planAccess)
        || !GpuPacketRuntimeDetail::ValidateExternalCompletionBindings(
            graph,
            declarationAccess,
            compiledGraph,
            planAccess,
            externalCompletionTokens,
            externalCompletionTokenCount
        )
        || (timingTicketCount != 0u && !timingTickets)
        || (preSubmitHook && !preSubmitHook->valid())
    )
        return false;

    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packet = *packetView.plan;
    GpuTimingSubmissionTicket* const ownedTimingTicket = recordedGraph.packetTimingTicket(packetID, artifactAccess);
    if(packet.recordsTiming != static_cast<bool>(ownedTimingTicket))
        return false;
    for(usize timingTicketIndex = 0u; timingTicketIndex < timingTicketCount; ++timingTicketIndex){
        GpuTimingSubmissionTicket* const timingTicket = timingTickets[timingTicketIndex];
        if(!timingTicket)
            return false;
        for(usize previousIndex = 0u; previousIndex < timingTicketIndex; ++previousIndex){
            if(timingTickets[previousIndex] == timingTicket)
                return false;
        }
        for(usize ownerIndex = 0u; ownerIndex < planAccess.packetCount(); ++ownerIndex){
            const GpuSubmissionPacketId ownerPacket = planAccess.packetIdAt(ownerIndex);
            if(
                ownerPacket != packetID
                && recordedGraph.packetTimingTicket(ownerPacket, artifactAccess) == timingTicket
            )
                return false;
        }
    }
    Vector<GpuTimingSubmissionTicket*, Alloc::ScratchArena> submissionTimingTickets(scratchArena);
    submissionTimingTickets.reserve(timingTicketCount + (ownedTimingTicket ? 1u : 0u));
    if(ownedTimingTicket)
        submissionTimingTickets.push_back(ownedTimingTicket);
    for(usize timingTicketIndex = 0u; timingTicketIndex < timingTicketCount; ++timingTicketIndex){
        if(timingTickets[timingTicketIndex] != ownedTimingTicket)
            submissionTimingTickets.push_back(timingTickets[timingTicketIndex]);
    }
    if(!graph.packetReadyForSubmission(
        compiledGraph,
        planAccess,
        packetID,
        recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactAccess)
    ))
        return false;

    const GpuRecordedPacket* const recordedPacket = recordedGraph.findWithinArtifactOperation(
        packetID,
        artifactAccess
    );
    const GpuPhysicalQueueInfo* const queue = planAccess.queueInfo(packet.queue);
    // All validation before the transaction reservation is retryable.  A caller that abandons this artifact uses
    // discardUnaccepted() explicitly; a corrected dependency/completion must not discard graph-owned task state.
    if(
        !recordedPacket
        || recordedPacket->commandListCount == 0u
        || recordedPacket->commandListCount > GpuRecordedPacket::s_MaxCommandLists
        || !queue
        || !m_device.matchesPhysicalQueueIdentity(packet.queue)
    ){
        return false;
    }
    for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex){
        CommandList* const commandList = recordedPacket->commandLists[commandListIndex];
        if(!commandList)
            return false;
    }

    Vector<QueueSubmissionToken, Alloc::ScratchArena> waitTokens(scratchArena);
    waitTokens.reserve(
        packet.dependencyCount
        + packet.externalDependencyCount
        + (packet.joinsAcceptedQueueFrontier ? planAccess.packetCount() : 0u)
    );
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
        const QueueSubmissionToken* token = declarationAccess.externalCompletionToken(completion);
        if(!token){
            for(usize tokenIndex = 0u; tokenIndex < externalCompletionTokenCount; ++tokenIndex){
                const GpuTaskGraphExternalCompletionToken& binding = externalCompletionTokens[tokenIndex];
                if(binding.completion == completion){
                    token = &binding.token;
                    break;
                }
            }
        }
        if(!token)
            return false;
        const GpuPhysicalQueueInfo* const externalQueue = m_device.getPhysicalQueueInfo(GpuPhysicalQueueId{
            token->physicalQueueIndex,
            token->deviceGeneration,
        });
        if(
            !externalQueue
            || externalQueue->queueClass != token->queue
            || !__hidden_gpu_packet_runtime_submission::ValidateInitialOwnershipCompletion(
                declarationAccess,
                planAccess,
                packetID,
                completion,
                *token
            )
        )
            return false;
        waitTokens.push_back(*token);
    }

    if(
        packet.joinsAcceptedQueueFrontier
        && !transaction.appendAcceptedQueueFrontierWaitTokens(packet.queue, waitTokens)
    )
        return false;

    // Freeze every timing ticket before validating the complete wait frontier: a query reservation may contribute
    // the accepted frame-reset token that ordered this pool on another physical queue. Preparation remains retryable
    // until the graph packet lease is acquired.
    SubmittingPacketUnwindScope submittingPacketUnwind(graph, compiledGraph, planAccess, transaction);
    usize preparedTimingTicketCount = 0u;
    PreparedTimingTicketsUnwindScope preparedTimingTicketsUnwind(
        submissionTimingTickets.data(),
        preparedTimingTicketCount
    );
    for(; preparedTimingTicketCount < submissionTimingTickets.size(); ++preparedTimingTicketCount){
        if(submissionTimingTickets[preparedTimingTicketCount]->prepareSubmissionAfterCommandListValidation(
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            waitTokens
        ))
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

    // Device repeats this validation at its final boundary because another queue may still be resolving a concurrent
    // native submit. Roll ticket preparation back here so a corrected external dependency can retry this packet.
    for(const QueueSubmissionToken& waitToken : waitTokens){
        if(m_device.validateSubmissionWaitToken(waitToken))
            continue;
        return false;
    }

    GpuGraphSubmissionTransaction::NativeSubmissionInfo nativeSubmissionInfo;
    nativeSubmissionInfo.commandListCount = recordedPacket->commandListCount;
    nativeSubmissionInfo.plannedWaitTokenCount = waitTokens.size();
    for(usize waitIndex = 0u; waitIndex < waitTokens.size(); ++waitIndex){
        const QueueSubmissionToken& waitToken = waitTokens[waitIndex];
        if(waitToken.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)){
            ++nativeSubmissionInfo.sameQueueWaitElisionCount;
            continue;
        }

        bool merged = false;
        for(usize priorWaitIndex = 0u; priorWaitIndex < waitIndex; ++priorWaitIndex){
            const QueueSubmissionToken& priorWaitToken = waitTokens[priorWaitIndex];
            if(
                waitToken.physicalQueueIndex == priorWaitToken.physicalQueueIndex
                && waitToken.deviceGeneration == priorWaitToken.deviceGeneration
            ){
                merged = true;
                break;
            }
        }
        if(merged)
            ++nativeSubmissionInfo.mergedTimelineWaitCount;
        else
            ++nativeSubmissionInfo.timelineWaitCount;
    }

    // A bad dependency or external completion is a pre-submit input error. Preserve the completed native packet
    // so the caller can retry it with corrected tokens; the graph-owned reservation starts only once submission is
    // unavoidable and keeps cancellation from racing Device::executeCommandLists().
    GpuTaskGraph::PacketSubmissionLease submissionLease;
    if(!transaction.beginPacketSubmission(
        graph,
        compiledGraph,
        packetID,
        recordedGraph.recordingAttemptGeneration(),
        submissionLease
    ))
        return false;
    preparedTimingTicketsUnwind.abandonOnUnwind();
    submittingPacketUnwind.arm(packetID, submissionLease);

    QueueSubmissionDesc submitDesc;
    if(!waitTokens.empty())
        submitDesc.setWaitTokens(waitTokens.data(), waitTokens.size());
    if(preSubmitHook)
        submitDesc.setPreSubmitHook(*preSubmitHook);
    const Timer submissionBegin = TimerNow();
    const QueueSubmissionToken token = m_device.executeGraphCommandLists(
        recordedPacket->commandLists,
        recordedPacket->commandListCount,
        packet.queue,
        submitDesc
    );
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
    // Device has released its native Queue locks. Serialize only the irreversible CPU publication tail so another
    // ordinary packet may already enter Vulkan without exposing timing, payload, or transaction state out of order.
    SubmittingPacketUnwindScope resolutionSubmittingPacketUnwind(graph, compiledGraph, planAccess, transaction);
    PreparedTimingTicketsUnwindScope resolutionPreparedTimingTicketsUnwind(
        submissionTimingTickets.data(),
        preparedTimingTicketCount
    );
    ScopedLock resolutionLock(transaction.m_resolutionMutex);
    resolutionPreparedTimingTicketsUnwind.abandonOnUnwind();
    resolutionSubmittingPacketUnwind.arm(packetID, submissionLease);
    preparedTimingTicketsUnwind.release();
    submittingPacketUnwind.release();

    if(!token.valid()){
        bool timingResolved = true;
        for(GpuTimingSubmissionTicket* const timingTicket : submissionTimingTickets){
            if(!timingTicket->resolveSubmission({}))
                timingResolved = false;
        }
        resolutionPreparedTimingTicketsUnwind.release();
        resolutionSubmittingPacketUnwind.release();
        transaction.rejectSubmittingPacket(graph, compiledGraph, planAccess, packetID, submissionLease);
        if(submissionLease.valid()){
            NWB_FATAL_ASSERT_MSG(false, "rejected native submission must consume its exact packet lease");
            TerminateInvariant();
        }
        if(!timingResolved)
            NWB_LOGGER_ERROR(NWB_TEXT("GPU task graph: Rejected packet failed to discard prepared timing ownership"));
        return false;
    }

    NWB_ASSERT(token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration));
    nativeSubmissionInfo.submissionSeconds = DurationInSeconds<f64>(TimerNow(), submissionBegin);
    resolutionPreparedTimingTicketsUnwind.release();
    resolutionSubmittingPacketUnwind.release();
    return transaction.acceptSubmittingPacket(
        graph,
        compiledGraph,
        packetID,
        token,
        submissionLease,
        nativeSubmissionInfo,
        submissionTimingTickets.data(),
        submissionTimingTickets.size(),
        taskAcceptedCallbacks,
        taskAcceptedCallbackCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

