// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "texture_resource_detail.h"
#include "native_sharing_validation.h"

#include <core/graphics/rhi/queue_sharing.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkImageUsageFlags RequiredImageUsageForResourceStates(const ResourceStates::Mask states)noexcept{
    VkImageUsageFlags usage = 0u;
    if(states & ResourceStates::ShaderResource)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if(states & ResourceStates::UnorderedAccess)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if(states & ResourceStates::RenderTarget)
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(states & (ResourceStates::DepthRead | ResourceStates::DepthWrite))
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(states & (ResourceStates::CopySource | ResourceStates::ResolveSource))
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if(states & (ResourceStates::CopyDest | ResourceStates::ResolveDest))
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(states & ResourceStates::ShadingRateSurface)
        usage |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    return usage;
}

u32 GetMaxMipLevels(const TextureDesc& desc)noexcept{
    const u32 depth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
    u32 maxExtent = Max(Max(desc.width, desc.height), depth);
    u32 levels = 1;
    while(maxExtent > 1){
        maxExtent >>= 1;
        ++levels;
    }
    return levels;
}

b8 IsTextureDescShapeValid(const TextureDesc& desc)noexcept{
    if(desc.width == 0u || desc.height == 0u || desc.depth == 0u || desc.mipLevels == 0u || desc.arraySize == 0u)
        return false;

    b8 dimensionValid = false;
    switch(desc.dimension){
    case TextureDimension::Texture1D:
        dimensionValid = desc.height == 1u && desc.depth == 1u && desc.arraySize == 1u;
        break;
    case TextureDimension::Texture1DArray:
        dimensionValid = desc.height == 1u && desc.depth == 1u;
        break;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DMS:
        dimensionValid = desc.depth == 1u && desc.arraySize == 1u;
        break;
    case TextureDimension::Texture2DArray:
    case TextureDimension::Texture2DMSArray:
        dimensionValid = desc.depth == 1u;
        break;
    case TextureDimension::TextureCube:
        dimensionValid = desc.depth == 1u && desc.width == desc.height && desc.arraySize == s_TextureCubeLayerCount;
        break;
    case TextureDimension::TextureCubeArray:
        dimensionValid = desc.depth == 1u && desc.width == desc.height && desc.arraySize >= s_TextureCubeLayerCount && (desc.arraySize % s_TextureCubeLayerCount) == 0u;
        break;
    case TextureDimension::Texture3D:
        dimensionValid = desc.arraySize == 1u;
        break;
    default:
        return false;
    }

    return dimensionValid && desc.mipLevels <= GetMaxMipLevels(desc);
}

