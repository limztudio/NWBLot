// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#define VMA_IMPLEMENTATION
#include "vulkan_vma.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_allocator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline VmaAllocationCreateInfo BuildBufferAllocationInfo(const BufferDesc& desc, const VkDeviceSize bufferSize){
    VmaAllocationCreateInfo allocInfo{};

    if(desc.isVolatile || desc.cpuAccess == CpuAccessMode::Write){
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else if(desc.cpuAccess == CpuAccessMode::Read){
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }
    else{
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    if(bufferSize >= s_LargeBufferThreshold)
        allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    return allocInfo;
}

inline VmaAllocationCreateInfo BuildTextureAllocationInfo(){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    return allocInfo;
}

inline VmaAllocationCreateInfo BuildStagingTextureAllocationInfo(const CpuAccessMode::Enum cpuAccess){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    if(cpuAccess == CpuAccessMode::Read){
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }
    else{
        allocInfo.requiredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if(cpuAccess != CpuAccessMode::None)
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    if(cpuAccess != CpuAccessMode::None)
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    return allocInfo;
}

inline VmaAllocationCreateInfo BuildHeapAllocationInfo(const HeapDesc& desc){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    switch(desc.type){
    case HeapType::DeviceLocal:
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case HeapType::Upload:
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case HeapType::Readback:
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    default:
        break;
    }

    return allocInfo;
}

inline VmaAllocationCreateInfo BuildHostMappedBufferAllocationInfo(){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    return allocInfo;
}

inline u32 BuildAllMemoryTypeBits(const VkPhysicalDeviceMemoryProperties& memoryProperties){
    if(memoryProperties.memoryTypeCount >= 32u)
        return UINT32_MAX;
    return (1u << memoryProperties.memoryTypeCount) - 1u;
}

inline VkResult MapAllocation(const VmaAllocator allocator, const VmaAllocation allocation, void** outData){
    if(!outData || !allocation)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return vmaMapMemory(allocator, allocation, outData);
}

inline VkResult InvalidateAllocation(const VmaAllocator allocator, const VmaAllocation allocation){
    if(!allocation)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
}

inline void UnmapTransientAllocation(
    const VmaAllocator allocator,
    const VmaAllocation allocation,
    void*& mappedMemory,
    const bool persistentlyMapped
){
    if(mappedMemory && !persistentlyMapped){
        vmaUnmapMemory(allocator, allocation);
        mappedMemory = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanAllocator::VulkanAllocator(const VulkanContext& context)
    : m_context(context)
{}
VulkanAllocator::~VulkanAllocator(){
    if(m_allocator){
        vmaDestroyAllocator(m_allocator);
        m_allocator = nullptr;
    }
}

bool VulkanAllocator::initialize(){
    if(m_allocator)
        return true;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_context.physicalDevice;
    allocatorInfo.device = m_context.device;
    allocatorInfo.instance = m_context.instance;
    allocatorInfo.vulkanApiVersion = s_MinimumVersion;
    allocatorInfo.pAllocationCallbacks = m_context.allocationCallbacks;
    if(m_context.extensions.buffer_device_address)
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VmaVulkanFunctions vulkanFunctions{};
    VkResult res = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to import Vulkan functions for VMA: {}"), ResultToString(res));
        return false;
    }

    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    res = vmaCreateAllocator(&allocatorInfo, &m_allocator);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create VMA allocator: {}"), ResultToString(res));
        m_allocator = nullptr;
        return false;
    }

    return true;
}

VkResult VulkanAllocator::createBuffer(Buffer& buffer, const VkBufferCreateInfo& bufferInfo){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildBufferAllocationInfo(buffer.m_desc, bufferInfo.size);
    VmaAllocationInfo allocationInfo{};
    const VkResult res = vmaCreateBuffer(
        m_allocator,
        &bufferInfo,
        &allocInfo,
        &buffer.m_buffer,
        &buffer.m_allocation,
        &allocationInfo
    );
    if(res == VK_SUCCESS){
        buffer.m_mappedMemory = allocationInfo.pMappedData;
        buffer.m_persistentlyMapped = allocationInfo.pMappedData != nullptr;
    }
    return res;
}

VkResult VulkanAllocator::createVirtualBuffer(Buffer& buffer, const VkBufferCreateInfo& bufferInfo){
    return vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &buffer.m_buffer);
}

void VulkanAllocator::destroyBuffer(Buffer& buffer){
    if(buffer.m_allocation){
        __hidden_vulkan_allocator::UnmapTransientAllocation(
            m_allocator,
            buffer.m_allocation,
            buffer.m_mappedMemory,
            buffer.m_persistentlyMapped
        );
        vmaDestroyBuffer(m_allocator, buffer.m_buffer, buffer.m_allocation);
    }
    else if(buffer.m_desc.isVirtual && buffer.m_buffer != VK_NULL_HANDLE){
        vkDestroyBuffer(m_context.device, buffer.m_buffer, m_context.allocationCallbacks);
    }
    else{
        NWB_ASSERT(buffer.m_buffer == VK_NULL_HANDLE);
    }

    buffer.m_buffer = VK_NULL_HANDLE;
    buffer.m_allocation = nullptr;
    buffer.m_mappedMemory = nullptr;
    buffer.m_persistentlyMapped = false;
}

VkResult VulkanAllocator::mapBufferMemory(Buffer& buffer, void** outData){
    return __hidden_vulkan_allocator::MapAllocation(m_allocator, buffer.m_allocation, outData);
}

void VulkanAllocator::unmapBufferMemory(Buffer& buffer){
    NWB_ASSERT(buffer.m_allocation);
    vmaUnmapMemory(m_allocator, buffer.m_allocation);
}

VkResult VulkanAllocator::invalidateBufferMemory(Buffer& buffer){
    return __hidden_vulkan_allocator::InvalidateAllocation(m_allocator, buffer.m_allocation);
}

VkResult VulkanAllocator::createTexture(Texture& texture, const VkImageCreateInfo& imageInfo){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildTextureAllocationInfo();
    return vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &texture.m_image, &texture.m_allocation, nullptr);
}

