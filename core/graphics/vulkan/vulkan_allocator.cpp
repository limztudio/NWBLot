// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanAllocator::VulkanAllocator(const VulkanContext& context)
    : m_context(context)
{}

u32 VulkanAllocator::findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties)const{
    for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
        if((typeFilter & (1 << i)) && (m_context.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    return UINT32_MAX;
}

VkResult VulkanAllocator::allocateBufferMemory(Buffer* buffer, bool enableDeviceAddress){
    VkResult res = VK_SUCCESS;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, buffer->m_buffer, &memRequirements);

    VkMemoryPropertyFlags memoryProperties = 0;
    if(buffer->m_desc.cpuAccess == CpuAccessMode::Write)
        memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    else if(buffer->m_desc.cpuAccess == CpuAccessMode::Read)
        memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    else
        memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    u32 memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, memoryProperties);
    if(memoryTypeIndex == UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to find suitable memory type for buffer"));
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    void* pNext = nullptr;

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    if(enableDeviceAddress){
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocFlagsInfo.pNext = pNext;
        pNext = &allocFlagsInfo;
    }

    // Use dedicated allocation for large buffers (improves performance)
    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    if(memRequirements.size >= s_LargeBufferThreshold){
        dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicatedInfo.buffer = buffer->m_buffer;
        dedicatedInfo.image = VK_NULL_HANDLE;
        dedicatedInfo.pNext = pNext;
        pNext = &dedicatedInfo;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    allocInfo.pNext = pNext;

    res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &buffer->m_memory);
    if(res != VK_SUCCESS)
        return res;

    res = vkBindBufferMemory(m_context.device, buffer->m_buffer, buffer->m_memory, 0);
    if(res != VK_SUCCESS){
        vkFreeMemory(m_context.device, buffer->m_memory, m_context.allocationCallbacks);
        buffer->m_memory = VK_NULL_HANDLE;
        return res;
    }

    return VK_SUCCESS;
}

void VulkanAllocator::freeBufferMemory(Buffer* buffer){
    if(buffer->m_memory != VK_NULL_HANDLE){
        if(buffer->m_mappedMemory){
            vkUnmapMemory(m_context.device, buffer->m_memory);
            buffer->m_mappedMemory = nullptr;
        }
        vkFreeMemory(m_context.device, buffer->m_memory, m_context.allocationCallbacks);
        buffer->m_memory = VK_NULL_HANDLE;
    }
}

VkResult VulkanAllocator::allocateTextureMemory(Texture* texture){
    VkResult res = VK_SUCCESS;

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture->m_image, &memRequirements);

    u32 memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(memoryTypeIndex == UINT32_MAX)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    void* pNext = nullptr;

    // Use dedicated allocation for textures (recommended for optimal performance)
    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.image = texture->m_image;
    dedicatedInfo.buffer = VK_NULL_HANDLE;
    dedicatedInfo.pNext = pNext;
    pNext = &dedicatedInfo;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    allocInfo.pNext = pNext;

    res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &texture->m_memory);
    if(res != VK_SUCCESS)
        return res;

    res = vkBindImageMemory(m_context.device, texture->m_image, texture->m_memory, 0);
    if(res != VK_SUCCESS){
        vkFreeMemory(m_context.device, texture->m_memory, m_context.allocationCallbacks);
        texture->m_memory = VK_NULL_HANDLE;
        return res;
    }

    return VK_SUCCESS;
}

void VulkanAllocator::freeTextureMemory(Texture* texture){
    if(texture->m_memory != VK_NULL_HANDLE){
        vkFreeMemory(m_context.device, texture->m_memory, m_context.allocationCallbacks);
        texture->m_memory = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