VkImageViewType TextureDimensionToViewType(TextureDimension::Enum dimension)noexcept{
    switch(dimension){
    case TextureDimension::Texture1D: return VK_IMAGE_VIEW_TYPE_1D;
    case TextureDimension::Texture1DArray: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case TextureDimension::Texture2D: return VK_IMAGE_VIEW_TYPE_2D;
    case TextureDimension::Texture2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::TextureCube: return VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureDimension::TextureCubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case TextureDimension::Texture2DMS: return VK_IMAGE_VIEW_TYPE_2D;
    case TextureDimension::Texture2DMSArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
    default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}

VkImageUsageFlags PickImageUsage(const TextureDesc& desc, const VkImageAspectFlags aspectMask)noexcept{
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if(desc.isShaderResource)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    if(desc.isRenderTarget){
        if((aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0)
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if(desc.isUAV)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;

    if(desc.isShadingRateSurface)
        usage |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    return usage;
}

VkImageCreateFlags PickImageFlags(const TextureDesc& desc)noexcept{
    VkImageCreateFlags flags = 0;

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray)
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if(desc.dimension == TextureDimension::Texture3D && desc.isRenderTarget)
        flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;

    return flags;
}

VkImageCreateInfo BuildTextureImageCreateInfo(const TextureDesc& desc, const TextureCreateMetadata& metadata)noexcept{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = metadata.imageType;
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arraySize;
    imageInfo.format = metadata.format;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = metadata.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = metadata.sampleCount;
    imageInfo.flags = metadata.flags;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    return imageInfo;
}

b8 IsTextureImageInfoConsistent(const TextureDesc& desc, const VkImageCreateInfo& imageInfo, const VkImageUsageFlags requiredUsage)noexcept{
    const auto expectedImageType = TryTextureDimensionToImageType(desc.dimension);
    if(!expectedImageType || !VulkanDetail::IsSupportedSampleCount(desc.sampleCount) || desc.sampleQuality != 0u)
        return false;

    const VkFormat expectedFormat = VulkanDetail::ConvertFormat(desc.format);
    if(expectedFormat == VK_FORMAT_UNDEFINED)
        return false;
    const VkImageAspectFlags expectedAspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(desc.format));
    const VkImageUsageFlags descriptionUsage = (PickImageUsage(desc, expectedAspectMask) & ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) | RequiredImageUsageForResourceStates(desc.initialState);
    const VkImageCreateFlags requiredFlags = PickImageFlags(desc);
    const b8 sharingInfoConsistent =
        (imageInfo.sharingMode == VK_SHARING_MODE_EXCLUSIVE && imageInfo.queueFamilyIndexCount == 0u && imageInfo.pQueueFamilyIndices == nullptr)
        || (imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT && imageInfo.queueFamilyIndexCount >= 2u && imageInfo.pQueueFamilyIndices != nullptr)
    ;
    return
        imageInfo.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
        && imageInfo.imageType == *expectedImageType
        && imageInfo.format == expectedFormat
        && imageInfo.extent.width == desc.width
        && imageInfo.extent.height == desc.height
        && imageInfo.extent.depth == desc.depth
        && imageInfo.mipLevels == desc.mipLevels
        && imageInfo.arrayLayers == desc.arraySize
        && imageInfo.samples == VulkanDetail::GetSampleCountFlagBits(desc.sampleCount)
        && imageInfo.tiling == VK_IMAGE_TILING_OPTIMAL
        && (imageInfo.usage & (descriptionUsage | requiredUsage)) == (descriptionUsage | requiredUsage)
        && (imageInfo.flags & requiredFlags) == requiredFlags
        && (imageInfo.flags & (VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT | VK_IMAGE_CREATE_PROTECTED_BIT)) == 0u
        && sharingInfoConsistent
        && imageInfo.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED
    ;
}

b8 TextureFormatMetadataMatches(const Format::Enum format, const VkImageAspectFlags aspectMask, const VulkanDetail::TextureFormatBlockLayout& formatLayout)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(format);
    const auto expectedFormatLayout = VulkanDetail::GetTextureFormatBlockLayout(formatInfo);
    return
        expectedFormatLayout
        && aspectMask == VulkanDetail::GetImageAspectMask(formatInfo)
        && formatLayout.blockWidth == expectedFormatLayout->blockWidth
        && formatLayout.blockHeight == expectedFormatLayout->blockHeight
        && formatLayout.bytesPerBlock == expectedFormatLayout->bytesPerBlock
    ;
}

