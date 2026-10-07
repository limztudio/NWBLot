// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const GpuPacketStateSeed* TaskPrologueStateSeeds(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuCompiledTask& task
)noexcept{
    if(
        task.prologueStateSeedCount == 0u
        || task.prologueStateSeedOffset > compiledPlan.prologueStateSeeds.size()
        || task.prologueStateSeedCount > compiledPlan.prologueStateSeeds.size() - task.prologueStateSeedOffset
    )
        return nullptr;
    return compiledPlan.prologueStateSeeds.data() + task.prologueStateSeedOffset;
}

[[nodiscard]] bool PlanPacketDependencies(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies,
    const Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies,
    Vector<GpuPacketDependency, Alloc::ScratchArena>& resourceStateDependencies,
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    Alloc::ScratchArena& scratchArena
){
    const usize packetCount = compiledPlan.packets.size();
    const bool plansResourceStateDependencies = !resourceStateDependencies.empty();
    if(
        packetCount > static_cast<usize>(Limit<u32>::s_Max)
        || compiledPlan.packetDependencies.size() > static_cast<usize>(Limit<u32>::s_Max)
        || compiledPlan.packetExternalDependencies.size() > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    for(const GpuPacketDependency& dependency : resourceStateDependencies){
        if(
            !dependency.producer.valid()
            || !dependency.consumer.valid()
            || dependency.producer.generation != compiledPlan.planGeneration
            || dependency.consumer.generation != compiledPlan.planGeneration
            || dependency.producer.index >= dependency.consumer.index
            || dependency.consumer.index >= packetCount
        )
            return false;
    }
    for(const GpuTaskExternalDependencyEdge& dependency : initialOwnershipDependencies){
        if(
            !graph.validTask(dependency.consumer)
            || !graph.validExternalCompletion(dependency.completion)
        )
            return false;
    }
    for(const GpuTaskExternalDependencyEdge& dependency : initialAvailabilityDependencies){
        if(
            !graph.validTask(dependency.consumer)
            || !graph.validExternalCompletion(dependency.completion)
        )
            return false;
    }

    if(plansResourceStateDependencies){
        Sort(
            resourceStateDependencies.begin(),
            resourceStateDependencies.end(),
            [](const GpuPacketDependency& lhs, const GpuPacketDependency& rhs){
                if(lhs.consumer.index != rhs.consumer.index)
                    return lhs.consumer.index < rhs.consumer.index;
                return lhs.producer.index > rhs.producer.index;
            }
        );
    }

    Vector<u32, Alloc::ScratchArena> packetDependencyConsumerMarkers(packetCount, scratchArena);
    for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex)
        packetDependencyConsumerMarkers[packetIndex] = Limit<u32>::s_Max;
    Vector<u32, Alloc::ScratchArena> externalDependencyConsumerMarkers(graph.externalCompletionCount(), scratchArena);
    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex)
        externalDependencyConsumerMarkers[completionIndex] = Limit<u32>::s_Max;

    constexpr usize s_BitsPerPacketReachabilityWord = sizeof(u64) * 8u;
    Vector<u64, Alloc::ScratchArena> packetReachability(scratchArena);
    usize packetReachabilityWordsPerPacket = 0u;
    if(plansResourceStateDependencies && packetCount == 0u)
        return false;
    const auto publishPacketDependency = [&](const GpuSubmissionPacketId producer, const GpuSubmissionPacketId consumer){
        if(
            !producer.valid()
            || !consumer.valid()
            || producer.generation != compiledPlan.planGeneration
            || consumer.generation != compiledPlan.planGeneration
            || producer.index >= consumer.index
            || consumer.index >= packetCount
        )
            return false;

        const usize producerRowOffset = static_cast<usize>(producer.index) * packetReachabilityWordsPerPacket;
        const usize consumerRowOffset = static_cast<usize>(consumer.index) * packetReachabilityWordsPerPacket;
        const usize producerWordCount = static_cast<usize>(producer.index) / s_BitsPerPacketReachabilityWord + 1u;
        for(usize wordIndex = 0u; wordIndex < producerWordCount; ++wordIndex)
            packetReachability[consumerRowOffset + wordIndex] |= packetReachability[producerRowOffset + wordIndex];
        const usize producerWord = consumerRowOffset + producer.index / s_BitsPerPacketReachabilityWord;
        packetReachability[producerWord] |= static_cast<u64>(1u) << (producer.index % s_BitsPerPacketReachabilityWord);
        return true;
    };
    const auto initializePacketReachability = [&](const usize currentConsumerIndex){
        packetReachabilityWordsPerPacket = (packetCount - 1u) / s_BitsPerPacketReachabilityWord + 1u;
        usize packetReachabilityWordCount = 0u;
        if(
            !TryMultiply<usize>(packetCount, packetReachabilityWordsPerPacket, packetReachabilityWordCount)
            || packetReachabilityWordCount > Limit<usize>::s_Max / sizeof(u64)
            || packetReachabilityWordCount > packetReachability.max_size()
        )
            return false;
        packetReachability.resize(packetReachabilityWordCount, 0u);
        // Earlier consumers may already contain terminal edges accepted without the dense closure.
        for(usize packetIndex = 0u; packetIndex <= currentConsumerIndex; ++packetIndex){
            const GpuSubmissionPacket& packet = compiledPlan.packets[packetIndex];
            for(u32 dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
                const GpuPacketDependency& dependency = compiledPlan.packetDependencies[packet.dependencyOffset + dependencyIndex];
                if(!publishPacketDependency(dependency.producer, dependency.consumer))
                    return false;
            }
        }
        return true;
    };

    usize initialOwnershipDependencyIndex = 0u;
    usize initialAvailabilityDependencyIndex = 0u;
    usize resourceStateDependencyIndex = 0u;
    for(usize consumerPacketIndex = 0u; consumerPacketIndex < compiledPlan.packets.size(); ++consumerPacketIndex){
        GpuSubmissionPacket& consumerPacket = compiledPlan.packets[consumerPacketIndex];
        const GpuSubmissionPacketId consumerPacketID{
            .generation = compiledPlan.planGeneration,
            .index = static_cast<u32>(consumerPacketIndex),
        };
        consumerPacket.dependencyOffset = static_cast<u32>(compiledPlan.packetDependencies.size());
        const auto appendPacketDependency = [&](const GpuSubmissionPacketId producerPacket){
            if(producerPacket == consumerPacketID)
                return true;
            if(
                !producerPacket.valid()
                || producerPacket.index >= consumerPacketIndex
                || producerPacket.generation != compiledPlan.planGeneration
            )
                return false;

            if(packetDependencyConsumerMarkers[producerPacket.index] == consumerPacketID.index)
                return true;
            if(compiledPlan.packetDependencies.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;

            compiledPlan.packetDependencies.push_back(GpuPacketDependency{
                .producer = producerPacket,
                .consumer = consumerPacketID,
            });
            packetDependencyConsumerMarkers[producerPacket.index] = consumerPacketID.index;
            ++consumerPacket.dependencyCount;
            return true;
        };
        consumerPacket.externalDependencyOffset = static_cast<u32>(compiledPlan.packetExternalDependencies.size());
        const auto appendExternalDependency = [&](const GpuExternalCompletionId completion){
            if(!graph.validExternalCompletion(completion))
                return false;
            if(externalDependencyConsumerMarkers[completion.index] == consumerPacketID.index)
                return true;
            if(compiledPlan.packetExternalDependencies.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;

            compiledPlan.packetExternalDependencies.push_back(completion);
            externalDependencyConsumerMarkers[completion.index] = consumerPacketID.index;
            ++consumerPacket.externalDependencyCount;
            return true;
        };
        for(u32 taskIndex = 0u; taskIndex < consumerPacket.taskCount; ++taskIndex){
            const GpuTaskId consumerTask = compiledPlan.packetTasks[consumerPacket.taskOffset + taskIndex];
            const GpuCompiledTask* const compiledConsumerTask = FindCompiledTask(compiledPlan, consumerTask);
            if(!compiledConsumerTask || !graph.validTask(consumerTask))
                return false;
            const GpuTaskGraphTaskView consumerTaskView = graph.taskAt(consumerTask.index);

            const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(consumerTask);
            for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
                const GpuTaskId producerTask{ .generation = consumerTask.generation, .index = static_cast<u32>(producerIndices[producerIndex]) };
                const GpuSubmissionPacketId producerPacket = FindCompiledPacketForTask(compiledPlan, producerTask);
                if(!appendPacketDependency(producerPacket))
                    return false;
            }

            const GpuPacketStateSeed* const stateSeeds = TaskPrologueStateSeeds(compiledPlan, *compiledConsumerTask);
            if(compiledConsumerTask->prologueStateSeedCount != 0u && !stateSeeds)
                return false;
            for(u32 stateSeedIndex = 0u; stateSeedIndex < compiledConsumerTask->prologueStateSeedCount; ++stateSeedIndex){
                if(!appendPacketDependency(stateSeeds[stateSeedIndex].sourcePacket))
                    return false;
            }

            for(usize dependencyIndex = 0u; dependencyIndex < consumerTaskView.externalDependencyCount; ++dependencyIndex){
                if(!appendExternalDependency(consumerTaskView.externalDependencies[dependencyIndex]))
                    return false;
            }
            while(
                initialOwnershipDependencyIndex < initialOwnershipDependencies.size()
                && initialOwnershipDependencies[initialOwnershipDependencyIndex].consumer == consumerTask
            ){
                const GpuExternalCompletionId completion =
                    initialOwnershipDependencies[initialOwnershipDependencyIndex].completion
                ;
                ++initialOwnershipDependencyIndex;
                if(!appendExternalDependency(completion))
                    return false;
            }
            while(
                initialAvailabilityDependencyIndex < initialAvailabilityDependencies.size()
                && initialAvailabilityDependencies[initialAvailabilityDependencyIndex].consumer == consumerTask
            ){
                const GpuExternalCompletionId completion =
                    initialAvailabilityDependencies[initialAvailabilityDependencyIndex].completion
                ;
                ++initialAvailabilityDependencyIndex;
                if(!appendExternalDependency(completion))
                    return false;
            }
        }
        if(!plansResourceStateDependencies)
            continue;

        bool consumerDependsOnNonRoot = false;
        const bool hasResourceStateConsumerGroup = resourceStateDependencyIndex < resourceStateDependencies.size()
            && resourceStateDependencies[resourceStateDependencyIndex].consumer == consumerPacketID
        ;
        if(packetReachabilityWordsPerPacket != 0u || hasResourceStateConsumerGroup){
            for(u32 dependencyIndex = 0u; dependencyIndex < consumerPacket.dependencyCount; ++dependencyIndex){
                const GpuPacketDependency& dependency = compiledPlan.packetDependencies[
                    consumerPacket.dependencyOffset + dependencyIndex
                ];
                if(packetReachabilityWordsPerPacket != 0u){
                    if(!publishPacketDependency(dependency.producer, dependency.consumer))
                        return false;
                }
                else if(compiledPlan.packets[dependency.producer.index].dependencyCount != 0u)
                    consumerDependsOnNonRoot = true;
            }
        }

        // Each state-synchronization consumer group is ordered nearest-producer first. If an existing or newly added path already joins
        // an older producer into this packet, avoid adding a redundant direct edge while retaining the
        // canonical dependency order.
        while(
            resourceStateDependencyIndex < resourceStateDependencies.size()
            && resourceStateDependencies[resourceStateDependencyIndex].consumer == consumerPacketID
        ){
            const GpuSubmissionPacketId producerPacketID =
                resourceStateDependencies[resourceStateDependencyIndex].producer
            ;
            ++resourceStateDependencyIndex;
            if(packetDependencyConsumerMarkers[producerPacketID.index] == consumerPacketID.index)
                continue;
            if(
                packetReachabilityWordsPerPacket == 0u
                && (consumerDependsOnNonRoot || compiledPlan.packets[producerPacketID.index].dependencyCount != 0u)
            ){
                // Root producers cannot form an indirect path; allocate closure only when that proof no longer holds.
                if(!initializePacketReachability(consumerPacketIndex))
                    return false;
            }
            if(packetReachabilityWordsPerPacket != 0u){
                const usize reachabilityWord = consumerPacketIndex * packetReachabilityWordsPerPacket
                    + producerPacketID.index / s_BitsPerPacketReachabilityWord
                ;
                const u64 reachabilityBit = static_cast<u64>(1u)
                    << (producerPacketID.index % s_BitsPerPacketReachabilityWord)
                ;
                if((packetReachability[reachabilityWord] & reachabilityBit) != 0u)
                    continue;
            }
            if(!appendPacketDependency(producerPacketID))
                return false;
            if(packetReachabilityWordsPerPacket != 0u && !publishPacketDependency(producerPacketID, consumerPacketID))
                return false;
        }
    }
    if(initialOwnershipDependencyIndex != initialOwnershipDependencies.size())
        return false;
    if(initialAvailabilityDependencyIndex != initialAvailabilityDependencies.size())
        return false;
    if(resourceStateDependencyIndex != resourceStateDependencies.size())
        return false;

    // Packet dependencies are already constrained to earlier compiler-order packets and remain the authoritative
    // GPU submission order. Native recording only needs prior packets that export a state snapshot consumed by a
    // prologue seed, so retain the longest such chain as immutable ready-frontier depth without unnecessarily
    // serializing explicit ordering-only packet dependencies on the CPU.
    for(usize packetIndex = 0u; packetIndex < compiledPlan.packets.size(); ++packetIndex){
        GpuSubmissionPacket& packet = compiledPlan.packets[packetIndex];
        const GpuSubmissionPacketId packetID{
            .generation = compiledPlan.planGeneration,
            .index = static_cast<u32>(packetIndex),
        };
        for(u32 dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            const GpuPacketDependency& dependency = compiledPlan.packetDependencies[
                packet.dependencyOffset + dependencyIndex
            ];
            if(
                dependency.consumer.index != packetIndex
                || dependency.consumer.generation != compiledPlan.planGeneration
                || dependency.producer.index >= packetIndex
                || dependency.producer.generation != compiledPlan.planGeneration
            )
                return false;
        }

        u32 frontier = 0u;
        for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
            const GpuTaskId task = compiledPlan.packetTasks[packet.taskOffset + taskIndex];
            const GpuCompiledTask* const compiledTask = FindCompiledTask(compiledPlan, task);
            if(!compiledTask)
                return false;

            const GpuPacketStateSeed* const stateSeeds = TaskPrologueStateSeeds(compiledPlan, *compiledTask);
            if(compiledTask->prologueStateSeedCount != 0u && !stateSeeds)
                return false;
            for(u32 stateSeedIndex = 0u; stateSeedIndex < compiledTask->prologueStateSeedCount; ++stateSeedIndex){
                const GpuSubmissionPacketId sourcePacket = stateSeeds[stateSeedIndex].sourcePacket;
                if(
                    !sourcePacket.valid()
                    || sourcePacket == packetID
                    || sourcePacket.index >= packetIndex
                    || sourcePacket.generation != compiledPlan.planGeneration
                )
                    return false;

                const u32 producerFrontier = compiledPlan.packets[sourcePacket.index].recordingFrontier;
                if(producerFrontier == Limit<u32>::s_Max)
                    return false;
                const u32 candidateFrontier = producerFrontier + 1u;
                if(candidateFrontier > frontier)
                    frontier = candidateFrontier;
            }
        }
        packet.recordingFrontier = frontier;
    }

    if(const GpuPresentEndpoint* const endpoint = graph.presentEndpoint()){
        const GpuCompiledTask* const producer = FindCompiledTask(compiledPlan, endpoint->producer);
        const GpuPhysicalQueueInfo* const queue = producer ? FindCompiledQueueInfo(compiledPlan, producer->queue) : nullptr;
        if(
            !producer
            || !producer->packet.valid()
            || producer->packet.generation != compiledPlan.planGeneration
            || !queue
            || queue->queueClass != CommandQueue::Graphics
        )
            return false;

        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                if(task.resourceUses[useIndex].resource != endpoint->backBuffer)
                    continue;

                const GpuCompiledTask* const user = FindCompiledTask(compiledPlan, task.id);
                if(!user || user->queue != producer->queue)
                    return false;
                break;
            }
        }
        compiledPlan.presentEndpoint = GpuCompiledPresentEndpoint{
            .producer = endpoint->producer,
            .backBuffer = endpoint->backBuffer,
            .packet = producer->packet,
            .queue = queue->id,
        };
        compiledPlan.hasPresentEndpoint = true;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

