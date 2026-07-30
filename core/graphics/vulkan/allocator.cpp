// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#define VMA_IMPLEMENTATION
#include "vma.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_allocator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline VmaAllocationCreateInfo BuildDeviceLocalAllocationInfo(){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    return allocInfo;
}

inline VmaAllocationCreateInfo BuildMappedHostAllocationInfo(
    const VkMemoryPropertyFlags requiredFlags,
    const VkMemoryPropertyFlags preferredFlags,
    const VmaAllocationCreateFlags accessFlags
){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | requiredFlags;
    allocInfo.preferredFlags = preferredFlags;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | accessFlags;
    return allocInfo;
}

inline VmaAllocationCreateInfo BuildCpuAccessAllocationInfo(const CpuAccessMode::Enum cpuAccess){
    if(cpuAccess == CpuAccessMode::Read){
        return BuildMappedHostAllocationInfo(
            0,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        );
    }

    return BuildMappedHostAllocationInfo(
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        0,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
}

inline VmaAllocationCreateInfo BuildBufferAllocationInfo(const BufferDesc& desc, const VkDeviceSize bufferSize){
    VmaAllocationCreateInfo allocInfo{};
    if(desc.isVolatile || desc.cpuAccess == CpuAccessMode::Write)
        allocInfo = BuildCpuAccessAllocationInfo(CpuAccessMode::Write);
    else if(desc.cpuAccess == CpuAccessMode::Read)
        allocInfo = BuildCpuAccessAllocationInfo(CpuAccessMode::Read);
    else
        allocInfo = BuildDeviceLocalAllocationInfo();

    if(bufferSize >= s_LargeBufferThreshold)
        allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    return allocInfo;
}

inline VmaAllocationCreateInfo BuildHostMappedBufferAllocationInfo(){
    return BuildMappedHostAllocationInfo(
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        0,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    );
}

inline bool BuildRequiresInvalidate(const VkPhysicalDeviceMemoryProperties& memoryProperties, const u32 memoryTypeIndex){
    if(memoryTypeIndex >= memoryProperties.memoryTypeCount)
        return true;

    const VkMemoryPropertyFlags propertyFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    return (propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 && (propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0;
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

inline VkResult InvalidateAllocation(
    const VulkanAllocatorHandle allocator,
    const VulkanAllocationHandle allocation,
    const u64 offset,
    const u64 size
){
    if(!allocation)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return vmaInvalidateAllocation(ToVmaAllocator(allocator), ToVmaAllocation(allocation), offset, size);
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
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    const VkBufferCreateInfo& bufferInfo,
    const VmaAllocationCreateInfo& allocInfo,
    VkBuffer& buffer,
    VulkanAllocationHandle& allocation,
    void*& mappedMemory,
    bool* outRequiresInvalidate
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
        if(outRequiresInvalidate)
            *outRequiresInvalidate = BuildRequiresInvalidate(memoryProperties, allocationInfo.memoryType);
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
        m_context.memoryProperties,
        bufferInfo,
        allocInfo,
        buffer.m_buffer,
        buffer.m_allocation,
        buffer.m_mappedMemory,
        &buffer.m_requiresInvalidate
    );
    if(res == VK_SUCCESS){
        buffer.m_persistentlyMapped = buffer.m_mappedMemory != nullptr;
#if defined(NWB_DEBUG)
        if(buffer.m_desc.debugName)
            vmaSetAllocationName(__hidden_vulkan_allocator::ToVmaAllocator(m_allocator), __hidden_vulkan_allocator::ToVmaAllocation(buffer.m_allocation), buffer.m_desc.debugName.logText());
#endif
    }
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
    buffer.m_requiresInvalidate = false;
}

VkResult VulkanAllocator::mapBufferMemory(Buffer& buffer, void** outData){
    return __hidden_vulkan_allocator::MapAllocation(m_allocator, buffer.m_allocation, outData);
}

void VulkanAllocator::unmapBufferMemory(Buffer& buffer){
    __hidden_vulkan_allocator::UnmapAllocation(m_allocator, buffer.m_allocation);
}

VkResult VulkanAllocator::invalidateBufferMemory(Buffer& buffer){
    if(!buffer.m_requiresInvalidate)
        return VK_SUCCESS;
    return __hidden_vulkan_allocator::InvalidateAllocation(m_allocator, buffer.m_allocation, 0, VK_WHOLE_SIZE);
}

VkResult VulkanAllocator::createTexture(Texture& texture, const VkImageCreateInfo& imageInfo){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildDeviceLocalAllocationInfo();
    VmaAllocation allocation = nullptr;
    const VkResult res = vmaCreateImage(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        &imageInfo,
        &allocInfo,
        &texture.m_image,
        &allocation,
        nullptr
    );
    if(res == VK_SUCCESS){
        texture.m_allocation = __hidden_vulkan_allocator::ToVulkanAllocationHandle(allocation);
#if defined(NWB_DEBUG)
        if(texture.m_desc.name)
            vmaSetAllocationName(__hidden_vulkan_allocator::ToVmaAllocator(m_allocator), allocation, texture.m_desc.name.logText());
#endif
    }
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
        m_context.memoryProperties,
        bufferInfo,
        allocInfo,
        buffer,
        allocation,
        mappedMemory,
        nullptr
    );
}

void VulkanAllocator::destroyHostMappedBuffer(VkBuffer& buffer, VulkanAllocationHandle& allocation, void*& mappedMemory){
    __hidden_vulkan_allocator::DestroyBufferAllocation(m_allocator, buffer, allocation, mappedMemory, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

