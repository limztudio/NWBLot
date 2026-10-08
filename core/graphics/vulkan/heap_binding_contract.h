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


[[nodiscard]] inline Expected<CpuAccessMode::Enum> TryResolveBufferCpuAccess(
    const CpuAccessMode::Enum declaredAccess, const bool isVolatile
)noexcept{
    switch(declaredAccess){
    case CpuAccessMode::None:
    case CpuAccessMode::Read:
    case CpuAccessMode::Write:
        break;
    default:
        return MakeUnexpected(Failure{});
    }
    if(isVolatile){
        if(declaredAccess == CpuAccessMode::Read)
            return MakeUnexpected(Failure{});
        return CpuAccessMode::Write;
    }
    return declaredAccess;
}

[[nodiscard]] inline bool IsBufferHeapTypeCompatible(
    const CpuAccessMode::Enum declaredAccess,
    const bool isVolatile,
    const HeapType::Enum heapType
)noexcept{
    const auto effectiveAccess = TryResolveBufferCpuAccess(declaredAccess, isVolatile);
    if(!effectiveAccess)
        return false;

    switch(*effectiveAccess){
    case CpuAccessMode::None:
        return heapType == HeapType::DeviceLocal;
    case CpuAccessMode::Read:
        return heapType == HeapType::Readback;
    case CpuAccessMode::Write:
        return heapType == HeapType::Upload;
    default:
        return false;
    }
}

[[nodiscard]] inline Expected<MemoryRequirements> TryBuildBufferHeapRequirements(
    const MemoryRequirements& nativeRequirements,
    const CpuAccessMode::Enum declaredAccess,
    const bool isVolatile,
    const u64 nonCoherentAtomSize
)noexcept{
    if(nativeRequirements.size == 0u || nativeRequirements.alignment == 0u)
        return MakeUnexpected(Failure{});
    const auto effectiveAccess = TryResolveBufferCpuAccess(declaredAccess, isVolatile);
    if(!effectiveAccess)
        return MakeUnexpected(effectiveAccess.error());
    MemoryRequirements adjusted = nativeRequirements;
    if(*effectiveAccess == CpuAccessMode::Read){
        const u64 atomSize = Max<u64>(nonCoherentAtomSize, 1u);
        adjusted.alignment = Max<u64>(adjusted.alignment, atomSize);
        const auto size = AlignUpChecked(adjusted.size, atomSize);
        if(!size)
            return MakeUnexpected(size.error());
        adjusted.size = *size;
    }
    return adjusted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<HeapBindingRange> TryBuildHeapBindingRange(
    const u64 heapCapacity,
    const u64 heapMemoryOffset,
    const u64 localOffset,
    const u64 size,
    const u64 alignment
)noexcept{
    if(size == 0u || localOffset > heapCapacity || size > heapCapacity - localOffset)
        return MakeUnexpected(Failure{});
    if(localOffset > Limit<u64>::s_Max - heapMemoryOffset)
        return MakeUnexpected(Failure{});
    const u64 absoluteBegin = heapMemoryOffset + localOffset;
    const u64 requiredAlignment = Max<u64>(alignment, 1u);
    if((absoluteBegin % requiredAlignment) != 0u || size > Limit<u64>::s_Max - absoluteBegin)
        return MakeUnexpected(Failure{});
    return HeapBindingRange{ localOffset, size, absoluteBegin, absoluteBegin + size };
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

