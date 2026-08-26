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

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator, d);
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
    if(!m_allocator.tryRegisterTextureNativeIdentity(*texture)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture: native image identity is already registered"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: A newly created texture duplicated a live native image identity"));
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
    if(!textureResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: texture is null"));
        return {};
    }

    Texture& texture = *textureResource;
    if(&texture.m_context != &m_context || &texture.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: texture belongs to another device"));
        return {};
    }
    if(!texture.m_managed || !texture.m_creationDesc.isVirtual || texture.m_allocation != nullptr){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to get texture memory requirements: texture is not a managed virtual texture")
        );
        return {};
    }
    if(texture.m_image == VK_NULL_HANDLE || texture.m_imageInfo.tiling != VK_IMAGE_TILING_OPTIMAL){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: native optimal image is invalid"));
        return {};
    }

    VkImageMemoryRequirementsInfo2 requirementsInfo{};
    requirementsInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    requirementsInfo.image = texture.m_image;
    VkMemoryRequirements2 memoryRequirements{};
    memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    vkGetImageMemoryRequirements2(m_context.device, &requirementsInfo, &memoryRequirements);

    MemoryRequirements result;
    result.size = memoryRequirements.memoryRequirements.size;
    result.alignment = memoryRequirements.memoryRequirements.alignment;
    return result;
}

bool Device::bindTextureMemory(Texture* textureResource, Heap* heap, u64 offset){
    if(!textureResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture is null"));
        return false;
    }
    if(!heap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: heap is null"));
        return false;
    }
    Texture& texture = *textureResource;
    if(&texture.m_context != &m_context || &texture.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture belongs to another device"));
        return false;
    }
    if(!texture.m_managed || !texture.m_creationDesc.isVirtual || texture.m_allocation != nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture is not a managed virtual texture"));
        return false;
    }
    if(texture.m_image == VK_NULL_HANDLE || texture.m_imageInfo.tiling != VK_IMAGE_TILING_OPTIMAL){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: native optimal image is invalid"));
        return false;
    }

    Heap& memoryHeap = *heap;
    if(&memoryHeap.m_context != &m_context || &memoryHeap.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: heap belongs to another device"));
        return false;
    }
    if(memoryHeap.m_allocation == nullptr || memoryHeap.m_memory == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: heap is invalid"));
        return false;
    }
    HeapHandle retainedHeap(heap, HeapHandle::deleter_type(&memoryHeap.m_context.objectArena));
    ScopedLock resourceLock(texture.m_memoryBindingMutex);
    if(texture.m_boundHeap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture memory was already bound"));
        return false;
    }

    VkMemoryDedicatedRequirements dedicatedRequirements{};
    dedicatedRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 memoryRequirements{};
    memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memoryRequirements.pNext = &dedicatedRequirements;
    VkImageMemoryRequirementsInfo2 requirementsInfo{};
    requirementsInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    requirementsInfo.image = texture.m_image;
    vkGetImageMemoryRequirements2(m_context.device, &requirementsInfo, &memoryRequirements);

    ScopedLock heapLock(memoryHeap.m_bindingMutex);
    VulkanDetail::HeapBindingRange bindingRange;
    if(!validateHeapMemoryBinding(
        memoryHeap,
        memoryRequirements.memoryRequirements,
        dedicatedRequirements,
        offset,
        VulkanDetail::HeapBindingResourceClass::OptimalImage,
        NWB_TEXT("bind texture memory"),
        NWB_TEXT("texture"),
        bindingRange
    ))
        return false;

    Heap::BindingReservation reservation;
    reservation.owner = &texture;
    reservation.resourceClass = VulkanDetail::HeapBindingResourceClass::OptimalImage;
    reservation.range = bindingRange;
    memoryHeap.m_bindingReservations.push_back(reservation);

    const VkResult res = m_allocator.bindHeapTextureMemory(texture, memoryHeap, offset);
    if(res != VK_SUCCESS){
        memoryHeap.m_bindingReservations.pop_back();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: {}"), ResultToString(res));
        return false;
    }
    texture.m_heapBindingRange = bindingRange;
    texture.m_boundHeap = Move(retainedHeap);

    return true;
}

bool Device::isTextureReadyForGpuUse(Texture* textureResource, const VkImageUsageFlags requiredUsage)const noexcept{
    if(!textureResource)
        return false;

    Texture& texture = *textureResource;
    ScopedLock resourceLock(texture.m_memoryBindingMutex);

    if(&texture.m_context != &m_context || &texture.m_allocator != &m_allocator)
        return false;
    if(!texture.descriptionMatchesCreation())
        return false;
    if(!VulkanTextureDetail::IsTextureImageInfoConsistent(texture.m_creationDesc, texture.m_imageInfo, requiredUsage))
        return false;
    if(texture.m_image == VK_NULL_HANDLE)
        return false;
    if(!m_allocator.isTextureNativeIdentityRegistered(texture.m_image, texture))
        return false;
    if(!texture.m_managed)
        return true;

    if(!texture.m_creationDesc.isVirtual)
        return texture.m_allocation != nullptr;
    if(texture.m_allocation || !texture.m_boundHeap || texture.m_heapBindingRange.size == 0u)
        return false;

    Heap& heap = *texture.m_boundHeap.get();
    ScopedLock heapLock(heap.m_bindingMutex);
    return &heap.m_context == &m_context
        && &heap.m_allocator == &m_allocator
        && heap.m_allocation != nullptr
        && heap.m_memory != VK_NULL_HANDLE
    ;
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

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator, desc);
    texture->m_formatLayout = metadata.formatLayout;
    texture->m_aspectMask = metadata.aspectMask;
    texture->m_image = nativeImage;
    texture->m_managed = false;
    texture->initializeRetainedSubresourceStates(desc.keepInitialState);

    texture->m_imageInfo = VulkanTextureDetail::BuildTextureImageCreateInfo(desc, metadata);
    texture->m_imageInfo.usage &= ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if(!VulkanTextureDetail::IsTextureImageInfoConsistent(desc, texture->m_imageInfo)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create texture handle for native texture: declared initial state is unsupported")
        );
        DestroyArenaObject(m_context.objectArena, texture);
        return nullptr;
    }

    if(!m_allocator.tryRegisterTextureNativeIdentity(*texture)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to create texture handle for native texture: a live wrapper already exists")
        );
        DestroyArenaObject(m_context.objectArena, texture);
        return nullptr;
    }

    return TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_FINAL)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::revokeUnmanagedNativeTextureForTesting(
    Texture* textureResource,
    const Object expectedNativeImageHandle
)noexcept{
    if(!textureResource)
        return false;

    Texture& texture = *textureResource;
    if(&texture.m_context != &m_context || &texture.m_allocator != &m_allocator)
        return false;

    auto* expectedNativeImage = static_cast<VkImage_T*>(expectedNativeImageHandle);
    return texture.revokeUnmanagedNativeImage(expectedNativeImage);
}

void Device::releaseRevokedNativeTextureIdentityForTesting(
    Texture* textureResource,
    const Object expectedNativeImageHandle
)noexcept{
    if(!textureResource)
        return;

    Texture& texture = *textureResource;
    if(&texture.m_context != &m_context || &texture.m_allocator != &m_allocator)
        return;

    auto* expectedNativeImage = static_cast<VkImage_T*>(expectedNativeImageHandle);
    texture.releaseRevokedNativeImageIdentity(expectedNativeImage);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


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

