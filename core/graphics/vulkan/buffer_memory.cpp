// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* Device::mapBuffer(Buffer* bufferResource, const CpuAccessMode::Enum requestedAccess){
    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: buffer is null"));
        return nullptr;
    }
    if(requestedAccess != CpuAccessMode::Read && requestedAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: invalid CPU access mode"));
        return nullptr;
    }

    Buffer& buffer = *bufferResource;
    if(&buffer.m_context != &m_context || &buffer.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: buffer belongs to another device"));
        return nullptr;
    }
    if(!buffer.m_managed){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: native buffer memory is not managed by this device"));
        return nullptr;
    }
    if(buffer.m_buffer == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: native buffer is null"));
        return nullptr;
    }
    CpuAccessMode::Enum effectiveAccess = CpuAccessMode::None;
    if(
        !VulkanDetail::TryResolveBufferCpuAccess(
            buffer.m_desc.cpuAccess,
            buffer.m_desc.isVolatile,
            effectiveAccess
        )
        || effectiveAccess == CpuAccessMode::None
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: buffer was created without CPU access"));
        return nullptr;
    }
    if(requestedAccess != effectiveAccess){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: requested access does not match the buffer CPU access"));
        return nullptr;
    }

    ScopedLock resourceLock(buffer.m_memoryBindingMutex);
    if(buffer.m_desc.isVirtual){
        if(!buffer.m_boundHeap || buffer.m_heapBindingRange.size == 0u || !buffer.m_mappedMemory){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: virtual buffer has no mapped heap binding"));
            return nullptr;
        }

        Heap& heap = *buffer.m_boundHeap.get();
        ScopedLock heapLock(heap.m_bindingMutex);
        if(
            &heap.m_context != &m_context
            || &heap.m_allocator != &m_allocator
            || !heap.m_allocation
            || !heap.m_mappedMemory
            || !VulkanDetail::IsBufferHeapTypeCompatible(
                buffer.m_desc.cpuAccess,
                buffer.m_desc.isVolatile,
                heap.m_desc.type
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: virtual buffer heap binding is invalid"));
            return nullptr;
        }

        if(effectiveAccess == CpuAccessMode::Read){
            const VkResult res = m_allocator.invalidateHeapMemory(
                heap,
                buffer.m_heapBindingRange.localOffset,
                buffer.m_heapBindingRange.size
            );
            if(res != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to invalidate readback heap mapping: {}"), ResultToString(res));
                return nullptr;
            }
        }
        return buffer.m_mappedMemory;
    }

    if(buffer.m_allocation == nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: buffer has no allocation"));
        return nullptr;
    }

    auto invalidateReadRange = [&]() -> bool{
        if(effectiveAccess != CpuAccessMode::Read)
            return true;

        const VkResult res = m_allocator.invalidateBufferMemory(buffer);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to invalidate readback buffer mapping: {}"), ResultToString(res));
            return false;
        }
        return true;
    };

    if(buffer.m_mappedMemory){
        if(!invalidateReadRange())
            return nullptr;
        return buffer.m_mappedMemory;
    }

    void* data = nullptr;
    const VkResult res = m_allocator.mapBufferMemory(buffer, &data);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer memory: {}"), ResultToString(res));
        return nullptr;
    }

    buffer.m_mappedMemory = data;
    if(!invalidateReadRange()){
        m_allocator.unmapBufferMemory(buffer);
        buffer.m_mappedMemory = nullptr;
        return nullptr;
    }
    return data;
}

void Device::unmapBuffer(Buffer* bufferResource){
    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: buffer is null"));
        return;
    }

    Buffer& buffer = *bufferResource;
    if(&buffer.m_context != &m_context || &buffer.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: buffer belongs to another device"));
        return;
    }
    if(!buffer.m_managed){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: native buffer memory is not managed by this device"));
        return;
    }
    if(buffer.m_buffer == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: native buffer is null"));
        return;
    }
    CpuAccessMode::Enum effectiveAccess = CpuAccessMode::None;
    if(
        !VulkanDetail::TryResolveBufferCpuAccess(
            buffer.m_desc.cpuAccess,
            buffer.m_desc.isVolatile,
            effectiveAccess
        )
        || effectiveAccess == CpuAccessMode::None
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: buffer was created without CPU access"));
        return;
    }

    ScopedLock resourceLock(buffer.m_memoryBindingMutex);
    if(buffer.m_desc.isVirtual){
        if(!buffer.m_boundHeap || buffer.m_heapBindingRange.size == 0u || !buffer.m_mappedMemory){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: virtual buffer has no mapped heap binding"));
            return;
        }

        Heap& heap = *buffer.m_boundHeap.get();
        ScopedLock heapLock(heap.m_bindingMutex);
        if(
            &heap.m_context != &m_context
            || &heap.m_allocator != &m_allocator
            || !heap.m_allocation
            || !heap.m_mappedMemory
            || !VulkanDetail::IsBufferHeapTypeCompatible(
                buffer.m_desc.cpuAccess,
                buffer.m_desc.isVolatile,
                heap.m_desc.type
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: virtual buffer heap binding is invalid"));
            return;
        }
        return;
    }

    if(buffer.m_allocation == nullptr || !buffer.m_mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap buffer: buffer memory is not mapped"));
        return;
    }

    if(buffer.m_mappedMemory && !buffer.m_persistentlyMapped){
        buffer.m_allocator.unmapBufferMemory(buffer);
        buffer.m_mappedMemory = nullptr;
    }
}

