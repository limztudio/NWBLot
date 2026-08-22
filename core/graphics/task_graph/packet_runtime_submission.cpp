// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_submission{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 TextureRangeEnd(
    const u32 base,
    const u32 count,
    const u32 all
)noexcept{
    return count == all ? Limit<u64>::s_Max : static_cast<u64>(base) + static_cast<u64>(count);
}

[[nodiscard]] static bool TextureRangeContains(
    const TextureSubresourceSet& outer,
    const TextureSubresourceSet& inner
)noexcept{
    return outer.baseMipLevel <= inner.baseMipLevel
        && TextureRangeEnd(outer.baseMipLevel, outer.numMipLevels, TextureSubresourceSet::AllMipLevels)
            >= TextureRangeEnd(inner.baseMipLevel, inner.numMipLevels, TextureSubresourceSet::AllMipLevels)
        && outer.baseArraySlice <= inner.baseArraySlice
        && TextureRangeEnd(outer.baseArraySlice, outer.numArraySlices, TextureSubresourceSet::AllArraySlices)
            >= TextureRangeEnd(inner.baseArraySlice, inner.numArraySlices, TextureSubresourceSet::AllArraySlices)
    ;
}

[[nodiscard]] static const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraphResourceView& resource,
    const GpuCompiledBarrier& barrier
)noexcept{
    if(
        resource.initialOwnerHandoffSourceCount == 0u
        || !resource.initialOwnerHandoffSources
        || resource.type != GpuGraphResourceType::Texture
    )
        return nullptr;

    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u;
        sourceIndex < resource.initialOwnerHandoffSourceCount;
        ++sourceIndex
    ){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        if(
            source.sourceQueue != barrier.sourceQueue
            || source.destinationQueue != barrier.destinationQueue
            || !TextureRangeContains(source.range.textureSubresources, barrier.range.textureSubresources)
        )
            continue;
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}
[[nodiscard]] bool ValidateTaskAcceptedCallbacks(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphTaskAcceptedCallback* const callbacks,
    const usize callbackCount
){
    if(callbackCount != 0u && !callbacks)
        return false;

    for(usize callbackIndex = 0u; callbackIndex < callbackCount; ++callbackIndex){
        const GpuTaskGraphTaskAcceptedCallback& callback = callbacks[callbackIndex];
        if(
            !callback.invoke
            || !graph.validTask(callback.task)
            || !compiledGraph.findTask(callback.task)
        )
            return false;

        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(callback.task);
        if(
            !packet.valid()
            || packet.index < range.first.index
            || static_cast<usize>(packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;

        for(usize previousIndex = 0u; previousIndex < callbackIndex; ++previousIndex){
            if(callbacks[previousIndex].task == callback.task)
                return false;
        }
    }
    return true;
}
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
            const GpuTaskGraphInitialOwnerHandoffSourceView* const multiSource = FindInitialOwnerHandoffSource(resource, barrier);
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


bool GpuTaskGraphSubmitter::submitPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketId& packetID,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuTimingSubmissionTicket* const timingTicket,
    const QueueSubmissionPreSubmitHook* const preSubmitHook
)const{
    if(
        !compiledGraph.validFor(graph)
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacket(packetID)
        || !recordedGraph.validFor(graph, compiledGraph)
        || !transaction.validFor(compiledGraph)
        || (externalCompletionTokenCount > 0u && !externalCompletionTokens)
        || (preSubmitHook && !preSubmitHook->valid())
    )
        return false;

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    if(!graph.packetReadyForSubmission(
        compiledGraph,
        tasks,
        packet.taskCount,
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
        QueueSubmissionToken token;
        for(usize tokenIndex = 0u; tokenIndex < externalCompletionTokenCount; ++tokenIndex){
            const GpuTaskGraphExternalCompletionToken& binding = externalCompletionTokens[tokenIndex];
            if(binding.completion == completion){
                if(
                    !binding.validFor(compiledGraph)
                    || !__hidden_gpu_packet_runtime_submission::ValidateInitialOwnershipCompletion(
                        graph,
                        compiledGraph,
                        packetID,
                        completion,
                        binding.token
                    )
                )
                    return false;
                token = binding.token;
                break;
            }
        }
        if(!token.valid())
            return false;
        waitTokens.push_back(token);
    }

    if(
        packet.joinsAcceptedQueueFrontier
        && !transaction.appendAcceptedQueueFrontierWaitTokens(packet.queue, waitTokens)
    )
        return false;

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
    GpuTaskPacketSubmissionLease submissionLease;
    if(!transaction.beginPacketSubmission(
        graph,
        compiledGraph,
        packetID,
        recordedGraph.recordingAttemptGeneration(),
        submissionLease
    ))
        return false;

    QueueSubmissionDesc submitDesc;
    if(!waitTokens.empty())
        submitDesc.setWaitTokens(waitTokens.data(), waitTokens.size());
    if(preSubmitHook)
        submitDesc.setPreSubmitHook(*preSubmitHook);
    const Timer submissionBegin = TimerNow();
    const QueueSubmissionToken token = timingTicket
        ? timingTicket->submit(
            m_device,
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            packet.queue,
            submitDesc
        )
        : m_device.executeCommandLists(
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            packet.queue,
            submitDesc
        )
    ;
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
        &nativeSubmissionInfo
    );
}


bool GpuTaskGraphSubmitter::submitPacketRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphPacketTimingTicket* const timingTickets,
    const usize timingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback,
    const GpuTaskGraphPacketSubmissionHook* const submissionHooks,
    const usize submissionHookCount,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !recordedGraph.validFor(graph, compiledGraph)
        || !transaction.validFor(compiledGraph)
        || !compiledGraph.validPacketRange(range)
        || (externalCompletionTokenCount != 0u && !externalCompletionTokens)
        || (timingTicketCount != 0u && !timingTickets)
        || (submissionHookCount != 0u && !submissionHooks)
        || (acceptedCallback && !acceptedCallback->invoke)
        || !__hidden_gpu_packet_runtime_submission::ValidateTaskAcceptedCallbacks(
            graph,
            compiledGraph,
            range,
            taskAcceptedCallbacks,
            taskAcceptedCallbackCount
        )
    )
        return false;

    for(usize tokenIndex = 0u; tokenIndex < externalCompletionTokenCount; ++tokenIndex){
        const GpuTaskGraphExternalCompletionToken& token = externalCompletionTokens[tokenIndex];
        if(!token.validFor(compiledGraph))
            return false;
        for(usize previousIndex = 0u; previousIndex < tokenIndex; ++previousIndex){
            if(externalCompletionTokens[previousIndex].completion == token.completion)
                return false;
        }
    }
    for(usize ticketIndex = 0u; ticketIndex < timingTicketCount; ++ticketIndex){
        const GpuTaskGraphPacketTimingTicket& ticket = timingTickets[ticketIndex];
        if(
            !ticket.timingTicket
            || !compiledGraph.validPacket(ticket.packet)
            || ticket.packet.index < range.first.index
            || static_cast<usize>(ticket.packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;
        for(usize previousIndex = 0u; previousIndex < ticketIndex; ++previousIndex){
            if(timingTickets[previousIndex].packet == ticket.packet)
                return false;
        }
    }
    for(usize hookIndex = 0u; hookIndex < submissionHookCount; ++hookIndex){
        const GpuTaskGraphPacketSubmissionHook& hook = submissionHooks[hookIndex];
        if(
            !hook.hook.valid()
            || !compiledGraph.validPacket(hook.packet)
            || hook.packet.index < range.first.index
            || static_cast<usize>(hook.packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;
        for(usize previousIndex = 0u; previousIndex < hookIndex; ++previousIndex){
            if(submissionHooks[previousIndex].packet == hook.packet)
                return false;
        }
    }

    // Packet IDs follow the compiler's topological task order. submitPacket resolves each internal producer from
    // transaction state, while every external completion remains a range-wide binding rather than a renderer-side
    // per-stage submission argument.
    for(
        usize packetIndex = range.first.index;
        packetIndex < static_cast<usize>(range.first.index) + range.packetCount;
        ++packetIndex
    ){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        for(usize ticketIndex = 0u; ticketIndex < timingTicketCount; ++ticketIndex){
            if(timingTickets[ticketIndex].packet == packet){
                timingTicket = timingTickets[ticketIndex].timingTicket;
                break;
            }
        }
        const QueueSubmissionPreSubmitHook* preSubmitHook = nullptr;
        for(usize hookIndex = 0u; hookIndex < submissionHookCount; ++hookIndex){
            if(submissionHooks[hookIndex].packet == packet){
                preSubmitHook = &submissionHooks[hookIndex].hook;
                break;
            }
        }
        if(!submitPacket(
            graph,
            compiledGraph,
            recordedGraph,
            packet,
            externalCompletionTokens,
            externalCompletionTokenCount,
            transaction,
            scratchArena,
            timingTicket,
            preSubmitHook
        )){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
        const QueueSubmissionToken token = transaction.packetToken(packet);
        if(acceptedCallback && (!token.valid() || !acceptedCallback->invoke(
            acceptedCallback->context,
            packet,
            token
        ))){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
        if(taskAcceptedCallbackCount != 0u){
            const GpuSubmissionPacket& submittedPacket = compiledGraph.packet(packet);
            const GpuTaskId* const submittedTasks = compiledGraph.packetTasks(packet);
            if(!token.valid() || (submittedPacket.taskCount != 0u && !submittedTasks)){
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
            for(u32 taskIndex = 0u; taskIndex < submittedPacket.taskCount; ++taskIndex){
                for(usize callbackIndex = 0u; callbackIndex < taskAcceptedCallbackCount; ++callbackIndex){
                    const GpuTaskGraphTaskAcceptedCallback& callback = taskAcceptedCallbacks[callbackIndex];
                    if(
                        callback.task == submittedTasks[taskIndex]
                        && !callback.invoke(callback.context, token)
                    ){
                        if(outFailedPacket)
                            *outFailedPacket = packet;
                        return false;
                    }
                }
            }
        }
    }
    return true;
}


bool GpuTaskGraphSubmitter::submitTaskRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphPacketTimingTicket* const timingTickets,
    const usize timingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback,
    const GpuTaskGraphPacketSubmissionHook* const submissionHooks,
    const usize submissionHookCount,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount
)const{
    return submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.packetRangeForTasks(firstTask, lastTask),
        externalCompletionTokens,
        externalCompletionTokenCount,
        timingTickets,
        timingTicketCount,
        transaction,
        scratchArena,
        outFailedPacket,
        acceptedCallback,
        submissionHooks,
        submissionHookCount,
        taskAcceptedCallbacks,
        taskAcceptedCallbackCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

