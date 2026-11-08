// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_ownership_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


namespace __hidden_gpu_task_graph_ownership_validator{


struct InitialOwnershipCompletionRequirement{
    GpuPhysicalQueueId sourceQueue;
    u64 minimumValue = 0u;
};


};


b8 GpuTaskGraphOwnershipValidator::validateCompletionQueues(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology){
    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex){
        const GpuTaskGraphExternalCompletionView completion = graph.externalCompletionAt(completionIndex);
        if(!completion.hasToken)
            continue;

        const GpuPhysicalQueueId producerQueue{ .index = completion.token.physicalQueueIndex, .deviceGeneration = completion.token.deviceGeneration, };
        b8 validProducerQueue = false;
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
            if(queue.id != producerQueue || queue.queueClass != completion.token.queue)
                continue;
            validProducerQueue = true;
            break;
        }
        if(!validProducerQueue)
            return false;
    }
    return true;
}


b8 GpuTaskGraphOwnershipValidator::validateInitialOwnership(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphCompilerDetail::GpuTaskGraphCompiledPlanStorage& compiledPlan, Alloc::ScratchArena& scratchArena){
    using namespace GpuTaskGraphCompilerDetail;
    using namespace __hidden_gpu_task_graph_ownership_validator;

    Vector<InitialOwnershipCompletionRequirement, Alloc::ScratchArena> initialOwnershipCompletionRequirements(graph.externalCompletionCount(), scratchArena);
    const auto appendInitialOwnershipCompletionRequirement = [&](const GpuExternalCompletionId completion, const GpuPhysicalQueueId sourceQueue, const u64 minimumValue)noexcept{
        if(!graph.validExternalCompletion(completion) || !sourceQueue.valid() || minimumValue == 0u)
            return false;

        InitialOwnershipCompletionRequirement& requirement = initialOwnershipCompletionRequirements[completion.index];
        if(requirement.sourceQueue.valid() && requirement.sourceQueue != sourceQueue)
            return false;
        requirement.sourceQueue = sourceQueue;
        if(requirement.minimumValue < minimumValue)
            requirement.minimumValue = minimumValue;
        return true;
    };

    // An import may name the exact physical queue that owned an exclusive texture/buffer/acceleration structure before graph work began.
    // A different first packet must also name a fixed release destination, an imported completion, and a native state handoff source.
    // That keeps the compiler from manufacturing an acquire or cross-queue wait on its own.
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        const b8 hasExternalFinalRelease = resource.externalFinalReleaseDestinationQueue.valid();
        if(hasExternalFinalRelease && (resource.externalFinalState == ResourceStates::Unknown || ResourceUsesConcurrentQueueSharing(resource, topology) || !FindCompiledQueueInfo(compiledPlan, resource.externalFinalReleaseDestinationQueue))){
            return false;
        }
        if(resource.initialOwnerHandoffSourceCount != 0u){
            if(
                (resource.type != GpuGraphResourceType::Texture && resource.type != GpuGraphResourceType::Buffer && resource.type != GpuGraphResourceType::AccelStruct)
                || !resource.initialOwnerHandoffSources
                || ResourceUsesConcurrentQueueSharing(resource, topology)
            ){
                return false;
            }
            for(usize sourceIndex = 0u; sourceIndex < resource.initialOwnerHandoffSourceCount; ++sourceIndex){
                const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
                Expected<GpuTaskResourceRange> plannedSourceRange = MakeUnexpected(Failure{});
                const GpuPhysicalQueueInfo* const sourceQueueInfo = FindCompiledQueueInfo(compiledPlan, source.sourceQueue);
                if(
                    !(plannedSourceRange = ResolveResourceRangeForPlanning(graph, resource, source.range))
                    || !source.sourceQueue.valid()
                    || !source.destinationQueue.valid()
                    || !sourceQueueInfo
                    || !FindCompiledQueueInfo(compiledPlan, source.destinationQueue)
                    || !graph.validExternalCompletion(source.completion)
                    || !source.minimumCompletionToken.valid()
                    || source.minimumCompletionToken.queue != sourceQueueInfo->queueClass
                    || !source.minimumCompletionToken.matchesPhysicalQueue(source.sourceQueue.index, source.sourceQueue.deviceGeneration)
                    || !source.stateSource
                    || !appendInitialOwnershipCompletionRequirement(source.completion, source.sourceQueue, source.minimumCompletionToken.value)
                ){
                    return false;
                }
            }
            continue;
        }
        if(!resource.initialOwnerQueue.valid())
            continue;
        if(ResourceUsesConcurrentQueueSharing(resource, topology) || !FindCompiledQueueInfo(compiledPlan, resource.initialOwnerQueue))
            return false;
    }
    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex){
        const InitialOwnershipCompletionRequirement& requirement = initialOwnershipCompletionRequirements[completionIndex];
        if(!requirement.sourceQueue.valid())
            continue;

        const GpuTaskGraphExternalCompletionView completion = graph.externalCompletionAt(completionIndex);
        if(!completion.hasToken)
            continue;
        const GpuPhysicalQueueInfo* const sourceQueueInfo = FindCompiledQueueInfo(compiledPlan, requirement.sourceQueue);
        if(!sourceQueueInfo || completion.token.queue != sourceQueueInfo->queueClass || !completion.token.matchesPhysicalQueue(requirement.sourceQueue.index, requirement.sourceQueue.deviceGeneration) || completion.token.value < requirement.minimumValue){
            return false;
        }
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