MemoryRequirements Device::getBufferMemoryRequirements(Buffer* bufferResource){
    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get buffer memory requirements: buffer is null"));
        return {};
    }

    Buffer& buffer = *bufferResource;
    if(&buffer.m_context != &m_context || &buffer.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get buffer memory requirements: buffer belongs to another device"));
        return {};
    }
    if(!buffer.m_managed || !buffer.m_desc.isVirtual || buffer.m_allocation != nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get buffer memory requirements: buffer is not a managed virtual buffer"));
        return {};
    }
    if(buffer.m_buffer == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get buffer memory requirements: native buffer is null"));
        return {};
    }

    ScopedLock resourceLock(buffer.m_memoryBindingMutex);

    VkBufferMemoryRequirementsInfo2 requirementsInfo{};
    requirementsInfo.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    requirementsInfo.buffer = buffer.m_buffer;
    VkMemoryRequirements2 memoryRequirements{};
    memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    vkGetBufferMemoryRequirements2(m_context.device, &requirementsInfo, &memoryRequirements);

    const MemoryRequirements nativeRequirements{
        .size = memoryRequirements.memoryRequirements.size,
        .alignment = memoryRequirements.memoryRequirements.alignment,
    };
    MemoryRequirements result;
    if(!VulkanDetail::TryBuildBufferHeapRequirements(
        nativeRequirements,
        buffer.m_desc.cpuAccess,
        buffer.m_desc.isVolatile,
        m_context.physicalDeviceProperties.limits.nonCoherentAtomSize,
        result
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get buffer memory requirements: padded requirements overflow"));
        return {};
    }
    return result;
}

