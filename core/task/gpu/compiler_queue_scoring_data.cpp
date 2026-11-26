// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskQueueScoringData::GpuTaskQueueScoringData(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskSchedulingReachability& reachability,
    const GpuTaskGraphQueueAssignmentOptions& options,
    Alloc::ScratchArena& scratchArena,
    const GpuPhysicalQueueTopology* const topology,
    const b8 hasNonHazardDomainResource
)
    : taskCosts(CompilerGraphAccess::TaskCount(graph), scratchArena)
    , taskCostGroups(reachability.m_words.empty() ? 0u : CompilerGraphAccess::TaskCount(graph), scratchArena)
    , ownershipEdgeOffsets(CompilerGraphAccess::TaskCount(graph) + 1u, 0u, scratchArena)
    , ownershipEdges(scratchArena)
    , resourceConcurrentAdmittedMasks(scratchArena)
    , assignedQueueLoads(scratchArena)
    , externalQueueLoadsByIndex(scratchArena)
    , queueFamilyByIndex(scratchArena)
    , assignedQueueIndexByTask(CompilerGraphAccess::TaskCount(graph), Limit<u16>::s_Max, scratchArena)
    , allCostWords(scratchArena)
    , queueCostWords(scratchArena)
    , queueNonEmptyWordMasks(scratchArena)
    , relatedAllCostByTask(scratchArena)
    , externalQueueLoads(options.queueLoads)
    , externalQueueLoadCount(options.queueLoadCount)
{
    for(usize costGroup = 0u; costGroup < GpuTaskCostHint::kCount; ++costGroup)
        costGroupWeights[costGroup] = QueueCostWeight(static_cast<GpuTaskCostHint::Enum>(costGroup));
    for(usize taskIndex = 0u; taskIndex < taskCosts.size(); ++taskIndex){
        const GpuTaskSchedulingHint& scheduling = CompilerGraphAccess::TaskNode(graph, taskIndex).scheduling;
        const auto costGroup = scheduling.cost;
        if(!taskCostGroups.empty())
            taskCostGroups[taskIndex] = static_cast<u8>(costGroup);
        taskCosts[taskIndex] = scheduling.isRecoverySubmission ? 0u : costGroupWeights[costGroup];
    }

    if(topology && topology->queueCount > 0u){
        queueFamilyByIndex.resize(topology->queueCount);
        for(usize queueIndex = 0u; queueIndex < topology->queueCount; ++queueIndex)
            queueFamilyByIndex[queueIndex] = topology->queues[queueIndex].familyIndex;

        if(externalQueueLoads && externalQueueLoadCount != 0u){
            externalQueueLoadsByIndex.assign(topology->queueCount, 0u);
            for(usize loadIndex = 0u; loadIndex < externalQueueLoadCount; ++loadIndex){
                if(const GpuPhysicalQueueInfo* const queue = FindPhysicalQueueInfo(*topology, externalQueueLoads[loadIndex].queue)){
                    const usize queueIndex = static_cast<usize>(queue - topology->queues);
                    externalQueueLoadsByIndex[queueIndex] = externalQueueLoads[loadIndex].estimatedCost;
                }
            }
        }

        b8 singleFamily = true;
        const u32 firstFamily = queueFamilyByIndex[0u];
        for(usize queueIndex = 1u; queueIndex < topology->queueCount; ++queueIndex){
            if(queueFamilyByIndex[queueIndex] != firstFamily){
                singleFamily = false;
                break;
            }
        }
        if(singleFamily)
            return;
    }

    if(!hasNonHazardDomainResource || analysis.inferredEdges().empty())
        return;

    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    const usize resourceCount = CompilerGraphAccess::ResourceCount(graph);
    const b8 buildConcurrentMasks = topology && topology->queueCount > 0u && topology->queueCount <= s_BitsPerWord;
    Vector<u8, Alloc::ScratchArena> computedConcurrentMask(buildConcurrentMasks ? resourceCount : 0u, 0u, scratchArena);
    TopologySharingLut sharingLut{};
    if(buildConcurrentMasks){
        resourceConcurrentAdmittedMasks.assign(resourceCount, 0u);
        hasResourceConcurrentMasks = true;
        sharingLut = TopologySharingLut::Build(*topology);
    }

    Vector<OwnershipEdge, Alloc::ScratchArena> uniqueEdges(scratchArena);
    uniqueEdges.reserve(analysis.inferredEdges().size());
    for(const GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(edge.hazard == GpuTaskHazardType::VersionDependency || edge.hazard == GpuTaskHazardType::VersionLifetime || !edge.resource.valid())
            continue;
        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, edge.resource.index);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;
        if(buildConcurrentMasks && !computedConcurrentMask[edge.resource.index]){
            computedConcurrentMask[edge.resource.index] = 1u;
            if(!resource.hasQueueAdmission){
                if(sharingLut.logicalUsesConcurrent(resource.queueSharing))
                    resourceConcurrentAdmittedMasks[edge.resource.index] = sharingLut.logicalAdmittedMask(resource.queueSharing);
            }
            else if(ResourceNodeUsesConcurrentQueueSharing(graph, resource, *topology)){
                u64 admittedMask = 0u;
                for(usize queueIndex = 0u; queueIndex < topology->queueCount; ++queueIndex){
                    if(ResourceNodeSharingAdmitsQueue(graph, resource, *topology, topology->queues[queueIndex]))
                        admittedMask |= BitMask<u64>(static_cast<u32>(queueIndex));
                }
                resourceConcurrentAdmittedMasks[edge.resource.index] = admittedMask;
            }
        }
        uniqueEdges.push_back(OwnershipEdge{ .producerIndex = edge.producer.index, .consumerIndex = edge.consumer.index, .resourceIndex = edge.resource.index, });
    }
    if(uniqueEdges.empty())
        return;
    Sort(uniqueEdges.begin(), uniqueEdges.end(), [](const OwnershipEdge& lhs, const OwnershipEdge& rhs)noexcept{
        if(lhs.producerIndex != rhs.producerIndex)
            return lhs.producerIndex < rhs.producerIndex;
        if(lhs.consumerIndex != rhs.consumerIndex)
            return lhs.consumerIndex < rhs.consumerIndex;
        return lhs.resourceIndex < rhs.resourceIndex;
    });

    usize uniqueEdgeCount = 0u;
    for(const OwnershipEdge& edge : uniqueEdges){
        if(uniqueEdgeCount != 0u){
            const OwnershipEdge& previous = uniqueEdges[uniqueEdgeCount - 1u];
            if(previous.producerIndex == edge.producerIndex && previous.consumerIndex == edge.consumerIndex && previous.resourceIndex == edge.resourceIndex)
                continue;
        }
        uniqueEdges[uniqueEdgeCount] = edge;
        ++uniqueEdgeCount;
        ++ownershipEdgeOffsets[edge.producerIndex + 1u];
        ++ownershipEdgeOffsets[edge.consumerIndex + 1u];
    }
    uniqueEdges.erase(uniqueEdges.begin() + uniqueEdgeCount, uniqueEdges.end());
    for(usize taskIndex = 1u; taskIndex < ownershipEdgeOffsets.size(); ++taskIndex)
        ownershipEdgeOffsets[taskIndex] += ownershipEdgeOffsets[taskIndex - 1u];

    ownershipEdges.resize(ownershipEdgeOffsets.back());
    Vector<usize, Alloc::ScratchArena> writeOffsets(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < writeOffsets.size(); ++taskIndex)
        writeOffsets[taskIndex] = ownershipEdgeOffsets[taskIndex];
    for(const OwnershipEdge& edge : uniqueEdges){
        ownershipEdges[writeOffsets[edge.producerIndex]] = edge;
        ownershipEdges[writeOffsets[edge.consumerIndex]] = edge;
        ++writeOffsets[edge.producerIndex];
        ++writeOffsets[edge.consumerIndex];
    }
}

