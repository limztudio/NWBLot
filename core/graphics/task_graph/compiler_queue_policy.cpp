// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/atomic.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u8 s_ValidQueueCapabilityMask =
    static_cast<u8>(GpuQueueCapability::Transfer)
    | static_cast<u8>(GpuQueueCapability::Compute)
    | static_cast<u8>(GpuQueueCapability::Graphics)
;

// Compilers are short-lived value objects, so packet identity must be allocated process-wide. The graph
// generation remains the identity of declared task/resource handles; this distinct generation invalidates every
// packet-local artifact whenever a graph is compiled into a replacement immutable plan.
static Atomic<u64> s_NextCompiledPlanGeneration{ 1u };

[[nodiscard]] u64 AllocateCompiledPlanGeneration()noexcept{
    u64 generation = s_NextCompiledPlanGeneration.fetch_add(1u, MemoryOrder::relaxed);
    if(generation == 0u)
        generation = s_NextCompiledPlanGeneration.fetch_add(1u, MemoryOrder::relaxed);
    return generation;
}

[[nodiscard]] bool HasCapabilities(
    const GpuQueueCapability::Mask available,
    const GpuQueueCapability::Mask required
)noexcept{
    const u8 availableMask = static_cast<u8>(available);
    const u8 requiredMask = static_cast<u8>(required);
    return (availableMask & requiredMask) == requiredMask;
}

[[nodiscard]] static bool IsKnownQueueClass(const CommandQueue::Enum queueClass)noexcept{
    return queueClass < CommandQueue::kCount;
}

[[nodiscard]] static bool LogicalSharingIncludesQueueFamily(
    const ResourceQueueSharing::Mask sharing,
    const GpuTaskGraphQueueTopology& topology,
    const u32 familyIndex
)noexcept{
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(
            queue.familyIndex == familyIndex
            && ResourceQueueSharing::IncludesQueueClass(sharing, queue.queueClass)
        )
            return true;
    }
    return false;
}

// A sharing mask becomes Vulkan concurrent sharing only when it names at least two distinct families supplied by
// this compile topology. A single requested family remains exclusive and may use ordinary ownership handoffs.
[[nodiscard]] static bool LogicalSharingUsesConcurrentQueueSharing(
    const ResourceQueueSharing::Mask sharing,
    const GpuTaskGraphQueueTopology& topology
)noexcept{
    if(sharing == ResourceQueueSharing::Exclusive)
        return false;

    usize familyCount = 0u;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(!ResourceQueueSharing::IncludesQueueClass(sharing, queue.queueClass))
            continue;

        bool familyAlreadyIncluded = false;
        for(usize previousIndex = 0u; previousIndex < queueIndex; ++previousIndex){
            const GpuPhysicalQueueInfo& previous = topology.queues[previousIndex];
            familyAlreadyIncluded = ResourceQueueSharing::IncludesQueueClass(sharing, previous.queueClass)
                && previous.familyIndex == queue.familyIndex
            ;
            if(familyAlreadyIncluded)
                break;
        }
        if(!familyAlreadyIncluded && ++familyCount >= 2u)
            return true;
    }
    return false;
}

[[nodiscard]] bool ResourceSharingAdmitsQueue(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    if(resource.hasQueueAdmission){
        const ResourceQueueAdmissionSnapshot& admission = resource.queueAdmission;
        return ResourceQueueAdmissionAdmitsQueue(admission, queue);
    }

    return !LogicalSharingUsesConcurrentQueueSharing(resource.queueSharing, topology)
        || LogicalSharingIncludesQueueFamily(resource.queueSharing, topology, queue.familyIndex)
    ;
}

[[nodiscard]] bool ResourceUsesConcurrentQueueSharing(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskGraphQueueTopology& topology
)noexcept{
    if(resource.hasQueueAdmission)
        return resource.queueAdmission.usesConcurrentSharing;
    return LogicalSharingUsesConcurrentQueueSharing(resource.queueSharing, topology);
}

[[nodiscard]] bool ResourceSharesQueuePairConcurrently(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& sourceQueue,
    const GpuPhysicalQueueInfo& destinationQueue
)noexcept{
    return sourceQueue.familyIndex != destinationQueue.familyIndex
        && ResourceUsesConcurrentQueueSharing(resource, topology)
        && ResourceSharingAdmitsQueue(resource, topology, sourceQueue)
        && ResourceSharingAdmitsQueue(resource, topology, destinationQueue)
    ;
}