bool Device::bindBufferMemory(Buffer* bufferResource, Heap* heap, u64 offset){
    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: buffer is null"));
        return false;
    }
    if(!heap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: heap is null"));
        return false;
    }
    Buffer& buffer = *bufferResource;
    if(&buffer.m_context != &m_context || &buffer.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: buffer belongs to another device"));
        return false;
    }
    if(!buffer.m_managed || !buffer.m_desc.isVirtual || buffer.m_allocation != nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: buffer is not a managed virtual buffer"));
        return false;
    }
    if(buffer.m_buffer == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: native buffer is null"));
        return false;
    }

    Heap& memoryHeap = *heap;
    if(&memoryHeap.m_context != &m_context || &memoryHeap.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: heap belongs to another device"));
        return false;
    }
    if(memoryHeap.m_allocation == nullptr || memoryHeap.m_memory == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: heap is invalid"));
        return false;
    }
    HeapHandle retainedHeap(heap, HeapHandle::deleter_type(&memoryHeap.m_context.objectArena));
    ScopedLock resourceLock(buffer.m_memoryBindingMutex);
    if(buffer.m_boundHeap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: buffer memory was already bound"));
        return false;
    }
    if(!VulkanDetail::IsBufferHeapTypeCompatible(
        buffer.m_desc.cpuAccess,
        buffer.m_desc.isVolatile,
        memoryHeap.m_desc.type
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: CPU access is incompatible with heap type"));
        return false;
    }

    VkMemoryDedicatedRequirements dedicatedRequirements{};
    dedicatedRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 memoryRequirements{};
    memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memoryRequirements.pNext = &dedicatedRequirements;
    VkBufferMemoryRequirementsInfo2 requirementsInfo{};
    requirementsInfo.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    requirementsInfo.buffer = buffer.m_buffer;
    vkGetBufferMemoryRequirements2(m_context.device, &requirementsInfo, &memoryRequirements);

    const MemoryRequirements nativeRequirements{
        .size = memoryRequirements.memoryRequirements.size,
        .alignment = memoryRequirements.memoryRequirements.alignment,
    };
    MemoryRequirements bindingRequirements;
    if(!VulkanDetail::TryBuildBufferHeapRequirements(
        nativeRequirements,
        buffer.m_desc.cpuAccess,
        buffer.m_desc.isVolatile,
        m_context.physicalDeviceProperties.limits.nonCoherentAtomSize,
        bindingRequirements
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: padded requirements overflow"));
        return false;
    }
    memoryRequirements.memoryRequirements.size = bindingRequirements.size;
    memoryRequirements.memoryRequirements.alignment = bindingRequirements.alignment;

    ScopedLock heapLock(memoryHeap.m_bindingMutex);
    if(memoryHeap.m_desc.type != HeapType::DeviceLocal && !memoryHeap.m_mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: CPU-visible heap is not mapped"));
        return false;
    }
    VulkanDetail::HeapBindingRange bindingRange;
    if(!validateHeapMemoryBinding(
        memoryHeap,
        memoryRequirements.memoryRequirements,
        dedicatedRequirements,
        offset,
        VulkanDetail::HeapBindingResourceClass::Buffer,
        NWB_TEXT("bind buffer memory"),
        NWB_TEXT("buffer"),
        bindingRange
    ))
        return false;

    Heap::BindingReservation reservation;
    reservation.owner = &buffer;
    reservation.resourceClass = VulkanDetail::HeapBindingResourceClass::Buffer;
    reservation.range = bindingRange;
    memoryHeap.m_bindingReservations.push_back(reservation);

    const VkResult res = m_allocator.bindHeapBufferMemory(buffer, memoryHeap, offset);
    if(res != VK_SUCCESS){
        memoryHeap.m_bindingReservations.pop_back();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: {}"), ResultToString(res));
        return false;
    }
    if(m_context.extensions.buffer_device_address){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer.m_buffer;
        buffer.m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    }
    buffer.m_heapBindingRange = bindingRange;
    if(memoryHeap.m_desc.type != HeapType::DeviceLocal){
        buffer.m_mappedMemory = static_cast<u8*>(memoryHeap.m_mappedMemory) + bindingRange.localOffset;
        buffer.m_persistentlyMapped = true;
    }
    buffer.m_boundHeap = Move(retainedHeap);

    return true;
}

bool Device::isBufferReadyForGpuUse(Buffer* bufferResource)const noexcept{
    if(!bufferResource)
        return false;

    Buffer& buffer = *bufferResource;
    ScopedLock resourceLock(buffer.m_memoryBindingMutex);

    if(&buffer.m_context != &m_context || &buffer.m_allocator != &m_allocator)
        return false;
    if(buffer.m_buffer == VK_NULL_HANDLE)
        return false;
    if(!m_allocator.isBufferNativeIdentityRegistered(buffer))
        return false;
    if(!buffer.m_managed)
        return true;

    if(!buffer.m_desc.isVirtual)
        return buffer.m_allocation != nullptr;
    if(buffer.m_allocation || !buffer.m_boundHeap || buffer.m_heapBindingRange.size == 0u)
        return false;

    Heap& heap = *buffer.m_boundHeap.get();
    ScopedLock heapLock(heap.m_bindingMutex);
    return &heap.m_context == &m_context
        && &heap.m_allocator == &m_allocator
        && heap.m_allocation != nullptr
        && heap.m_memory != VK_NULL_HANDLE
    ;
}

bool Device::validateHeapMemoryBinding(
    const Heap& heap,
    const VkMemoryRequirements& memoryRequirements,
    const VkMemoryDedicatedRequirements& dedicatedRequirements,
    const u64 offset,
    const VulkanDetail::HeapBindingResourceClass::Enum resourceClass,
    const tchar* operationName,
    const tchar* resourceName,
    VulkanDetail::HeapBindingRange& outRange
)const{
    outRange = {};
    if(&heap.m_context != &m_context || &heap.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: heap belongs to another device"), operationName);
        return false;
    }
    if(heap.m_allocation == nullptr || heap.m_memory == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: heap is invalid"), operationName);
        return false;
    }
    if(!VulkanDetail::AllowsGenericHeapBinding(dedicatedRequirements)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to {}: the {} requires a dedicated allocation")
            , operationName
            , resourceName
        );
        return false;
    }
    if(!VulkanDetail::IsHeapMemoryTypeCompatible(
        m_context.memoryProperties,
        heap.m_memoryTypeIndex,
        memoryRequirements.memoryTypeBits
    )){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to {}: heap memory type is incompatible with the {}"),
            operationName,
            resourceName
        );
        return false;
    }
    if(!VulkanDetail::TryBuildHeapBindingRange(
        heap.m_desc.capacity,
        heap.m_memoryOffset,
        offset,
        memoryRequirements.size,
        memoryRequirements.alignment,
        outRange
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: offset {} size {} is misaligned or outside heap capacity {}")
            , operationName
            , offset
            , static_cast<u64>(memoryRequirements.size)
            , heap.m_desc.capacity
        );
        return false;
    }
    const u64 granularity = Max<u64>(m_context.physicalDeviceProperties.limits.bufferImageGranularity, 1u);
    for(const Heap::BindingReservation& reservation : heap.m_bindingReservations){
        if(!VulkanDetail::HeapBindingRangesConflict(
            reservation.range,
            reservation.resourceClass,
            outRange,
            resourceClass,
            granularity
        ))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: the {} heap range conflicts with a live binding")
            , operationName
            , resourceName
        );
        outRange = {};
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

