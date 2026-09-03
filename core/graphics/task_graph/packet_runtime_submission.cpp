// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"
#include "packet_runtime_internal.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>

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
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const GpuExternalCompletionId& completion,
    const QueueSubmissionToken& token
){
    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    if(packet.taskCount != 0u && !tasks)
        return false;

    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(tasks[taskIndex]);
        const GpuCompiledBarrier* const barriers = compiledGraph.taskPrologueBarriers(tasks[taskIndex]);
        if(!compiledTask || (compiledTask->prologueBarrierCount != 0u && !barriers))
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(!barrier.isInitialOwnerHandoff)
                continue;
            if(
                barrier.type != GpuCompiledBarrierType::TextureOwnershipAcquire
                && barrier.type != GpuCompiledBarrierType::BufferOwnershipAcquire
                && barrier.type != GpuCompiledBarrierType::AccelStructOwnershipAcquire
            )
                return false;

            const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
            const GpuTaskGraphInitialOwnerHandoffSourceView* const multiSource = GpuPacketRuntimeDetail::FindInitialOwnerHandoffSource(resource, barrier);
            if(resource.initialOwnerHandoffSourceCount != 0u && !multiSource)
                return false;
            if(multiSource && multiSource->completion != completion)
                continue;
            if(!multiSource && resource.initialOwnerCompletion != completion)
                continue;
            const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
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
                            || !multiSource->stateSource->validForDeviceGeneration(compiledGraph.deviceGeneration())
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphSubmitter::submitPacketWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
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
        !compiledGraph.validFor(graph)
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacket(packetID)
        || !recordedGraph.validFor(graph, compiledGraph)
        || !transaction.validFor(compiledGraph)
        || !GpuPacketRuntimeDetail::ValidateExternalCompletionBindings(
            graph,
            compiledGraph,
            externalCompletionTokens,
            externalCompletionTokenCount
        )
        || (timingTicketCount != 0u && !timingTickets)
        || (preSubmitHook && !preSubmitHook->valid())
    )
        return false;

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    GpuTimingSubmissionTicket* const ownedTimingTicket = recordedGraph.packetTimingTicket(packetID);
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
        for(usize ownerIndex = 0u; ownerIndex < compiledGraph.packetCount(); ++ownerIndex){
            const GpuSubmissionPacketId ownerPacket = compiledGraph.packetIdAt(ownerIndex);
            if(ownerPacket != packetID && recordedGraph.packetTimingTicket(ownerPacket) == timingTicket)
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
        packetID,
        recordedGraph.recordingAttemptGeneration()
    ))
        return false;

    const GpuRecordedPacket* const recordedPacket = recordedGraph.find(packetID);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packet.queue);
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
        if(!commandList || !commandList->hasCommandBuffer())
            return false;
    }

    Vector<QueueSubmissionToken, Alloc::ScratchArena> waitTokens(scratchArena);
    waitTokens.reserve(
        packet.dependencyCount
        + packet.externalDependencyCount
        + (packet.joinsAcceptedQueueFrontier ? compiledGraph.packetCount() : 0u)
    );
    const GpuPacketDependency* const dependencies = compiledGraph.packetDependencies(packetID);
    for(u32 dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
        const QueueSubmissionToken token = transaction.packetToken(dependencies[dependencyIndex].producer);
        if(!token.valid())
            return false;
        waitTokens.push_back(token);
    }

    const GpuExternalCompletionId* const externalDependencies = compiledGraph.packetExternalDependencies(packetID);
    for(u32 dependencyIndex = 0u; dependencyIndex < packet.externalDependencyCount; ++dependencyIndex){
        const GpuExternalCompletionId completion = externalDependencies[dependencyIndex];
        const QueueSubmissionToken* token = graph.externalCompletionToken(completion);
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
                graph,
                compiledGraph,
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
    usize preparedTimingTicketCount = 0u;
    for(; preparedTimingTicketCount < submissionTimingTickets.size(); ++preparedTimingTicketCount){
        if(submissionTimingTickets[preparedTimingTicketCount]->prepareSubmission(
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            waitTokens
        ))
            continue;
        while(preparedTimingTicketCount > 0u)
            submissionTimingTickets[--preparedTimingTicketCount]->rollbackPreparedSubmission();
        return false;
    }

    // Device repeats this validation at its final boundary because another queue may still be resolving a concurrent
    // native submit. Roll ticket preparation back here so a corrected external dependency can retry this packet.
    for(const QueueSubmissionToken& waitToken : waitTokens){
        if(m_device.validateSubmissionWaitToken(waitToken))
            continue;
        while(preparedTimingTicketCount > 0u)
            submissionTimingTickets[--preparedTimingTicketCount]->rollbackPreparedSubmission();
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
    )){
        while(preparedTimingTicketCount > 0u)
            submissionTimingTickets[--preparedTimingTicketCount]->rollbackPreparedSubmission();
        return false;
    }

    QueueSubmissionDesc submitDesc;
    if(!waitTokens.empty())
        submitDesc.setWaitTokens(waitTokens.data(), waitTokens.size());
    if(preSubmitHook)
        submitDesc.setPreSubmitHook(*preSubmitHook);
    const Timer submissionBegin = TimerNow();
    const QueueSubmissionToken token = m_device.executeCommandLists(
        recordedPacket->commandLists,
        recordedPacket->commandListCount,
        packet.queue,
        submitDesc
    );
    // Device has released its native Queue locks. Serialize only the irreversible CPU publication tail so another
    // ordinary packet may already enter Vulkan without exposing timing, payload, or transaction state out of order.
    ScopedLock resolutionLock(transaction.m_resolutionMutex);

    for(usize timingTicketIndex = 0u; timingTicketIndex < submissionTimingTickets.size(); ++timingTicketIndex)
        submissionTimingTickets[timingTicketIndex]->resolveSubmission(token);
    if(!token.valid()){
        transaction.rejectSubmittingPacket(graph, compiledGraph, packetID, submissionLease);
        return false;
    }

    NWB_ASSERT(token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration));
    nativeSubmissionInfo.submissionSeconds = DurationInSeconds<f64>(TimerNow(), submissionBegin);
    return transaction.acceptSubmittingPacket(
        graph,
        compiledGraph,
        packetID,
        token,
        submissionLease,
        nativeSubmissionInfo,
        taskAcceptedCallbacks,
        taskAcceptedCallbackCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

