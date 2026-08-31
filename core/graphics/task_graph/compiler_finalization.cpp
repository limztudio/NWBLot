// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PlanExternalResourceExports(GpuTaskGraphResourceStatePlan& plan){
    const GpuTaskGraph& graph = plan.graph;
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    Alloc::ScratchArena& scratchArena = plan.scratchArena;
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates = plan.trackedResourceStates;
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena>& pendingEpilogueBarriers = plan.pendingEpilogueBarriers;
    Vector<GpuPacketDependency, Alloc::ScratchArena>& terminalFinalizationDependencies =
        plan.terminalFinalizationDependencies
    ;
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& stateFragments = plan.stateFragments;

    // Imported texture/buffer/acceleration-structure metadata can require a graph-owned terminal state for code
    // that resumes outside this compiled graph. Texture exports retain every terminal subresource fragment; buffers
    // and acceleration structures remain whole-allocation. The runtime lowers each export through the native state
    // tracker and retains the requested state even when no native transition was required.
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        if(resource.externalFinalState == ResourceStates::Unknown)
            continue;

        const bool hasExternalFinalRelease = resource.externalFinalReleaseDestinationQueue.valid();

        const GpuCompiledBarrierType::Enum exportType = StateExportBarrierType(resource.type);
        if(exportType >= GpuCompiledBarrierType::kCount)
            return false;

        bool hasTerminalDeclaredRange = false;
        GpuTaskId externalExportTask;
        GpuSubmissionPacketId externalExportPacket;
        GpuPhysicalQueueId externalExportSourceQueue;
        bool externalExportUsesOnePacket = true;
        const u32 externalExportSourceOffset = static_cast<u32>(
            compiledPlan.externalResourceExportSources.size()
        );
        u32 externalExportSourceCount = 0u;
        const auto appendTerminalState = [&](
            const TrackedCompiledResourceState& state,
            const usize terminalStateIndex,
            const GpuTaskResourceRange& terminalRange
        ){
            if(
                terminalStateIndex >= trackedResourceStates.size()
                || &trackedResourceStates[terminalStateIndex] != &state
            )
                return false;

            const GpuSubmissionPacketId terminalPacket = FindCompiledPacketForTask(compiledPlan, state.task);
            if(
                !terminalPacket.valid()
                || terminalPacket.generation != compiledPlan.planGeneration
                || terminalPacket.index >= compiledPlan.packets.size()
            )
                return false;

            const bool performsFinalization = state.state != resource.externalFinalState
                || (
                    hasExternalFinalRelease
                    && state.queue != resource.externalFinalReleaseDestinationQueue
                )
            ;
            if(performsFinalization){
                // The selected terminal task owns the external transition/release for this exact fragment. Any
                // earlier overlapping terminal user that was shadowed by it must finish before that operation; a
                // matching state alone does not otherwise order concurrent read-only packets.
                for(usize earlierStateIndex = 0u; earlierStateIndex < terminalStateIndex; ++earlierStateIndex){
                    const TrackedCompiledResourceState& earlierState = trackedResourceStates[earlierStateIndex];
                    if(
                        earlierState.resource != resource.id
                        || !RangesOverlap(resource, earlierState.range, terminalRange)
                    )
                        continue;

                    const GpuSubmissionPacketId earlierPacket = FindCompiledPacketForTask(
                        compiledPlan,
                        earlierState.task
                    );
                    if(
                        !earlierPacket.valid()
                        || earlierPacket.generation != compiledPlan.planGeneration
                        || earlierPacket.index >= compiledPlan.packets.size()
                    )
                        return false;
                    if(earlierPacket == terminalPacket)
                        continue;
                    if(earlierPacket.index >= terminalPacket.index)
                        return false;

                    bool alreadyAdded = false;
                    for(const GpuPacketDependency& dependency : terminalFinalizationDependencies){
                        if(
                            dependency.producer == earlierPacket
                            && dependency.consumer == terminalPacket
                        ){
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if(!alreadyAdded){
                        terminalFinalizationDependencies.push_back(GpuPacketDependency{
                            .producer = earlierPacket,
                            .consumer = terminalPacket,
                        });
                    }
                }
            }

            if(hasExternalFinalRelease){
                if(!externalExportTask.valid()){
                    externalExportTask = state.task;
                    externalExportPacket = terminalPacket;
                    externalExportSourceQueue = state.queue;
                }
                else if(
                    externalExportPacket != terminalPacket
                    || externalExportSourceQueue != state.queue
                ){
                    externalExportUsesOnePacket = false;
                }
                // Texture ownership and state snapshots are subresource-granular, so disjoint terminal mips may
                // safely arrive from different physical queues. Buffer and acceleration-structure state tracking
                // remains whole-allocation, so two packet-local external releases would publish contradictory native
                // ownership/state snapshots even when their declared byte ranges do not overlap. Keep that existing
                // rejection until range-granular Buffer/AS native handoffs exist.
                if(
                    resource.type != GpuGraphResourceType::Texture
                    && (
                        externalExportPacket != terminalPacket
                        || externalExportSourceQueue != state.queue
                    )
                )
                    return false;
                compiledPlan.externalResourceExportSources.push_back(
                    GpuCompiledExternalResourceExportSource{
                        .producerTask = state.task,
                        .sourceQueue = state.queue,
                        .range = terminalRange,
                    }
                );
                ++externalExportSourceCount;
            }

            pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                .task = state.task,
                .barrier = GpuCompiledBarrier{
                    .resource = state.resource,
                    .range = terminalRange,
                    .before = state.state,
                    .after = resource.externalFinalState,
                    .sourceQueue = state.queue,
                    .destinationQueue = state.queue,
                    .type = exportType,
                },
            });
            if(hasExternalFinalRelease && state.queue != resource.externalFinalReleaseDestinationQueue){
                const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                if(releaseType >= GpuCompiledBarrierType::kCount)
                    return false;
                // Export the exact final state before ownership moves. Native lowering therefore captures the same
                // state in the released snapshot and the paired Vulkan queue-family release barrier.
                if(!AppendCompiledOwnershipTransfer(
                    plan,
                    resource,
                    terminalRange,
                    state.task,
                    GpuTaskId{},
                    state.queue,
                    resource.externalFinalReleaseDestinationQueue,
                    GpuOwnershipTransferRoute::ExternalExport
                ))
                    return false;
                pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                    .task = state.task,
                    .barrier = GpuCompiledBarrier{
                        .resource = state.resource,
                        .range = terminalRange,
                        .before = resource.externalFinalState,
                        .after = resource.externalFinalState,
                        .sourceQueue = state.queue,
                        .destinationQueue = resource.externalFinalReleaseDestinationQueue,
                        .type = releaseType,
                    },
                });
            }
            hasTerminalDeclaredRange = true;
            return true;
        };

        if(resource.type == GpuGraphResourceType::Texture){
            stateFragments.clear();
            if(!CollectTerminalTextureStateFragments(
                trackedResourceStates,
                resource.id,
                scratchArena,
                stateFragments
            ))
                return false;
            for(const TrackedTextureStateFragment& fragment : stateFragments){
                if(
                    !fragment.state
                    || !appendTerminalState(*fragment.state, fragment.stateIndex, fragment.range)
                )
                    return false;
            }
        }
        else{
            for(usize stateIndex = 0u; stateIndex < trackedResourceStates.size(); ++stateIndex){
                const TrackedCompiledResourceState& state = trackedResourceStates[stateIndex];
                if(state.resource != resource.id)
                    continue;

                bool hasLaterOverlappingUse = false;
                for(usize laterStateIndex = stateIndex + 1u;
                    laterStateIndex < trackedResourceStates.size();
                    ++laterStateIndex
                ){
                    const TrackedCompiledResourceState& later = trackedResourceStates[laterStateIndex];
                    if(
                        later.resource == resource.id
                        && RangesOverlap(resource, state.range, later.range)
                    ){
                        hasLaterOverlappingUse = true;
                        break;
                    }
                }
                if(hasLaterOverlappingUse)
                    continue;
                if(!appendTerminalState(state, stateIndex, state.range))
                    return false;
            }
        }
        if(!hasTerminalDeclaredRange){
            // A final-state requirement cannot be published from an untouched or state-unknown resource. Reject
            // compilation rather than leaving a direct renderer bridge to guess whether the requirement held.
            return false;
        }
        if(hasExternalFinalRelease){
            if(
                !externalExportTask.valid()
                || !externalExportPacket.valid()
                || !externalExportSourceQueue.valid()
                || externalExportSourceCount == 0u
            )
                return false;
            compiledPlan.externalResourceExports.push_back(GpuCompiledExternalResourceExport{
                .resource = resource.id,
                .producerTask = externalExportUsesOnePacket ? externalExportTask : GpuTaskId{},
                .sourceQueue = externalExportUsesOnePacket ? externalExportSourceQueue : GpuPhysicalQueueId{},
                .sourceOffset = externalExportSourceOffset,
                .sourceCount = externalExportSourceCount,
                .destinationQueue = resource.externalFinalReleaseDestinationQueue,
                .finalState = resource.externalFinalState,
            });
        }
    }
    return true;
}