VkResult VulkanAllocator::createVirtualTexture(Texture& texture, const VkImageCreateInfo& imageInfo){
    return vkCreateImage(m_context.device, &imageInfo, m_context.allocationCallbacks, &texture.m_image);
}

void VulkanAllocator::destroyTexture(Texture& texture){
    if(texture.m_allocation){
        vmaDestroyImage(m_allocator, texture.m_image, texture.m_allocation);
    }
    else if(texture.m_desc.isVirtual && texture.m_image != VK_NULL_HANDLE){
        vkDestroyImage(m_context.device, texture.m_image, m_context.allocationCallbacks);
    }
    else{
        NWB_ASSERT(texture.m_image == VK_NULL_HANDLE);
    }

    texture.m_image = VK_NULL_HANDLE;
    texture.m_allocation = nullptr;
}

VkResult VulkanAllocator::createStagingTexture(
    StagingTexture& texture,
    const VkBufferCreateInfo& bufferInfo,
    const CpuAccessMode::Enum cpuAccess
){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildStagingTextureAllocationInfo(cpuAccess);
    VmaAllocationInfo allocationInfo{};
    const VkResult res = vmaCreateBuffer(
        m_allocator,
        &bufferInfo,
        &allocInfo,
        &texture.m_buffer,
        &texture.m_allocation,
        &allocationInfo
    );
    if(res == VK_SUCCESS){
        texture.m_mappedMemory = allocationInfo.pMappedData;
        texture.m_persistentlyMapped = allocationInfo.pMappedData != nullptr;
    }
    return res;
}

