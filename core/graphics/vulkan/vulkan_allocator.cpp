// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


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
    return 0;
}

VkResult VulkanAllocator::allocateBufferMemory(Buffer* buffer, bool enableDeviceAddress){
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, buffer->buffer, &memRequirements);
    
    VkMemoryPropertyFlags memoryProperties = 0;
    if(buffer->desc.cpuAccess == CpuAccessMode::Write)
        memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    else if(buffer->desc.cpuAccess == CpuAccessMode::Read)
        memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    else
        memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, memoryProperties);
    
    // Enable device address if needed
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    if(enableDeviceAddress){
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &allocFlagsInfo;
    }
    
    VkResult res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &buffer->memory);
    if(res != VK_SUCCESS)
        return res;
    
    return vkBindBufferMemory(m_context.device, buffer->buffer, buffer->memory, 0);
}

void VulkanAllocator::freeBufferMemory(Buffer* buffer){
    if(buffer->memory != VK_NULL_HANDLE){
        if(buffer->mappedMemory){
            vkUnmapMemory(m_context.device, buffer->memory);
            buffer->mappedMemory = nullptr;
        }
        vkFreeMemory(m_context.device, buffer->memory, m_context.allocationCallbacks);
        buffer->memory = VK_NULL_HANDLE;
    }
}

VkResult VulkanAllocator::allocateTextureMemory(Texture* texture){
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture->image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    VkResult res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &texture->memory);
    if(res != VK_SUCCESS)
        return res;
    
    return vkBindImageMemory(m_context.device, texture->image, texture->memory, 0);
}

void VulkanAllocator::freeTextureMemory(Texture* texture){
    if(texture->memory != VK_NULL_HANDLE){
        vkFreeMemory(m_context.device, texture->memory, m_context.allocationCallbacks);
        texture->memory = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
