// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextureHandle Device::createTexture(const TextureDesc& d){
    VulkanTextureDetail::TextureCreateMetadata metadata;
    if(!VulkanTextureDetail::ValidateTextureCreateDesc(d, NWB_TEXT("create texture"), true, metadata))
        return nullptr;

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = d;
    texture->m_formatLayout = metadata.formatLayout;
    texture->m_aspectMask = metadata.aspectMask;
    texture->initializeRetainedSubresourceStates(false);

    texture->m_imageInfo = VulkanTextureDetail::BuildTextureImageCreateInfo(d, metadata);
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(d.queueSharing, m_context);
    texture->m_imageInfo.sharingMode = sharingInfo.mode;
    texture->m_imageInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    texture->m_imageInfo.pQueueFamilyIndices = sharingInfo.data();

    VkResult res;
    if(d.isVirtual)
        res = vkCreateImage(m_context.device, &texture->m_imageInfo, m_context.allocationCallbacks, &texture->m_image);
    else
        res = m_allocator.createTexture(*texture, texture->m_imageInfo);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, texture);
        return nullptr;
    }

    // Vulkan consumed the queue-family pointer during creation. Clear the transient pointer so the retained
    // image metadata cannot dangle into this stack frame.
    texture->m_imageInfo.queueFamilyIndexCount = 0u;
    texture->m_imageInfo.pQueueFamilyIndices = nullptr;

    return TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

MemoryRequirements Device::getTextureMemoryRequirements(Texture* textureResource){
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("get texture memory requirements"), NWB_TEXT("texture is null"), textureResource))
        return {};

    Texture& texture = *textureResource;

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture.m_image, &memRequirements);

    MemoryRequirements result;
    result.size = memRequirements.size;
    result.alignment = memRequirements.alignment;
    return result;
}

bool Device::bindTextureMemory(Texture* textureResource, Heap* heap, u64 offset){
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("bind texture memory"), NWB_TEXT("texture is null"), textureResource))
        return false;

    Texture& texture = *textureResource;
#if defined(NWB_DEBUG)
    if(!texture.m_desc.isVirtual){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture was not created as virtual"));
        return false;
    }
#endif

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture.m_image, &memRequirements);
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("bind texture memory"), NWB_TEXT("heap is invalid"), heap))
        return false;
#if defined(NWB_DEBUG)
    Heap& memoryHeap = *heap;
    if(!validateHeapMemoryBinding(memoryHeap, memRequirements, offset, NWB_TEXT("bind texture memory"), NWB_TEXT("texture")))
        return false;
#else
    Heap& memoryHeap = *heap;
#endif

    const VkResult res = m_allocator.bindHeapTextureMemory(texture, memoryHeap, offset);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: {}"), ResultToString(res));
        return false;
    }

    return true;
}

TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object nativeTextureHandle, const TextureDesc& desc){
    if(objectType != ObjectTypes::VK_Image){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: object type is not VK_Image"));
        return nullptr;
    }

    auto* nativeImage = static_cast<VkImage>(static_cast<VkImage_T*>(nativeTextureHandle));
    if(nativeImage == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: image handle is null"));
        return nullptr;
    }
    VulkanTextureDetail::TextureCreateMetadata metadata;
    if(!VulkanTextureDetail::ValidateTextureCreateDesc(desc, NWB_TEXT("create texture handle for native texture"), false, metadata))
        return nullptr;

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = desc;
    texture->m_formatLayout = metadata.formatLayout;
    texture->m_aspectMask = metadata.aspectMask;
    texture->m_image = nativeImage;
    texture->m_managed = false;
    texture->initializeRetainedSubresourceStates(desc.keepInitialState);

    texture->m_imageInfo = VulkanTextureDetail::BuildTextureImageCreateInfo(desc, metadata);

    return TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SamplerHandle Device::createSampler(const SamplerDesc& d){
    SamplerDesc normalizedDesc = d;
    const f32 maxSupportedAnisotropy = Max(m_context.physicalDeviceProperties.limits.maxSamplerAnisotropy, 1.f);
    if(!(normalizedDesc.maxAnisotropy >= 1.f))
        normalizedDesc.maxAnisotropy = 1.f;
    if(normalizedDesc.maxAnisotropy > maxSupportedAnisotropy)
        normalizedDesc.maxAnisotropy = maxSupportedAnisotropy;

    auto* sampler = NewArenaObject<Sampler>(m_context.objectArena, m_context);
    sampler->m_desc = normalizedDesc;

    const VkSamplerCreateInfo samplerInfo = VulkanDetail::BuildSamplerCreateInfo(normalizedDesc);

    const VkResult res = vkCreateSampler(m_context.device, &samplerInfo, m_context.allocationCallbacks, &sampler->m_sampler);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create sampler"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, sampler);
        return nullptr;
    }

    return SamplerHandle(sampler, SamplerHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