void AppendPendingEpilogueBarriers(GpuTaskGraphResourceStatePlan& plan){
    // Consumers are visited after their producer, so ownership releases are discovered late. Group them only after
    // planning completes to keep every task's epilogue span contiguous in the immutable compiled graph.
    for(GpuCompiledTask& compiledTask : plan.compiledPlan.tasks){
        compiledTask.epilogueBarrierOffset = static_cast<u32>(plan.compiledPlan.epilogueBarriers.size());
        for(const PendingCompiledEpilogueBarrier& pending : plan.pendingEpilogueBarriers){
            if(pending.task == compiledTask.task)
                plan.compiledPlan.epilogueBarriers.push_back(pending.barrier);
        }
        compiledTask.epilogueBarrierCount = static_cast<u32>(plan.compiledPlan.epilogueBarriers.size())
            - compiledTask.epilogueBarrierOffset
        ;
    }
}


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
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies,
    const Vector<GpuPacketDependency, Alloc::ScratchArena>& terminalFinalizationDependencies,
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    Alloc::ScratchArena& scratchArena
){
    const usize packetCount = compiledPlan.packets.size();
    const bool plansTerminalFinalizationDependencies = !terminalFinalizationDependencies.empty();
    if(
        packetCount > static_cast<usize>(Limit<u32>::s_Max)
        || compiledPlan.packetDependencies.size() > static_cast<usize>(Limit<u32>::s_Max)
        || compiledPlan.packetExternalDependencies.size() > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    for(const GpuPacketDependency& dependency : terminalFinalizationDependencies){
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

    Vector<u32, Alloc::ScratchArena> packetDependencyConsumerMarkers(packetCount, scratchArena);
    for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex)
        packetDependencyConsumerMarkers[packetIndex] = Limit<u32>::s_Max;

    constexpr usize s_BitsPerPacketReachabilityWord = sizeof(u64) * 8u;
    Vector<u64, Alloc::ScratchArena> packetReachability(scratchArena);
    usize packetReachabilityWordsPerPacket = 0u;
    if(plansTerminalFinalizationDependencies){
        if(packetCount == 0u)
            return false;
        packetReachabilityWordsPerPacket = (packetCount - 1u) / s_BitsPerPacketReachabilityWord + 1u;
        usize packetReachabilityWordCount = 0u;
        if(
            !TryMultiply<usize>(packetCount, packetReachabilityWordsPerPacket, packetReachabilityWordCount)
            || packetReachabilityWordCount > Limit<usize>::s_Max / sizeof(u64)
            || packetReachabilityWordCount > packetReachability.max_size()
        )
            return false;
        packetReachability.resize(packetReachabilityWordCount, 0u);
    }
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

    for(usize consumerPacketIndex = 0u; consumerPacketIndex < compiledPlan.packets.size(); ++consumerPacketIndex){
        GpuSubmissionPacket& consumerPacket = compiledPlan.packets[consumerPacketIndex];
        const GpuSubmissionPacketId consumerPacketID{
            static_cast<u32>(consumerPacketIndex),
            compiledPlan.planGeneration,
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
        for(u32 taskIndex = 0u; taskIndex < consumerPacket.taskCount; ++taskIndex){
            const GpuTaskId consumerTask = compiledPlan.packetTasks[consumerPacket.taskOffset + taskIndex];
            const GpuCompiledTask* const compiledConsumerTask = FindCompiledTask(compiledPlan, consumerTask);
            if(!compiledConsumerTask)
                return false;

            const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(consumerTask);
            for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
                const GpuTaskId producerTask{ producerIndices[producerIndex], consumerTask.generation };
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

            const auto appendExternalDependency = [&](const GpuTaskExternalDependencyEdge& edge){
                for(u32 dependencyIndex = 0u; dependencyIndex < consumerPacket.externalDependencyCount; ++dependencyIndex){
                    if(compiledPlan.packetExternalDependencies[
                        consumerPacket.externalDependencyOffset + dependencyIndex
                    ] == edge.completion)
                        return true;
                }
                if(compiledPlan.packetExternalDependencies.size() >= static_cast<usize>(Limit<u32>::s_Max))
                    return false;

                compiledPlan.packetExternalDependencies.push_back(edge.completion);
                ++consumerPacket.externalDependencyCount;
                return true;
            };
            for(const GpuTaskExternalDependencyEdge& edge : analysis.externalDependencies()){
                if(edge.consumer == consumerTask && !appendExternalDependency(edge))
                    return false;
            }
            for(const GpuTaskExternalDependencyEdge& edge : initialOwnershipDependencies){
                if(edge.consumer == consumerTask && !appendExternalDependency(edge))
                    return false;
            }
        }
        if(!plansTerminalFinalizationDependencies)
            continue;

        for(u32 dependencyIndex = 0u; dependencyIndex < consumerPacket.dependencyCount; ++dependencyIndex){
            const GpuPacketDependency& dependency = compiledPlan.packetDependencies[
                consumerPacket.dependencyOffset + dependencyIndex
            ];
            if(!publishPacketDependency(dependency.producer, dependency.consumer))
                return false;
        }

        // Visit the nearest shadowed producers first. If an existing or newly added path already joins an older
        // producer into this terminal packet, avoid adding a redundant direct edge while retaining deterministic
        // dependency order.
        for(usize producerPacketIndex = consumerPacketIndex; producerPacketIndex > 0u; --producerPacketIndex){
            const GpuSubmissionPacketId producerPacketID{
                static_cast<u32>(producerPacketIndex - 1u),
                compiledPlan.planGeneration,
            };
            bool needsTerminalFinalizationDependency = false;
            for(const GpuPacketDependency& dependency : terminalFinalizationDependencies){
                if(
                    dependency.producer == producerPacketID
                    && dependency.consumer == consumerPacketID
                ){
                    needsTerminalFinalizationDependency = true;
                    break;
                }
            }
            if(!needsTerminalFinalizationDependency)
                continue;
            const usize reachabilityWord = consumerPacketIndex * packetReachabilityWordsPerPacket
                + producerPacketID.index / s_BitsPerPacketReachabilityWord
            ;
            const u64 reachabilityBit = static_cast<u64>(1u)
                << (producerPacketID.index % s_BitsPerPacketReachabilityWord)
            ;
            if((packetReachability[reachabilityWord] & reachabilityBit) != 0u)
                continue;
            if(
                !appendPacketDependency(producerPacketID)
                || !publishPacketDependency(producerPacketID, consumerPacketID)
            )
                return false;
        }
    }

    // Packet dependencies are already constrained to earlier compiler-order packets and remain the authoritative
    // GPU submission order. Native recording only needs prior packets that export a state snapshot consumed by a
    // prologue seed, so retain the longest such chain as immutable ready-frontier depth without unnecessarily
    // serializing explicit ordering-only packet dependencies on the CPU.
    for(usize packetIndex = 0u; packetIndex < compiledPlan.packets.size(); ++packetIndex){
        GpuSubmissionPacket& packet = compiledPlan.packets[packetIndex];
        const GpuSubmissionPacketId packetID{
            static_cast<u32>(packetIndex),
            compiledPlan.planGeneration,
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

