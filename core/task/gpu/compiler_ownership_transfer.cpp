// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/simplemath.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AppendCompiledOwnershipTransfer(
    GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const GpuTaskId sourceTask,
    const GpuTaskId destinationTask,
    const GpuPhysicalQueueId sourceQueue,
    const GpuPhysicalQueueId destinationQueue,
    const GpuOwnershipTransferRoute::Enum route,
    const ResourceShareCacheEntry* const precomputedShareInfo,
    const b8 rangePreResolved
){
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    if(!plan.graph.validResource(resource.id) || resource.id.generation != compiledPlan.graphGeneration || resource.identity == s_NameNone || resource.type >= GpuGraphResourceType::HazardDomain || !ResourceQueueSharing::IsValid(resource.queueSharing))
        return false;

    const GpuTaskGraphDetail::GpuGraphResourceNode& declaredResource = CompilerGraphAccess::ResourceNode(plan.graph, resource.id.index);
    if(declaredResource.identity != resource.identity || declaredResource.type != resource.type || declaredResource.queueSharing != resource.queueSharing)
        return false;

    const GpuCompiledTask* compiledSourceTask = nullptr;
    const GpuCompiledTask* compiledDestinationTask = nullptr;
    switch(route){
    case GpuOwnershipTransferRoute::Internal:
        if(!sourceTask.valid() || !destinationTask.valid())
            return false;
        compiledSourceTask = FindCompiledTask(compiledPlan, sourceTask);
        compiledDestinationTask = FindCompiledTask(compiledPlan, destinationTask);
        break;
    case GpuOwnershipTransferRoute::ExternalImport:
        if(sourceTask.valid() || !destinationTask.valid())
            return false;
        compiledDestinationTask = FindCompiledTask(compiledPlan, destinationTask);
        break;
    case GpuOwnershipTransferRoute::ExternalExport:
        if(!sourceTask.valid() || destinationTask.valid())
            return false;
        compiledSourceTask = FindCompiledTask(compiledPlan, sourceTask);
        break;
    default:
        return false;
    }
    if((sourceTask.valid() && !compiledSourceTask) || (destinationTask.valid() && !compiledDestinationTask) || (route == GpuOwnershipTransferRoute::Internal && sourceTask == destinationTask))
        return false;

    const GpuSubmissionPacketId sourcePacket = compiledSourceTask ? compiledSourceTask->packet : GpuSubmissionPacketId{};
    const GpuSubmissionPacketId destinationPacket = compiledDestinationTask ? compiledDestinationTask->packet : GpuSubmissionPacketId{};
    const auto validCompiledPacket = [&compiledPlan](const GpuSubmissionPacketId packet)noexcept{
        return packet.valid() && packet.generation == compiledPlan.planGeneration && packet.index < compiledPlan.packets.size();
    };
    if(
        (compiledSourceTask && (compiledSourceTask->queue != sourceQueue || !validCompiledPacket(sourcePacket)))
        || (compiledDestinationTask && (compiledDestinationTask->queue != destinationQueue || !validCompiledPacket(destinationPacket)))
        || (route == GpuOwnershipTransferRoute::Internal && sourcePacket == destinationPacket)
    )
        return false;

    Expected<GpuTaskResourceRange> transferRange = GpuTaskResourceRange{};
    if(rangePreResolved){
        switch(resource.type){
        case GpuGraphResourceType::Texture:
            transferRange = range;
            transferRange->bufferRange = s_EntireBuffer;
            break;
        case GpuGraphResourceType::Buffer:
            transferRange = range;
            transferRange->textureSubresources = s_AllSubresources;
            break;
        case GpuGraphResourceType::AccelStruct:
            break;
        default:
            return false;
        }
    }
    else{
        switch(resource.type){
        case GpuGraphResourceType::Texture:
            if(!(transferRange = ResolveTextureRangeForPlanning(plan.graph.textureForResource(resource.id), range)) || !transferRange->textureSubresources.hasExtent())
                return false;
            transferRange->bufferRange = s_EntireBuffer;
            break;
        case GpuGraphResourceType::Buffer:
            if(!(transferRange = ResolveResourceRangeForPlanning(plan.graph, resource, range)))
                return false;
            transferRange->textureSubresources = s_AllSubresources;
            break;
        case GpuGraphResourceType::AccelStruct:
            break;
        default:
            return false;
        }
    }

    const GpuPhysicalQueueInfo* const sourceQueueInfo = FindCompiledQueueInfo(compiledPlan, sourceQueue);
    const GpuPhysicalQueueInfo* const destinationQueueInfo = FindCompiledQueueInfo(compiledPlan, destinationQueue);
    if(!sourceQueueInfo || !destinationQueueInfo || sourceQueueInfo->familyIndex == Limit<u32>::s_Max || destinationQueueInfo->familyIndex == Limit<u32>::s_Max)
        return false;
    if(sourceQueueInfo->familyIndex == destinationQueueInfo->familyIndex)
        return true;

    const b8 usesConcurrentSharing = precomputedShareInfo
        ? precomputedShareInfo->isConcurrent
        : (!resource.hasQueueAdmission ? plan.sharingLut.logicalUsesConcurrent(resource.queueSharing) : ResourceUsesConcurrentQueueSharing(resource, plan.topology))
    ;
    if(usesConcurrentSharing){
        b8 concurrentPair = false;
        if(plan.sharingLut.validBitmask && (!resource.hasQueueAdmission || precomputedShareInfo)){
            const auto queueIndexFor = [&](const GpuPhysicalQueueInfo& queue)noexcept -> usize{
                if(compiledPlan.queueTopology.data() && &queue >= compiledPlan.queueTopology.data() && &queue < compiledPlan.queueTopology.data() + compiledPlan.queueTopology.size())
                    return static_cast<usize>(&queue - compiledPlan.queueTopology.data());
                if(plan.topology.queues && &queue >= plan.topology.queues && &queue < plan.topology.queues + plan.topology.queueCount)
                    return static_cast<usize>(&queue - plan.topology.queues);
                return Limit<usize>::s_Max;
            };
            const usize srcIdx = queueIndexFor(*sourceQueueInfo);
            const usize dstIdx = queueIndexFor(*destinationQueueInfo);
            if(srcIdx < plan.topology.queueCount && dstIdx < plan.topology.queueCount){
                const u64 admittedMask = precomputedShareInfo ? precomputedShareInfo->admittedQueueMask : plan.sharingLut.logicalAdmittedMask(resource.queueSharing);
                concurrentPair = (admittedMask & BitMask<u64>(static_cast<u32>(srcIdx))) != 0u && (admittedMask & BitMask<u64>(static_cast<u32>(dstIdx))) != 0u;
            }
            else{
                concurrentPair = ResourceSharesQueuePairConcurrently(resource, plan.topology, *sourceQueueInfo, *destinationQueueInfo);
            }
        }
        else{
            concurrentPair = ResourceSharesQueuePairConcurrently(resource, plan.topology, *sourceQueueInfo, *destinationQueueInfo);
        }
        if(!concurrentPair)
            return false;
        return true;
    }

    const GpuCompiledOwnershipTransfer transfer{
        .resource = resource.id,
        .range = (*transferRange),
        .sourceTask = compiledSourceTask ? sourceTask : GpuTaskId{},
        .destinationTask = compiledDestinationTask ? destinationTask : GpuTaskId{},
        .sourcePacket = sourcePacket,
        .destinationPacket = destinationPacket,
        .sourceQueueFamilyIndex = sourceQueueInfo->familyIndex,
        .destinationQueueFamilyIndex = destinationQueueInfo->familyIndex,
        .resourceIdentity = resource.identity,
        .sourceQueue = sourceQueue,
        .destinationQueue = destinationQueue,
        .declaredQueueSharing = resource.queueSharing,
        .resourceType = resource.type,
        .route = route,
        .concurrentSharingCouldAvoid = true,
    };
    if(!transfer.valid())
        return false;
    compiledPlan.ownershipTransfers.push_back(transfer);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