[[nodiscard]] static bool IsBetterQueue(
    const GpuPhysicalQueueInfo& candidate,
    const GpuPhysicalQueueInfo* const current
)noexcept{
    if(!current)
        return true;
    if(candidate.id.index != current->id.index)
        return candidate.id.index < current->id.index;
    return candidate.queueClass < current->queueClass;
}

[[nodiscard]] bool IsValidQueueTopology(const GpuTaskGraphQueueTopology& topology)noexcept{
    if(!topology.queues || topology.queueCount == 0u)
        return false;

    u16 deviceGeneration = 0u;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        const u8 capabilityMask = static_cast<u8>(queue.capabilities);
        if(
            !queue.id.valid()
            || !IsKnownQueueClass(queue.queueClass)
            || queue.familyIndex == Limit<u32>::s_Max
            || capabilityMask == 0u
            || (capabilityMask & ~s_ValidQueueCapabilityMask) != 0u
        )
            return false;

        switch(queue.queueClass){
        case CommandQueue::Graphics:
            if(!HasCapabilities(queue.capabilities, GpuQueueCapability::Graphics))
                return false;
            break;
        case CommandQueue::Compute:
            if(!HasCapabilities(queue.capabilities, GpuQueueCapability::Compute))
                return false;
            break;
        case CommandQueue::Transfer:
            if(!HasCapabilities(queue.capabilities, GpuQueueCapability::Transfer))
                return false;
            break;
        default:
            return false;
        }

        if(queueIndex == 0u)
            deviceGeneration = queue.id.deviceGeneration;
        else if(queue.id.deviceGeneration != deviceGeneration)
            return false;

        for(usize previousIndex = 0u; previousIndex < queueIndex; ++previousIndex){
            const GpuPhysicalQueueInfo& previous = topology.queues[previousIndex];
            if(
                previous.id == queue.id
                // Queue IDs are graph-facing handles, but one family/index pair must still name exactly one native
                // transport. Otherwise same-class routing could manufacture distinct packet identities for the same
                // VkQueue and incorrectly turn ordinary queue order into a timeline edge.
                || (previous.familyIndex == queue.familyIndex && previous.queueIndex == queue.queueIndex)
            )
                return false;
        }
    }
    return true;
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindBestCompatibleQueue(
    const GpuTaskGraphQueueTopology& topology,
    const GpuQueueCapability::Mask requiredCapabilities,
    const CommandQueue::Enum requiredClass
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(
            (requiredClass != CommandQueue::kCount && queue.queueClass != requiredClass)
            || !HasCapabilities(queue.capabilities, requiredCapabilities)
            || !IsBetterQueue(queue, result)
        )
            continue;
        result = &queue;
    }
    return result;
}

[[nodiscard]] bool RequiresGraphics(const GpuQueueCapability::Mask requiredCapabilities)noexcept{
    return HasCapabilities(requiredCapabilities, GpuQueueCapability::Graphics);
}

[[nodiscard]] bool ShouldUseDedicatedCompute(const GpuTaskSchedulingHint& hint)noexcept{
    return hint.cost != GpuTaskCostHint::Tiny
        && hint.overlapPreferred
        && !hint.avoidQueueCrossing
    ;
}

[[nodiscard]] bool ShouldUseDedicatedTransfer(const GpuTaskSchedulingHint& hint)noexcept{
    // Dedicated copies buy queue overlap only when their synchronization cost is plausibly amortized. Keep the
    // same conservative threshold as Compute until per-packet timing feeds a richer queue score.
    return ShouldUseDedicatedCompute(hint);
}

[[nodiscard]] bool IsValidQueueRequest(const GpuQueueRequest& request)noexcept{
    return (static_cast<u8>(request.requiredCapabilities) & ~s_ValidQueueCapabilityMask) == 0u
        && request.preferredQueue < GpuQueuePreference::kCount;
}

[[nodiscard]] bool IsValidSchedulingHint(const GpuTaskSchedulingHint& hint)noexcept{
    return hint.cost < GpuTaskCostHint::kCount;
}

[[nodiscard]] u64 QueueCostWeight(const GpuTaskCostHint::Enum cost)noexcept{
    switch(cost){
    case GpuTaskCostHint::Tiny: return 1u;
    case GpuTaskCostHint::Small: return 2u;
    case GpuTaskCostHint::Medium: return 4u;
    case GpuTaskCostHint::Large: return 8u;
    default: return 0u;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

