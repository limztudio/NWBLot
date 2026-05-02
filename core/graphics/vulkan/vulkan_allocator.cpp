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
    if(cpuAccess == CpuAccessMode::None){
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        return allocInfo;
    }

    if(cpuAccess == CpuAccessMode::Read){
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }
    else{
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

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

inline VmaAllocator ToVmaAllocator(const VulkanAllocatorHandle allocator){
    static_assert(sizeof(VmaAllocator) == sizeof(VulkanAllocatorHandle));
    return reinterpret_cast<VmaAllocator>(allocator);
}

inline VulkanAllocatorHandle ToVulkanAllocatorHandle(const VmaAllocator allocator){
    static_assert(sizeof(VulkanAllocatorHandle) == sizeof(VmaAllocator));
    return reinterpret_cast<VulkanAllocatorHandle>(allocator);
}

inline VmaAllocation ToVmaAllocation(const VulkanAllocationHandle allocation){
    static_assert(sizeof(VmaAllocation) == sizeof(VulkanAllocationHandle));
    return reinterpret_cast<VmaAllocation>(allocation);
}

inline VulkanAllocationHandle ToVulkanAllocationHandle(const VmaAllocation allocation){
    static_assert(sizeof(VulkanAllocationHandle) == sizeof(VmaAllocation));
    return reinterpret_cast<VulkanAllocationHandle>(allocation);
}

inline VkResult MapAllocation(const VulkanAllocatorHandle allocator, const VulkanAllocationHandle allocation, void** outData){
    if(!outData || !allocation)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return vmaMapMemory(ToVmaAllocator(allocator), ToVmaAllocation(allocation), outData);
}

inline VkResult InvalidateAllocation(const VulkanAllocatorHandle allocator, const VulkanAllocationHandle allocation){
    if(!allocation)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return vmaInvalidateAllocation(ToVmaAllocator(allocator), ToVmaAllocation(allocation), 0, VK_WHOLE_SIZE);
}

inline void UnmapAllocation(const VulkanAllocatorHandle allocator, const VulkanAllocationHandle allocation){
    NWB_ASSERT(allocation);
    vmaUnmapMemory(ToVmaAllocator(allocator), ToVmaAllocation(allocation));
}

inline void UnmapTransientAllocation(
    const VulkanAllocatorHandle allocator,
    const VulkanAllocationHandle allocation,
    void*& mappedMemory,
    const bool persistentlyMapped
){
    if(mappedMemory && !persistentlyMapped){
        UnmapAllocation(allocator, allocation);
        mappedMemory = nullptr;
    }
}

inline VkResult CreateBufferAllocation(
    const VulkanAllocatorHandle allocator,
    const VkBufferCreateInfo& bufferInfo,
    const VmaAllocationCreateInfo& allocInfo,
    VkBuffer& buffer,
    VulkanAllocationHandle& allocation,
    void*& mappedMemory
){
    VmaAllocationInfo allocationInfo{};
    VmaAllocation vmaAllocation = nullptr;
    const VkResult res = vmaCreateBuffer(
        ToVmaAllocator(allocator),
        &bufferInfo,
        &allocInfo,
        &buffer,
        &vmaAllocation,
        &allocationInfo
    );
    if(res == VK_SUCCESS){
        allocation = ToVulkanAllocationHandle(vmaAllocation);
        mappedMemory = allocationInfo.pMappedData;
    }
    return res;
}

inline void DestroyBufferAllocation(
    const VulkanAllocatorHandle allocator,
    VkBuffer& buffer,
    VulkanAllocationHandle& allocation,
    void*& mappedMemory,
    const bool persistentlyMapped
){
    if(allocation){
        UnmapTransientAllocation(allocator, allocation, mappedMemory, persistentlyMapped);
        vmaDestroyBuffer(ToVmaAllocator(allocator), buffer, ToVmaAllocation(allocation));
        allocation = nullptr;
    }
    else{
        NWB_ASSERT(buffer == VK_NULL_HANDLE);
    }

    buffer = VK_NULL_HANDLE;
    mappedMemory = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanAllocator::VulkanAllocator(const VulkanContext& context)
    : m_context(context)
{}
VulkanAllocator::~VulkanAllocator(){
    if(m_allocator){
        vmaDestroyAllocator(__hidden_vulkan_allocator::ToVmaAllocator(m_allocator));
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
    VmaAllocator allocator = nullptr;
    res = vmaCreateAllocator(&allocatorInfo, &allocator);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create VMA allocator: {}"), ResultToString(res));
        m_allocator = nullptr;
        return false;
    }

    m_allocator = __hidden_vulkan_allocator::ToVulkanAllocatorHandle(allocator);
    return true;
}

VkResult VulkanAllocator::createBuffer(Buffer& buffer, const VkBufferCreateInfo& bufferInfo){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildBufferAllocationInfo(buffer.m_desc, bufferInfo.size);
    const VkResult res = __hidden_vulkan_allocator::CreateBufferAllocation(
        m_allocator,
        bufferInfo,
        allocInfo,
        buffer.m_buffer,
        buffer.m_allocation,
        buffer.m_mappedMemory
    );
    if(res == VK_SUCCESS)
        buffer.m_persistentlyMapped = buffer.m_mappedMemory != nullptr;
    return res;
}

void VulkanAllocator::destroyBuffer(Buffer& buffer){
    __hidden_vulkan_allocator::DestroyBufferAllocation(
        m_allocator,
        buffer.m_buffer,
        buffer.m_allocation,
        buffer.m_mappedMemory,
        buffer.m_persistentlyMapped
    );
    buffer.m_persistentlyMapped = false;
}

VkResult VulkanAllocator::mapBufferMemory(Buffer& buffer, void** outData){
    return __hidden_vulkan_allocator::MapAllocation(m_allocator, buffer.m_allocation, outData);
}

void VulkanAllocator::unmapBufferMemory(Buffer& buffer){
    __hidden_vulkan_allocator::UnmapAllocation(m_allocator, buffer.m_allocation);
}

VkResult VulkanAllocator::invalidateBufferMemory(Buffer& buffer){
    return __hidden_vulkan_allocator::InvalidateAllocation(m_allocator, buffer.m_allocation);
}

VkResult VulkanAllocator::createTexture(Texture& texture, const VkImageCreateInfo& imageInfo){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildTextureAllocationInfo();
    VmaAllocation allocation = nullptr;
    const VkResult res = vmaCreateImage(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        &imageInfo,
        &allocInfo,
        &texture.m_image,
        &allocation,
        nullptr
    );
    if(res == VK_SUCCESS)
        texture.m_allocation = __hidden_vulkan_allocator::ToVulkanAllocationHandle(allocation);
    return res;
}

void VulkanAllocator::destroyTexture(Texture& texture){
    if(texture.m_allocation){
        vmaDestroyImage(
            __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
            texture.m_image,
            __hidden_vulkan_allocator::ToVmaAllocation(texture.m_allocation)
        );
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
    const VkResult res = __hidden_vulkan_allocator::CreateBufferAllocation(
        m_allocator,
        bufferInfo,
        allocInfo,
        texture.m_buffer,
        texture.m_allocation,
        texture.m_mappedMemory
    );
    if(res == VK_SUCCESS)
        texture.m_persistentlyMapped = texture.m_mappedMemory != nullptr;
    return res;
}

void VulkanAllocator::destroyStagingTexture(StagingTexture& texture){
    __hidden_vulkan_allocator::DestroyBufferAllocation(
        m_allocator,
        texture.m_buffer,
        texture.m_allocation,
        texture.m_mappedMemory,
        texture.m_persistentlyMapped
    );
    texture.m_persistentlyMapped = false;
}

VkResult VulkanAllocator::mapStagingTextureMemory(StagingTexture& texture, void** outData){
    return __hidden_vulkan_allocator::MapAllocation(m_allocator, texture.m_allocation, outData);
}

void VulkanAllocator::unmapStagingTextureMemory(StagingTexture& texture){
    __hidden_vulkan_allocator::UnmapAllocation(m_allocator, texture.m_allocation);
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
    VmaAllocation allocation = nullptr;
    const VkResult res = vmaAllocateDedicatedMemory(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        &memRequirements,
        &allocInfo,
        nullptr,
        &allocation,
        &allocationInfo
    );
    if(res == VK_SUCCESS){
        heap.m_allocation = __hidden_vulkan_allocator::ToVulkanAllocationHandle(allocation);
        heap.m_memory = allocationInfo.deviceMemory;
        heap.m_memoryOffset = allocationInfo.offset;
        heap.m_memoryTypeIndex = allocationInfo.memoryType;
    }

    return res;
}

void VulkanAllocator::freeHeap(Heap& heap){
    if(heap.m_allocation){
        vmaFreeMemory(
            __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
            __hidden_vulkan_allocator::ToVmaAllocation(heap.m_allocation)
        );
        heap.m_allocation = nullptr;
    }
    heap.m_memory = VK_NULL_HANDLE;
    heap.m_memoryOffset = 0;
    heap.m_memoryTypeIndex = UINT32_MAX;
}

VkResult VulkanAllocator::bindHeapBufferMemory(Buffer& buffer, Heap& heap, const u64 offset){
    if(!heap.m_allocation)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vmaBindBufferMemory2(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        __hidden_vulkan_allocator::ToVmaAllocation(heap.m_allocation),
        offset,
        buffer.m_buffer,
        nullptr
    );
}

VkResult VulkanAllocator::bindHeapTextureMemory(Texture& texture, Heap& heap, const u64 offset){
    if(!heap.m_allocation)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vmaBindImageMemory2(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        __hidden_vulkan_allocator::ToVmaAllocation(heap.m_allocation),
        offset,
        texture.m_image,
        nullptr
    );
}

VkResult VulkanAllocator::createHostMappedBuffer(
    VkBuffer& buffer,
    VulkanAllocationHandle& allocation,
    void*& mappedMemory,
    const VkBufferCreateInfo& bufferInfo
){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildHostMappedBufferAllocationInfo();
    return __hidden_vulkan_allocator::CreateBufferAllocation(
        m_allocator,
        bufferInfo,
        allocInfo,
        buffer,
        allocation,
        mappedMemory
    );
}

void VulkanAllocator::destroyHostMappedBuffer(VkBuffer& buffer, VulkanAllocationHandle& allocation, void*& mappedMemory){
    __hidden_vulkan_allocator::DestroyBufferAllocation(m_allocator, buffer, allocation, mappedMemory, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

