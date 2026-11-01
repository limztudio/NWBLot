// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_heap.h"

#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_heap{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr StringView s_HeapLifecycleDiagnosticCategory = "vulkan_heap_lifecycle";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CpuAccessMode::Enum> TryResolveBufferCpuAccess(const CpuAccessMode::Enum declaredAccess, const b8 isVolatile)noexcept{
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

b8 IsBufferHeapTypeCompatible(const CpuAccessMode::Enum declaredAccess, const b8 isVolatile, const HeapType::Enum heapType)noexcept{
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

Expected<MemoryRequirements> TryBuildBufferHeapRequirements(const MemoryRequirements& nativeRequirements, const CpuAccessMode::Enum declaredAccess, const b8 isVolatile, const u64 nonCoherentAtomSize)noexcept{
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


Expected<HeapBindingRange> TryBuildHeapBindingRange(const u64 heapCapacity, const u64 heapMemoryOffset, const u64 localOffset, const u64 size, const u64 alignment)noexcept{
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

b8 HeapBindingRangesConflict(const HeapBindingRange& first, const HeapBindingResourceClass::Enum firstClass, const HeapBindingRange& second, const HeapBindingResourceClass::Enum secondClass, const u64 bufferImageGranularity)noexcept{
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

u32 BuildNonProtectedMemoryTypeBits(const VkPhysicalDeviceMemoryProperties& memoryProperties)noexcept{
    u32 memoryTypeBits = 0u;
    const u32 memoryTypeCount = Min<u32>(memoryProperties.memoryTypeCount, s_VulkanMemoryTypeBitCount);
    for(u32 memoryTypeIndex = 0u; memoryTypeIndex < memoryTypeCount; ++memoryTypeIndex){
        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
        if((flags & VK_MEMORY_PROPERTY_PROTECTED_BIT) == 0u)
            memoryTypeBits |= 1u << memoryTypeIndex;
    }
    return memoryTypeBits;
}

b8 IsHeapMemoryTypeCompatible(const VkPhysicalDeviceMemoryProperties& memoryProperties, const u32 heapMemoryTypeIndex, const u32 resourceMemoryTypeBits)noexcept{
    if(heapMemoryTypeIndex >= memoryProperties.memoryTypeCount || heapMemoryTypeIndex >= s_VulkanMemoryTypeBitCount || (resourceMemoryTypeBits & (1u << heapMemoryTypeIndex)) == 0u)
        return false;

    const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[heapMemoryTypeIndex].propertyFlags;
    return (flags & VK_MEMORY_PROPERTY_PROTECTED_BIT) == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Heap::Heap(const VulkanContext& context, VulkanAllocator& allocator)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_bindingReservations(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}

Heap::~Heap()noexcept{
    {
        ScopedLock lock(m_bindingMutex);
        if(!m_bindingReservations.empty()){
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = DiagnosticEventName::s_Error,
                .category = __hidden_vulkan_heap::s_HeapLifecycleDiagnosticCategory,
                .message = "Vulkan: Refusing to free a heap with live placed-resource bindings",
                .file = __FILE__,
                .line = __LINE__,
            });
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Heap destroyed with live placed-resource bindings"));
            return;
        }
    }
    m_allocator.freeHeap(*this);
}

Object Heap::getNativeHandle(ObjectType objectType)noexcept{
    if(objectType == ObjectTypes::s_DeviceMemory)
        return Object(m_memory);
    return Object(nullptr);
}

void Heap::eraseBindingReservationLocked(const NotNull<const void*> owner)noexcept{
    for(auto reservation = m_bindingReservations.begin(); reservation != m_bindingReservations.end(); ++reservation){
        if(reservation->owner != owner)
            continue;

        m_bindingReservations.erase(reservation);
        return;
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = __hidden_vulkan_heap::s_HeapLifecycleDiagnosticCategory,
        .message = "Vulkan: Failed to unregister a placed-resource heap binding",
        .file = __FILE__,
        .line = __LINE__,
    });
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Missing placed-resource heap binding"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<HeapHandle> Device::createHeap(const HeapDesc& d){
    VkResult res = VK_SUCCESS;

    if(d.capacity == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        return MakeUnexpected(Failure{});
    }

    switch(d.type){
    case HeapType::DeviceLocal:
    case HeapType::Upload:
    case HeapType::Readback:
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        return MakeUnexpected(Failure{});
    }

    HeapHandle heap(NewArenaObject<Heap>(m_context.objectArena, m_context, m_allocator), HeapHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!heap)
        return MakeUnexpected(Failure{});
    heap->m_desc = d;

    res = m_allocator.allocateHeap(*heap);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate heap memory ({} bytes): {}"), d.capacity, ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    return heap;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