usize GpuTaskQueueScoringData::findAssignedQueueIndex(const GpuPhysicalQueueId& queue)const noexcept{
    const usize count = assignedQueueLoads.size();
    if(queue.index < count && assignedQueueLoads[queue.index].queue == queue)
        return queue.index;
    for(usize queueIndex = 0u; queueIndex < count; ++queueIndex){
        if(assignedQueueLoads[queueIndex].queue == queue)
            return queueIndex;
    }
    return count;
}

u64 GpuTaskQueueScoringData::externalQueueLoad(const GpuPhysicalQueueId& queue)const noexcept{
    if(!externalQueueLoadsByIndex.empty() && queue.index < externalQueueLoadsByIndex.size() && assignedQueueLoads.size() == externalQueueLoadsByIndex.size() && assignedQueueLoads[queue.index].queue == queue)
        return externalQueueLoadsByIndex[queue.index];
    if(!externalQueueLoads)
        return 0u;
    for(usize queueLoadIndex = 0u; queueLoadIndex < externalQueueLoadCount; ++queueLoadIndex){
        const GpuTaskQueueLoad& externalLoad = externalQueueLoads[queueLoadIndex];
        if(externalLoad.queue == queue)
            return externalLoad.estimatedCost;
    }
    return 0u;
}

void GpuTaskQueueScoringData::rebuildAssignmentLoads(const Span<const GpuTaskQueueAssignment> assignments, const GpuPhysicalQueueTopology& topology){
    assignedQueueLoads.resize(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        assignedQueueLoads[queueIndex] = { .queue = topology.queues[queueIndex].id, .estimatedCost = 0u };

    const b8 trackTaskQueueIndices = topology.queueCount < Limit<u16>::s_Max && assignedQueueIndexByTask.size() == taskCosts.size();
    if(taskCostGroups.empty()){
        relatedAllCostByTask.clear();
        allCostWordsFullGraph = false;
        allCostWords.clear();
        queueCostWords.clear();
        queueNonEmptyWordMasks.clear();
        costWordsPerGroup = 0u;
        nonEmptyCostGroupMask = 0u;
        totalAssignedCost = 0u;
        for(const GpuTaskQueueAssignment& assignment : assignments){
            const usize queueIndex = findAssignedQueueIndex(assignment.queue);
            if(trackTaskQueueIndices && queueIndex < topology.queueCount)
                assignedQueueIndexByTask[assignment.task.index] = static_cast<u16>(queueIndex);
            const u64 cost = taskCosts[assignment.task.index];
            if(cost == 0u)
                continue;
            totalAssignedCost += cost;
            if(queueIndex < assignedQueueLoads.size())
                assignedQueueLoads[queueIndex].estimatedCost += cost;
        }
        return;
    }

    costWordsPerGroup = taskCosts.empty() ? 0u : (taskCosts.size() - 1u) / s_BitsPerWord + 1u;
    const auto queueWordCount = TryMultiply<usize>(topology.queueCount, costWordsPerGroup);
    if(!queueWordCount)
        throw AllocationSizeException{};
    const usize nonEmptyWordMaskCount = costWordsPerGroup <= s_BitsPerWord ? topology.queueCount : 0u;
    const b8 isFullGraph = assignments.size() == taskCosts.size();
    const b8 reuseAllCostWords = allCostWordsFullGraph && isFullGraph && allCostWords.size() == costWordsPerGroup;
    if(!reuseAllCostWords){
        relatedAllCostByTask.clear();
        allCostWords.resize(costWordsPerGroup);
        if(!allCostWords.empty())
            FOUNDATION_MEMSET(allCostWords.data(), 0, allCostWords.size() * sizeof(InterleavedCostWord));
        nonEmptyCostGroupMask = 0u;
        allCostWordsFullGraph = isFullGraph;
    }
    queueCostWords.resize(*queueWordCount);
    if(!queueCostWords.empty())
        FOUNDATION_MEMSET(queueCostWords.data(), 0, queueCostWords.size() * sizeof(InterleavedCostWord));
    queueNonEmptyWordMasks.resize(nonEmptyWordMaskCount);
    if(!queueNonEmptyWordMasks.empty())
        FOUNDATION_MEMSET(queueNonEmptyWordMasks.data(), 0, queueNonEmptyWordMasks.size() * sizeof(u64));

    totalAssignedCost = 0u;
    for(const GpuTaskQueueAssignment& assignment : assignments){
        const usize queueIndex = findAssignedQueueIndex(assignment.queue);
        if(trackTaskQueueIndices && queueIndex < topology.queueCount)
            assignedQueueIndexByTask[assignment.task.index] = static_cast<u16>(queueIndex);
        const u64 cost = taskCosts[assignment.task.index];
        if(cost == 0u)
            continue;
        totalAssignedCost += cost;
        const u8 costGroup = taskCostGroups[assignment.task.index];
        const usize wordIndex = assignment.task.index / s_BitsPerWord;
        const u64 mask = static_cast<u64>(1u) << (assignment.task.index % s_BitsPerWord);
        if(!reuseAllCostWords){
            nonEmptyCostGroupMask |= static_cast<u8>(1u << costGroup);
            allCostWords[wordIndex].groups[costGroup] |= mask;
        }
        if(queueIndex < assignedQueueLoads.size()){
            assignedQueueLoads[queueIndex].estimatedCost += cost;
            queueCostWords[queueIndex * costWordsPerGroup + wordIndex].groups[costGroup] |= mask;
            if(!queueNonEmptyWordMasks.empty())
                queueNonEmptyWordMasks[queueIndex] |= BitMask<u64>(static_cast<u32>(wordIndex));
        }
    }
}

void GpuTaskQueueScoringData::updateAssignmentLoads(const GpuTaskId& task, const GpuPhysicalQueueId& previousQueue, const GpuPhysicalQueueId& selectedQueue)noexcept{
    if(previousQueue == selectedQueue)
        return;
    const usize selectedQueueIndex = findAssignedQueueIndex(selectedQueue);
    if(selectedQueueIndex < assignedQueueLoads.size() && assignedQueueLoads.size() < Limit<u16>::s_Max && task.index < assignedQueueIndexByTask.size())
        assignedQueueIndexByTask[task.index] = static_cast<u16>(selectedQueueIndex);
    const u64 cost = taskCosts[task.index];
    if(cost == 0u)
        return;
    const usize previousQueueIndex = findAssignedQueueIndex(previousQueue);
    if(allCostWords.empty()){
        if(previousQueueIndex < assignedQueueLoads.size()){
            FOUNDATION_ASSERT(assignedQueueLoads[previousQueueIndex].estimatedCost >= cost);
            assignedQueueLoads[previousQueueIndex].estimatedCost -= cost;
        }
        if(selectedQueueIndex < assignedQueueLoads.size())
            assignedQueueLoads[selectedQueueIndex].estimatedCost += cost;
        return;
    }
    const u8 costGroup = taskCostGroups[task.index];
    const usize wordIndex = task.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (task.index % s_BitsPerWord);
    if(previousQueueIndex < assignedQueueLoads.size()){
        FOUNDATION_ASSERT(assignedQueueLoads[previousQueueIndex].estimatedCost >= cost);
        assignedQueueLoads[previousQueueIndex].estimatedCost -= cost;
        InterleavedCostWord& prevWord = queueCostWords[previousQueueIndex * costWordsPerGroup + wordIndex];
        prevWord.groups[costGroup] &= ~mask;
        if(!queueNonEmptyWordMasks.empty() && (prevWord.groups[0u] | prevWord.groups[1u] | prevWord.groups[2u] | prevWord.groups[3u]) == 0u)
            queueNonEmptyWordMasks[previousQueueIndex] &= ~BitMask<u64>(static_cast<u32>(wordIndex));
    }
    if(selectedQueueIndex < assignedQueueLoads.size()){
        assignedQueueLoads[selectedQueueIndex].estimatedCost += cost;
        queueCostWords[selectedQueueIndex * costWordsPerGroup + wordIndex].groups[costGroup] |= mask;
        if(!queueNonEmptyWordMasks.empty())
            queueNonEmptyWordMasks[selectedQueueIndex] |= BitMask<u64>(static_cast<u32>(wordIndex));
    }
}

u64 GpuTaskQueueScoringData::assignedQueueLoad(const GpuPhysicalQueueId& queue)const noexcept{
    const usize queueIndex = findAssignedQueueIndex(queue);
    return queueIndex < assignedQueueLoads.size() ? assignedQueueLoads[queueIndex].estimatedCost : 0u;
}

u64 GpuTaskQueueScoringData::relatedAllTaskCost(const GpuTaskSchedulingReachability& reachability, const usize taskIndex)const noexcept{
    if(!reachability.m_valid || reachability.m_totalOrder || reachability.m_words.empty() || nonEmptyCostGroupMask == 0u)
        return 0u;
    if(relatedAllCostByTask.empty())
        relatedAllCostByTask.assign(taskCosts.size(), Limit<u64>::s_Max);
    u64& cached = relatedAllCostByTask[taskIndex];
    if(cached != Limit<u64>::s_Max)
        return cached;

    FOUNDATION_ASSERT(costWordsPerGroup == reachability.m_wordsPerRow);
    const auto& range = reachability.m_relatedWordRanges[taskIndex];
    const usize firstWord = range.begin;
    const usize lastWord = range.end;
    const usize rowOffset = reachability.m_rowOffsets.empty() ? (taskIndex * costWordsPerGroup) : reachability.m_rowOffsets[taskIndex];
    u64 total = 0u;
    if((nonEmptyCostGroupMask & (nonEmptyCostGroupMask - 1u)) == 0u){
        const usize costGroup = static_cast<usize>(CountTrailingZeros(static_cast<u32>(nonEmptyCostGroupMask)));
        const u64 groupWeight = costGroupWeights[costGroup];
        u64 matchedBits = 0u;
        for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex){
            const u64 related = reachability.m_rowOffsets.empty()
                ? reachability.m_words[rowOffset + wordIndex]
                : reachability.m_words[rowOffset + wordIndex - firstWord];
            if(related != 0u)
                matchedBits += static_cast<u64>(CountSetBits(related & allCostWords[wordIndex].groups[costGroup]));
        }
        total = matchedBits * groupWeight;
    }
    else{
        for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex){
            const u64 related = reachability.m_rowOffsets.empty()
                ? reachability.m_words[rowOffset + wordIndex]
                : reachability.m_words[rowOffset + wordIndex - firstWord];
            if(related == 0u)
                continue;
            const InterleavedCostWord& cw = allCostWords[wordIndex];
            total += static_cast<u64>(CountSetBits(related & cw.groups[0u]))
                + (static_cast<u64>(CountSetBits(related & cw.groups[1u])) << 1u)
                + (static_cast<u64>(CountSetBits(related & cw.groups[2u])) << 2u)
                + (static_cast<u64>(CountSetBits(related & cw.groups[3u])) << 3u)
            ;
        }
    }
    cached = total;
    return total;
}