b8 ValidateTextureViewShape(const TextureDimension::Enum dimension, const TextureSubresourceSet& subresources){
    if(dimension == TextureDimension::TextureCube && subresources.numArraySlices != s_TextureCubeLayerCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: cube views must include exactly 6 array layers"));
        return false;
    }
    if(dimension == TextureDimension::TextureCubeArray && (subresources.numArraySlices < s_TextureCubeLayerCount || (subresources.numArraySlices % s_TextureCubeLayerCount) != 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: cube array views must include a positive multiple of 6 array layers"));
        return false;
    }

    return true;
}

Unexpected<Failure> ReportTextureCreateDescError(TStringView operationName, TStringView message, const b8 assertFailure){
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    if(assertFailure)
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    return MakeUnexpected(Failure{});
}

Expected<TextureCreateMetadata> ValidateTextureCreateDesc(const TextureDesc& desc, TStringView operationName, const b8 assertFailure){
    TextureCreateMetadata metadata{};
    if(!ResourceQueueSharing::IsValid(desc.queueSharing))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("queue sharing contains unknown bits"), assertFailure);
    if(!IsTextureCreationStateMaskValid(desc.initialState))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("initial state is invalid for a texture"), assertFailure);
    if(!VulkanDetail::ValidateTextureShape(desc, operationName)){
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: invalid texture shape"), operationName);
        return MakeUnexpected(Failure{});
    }

    metadata.format = VulkanDetail::ConvertFormat(desc.format);
    if(metadata.format == VK_FORMAT_UNDEFINED)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("format is unsupported"), assertFailure);

    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    const auto formatLayout = VulkanDetail::GetTextureFormatBlockLayout(formatInfo);
    if(!formatLayout)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("invalid texture format"), assertFailure);
    metadata.formatLayout = *formatLayout;

    metadata.aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    if(!VulkanDetail::IsSupportedSampleCount(desc.sampleCount))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("sample count is unsupported"), assertFailure);
    if(desc.sampleQuality != 0u)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("sample quality must be zero"), assertFailure);
    const auto imageType = TryTextureDimensionToImageType(desc.dimension);
    if(!imageType)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("texture dimension is unsupported"), assertFailure);
    metadata.imageType = *imageType;
    if(desc.sampleCount != 1u && (metadata.imageType != VK_IMAGE_TYPE_2D || (PickImageFlags(desc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("multisampled textures must be 2D and not cube-compatible"), assertFailure);
    if(desc.sampleCount != 1 && desc.mipLevels != 1)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("multisampled texture mip levels must be 1"), assertFailure);

    metadata.usage = PickImageUsage(desc, metadata.aspectMask);
    const VkImageUsageFlags requiredInitialUsage = RequiredImageUsageForResourceStates(desc.initialState);
    if((metadata.usage & requiredInitialUsage) != requiredInitialUsage){
        return ReportTextureCreateDescError(operationName, NWB_TEXT("initial state requires an undeclared image usage"), assertFailure);
    }
    metadata.flags = PickImageFlags(desc);
    metadata.sampleCount = VulkanDetail::GetSampleCountFlagBits(desc.sampleCount);
    return metadata;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureHandle> Device::createTexture(const TextureDesc& d){
    const auto metadata = VulkanTextureDetail::ValidateTextureCreateDesc(d, NWB_TEXT("create texture"), true);
    if(!metadata)
        return MakeUnexpected(Failure{});

    VkImageCreateInfo imageInfo = VulkanTextureDetail::BuildTextureImageCreateInfo(d, *metadata);
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(d.queueSharing, m_context);
    imageInfo.sharingMode = sharingInfo.mode;
    imageInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    imageInfo.pQueueFamilyIndices = sharingInfo.data();

    TextureHandle texture(NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator, d, imageInfo, false), TextureHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!texture)
        return MakeUnexpected(Failure{});
    texture->m_formatLayout = metadata->formatLayout;
    texture->m_aspectMask = metadata->aspectMask;

    VkResult res;
    if(d.isVirtual)
        res = m_context.deviceDispatch.vkCreateImage(m_context.device, &imageInfo, m_context.allocationCallbacks, &texture->m_image);
    else
        res = m_allocator.createTexture(*texture, imageInfo);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }
    if(!m_allocator.tryRegisterTextureNativeIdentity(*texture)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture: native image identity is already registered"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: A newly created texture duplicated a live native image identity"));
        return MakeUnexpected(Failure{});
    }

    return texture;
}

Expected<MemoryRequirements> Device::getTextureMemoryRequirements(Texture& texture){
    if(&texture.m_context != &m_context || &texture.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: texture belongs to another device"));
        return MakeUnexpected(Failure{});
    }
    if(!texture.m_managed || !texture.m_creationDesc.isVirtual || texture.m_allocation != nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: texture is not a managed virtual texture"));
        return MakeUnexpected(Failure{});
    }
    if(texture.m_image == VK_NULL_HANDLE || texture.m_imageInfo.tiling != VK_IMAGE_TILING_OPTIMAL){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: native optimal image is invalid"));
        return MakeUnexpected(Failure{});
    }

    VkImageMemoryRequirementsInfo2 requirementsInfo{};
    requirementsInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    requirementsInfo.image = texture.m_image;
    VkMemoryRequirements2 memoryRequirements{};
    memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    m_context.deviceDispatch.vkGetImageMemoryRequirements2(m_context.device, &requirementsInfo, &memoryRequirements);

    MemoryRequirements result;
    result.size = memoryRequirements.memoryRequirements.size;
    result.alignment = memoryRequirements.memoryRequirements.alignment;
    return result;
}

