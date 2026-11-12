// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "amd_breadcrumb_storage.h"
#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


b8 AmdBreadcrumbStorage::initializeMetadata(const VulkanDetail::AmdBreadcrumbRing::Layout& newLayout, const Name& allocationLog){
    const auto expectedSlotCount = TryMultiply<usize>(newLayout.physicalQueueCount, newLayout.slotsPerQueue);
    if(metadata || newLayout.physicalQueueCount == 0u || newLayout.slotsPerQueue == 0u || !expectedSlotCount || *expectedSlotCount != newLayout.totalSlotCount)
        return false;
    const auto slotRecordBytes = TryMultiply<usize>(newLayout.totalSlotCount, sizeof(AmdBreadcrumbSlotRecord));
    const auto nextSerialBytes = TryMultiply<usize>(newLayout.physicalQueueCount, sizeof(u64));
    if(!slotRecordBytes || !nextSerialBytes)
        return false;

    const usize slotRecordAllocationBytes = Alloc::PersistentArena::StructureAlignedSize(*slotRecordBytes, alignof(AmdBreadcrumbSlotRecord));
    const usize nextSerialAllocationBytes = Alloc::PersistentArena::StructureAlignedSize(*nextSerialBytes, alignof(u64));
    if(AddOverflows<usize>(slotRecordAllocationBytes, nextSerialAllocationBytes))
        return false;

    metadata.emplace(allocationLog, slotRecordAllocationBytes + nextSerialAllocationBytes, newLayout.totalSlotCount, newLayout.physicalQueueCount);
    if(!metadata->slotRecords || !metadata->nextSerials){
        metadata.reset();
        return false;
    }

    for(usize slotIndex = 0u; slotIndex < metadata->slotRecordCount; ++slotIndex)
        metadata->slotRecords[slotIndex] = {};
    for(usize queueIndex = 0u; queueIndex < metadata->nextSerialCount; ++queueIndex)
        metadata->nextSerials[queueIndex] = 0u;
    layout = newLayout;
    return true;
}

b8 AmdBreadcrumbStorage::initialize(VulkanAllocator& allocator, const GpuPhysicalQueueTopology& topology){
    const auto breadcrumbLayout = VulkanDetail::AmdBreadcrumbRing::BuildLayout(topology, s_MaxAmdBreadcrumbSlots);
    if(!breadcrumbLayout){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Invalid AMD breadcrumb ring layout; AMD GPU breadcrumbs disabled."));
        return false;
    }
    if(!initializeMetadata(*breadcrumbLayout, VulkanArenaScope::s_AmdBreadcrumbMetadataArena)){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to reserve AMD breadcrumb metadata; AMD GPU breadcrumbs disabled."));
        return false;
    }

    auto breadcrumbInfo = VulkanDetail::MakeVkStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    breadcrumbInfo.size = breadcrumbLayout->totalByteSize;
    breadcrumbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Alloc::ScratchArena breadcrumbQueueArena(VulkanArenaScope::s_QueueFamilyQueryArena);
    Vector<u32, Alloc::ScratchArena> breadcrumbQueueFamilies(breadcrumbQueueArena);
    VulkanDetail::AmdBreadcrumbRing::CollectSharingQueueFamilies(topology, breadcrumbQueueFamilies);
    breadcrumbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if(breadcrumbQueueFamilies.size() > 1u){
        breadcrumbInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        breadcrumbInfo.queueFamilyIndexCount = static_cast<u32>(breadcrumbQueueFamilies.size());
        breadcrumbInfo.pQueueFamilyIndices = breadcrumbQueueFamilies.data();
    }
    const auto breadcrumb = allocator.createHostMappedBuffer(breadcrumbInfo);
    if(breadcrumb){
        buffer = breadcrumb->buffer;
        allocation = breadcrumb->allocation;
        mappedMemory = breadcrumb->mappedMemory;
    }
    if(breadcrumb && mappedMemory){
        FOUNDATION_MEMSET(mappedMemory, 0, static_cast<usize>(breadcrumbInfo.size));
        return true;
    }
    if(breadcrumb)
        allocator.destroyHostMappedBuffer(buffer, allocation, mappedMemory);
    NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to allocate AMD breadcrumb buffer ({}); breadcrumbs disabled."), ResultToString(breadcrumb ? VK_SUCCESS : breadcrumb.error()));
    return false;
}

void AmdBreadcrumbStorage::release(VulkanAllocator& allocator)noexcept{
    if(allocation)
        allocator.destroyHostMappedBuffer(buffer, allocation, mappedMemory);
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

