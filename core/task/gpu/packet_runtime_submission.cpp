// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime_internal.h"
#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRuntimeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateInitialOwnershipCompletionToken(
    const GpuTaskGraphInitialOwnerHandoffSourceView& source,
    const GpuCompiledBarrier& barrier,
    const GpuPhysicalQueueInfo& sourceQueue,
    const QueueSubmissionToken& token,
    const u16 deviceGeneration
)noexcept{
    return
        source.sourceQueue == barrier.sourceQueue
        && source.destinationQueue == barrier.destinationQueue
        && source.minimumCompletionToken.valid()
        && source.minimumCompletionToken.matchesPhysicalQueue(barrier.sourceQueue.index, barrier.sourceQueue.deviceGeneration)
        && token.value >= source.minimumCompletionToken.value
        && source.stateSource
        && source.stateSource->validForDeviceGeneration(deviceGeneration)
        && token.queue == sourceQueue.queueClass
        && token.matchesPhysicalQueue(barrier.sourceQueue.index, barrier.sourceQueue.deviceGeneration)
    ;
}

// External completions may come from any current-device queue. A completion paired with an imported acquire is narrower: it must prove the exact source queue that released the resource.
[[nodiscard]] bool ValidateInitialOwnershipCompletions(const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId& packetID)noexcept{
    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packet = *packetView.plan;
    if(packet.externalDependencyCount == 0u)
        return true;
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
            if(barrier.type != GpuCompiledBarrierType::TextureOwnershipAcquire && barrier.type != GpuCompiledBarrierType::BufferOwnershipAcquire && barrier.type != GpuCompiledBarrierType::AccelStructOwnershipAcquire)
                return false;

            const GpuTaskGraphResourceView resource = declarationAccess.resourceAt(barrier.resource.index);
            const GpuTaskGraphInitialOwnerHandoffSourceView* const source = GpuPacketRuntimeDetail::FindInitialOwnerHandoffSource(resource, barrier);
            if(!source)
                return false;
            const QueueSubmissionToken* const token = declarationAccess.externalCompletionToken(source->completion);
            if(!token)
                return false;
            const GpuPhysicalQueueInfo* const sourceQueue = planAccess.queueInfo(barrier.sourceQueue);
            if(!sourceQueue || !ValidateInitialOwnershipCompletionToken(*source, barrier, *sourceQueue, *token, planAccess.deviceGeneration()))
                return false;
        }
    }
    return true;
}

[[nodiscard]] PacketWaitStatistics CountPacketWaitStatistics(const GpuPhysicalQueueId queue, const QueueSubmissionToken* const waitTokens, const usize waitTokenCount, Alloc::ScratchArena& scratchArena){
    constexpr usize s_InlineQueueCount = 8u;
    Array<GpuPhysicalQueueId, s_InlineQueueCount> inlineQueues;
    usize inlineQueueCount = 0u;
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> overflowQueues(scratchArena);
    PacketWaitStatistics statistics;
    statistics.plannedWaitTokenCount = waitTokenCount;
    for(usize waitIndex = 0u; waitIndex < waitTokenCount; ++waitIndex){
        const QueueSubmissionToken& waitToken = waitTokens[waitIndex];
        if(waitToken.matchesPhysicalQueue(queue.index, queue.deviceGeneration)){
            ++statistics.sameQueueWaitElisionCount;
            continue;
        }

        const GpuPhysicalQueueId waitQueue{ .index = waitToken.physicalQueueIndex, .deviceGeneration = waitToken.deviceGeneration, };
        const GpuPhysicalQueueId* const distinctQueues = overflowQueues.empty() ? inlineQueues.data() : overflowQueues.data();
        const usize distinctQueueCount = overflowQueues.empty() ? inlineQueueCount : overflowQueues.size();
        bool merged = false;
        for(usize queueIndex = 0u; queueIndex < distinctQueueCount; ++queueIndex){
            if(distinctQueues[queueIndex] == waitQueue){
                merged = true;
                break;
            }
        }
        if(merged){
            ++statistics.mergedTimelineWaitCount;
            continue;
        }

        if(inlineQueueCount < s_InlineQueueCount){
            inlineQueues[inlineQueueCount] = waitQueue;
            ++inlineQueueCount;
        }
        else{
            if(overflowQueues.empty()){
                overflowQueues.reserve(Min(waitTokenCount, s_InlineQueueCount * 2u));
                overflowQueues.insert(overflowQueues.end(), inlineQueues.begin(), inlineQueues.end());
            }
            overflowQueues.push_back(waitQueue);
        }
        ++statistics.timelineWaitCount;
    }
    return statistics;
}

[[nodiscard]] bool ValidateExternalDependencyTokens(const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketRange& range)noexcept{
    if(!planAccess.validFor(declarationAccess) || !planAccess.validPacketRange(range))
        return false;

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuCompiledPacketView packetView = planAccess.packet(planAccess.packetIdAt(packetIndex));
        if(!packetView.valid())
            return false;
        for(u32 dependencyIndex = 0u; dependencyIndex < packetView.plan->externalDependencyCount; ++dependencyIndex){
            if(!declarationAccess.externalCompletionToken(packetView.externalDependencies[dependencyIndex]))
                return false;
        }
    }
    return true;
}

[[nodiscard]] const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(const GpuTaskGraphResourceView& resource, const GpuCompiledBarrier& barrier)noexcept{
    if(resource.initialOwnerHandoffSourceCount == 0u || !resource.initialOwnerHandoffSources || (resource.type != GpuGraphResourceType::Texture && resource.type != GpuGraphResourceType::Buffer && resource.type != GpuGraphResourceType::AccelStruct))
        return nullptr;

    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u; sourceIndex < resource.initialOwnerHandoffSourceCount; ++sourceIndex){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        if(
            source.sourceQueue != barrier.sourceQueue
            || source.destinationQueue != barrier.destinationQueue
            || (resource.type == GpuGraphResourceType::Texture && !source.range.textureSubresources.contains(barrier.range.textureSubresources))
            || (resource.type == GpuGraphResourceType::Buffer && !source.range.bufferRange.contains(barrier.range.bufferRange))
        )
            continue;
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

