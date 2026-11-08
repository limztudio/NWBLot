// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "amd_breadcrumb_ring.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AmdBreadcrumbRing::CollectSharingQueueFamilies(const GpuPhysicalQueueTopology& topology, Vector<u32, Alloc::ScratchArena>& familyIndices){
    familyIndices.clear();
    if(!topology.queues)
        return;

    familyIndices.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const u32 familyIndex = topology.queues[queueIndex].familyIndex;
        if(familyIndex == VK_QUEUE_FAMILY_IGNORED)
            continue;

        b8 duplicate = false;
        for(const u32 existingFamilyIndex : familyIndices){
            if(existingFamilyIndex == familyIndex){
                duplicate = true;
                break;
            }
        }
        if(!duplicate)
            familyIndices.push_back(familyIndex);
    }
}

Expected<AmdBreadcrumbRing::Layout> AmdBreadcrumbRing::BuildLayout(const GpuPhysicalQueueTopology& topology, const usize slotsPerQueue)noexcept{
    Layout layout;
    if(!topology.queues || topology.queueCount == 0u || topology.queueCount > static_cast<usize>(Limit<u16>::s_Max) || slotsPerQueue == 0u)
        return MakeUnexpected(Failure{});

    const auto totalSlotCount = TryMultiply<usize>(topology.queueCount, slotsPerQueue);
    if(!totalSlotCount)
        return MakeUnexpected(Failure{});
    const auto totalByteSize = TryMultiply<usize>(*totalSlotCount, sizeof(u32));
    if(!totalByteSize || slotsPerQueue > static_cast<usize>(Limit<u32>::s_Max) || *totalByteSize > static_cast<usize>(Limit<VkDeviceSize>::s_Max))
        return MakeUnexpected(Failure{});

    const u16 deviceGeneration = topology.queues[0u].id.deviceGeneration;
    if(deviceGeneration == 0u)
        return MakeUnexpected(Failure{});
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueId& queue = topology.queues[queueIndex].id;
        if(!queue.valid() || queue.deviceGeneration != deviceGeneration || queue.index != queueIndex)
            return MakeUnexpected(Failure{});
    }

    layout.deviceGeneration = deviceGeneration;
    layout.physicalQueueCount = topology.queueCount;
    layout.slotsPerQueue = slotsPerQueue;
    layout.totalSlotCount = *totalSlotCount;
    layout.totalByteSize = static_cast<VkDeviceSize>(*totalByteSize);
    return layout;
}

Expected<AmdBreadcrumbRing::Slot> AmdBreadcrumbRing::ResolveSlot(const Layout& layout, const GpuPhysicalQueueId& queue, const usize localSlot)noexcept{
    if(
        layout.deviceGeneration == 0u
        || layout.physicalQueueCount == 0u
        || layout.slotsPerQueue == 0u
        || layout.totalSlotCount == 0u
        || localSlot >= layout.slotsPerQueue
        || !queue.valid()
        || queue.deviceGeneration != layout.deviceGeneration
        || static_cast<usize>(queue.index) >= layout.physicalQueueCount
    )
        return MakeUnexpected(Failure{});

    const auto queueFirstSlot = TryMultiply<usize>(static_cast<usize>(queue.index), layout.slotsPerQueue);
    if(!queueFirstSlot)
        return MakeUnexpected(Failure{});
    if(AddOverflows<usize>(*queueFirstSlot, localSlot))
        return MakeUnexpected(Failure{});

    const usize resolvedFlatSlot = *queueFirstSlot + localSlot;
    const auto resolvedByteOffset = TryMultiply<usize>(resolvedFlatSlot, sizeof(u32));
    if(resolvedFlatSlot >= layout.totalSlotCount || !resolvedByteOffset || *resolvedByteOffset > static_cast<usize>(Limit<VkDeviceSize>::s_Max))
        return MakeUnexpected(Failure{});

    return Slot{ .flatSlot = resolvedFlatSlot, .byteOffset = static_cast<VkDeviceSize>(*resolvedByteOffset) };
}

Expected<AmdBreadcrumbRing::Reservation> AmdBreadcrumbRing::BuildNextReservation(const u64 currentSerial, const usize slotsPerQueue)noexcept{
    Reservation reservation;
    if(slotsPerQueue == 0u || slotsPerQueue > static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    const auto maximumSerial = TryMultiply<u64>(static_cast<u64>(slotsPerQueue), Limit<u32>::s_Max);
    if(!maximumSerial)
        return MakeUnexpected(Failure{});
    if(currentSerial >= *maximumSerial)
        return MakeUnexpected(Failure{});

    const u64 serial = currentSerial + 1u;
    const u64 zeroBasedSerial = serial - 1u;
    reservation.serial = serial;
    reservation.marker = static_cast<u32>(zeroBasedSerial / static_cast<u64>(slotsPerQueue)) + 1u;
    reservation.localSlot = static_cast<usize>(zeroBasedSerial % static_cast<u64>(slotsPerQueue));
    return reservation;
}

b8 AmdBreadcrumbRing::MatchesObservation(const u32 observedMarker, const u32 reservedMarker)noexcept{
    return observedMarker != 0u && reservedMarker != 0u && observedMarker == reservedMarker;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