void VulkanAllocator::destroyStagingTexture(StagingTexture& texture){
    if(texture.m_allocation){
        __hidden_vulkan_allocator::UnmapTransientAllocation(
            m_allocator,
            texture.m_allocation,
            texture.m_mappedMemory,
            texture.m_persistentlyMapped
        );
        vmaDestroyBuffer(m_allocator, texture.m_buffer, texture.m_allocation);
    }
    else{
        NWB_ASSERT(texture.m_buffer == VK_NULL_HANDLE);
    }

    texture.m_buffer = VK_NULL_HANDLE;
    texture.m_allocation = nullptr;
    texture.m_mappedMemory = nullptr;
    texture.m_persistentlyMapped = false;
}

VkResult VulkanAllocator::mapStagingTextureMemory(StagingTexture& texture, void** outData){
    return __hidden_vulkan_allocator::MapAllocation(m_allocator, texture.m_allocation, outData);
}

void VulkanAllocator::unmapStagingTextureMemory(StagingTexture& texture){
    NWB_ASSERT(texture.m_allocation);
    vmaUnmapMemory(m_allocator, texture.m_allocation);
}

VkResult VulkanAllocator::invalidateStagingTextureMemory(StagingTexture& texture){
    return __hidden_vulkan_allocator::InvalidateAllocation(m_allocator, texture.m_allocation);
}

VkResult VulkanAllocator::allocateHeap(Heap& heap){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements memRequirements{};
    memRequirements.size = heap.m_desc.capacity;
    memRequirements.alignment = Max<VkDeviceSize>(m_context.physicalDeviceProperties.limits.bufferImageGranularity, 1u);
    memRequirements.memoryTypeBits = __hidden_vulkan_allocator::BuildAllMemoryTypeBits(m_context.memoryProperties);

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildHeapAllocationInfo(heap.m_desc);
    VmaAllocationInfo allocationInfo{};
    const VkResult res = vmaAllocateDedicatedMemory(
        m_allocator,
        &memRequirements,
        &allocInfo,
        nullptr,
        &heap.m_allocation,
        &allocationInfo
    );
    if(res == VK_SUCCESS){
        heap.m_memory = allocationInfo.deviceMemory;
        heap.m_memoryOffset = allocationInfo.offset;
        heap.m_memoryTypeIndex = allocationInfo.memoryType;
    }

    return res;
}

void VulkanAllocator::freeHeap(Heap& heap){
    if(heap.m_allocation){
        vmaFreeMemory(m_allocator, heap.m_allocation);
        heap.m_allocation = nullptr;
    }
    heap.m_memory = VK_NULL_HANDLE;
    heap.m_memoryOffset = 0;
    heap.m_memoryTypeIndex = UINT32_MAX;
}

VkResult VulkanAllocator::bindHeapBufferMemory(Buffer& buffer, Heap& heap, const u64 offset){
    if(!heap.m_allocation)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vmaBindBufferMemory2(m_allocator, heap.m_allocation, offset, buffer.m_buffer, nullptr);
}

VkResult VulkanAllocator::bindHeapTextureMemory(Texture& texture, Heap& heap, const u64 offset){
    if(!heap.m_allocation)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vmaBindImageMemory2(m_allocator, heap.m_allocation, offset, texture.m_image, nullptr);
}

VkResult VulkanAllocator::createHostMappedBuffer(
    VkBuffer& buffer,
    VmaAllocation& allocation,
    void*& mappedMemory,
    const VkBufferCreateInfo& bufferInfo
){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildHostMappedBufferAllocationInfo();
    VmaAllocationInfo allocationInfo{};
    const VkResult res = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, &allocationInfo);
    if(res == VK_SUCCESS)
        mappedMemory = allocationInfo.pMappedData;
    return res;
}

void VulkanAllocator::destroyHostMappedBuffer(VkBuffer& buffer, VmaAllocation& allocation, void*& mappedMemory){
    if(allocation){
        vmaDestroyBuffer(m_allocator, buffer, allocation);
        allocation = nullptr;
    }
    else{
        NWB_ASSERT(buffer == VK_NULL_HANDLE);
    }

    buffer = VK_NULL_HANDLE;
    mappedMemory = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