u64 GpuTaskQueueScoringData::independentQueueCostByIndex(const GpuTaskSchedulingReachability& reachability, const usize queueIndex, const Span<const GpuTaskQueueAssignment> members)const noexcept{
    if(!reachability.m_valid || reachability.m_totalOrder || members.empty() || !members.data() || queueIndex >= assignedQueueLoads.size())
        return 0u;
#if defined(NWB_DEBUG)
    for(const GpuTaskQueueAssignment& member : members){
        const GpuTaskId task = member.task;
        FOUNDATION_ASSERT(task.valid() && task.generation == reachability.m_graphGeneration && task.index < reachability.m_taskCount);
    }
#endif
    const GpuPhysicalQueueId queue = assignedQueueLoads[queueIndex].queue;
    const u64 queueEstimatedCost = assignedQueueLoads[queueIndex].estimatedCost;
    const u64 otherQueueCost = totalAssignedCost - queueEstimatedCost;
    if(otherQueueCost == 0u)
        return 0u;
    if(reachability.m_words.empty()){
        u64 independentCost = otherQueueCost;
        for(const GpuTaskQueueAssignment& member : members){
            if(member.queue != queue)
                independentCost -= taskCosts[member.task.index];
        }
        return independentCost;
    }
    if(nonEmptyCostGroupMask == 0u)
        return otherQueueCost;

    FOUNDATION_ASSERT(costWordsPerGroup == reachability.m_wordsPerRow);
    const InterleavedCostWord* const qWords = queueCostWords.data() + queueIndex * costWordsPerGroup;
    const b8 singleCostGroup = (nonEmptyCostGroupMask & (nonEmptyCostGroupMask - 1u)) == 0u;

    const auto weightedPopcount4 = [](const u64 mask, const InterleavedCostWord& cw)noexcept -> u64{
        return
            static_cast<u64>(CountSetBits(mask & cw.groups[0u]))
            + (static_cast<u64>(CountSetBits(mask & cw.groups[1u])) << 1u)
            + (static_cast<u64>(CountSetBits(mask & cw.groups[2u])) << 2u)
            + (static_cast<u64>(CountSetBits(mask & cw.groups[3u])) << 3u)
        ;
    };

    if(members.size() == 1u){
        const usize taskIndex = members[0u].task.index;
        const u64 relatedAll = relatedAllTaskCost(reachability, taskIndex);
        if(queueEstimatedCost == 0u)
            return otherQueueCost - relatedAll;

        const auto& range = reachability.m_relatedWordRanges[taskIndex];
        const usize firstWord = range.begin;
        const usize lastWord = range.end;
        const usize rowOffset = reachability.m_rowOffsets.empty() ? (taskIndex * costWordsPerGroup) : reachability.m_rowOffsets[taskIndex];
        u64 relatedOnQueue = 0u;
        const auto loadSingleRelated = [&](const usize wordIndex)noexcept -> u64{
            return reachability.m_rowOffsets.empty()
                ? reachability.m_words[rowOffset + wordIndex]
                : reachability.m_words[rowOffset + wordIndex - firstWord];
        };
        const usize singleGroupIndex = singleCostGroup ? static_cast<usize>(CountTrailingZeros(static_cast<u32>(nonEmptyCostGroupMask))) : 0u;
        const auto accumulateSingleWord = [&](const usize wordIndex)noexcept{
            const u64 related = loadSingleRelated(wordIndex);
            if(related == 0u)
                return;
            if(singleCostGroup)
                relatedOnQueue += static_cast<u64>(CountSetBits(related & qWords[wordIndex].groups[singleGroupIndex]));
            else
                relatedOnQueue += weightedPopcount4(related, qWords[wordIndex]);
        };
        if(!queueNonEmptyWordMasks.empty() && firstWord < lastWord){
            const u64 hiMask = lastWord >= s_BitsPerWord ? ~static_cast<u64>(0u) : ((static_cast<u64>(1u) << lastWord) - 1u);
            const u64 loMask = ~static_cast<u64>(0u) << firstWord;
            u64 activeWords = queueNonEmptyWordMasks[queueIndex] & (hiMask & loMask);
            while(activeWords != 0u){
                const usize wordIndex = static_cast<usize>(CountTrailingZeros(activeWords));
                activeWords &= activeWords - 1u;
                accumulateSingleWord(wordIndex);
            }
        }
        else{
            for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex)
                accumulateSingleWord(wordIndex);
        }
        if(singleCostGroup)
            relatedOnQueue *= costGroupWeights[singleGroupIndex];
        return otherQueueCost - (relatedAll - relatedOnQueue);
    }

    usize firstWord = costWordsPerGroup;
    usize lastWord = 0u;
    for(const GpuTaskQueueAssignment& member : members){
        const auto& range = reachability.m_relatedWordRanges[member.task.index];
        firstWord = Min(firstWord, range.begin);
        lastWord = Max(lastWord, range.end);
    }
    const auto loadMergedRelated = [&](const usize wordIndex)noexcept -> u64{
        u64 related = 0u;
        if(reachability.m_rowOffsets.empty()){
            for(const GpuTaskQueueAssignment& member : members)
                related |= reachability.m_words[member.task.index * costWordsPerGroup + wordIndex];
        }
        else{
            for(const GpuTaskQueueAssignment& member : members){
                const usize taskIndex = member.task.index;
                const auto& range = reachability.m_relatedWordRanges[taskIndex];
                if(wordIndex >= range.begin && wordIndex < range.end)
                    related |= reachability.m_words[reachability.m_rowOffsets[taskIndex] + wordIndex - range.begin];
            }
        }
        return related;
    };
    u64 independentCost = otherQueueCost;
    if(singleCostGroup){
        const usize costGroup = static_cast<usize>(CountTrailingZeros(static_cast<u32>(nonEmptyCostGroupMask)));
        const u64 groupWeight = costGroupWeights[costGroup];
        u64 matchedBits = 0u;
        for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex){
            const u64 related = loadMergedRelated(wordIndex);
            if(related == 0u)
                continue;
            const u64 offQueue = queueEstimatedCost == 0u
                ? allCostWords[wordIndex].groups[costGroup]
                : (allCostWords[wordIndex].groups[costGroup] & ~qWords[wordIndex].groups[costGroup])
            ;
            matchedBits += static_cast<u64>(CountSetBits(related & offQueue));
        }
        return independentCost - matchedBits * groupWeight;
    }

    for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex){
        const u64 related = loadMergedRelated(wordIndex);
        if(related == 0u)
            continue;
        const InterleavedCostWord& allWord = allCostWords[wordIndex];
        if(queueEstimatedCost == 0u){
            independentCost -= weightedPopcount4(related, allWord);
        }
        else{
            const InterleavedCostWord& qWord = qWords[wordIndex];
            InterleavedCostWord offQueueWord;
            for(usize g = 0u; g < GpuTaskCostHint::kCount; ++g)
                offQueueWord.groups[g] = allWord.groups[g] & ~qWord.groups[g];
            independentCost -= weightedPopcount4(related, offQueueWord);
        }
    }
    return independentCost;
}

u64 GpuTaskQueueScoringData::independentQueueCost(const GpuTaskSchedulingReachability& reachability, const GpuPhysicalQueueId& queue, const Span<const GpuTaskQueueAssignment> members)const noexcept{
    const usize queueIndex = findAssignedQueueIndex(queue);
    return independentQueueCostByIndex(reachability, queueIndex, members);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