b8 Device::bindTextureMemory(Texture& texture, Heap& heap, u64 offset){
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

    Heap& memoryHeap = heap;
    if(&memoryHeap.m_context != &m_context || &memoryHeap.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: heap belongs to another device"));
        return false;
    }
    if(memoryHeap.m_allocation == nullptr || memoryHeap.m_memory == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: heap is invalid"));
        return false;
    }
    HeapHandle retainedHeap(&heap, HeapHandle::deleter_type(&memoryHeap.m_context.objectArena));
    NothrowScopedLock resourceLock(texture.m_memoryBindingMutex);
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
    m_context.deviceDispatch.vkGetImageMemoryRequirements2(m_context.device, &requirementsInfo, &memoryRequirements);

    ScopedLock heapLock(memoryHeap.m_bindingMutex);
    const auto bindingRange = validateHeapMemoryBinding(
        memoryHeap,
        memoryRequirements.memoryRequirements,
        dedicatedRequirements,
        offset,
        VulkanDetail::HeapBindingResourceClass::OptimalImage,
        NWB_TEXT("bind texture memory"),
        VulkanArenaScope::s_TextureResourceLabel
    );
    if(!bindingRange)
        return false;

    const Heap::BindingReservation reservation{
        .owner = MakeNotNull<const void*>(&texture),
        .range = *bindingRange,
        .resourceClass = VulkanDetail::HeapBindingResourceClass::OptimalImage,
    };
    memoryHeap.m_bindingReservations.push_back(reservation);

    const VkResult res = m_allocator.bindHeapTextureMemory(texture, memoryHeap, offset);
    if(res != VK_SUCCESS){
        memoryHeap.m_bindingReservations.pop_back();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: {}"), ResultToString(res));
        return false;
    }
    texture.m_heapBindingRange = *bindingRange;
    texture.m_boundHeap = Move(retainedHeap);

    return true;
}

b8 Device::isTextureReadyForGpuUse(Texture* textureResource, const VkImageUsageFlags requiredUsage)const noexcept{
    if(!textureResource)
        return false;

    Texture& texture = *textureResource;
    ScopedLock resourceLock(texture.m_memoryBindingMutex);

    if(&texture.m_context != &m_context || &texture.m_allocator != &m_allocator)
        return false;
    NWB_ASSERT(texture.descriptionMatchesCreation());
    NWB_ASSERT(VulkanTextureDetail::IsTextureImageInfoConsistent(texture.m_creationDesc, texture.m_imageInfo));
    if((texture.m_imageInfo.usage & requiredUsage) != requiredUsage)
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
    return &heap.m_context == &m_context && &heap.m_allocator == &m_allocator && heap.m_allocation != nullptr && heap.m_memory != VK_NULL_HANDLE;
}

Expected<TextureHandle> Device::createHandleForNativeTexture(const ObjectType objectType, const Object nativeTextureHandle, const TextureDesc& desc, const NativeTextureProvenance& nativeProvenance){
    if(objectType != ObjectTypes::s_Image){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: object type is not VK_Image"));
        return MakeUnexpected(Failure{});
    }

    auto* nativeImage = static_cast<VkImage>(static_cast<VkImage_T*>(nativeTextureHandle));
    if(nativeImage == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: image handle is null"));
        return MakeUnexpected(Failure{});
    }
    const auto metadata = VulkanTextureDetail::ValidateTextureCreateDesc(desc, NWB_TEXT("create texture handle for native texture"), false);
    if(!metadata)
        return MakeUnexpected(Failure{});
    if(nativeProvenance.usage == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: native usage is zero"));
        return MakeUnexpected(Failure{});
    }
    if(!VulkanNativeSharingDetail::ValidateNativeResourceSharing(*this, m_context.instanceDispatch, m_context.physicalDevice, desc.queueSharing, nativeProvenance, "texture handle for native texture"))
        return MakeUnexpected(Failure{});

    VkImageCreateInfo imageInfo = VulkanTextureDetail::BuildTextureImageCreateInfo(desc, *metadata);
    imageInfo.usage = nativeProvenance.usage;
    imageInfo.flags = nativeProvenance.flags;
    imageInfo.sharingMode = nativeProvenance.sharingMode;
    imageInfo.queueFamilyIndexCount = nativeProvenance.queueFamilyIndexCount;
    imageInfo.pQueueFamilyIndices = nativeProvenance.queueFamilyIndices;
    if(!VulkanTextureDetail::IsTextureImageInfoConsistent(desc, imageInfo)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: native provenance contradicts the logical description or declared initial state"));
        return MakeUnexpected(Failure{});
    }

    TextureHandle texture(NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator, desc, imageInfo, nativeProvenance.initialStateKnown), TextureHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!texture)
        return MakeUnexpected(Failure{});
    texture->m_formatLayout = metadata->formatLayout;
    texture->m_aspectMask = metadata->aspectMask;
    texture->m_image = nativeImage;
    texture->m_managed = false;

    if(!m_allocator.tryRegisterTextureNativeIdentity(*texture)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: a live wrapper already exists"));
        return MakeUnexpected(Failure{});
    }

    return texture;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

