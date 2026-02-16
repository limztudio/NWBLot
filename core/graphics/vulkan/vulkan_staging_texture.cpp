// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess){
    auto* staging = new StagingTexture(m_context, m_allocator);
    staging->desc = d;
    staging->cpuAccess = cpuAccess;

    const FormatInfo& formatInfo = GetFormatInfo(d.format);

    u64 totalSize = 0;
    for(u32 arraySlice = 0; arraySlice < d.arraySize; ++arraySlice){
        for(u32 mip = 0; mip < d.mipLevels; ++mip){
            auto mipWidth = Max<u32>(d.width >> mip, 1u);
            auto mipHeight = Max<u32>(d.height >> mip, 1u);
            auto mipDepth = Max<u32>(d.depth >> mip, 1u);

            auto blocksX = Max<u32>(mipWidth / formatInfo.blockSize, 1u);
            auto blocksY = Max<u32>(mipHeight / formatInfo.blockSize, 1u);

            totalSize += static_cast<u64>(blocksX) * blocksY * mipDepth * formatInfo.bytesPerBlock;
        }
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &staging->buffer);
    if(res != VK_SUCCESS){
        delete staging;
        return nullptr;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, staging->buffer, &memRequirements);

    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if(cpuAccess == CpuAccessMode::Read)
        memProps |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    u32 memoryTypeIndex = 0;
    for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
        if((memRequirements.memoryTypeBits & (1 << i)) && (m_context.memoryProperties.memoryTypes[i].propertyFlags & memProps) == memProps){
            memoryTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &staging->memory);
    if(res != VK_SUCCESS){
        vkDestroyBuffer(m_context.device, staging->buffer, m_context.allocationCallbacks);
        staging->buffer = VK_NULL_HANDLE;
        delete staging;
        return nullptr;
    }

    res = vkBindBufferMemory(m_context.device, staging->buffer, staging->memory, 0);
    if(res != VK_SUCCESS){
        vkFreeMemory(m_context.device, staging->memory, m_context.allocationCallbacks);
        vkDestroyBuffer(m_context.device, staging->buffer, m_context.allocationCallbacks);
        staging->memory = VK_NULL_HANDLE;
        staging->buffer = VK_NULL_HANDLE;
        delete staging;
        return nullptr;
    }

    if(cpuAccess != CpuAccessMode::None){
        res = vkMapMemory(m_context.device, staging->memory, 0, totalSize, 0, &staging->mappedMemory);
        if(res != VK_SUCCESS){
            vkFreeMemory(m_context.device, staging->memory, m_context.allocationCallbacks);
            vkDestroyBuffer(m_context.device, staging->buffer, m_context.allocationCallbacks);
            staging->memory = VK_NULL_HANDLE;
            staging->buffer = VK_NULL_HANDLE;
            delete staging;
            return nullptr;
        }
    }

    return StagingTextureHandle(staging, AdoptRef);
}

void* Device::mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum cpuAccess, usize* outRowPitch){
    if(!tex)
        return nullptr;

    auto* staging = static_cast<StagingTexture*>(tex);

    if(!staging->mappedMemory){
        VkResult res = vkMapMemory(m_context.device, staging->memory, 0, VK_WHOLE_SIZE, 0, &staging->mappedMemory);
        if(res != VK_SUCCESS)
            return nullptr;
    }

    const TextureDesc& desc = staging->desc;
    TextureSlice resolved = slice.resolve(desc);
    const FormatInfo& formatInfo = GetFormatInfo(desc.format);

    u64 offset = 0;

    for(u32 arr = 0; arr < desc.arraySize; ++arr){
        for(u32 mip = 0; mip < desc.mipLevels; ++mip){
            if(arr == resolved.arraySlice && mip == resolved.mipLevel)
                goto foundOffset;

            auto mipWidth = Max<u32>(desc.width >> mip, 1u);
            auto mipHeight = Max<u32>(desc.height >> mip, 1u);
            auto mipDepth = Max<u32>(desc.depth >> mip, 1u);

            auto blocksX = Max<u32>(mipWidth / formatInfo.blockSize, 1u);
            auto blocksY = Max<u32>(mipHeight / formatInfo.blockSize, 1u);

            offset += static_cast<u64>(blocksX) * blocksY * mipDepth * formatInfo.bytesPerBlock;
        }
    }
    foundOffset:

    auto mipWidth = Max<u32>(desc.width >> resolved.mipLevel, 1u);
    auto blocksX = Max<u32>(mipWidth / formatInfo.blockSize, 1u);
    u32 rowPitch = blocksX * formatInfo.bytesPerBlock;

    if(outRowPitch)
        *outRowPitch = rowPitch;

    auto mipHeight = Max<u32>(desc.height >> resolved.mipLevel, 1u);
    auto blocksY = Max<u32>(mipHeight / formatInfo.blockSize, 1u);
    auto slicePitch = rowPitch * blocksY;

    offset += static_cast<u64>(resolved.z) * slicePitch
            + static_cast<u64>(resolved.y / formatInfo.blockSize) * rowPitch
            + static_cast<u64>(resolved.x / formatInfo.blockSize) * formatInfo.bytesPerBlock
    ;

    return static_cast<u8*>(staging->mappedMemory) + offset;
}

void Device::unmapStagingTexture(IStagingTexture* tex){
    // Memory is persistently mapped; no-op.
    // Unmapping is handled in StagingTexture destructor.
    (void)tex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

