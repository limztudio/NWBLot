// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace HeapBindingResourceClass{
    enum Enum : u8{
        Buffer,
        OptimalImage,
    };
};

struct HeapBindingRange{
    u64 localOffset = 0u;
    u64 size = 0u;
    u64 absoluteBegin = 0u;
    u64 absoluteEnd = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool TryBuildHeapBindingRange(
    const u64 heapCapacity,
    const u64 heapMemoryOffset,
    const u64 localOffset,
    const u64 size,
    const u64 alignment,
    HeapBindingRange& outRange
)noexcept{
    outRange = {};
    if(size == 0u || localOffset > heapCapacity || size > heapCapacity - localOffset)
        return false;
    if(localOffset > Limit<u64>::s_Max - heapMemoryOffset)
        return false;

    const u64 absoluteBegin = heapMemoryOffset + localOffset;
    const u64 requiredAlignment = Max<u64>(alignment, 1u);
    if((absoluteBegin % requiredAlignment) != 0u || size > Limit<u64>::s_Max - absoluteBegin)
        return false;

    outRange.localOffset = localOffset;
    outRange.size = size;
    outRange.absoluteBegin = absoluteBegin;
    outRange.absoluteEnd = absoluteBegin + size;
    return true;
}

[[nodiscard]] inline bool HeapBindingRangesConflict(
    const HeapBindingRange& first,
    const HeapBindingResourceClass::Enum firstClass,
    const HeapBindingRange& second,
    const HeapBindingResourceClass::Enum secondClass,
    const u64 bufferImageGranularity
)noexcept{
    if(first.size == 0u || second.size == 0u)
        return false;
    if(first.absoluteBegin < second.absoluteEnd && second.absoluteBegin < first.absoluteEnd)
        return true;
    if(firstClass == secondClass)
        return false;

    const u64 granularity = Max<u64>(bufferImageGranularity, 1u);
    const u64 firstFirstPage = first.absoluteBegin / granularity;
    const u64 firstLastPage = (first.absoluteEnd - 1u) / granularity;
    const u64 secondFirstPage = second.absoluteBegin / granularity;
    const u64 secondLastPage = (second.absoluteEnd - 1u) / granularity;
    return firstFirstPage <= secondLastPage && secondFirstPage <= firstLastPage;
}

[[nodiscard]] inline u32 BuildNonProtectedMemoryTypeBits(
    const VkPhysicalDeviceMemoryProperties& memoryProperties
)noexcept{
    u32 memoryTypeBits = 0u;
    const u32 memoryTypeCount = Min<u32>(memoryProperties.memoryTypeCount, s_VulkanMemoryTypeBitCount);
    for(u32 memoryTypeIndex = 0u; memoryTypeIndex < memoryTypeCount; ++memoryTypeIndex){
        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
        if((flags & VK_MEMORY_PROPERTY_PROTECTED_BIT) == 0u)
            memoryTypeBits |= 1u << memoryTypeIndex;
    }
    return memoryTypeBits;
}

[[nodiscard]] inline bool IsHeapMemoryTypeCompatible(
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    const u32 heapMemoryTypeIndex,
    const u32 resourceMemoryTypeBits
)noexcept{
    if(
        heapMemoryTypeIndex >= memoryProperties.memoryTypeCount
        || heapMemoryTypeIndex >= s_VulkanMemoryTypeBitCount
        || (resourceMemoryTypeBits & (1u << heapMemoryTypeIndex)) == 0u
    )
        return false;

    const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[heapMemoryTypeIndex].propertyFlags;
    return (flags & VK_MEMORY_PROPERTY_PROTECTED_BIT) == 0u;
}

[[nodiscard]] inline bool AllowsGenericHeapBinding(
    const VkMemoryDedicatedRequirements& dedicatedRequirements
)noexcept{
    return dedicatedRequirements.requiresDedicatedAllocation == VK_FALSE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

