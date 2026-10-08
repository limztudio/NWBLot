// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "host_readback_sync.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool HasBufferDeviceWriteState(const ResourceStates::Mask states)noexcept{
    constexpr ResourceStates::Mask s_BufferDeviceWriteStates = ResourceStates::UnorderedAccess
        | ResourceStates::StreamOut
        | ResourceStates::CopyDest
        | ResourceStates::ResolveDest
        | ResourceStates::AccelStructWrite
        | ResourceStates::AccelStructBuildBlas
        | ResourceStates::OpacityMicromapWrite
        | ResourceStates::ConvertCoopVecMatrixOutput
    ;
    return (states & s_BufferDeviceWriteStates) != ResourceStates::Unknown;
}

VkBufferMemoryBarrier2 BuildHostReadBufferBarrier(const VkBuffer buffer)noexcept{
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

void CollectUniquePhysicalQueueFamilyIndices(
    const GpuPhysicalQueueTopology& topology,
    Vector<u32, Alloc::ScratchArena>& familyIndices
){
    familyIndices.clear();
    if(!topology.queues)
        return;

    familyIndices.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const u32 familyIndex = topology.queues[queueIndex].familyIndex;
        if(familyIndex == VK_QUEUE_FAMILY_IGNORED)
            continue;

        bool duplicate = false;
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

Expected<AmdBreadcrumbRingLayout> TryBuildAmdBreadcrumbRingLayout(
    const GpuPhysicalQueueTopology& topology,
    const usize slotsPerQueue
)noexcept{
    AmdBreadcrumbRingLayout layout;
    if(
        !topology.queues
        || topology.queueCount == 0u
        || topology.queueCount > static_cast<usize>(Limit<u16>::s_Max)
        || slotsPerQueue == 0u
    )
        return MakeUnexpected(Failure{});

    const auto totalSlotCount = TryMultiply<usize>(topology.queueCount, slotsPerQueue);
    if(!totalSlotCount)
        return MakeUnexpected(Failure{});
    const auto totalByteSize = TryMultiply<usize>(*totalSlotCount, sizeof(u32));
    if(
        !totalByteSize
        || slotsPerQueue > static_cast<usize>(Limit<u32>::s_Max)
        || *totalByteSize > static_cast<usize>(Limit<VkDeviceSize>::s_Max)
    )
        return MakeUnexpected(Failure{});

    const u16 deviceGeneration = topology.queues[0u].id.deviceGeneration;
    if(deviceGeneration == 0u)
        return MakeUnexpected(Failure{});
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueId& queue = topology.queues[queueIndex].id;
        if(
            !queue.valid()
            || queue.deviceGeneration != deviceGeneration
            || queue.index != queueIndex
        )
            return MakeUnexpected(Failure{});
    }

    layout.deviceGeneration = deviceGeneration;
    layout.physicalQueueCount = topology.queueCount;
    layout.slotsPerQueue = slotsPerQueue;
    layout.totalSlotCount = *totalSlotCount;
    layout.totalByteSize = static_cast<VkDeviceSize>(*totalByteSize);
    return layout;
}

Expected<AmdBreadcrumbRingSlot> TryResolveAmdBreadcrumbRingSlot(
    const AmdBreadcrumbRingLayout& layout,
    const GpuPhysicalQueueId& queue,
    const usize localSlot
)noexcept{
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
    if(
        resolvedFlatSlot >= layout.totalSlotCount
        || !resolvedByteOffset
        || *resolvedByteOffset > static_cast<usize>(Limit<VkDeviceSize>::s_Max)
    )
        return MakeUnexpected(Failure{});

    return AmdBreadcrumbRingSlot{ resolvedFlatSlot, static_cast<VkDeviceSize>(*resolvedByteOffset) };
}

Expected<AmdBreadcrumbReservation> TryBuildNextAmdBreadcrumbReservation(
    const u64 currentSerial,
    const usize slotsPerQueue
)noexcept{
    AmdBreadcrumbReservation reservation;
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

bool MatchesAmdBreadcrumbObservation(const u32 observedMarker, const u32 reservedMarker)noexcept{
    return observedMarker != 0u && reservedMarker != 0u && observedMarker == reservedMarker;
}

HostReadbackBarrierTracker::HostReadbackBarrierTracker(Alloc::GlobalArena& arena)
    : m_buffers(arena)
{}

bool HostReadbackBarrierTracker::registerBuffer(const VkBuffer buffer){
    if(buffer == VK_NULL_HANDLE)
        return false;

    for(const VkBuffer trackedBuffer : m_buffers){
        if(trackedBuffer == buffer)
            return false;
    }

    m_buffers.push_back(buffer);
    return true;
}

void HostReadbackBarrierTracker::registerDeviceOwnedBuffer(const VkBuffer buffer){
    if(!registerBuffer(buffer))
        return;
}

void HostReadbackBarrierTracker::appendBarriers(
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& barriers
)const{
    barriers.reserve(barriers.size() + m_buffers.size());
    for(const VkBuffer buffer : m_buffers)
        barriers.push_back(BuildHostReadBufferBarrier(buffer));
}

void HostReadbackBarrierTracker::clear()noexcept{
    m_buffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

