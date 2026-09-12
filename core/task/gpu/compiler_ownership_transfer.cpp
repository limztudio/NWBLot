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




[[nodiscard]] bool AppendCompiledOwnershipTransfer(
    GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const GpuTaskId sourceTask,
    const GpuTaskId destinationTask,
    const GpuPhysicalQueueId sourceQueue,
    const GpuPhysicalQueueId destinationQueue,
    const GpuOwnershipTransferRoute::Enum route
){
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    if(
        !plan.graph.validResource(resource.id)
        || resource.id.generation != compiledPlan.graphGeneration
        || resource.identity == NAME_NONE
        || resource.type >= GpuGraphResourceType::HazardDomain
        || !ResourceQueueSharing::IsValid(resource.queueSharing)
    )
        return false;

    const GpuTaskGraphResourceView declaredResource = plan.graph.resourceAt(resource.id.index);
    if(
        declaredResource.id != resource.id
        || declaredResource.identity != resource.identity
        || declaredResource.type != resource.type
        || declaredResource.queueSharing != resource.queueSharing
    )
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
    if(
        (sourceTask.valid() && !compiledSourceTask)
        || (destinationTask.valid() && !compiledDestinationTask)
        || (
            route == GpuOwnershipTransferRoute::Internal
            && sourceTask == destinationTask
        )
    )
        return false;

    const GpuSubmissionPacketId sourcePacket = compiledSourceTask
        ? compiledSourceTask->packet
        : GpuSubmissionPacketId{}
    ;
    const GpuSubmissionPacketId destinationPacket = compiledDestinationTask
        ? compiledDestinationTask->packet
        : GpuSubmissionPacketId{}
    ;
    const auto validCompiledPacket = [&compiledPlan](const GpuSubmissionPacketId packet){
        return packet.valid()
            && packet.generation == compiledPlan.planGeneration
            && packet.index < compiledPlan.packets.size()
        ;
    };
    if(
        (compiledSourceTask && (compiledSourceTask->queue != sourceQueue || !validCompiledPacket(sourcePacket)))
        || (
            compiledDestinationTask
            && (compiledDestinationTask->queue != destinationQueue || !validCompiledPacket(destinationPacket))
        )
        || (
            route == GpuOwnershipTransferRoute::Internal
            && sourcePacket == destinationPacket
        )
    )
        return false;

    GpuTaskResourceRange transferRange;
    switch(resource.type){
    case GpuGraphResourceType::Texture:
        if(
            !ResolveTextureRangeForPlanning(plan.graph.textureForResource(resource.id), range, transferRange)
            || !transferRange.textureSubresources.hasExtent()
        )
            return false;
        transferRange.bufferRange = s_EntireBuffer;
        break;
    case GpuGraphResourceType::Buffer:
        if(!ResolveResourceRangeForPlanning(plan.graph, resource, range, transferRange))
            return false;
        transferRange.textureSubresources = s_AllSubresources;
        break;
    case GpuGraphResourceType::AccelStruct:
        break;
    default:
        return false;
    }

    const GpuPhysicalQueueInfo* const sourceQueueInfo = FindCompiledQueueInfo(compiledPlan, sourceQueue);
    const GpuPhysicalQueueInfo* const destinationQueueInfo = FindCompiledQueueInfo(compiledPlan, destinationQueue);
    if(
        !sourceQueueInfo
        || !destinationQueueInfo
        || sourceQueueInfo->familyIndex == Limit<u32>::s_Max
        || destinationQueueInfo->familyIndex == Limit<u32>::s_Max
    )
        return false;
    if(sourceQueueInfo->familyIndex == destinationQueueInfo->familyIndex)
        return true;

    const bool usesConcurrentSharing = ResourceUsesConcurrentQueueSharing(resource, plan.topology);
    if(usesConcurrentSharing){
        if(!ResourceSharesQueuePairConcurrently(
            resource,
            plan.topology,
            *sourceQueueInfo,
            *destinationQueueInfo
        ))
            return false;
        return true;
    }

    const GpuCompiledOwnershipTransfer transfer{
        .resource = resource.id,
        .resourceIdentity = resource.identity,
        .range = transferRange,
        .sourceTask = compiledSourceTask ? sourceTask : GpuTaskId{},
        .destinationTask = compiledDestinationTask ? destinationTask : GpuTaskId{},
        .sourcePacket = sourcePacket,
        .destinationPacket = destinationPacket,
        .sourceQueue = sourceQueue,
        .destinationQueue = destinationQueue,
        .sourceQueueFamilyIndex = sourceQueueInfo->familyIndex,
        .destinationQueueFamilyIndex = destinationQueueInfo->familyIndex,
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

