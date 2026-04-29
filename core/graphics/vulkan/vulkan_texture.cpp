// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkImageType TextureDimensionToImageType(TextureDimension::Enum dimension){
    switch(dimension){
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
        return VK_IMAGE_TYPE_1D;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
        return VK_IMAGE_TYPE_2D;
    case TextureDimension::Texture3D:
        return VK_IMAGE_TYPE_3D;
    default:
        return VK_IMAGE_TYPE_2D;
    }
}

VkImageViewType TextureDimensionToViewType(TextureDimension::Enum dimension){
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

bool IsSupportedSampleCount(u32 sampleCount){
    switch(sampleCount){
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
        return true;
    default:
        return false;
    }
}

VkImageUsageFlags PickImageUsage(const TextureDesc& desc){
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if(desc.isShaderResource)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    if(desc.isRenderTarget){
        const FormatInfo& formatInfo = GetFormatInfo(desc.format);
        if(formatInfo.hasDepth || formatInfo.hasStencil)
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

VkImageCreateFlags PickImageFlags(const TextureDesc& desc){
    VkImageCreateFlags flags = 0;

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray)
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if(desc.dimension == TextureDimension::Texture3D && desc.isRenderTarget)
        flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;

    return flags;
}

u32 GetMaxMipLevels(const TextureDesc& desc){
    const u32 depth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
    u32 maxExtent = Max(Max(desc.width, desc.height), depth);
    u32 levels = 1;
    while(maxExtent > 1){
        maxExtent >>= 1;
        ++levels;
    }
    return levels;
}

bool ValidateTextureShape(const TextureDesc& desc, const tchar* operationName){
    const u32 maxMipLevels = GetMaxMipLevels(desc);
    if(desc.mipLevels > maxMipLevels){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to {}: mip levels {} exceed maximum {} for texture dimensions {}x{}x{}"),
            operationName,
            desc.mipLevels,
            maxMipLevels,
            desc.width,
            desc.height,
            desc.depth
        );
        return false;
    }

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray){
        if(desc.width != desc.height){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube textures must have equal width and height"), operationName);
            return false;
        }
        if(desc.dimension == TextureDimension::TextureCube && desc.arraySize != 6){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube textures must have exactly 6 array layers"), operationName);
            return false;
        }
        if(desc.dimension == TextureDimension::TextureCubeArray && (desc.arraySize < 6 || (desc.arraySize % 6) != 0)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube texture arrays must have a positive multiple of 6 array layers"), operationName);
            return false;
        }
    }

    return true;
}

bool ValidateTextureViewShape(const TextureDesc& desc, const TextureDimension::Enum dimension, const TextureSubresourceSet& subresources){
    static_cast<void>(desc);

    if(dimension == TextureDimension::TextureCube && subresources.numArraySlices != 6){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: cube views must include exactly 6 array layers"));
        return false;
    }
    if(dimension == TextureDimension::TextureCubeArray && (subresources.numArraySlices < 6 || (subresources.numArraySlices % 6) != 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: cube array views must include a positive multiple of 6 array layers"));
        return false;
    }

    return true;
}

VkImageAspectFlags GetImageAspectMask(const FormatInfo& formatInfo){
    VkImageAspectFlags aspectMask = 0;
    if(formatInfo.hasDepth)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if(aspectMask == 0)
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    return aspectMask;
}

bool GetBufferImageCopyAspectMask(const FormatInfo& formatInfo, const tchar* operationName, VkImageAspectFlags& outAspectMask){
    outAspectMask = GetImageAspectMask(formatInfo);
    if((outAspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 && (outAspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: combined depth/stencil formats are not supported by buffer-image copy paths"), operationName);
        return false;
    }

    return true;
}

VkImageSubresourceRange BuildImageSubresourceRange(const TextureSubresourceSet& subresources, const VkImageAspectFlags aspectMask){
    VkImageSubresourceRange range{};
    range.aspectMask = aspectMask;
    range.baseMipLevel = subresources.baseMipLevel;
    range.levelCount = subresources.numMipLevels;
    range.baseArrayLayer = subresources.baseArraySlice;
    range.layerCount = subresources.numArraySlices;
    return range;
}

VkBufferImageCopy BuildStagingTextureCopyRegion(
    const TextureDesc& stagingDesc,
    const TextureSlice& stagingSlice,
    const TextureSlice& imageSlice,
    const VkImageAspectFlags aspectMask
){
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
    const u64 bufferOffset = ComputeStagingTextureOffset(stagingDesc, stagingSlice, nullptr, &bufferRowLength, &bufferImageHeight);

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = bufferRowLength;
    region.bufferImageHeight = bufferImageHeight;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = imageSlice.mipLevel;
    region.imageSubresource.baseArrayLayer = imageSlice.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { static_cast<i32>(imageSlice.x), static_cast<i32>(imageSlice.y), static_cast<i32>(imageSlice.z) };
    region.imageExtent = { imageSlice.width, imageSlice.height, imageSlice.depth };
    return region;
}

bool PrepareColorTextureClear(ITexture* textureResource, const TextureSubresourceSet subresources, const tchar* valueName, Texture*& outTexture, VkImageSubresourceRange& outRange){
    outTexture = checked_cast<Texture*>(textureResource);
    if(!outTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture: texture is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture: texture is null"));
        return false;
    }

    const TextureDesc& textureDesc = outTexture->getDescription();
    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    if(formatInfo.hasDepth || formatInfo.hasStencil){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture with {}: texture format is depth/stencil"), valueName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture with {}: texture format is depth/stencil"), valueName);
        return false;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(textureDesc, false);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        return false;
    }

    outRange = BuildImageSubresourceRange(resolvedSubresources, VK_IMAGE_ASPECT_COLOR_BIT);
    return true;
}

bool PrepareStagingTextureCopy(
    IStagingTexture* stagingResource,
    const TextureSlice& stagingSlice,
    ITexture* textureResource,
    const TextureSlice& textureSlice,
    const tchar* operationName,
    const tchar* singleSampleRequirement,
    StagingTexture*& outStaging,
    Texture*& outTexture,
    VkBufferImageCopy& outRegion
){
    outStaging = checked_cast<StagingTexture*>(stagingResource);
    outTexture = checked_cast<Texture*>(textureResource);
    if(!outStaging || !outTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: resource is invalid"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: resource is invalid"), operationName);
        return false;
    }
    const TextureDesc& stagingDesc = outStaging->getDescription();
    const TextureDesc& textureDesc = outTexture->getDescription();
    if(textureDesc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, singleSampleRequirement);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, singleSampleRequirement);
        return false;
    }
    if(textureDesc.format != stagingDesc.format){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: source and destination formats do not match"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: source and destination formats do not match"), operationName);
        return false;
    }
    VkImageAspectFlags copyAspectMask = 0;
    if(!GetBufferImageCopyAspectMask(GetFormatInfo(stagingDesc.format), operationName, copyAspectMask)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: combined depth/stencil buffer-image copies are not supported"), operationName);
        return false;
    }
    if(!IsTextureSliceInBounds(stagingDesc, stagingSlice) || !IsTextureSliceInBounds(textureDesc, textureSlice)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: slice is outside the texture"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: slice is outside the texture"), operationName);
        return false;
    }

    const TextureSlice resolvedStaging = stagingSlice.resolve(stagingDesc);
    const TextureSlice resolvedTexture = textureSlice.resolve(textureDesc);
    if(resolvedStaging.width != resolvedTexture.width || resolvedStaging.height != resolvedTexture.height || resolvedStaging.depth != resolvedTexture.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: source and destination extents do not match"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: source and destination extents do not match"), operationName);
        return false;
    }

    outRegion = BuildStagingTextureCopyRegion(stagingDesc, resolvedStaging, resolvedTexture, copyAspectMask);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture::Texture(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<ITexture>(context.threadPool)
    , m_views(0, TextureViewKeyHasher(), EqualTo<TextureViewKey>(), Alloc::CustomAllocator<Pair<const TextureViewKey, VkImageView>>(context.objectArena))
    , m_context(context)
    , m_allocator(allocator)
{}
Texture::~Texture(){
    for(const auto& [_, view] : m_views)
        vkDestroyImageView(m_context.device, view, m_context.allocationCallbacks);
    m_views.clear();

    if(m_managed){
        if(m_image != VK_NULL_HANDLE){
            vkDestroyImage(m_context.device, m_image, m_context.allocationCallbacks);
            m_image = VK_NULL_HANDLE;
        }

        m_allocator.freeTextureMemory(this);
    }
}

VkImageView Texture::getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV){
    VkResult res = VK_SUCCESS;

    if(dimension == TextureDimension::Unknown)
        dimension = m_desc.dimension;

    if(format == Format::UNKNOWN)
        format = m_desc.format;

    TextureSubresourceSet resolvedSubresources = subresources.resolve(m_desc, false);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view: invalid subresource range"));
        return VK_NULL_HANDLE;
    }

    TextureViewKey key{
        resolvedSubresources,
        dimension,
        format,
        isReadOnlyDSV
    };

    auto it = m_views.find(key);
    if(it != m_views.end())
        return it.value();

    VkImageViewType viewType = VulkanDetail::TextureDimensionToViewType(dimension);
    VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: format is unsupported"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view: format is unsupported"));
        return VK_NULL_HANDLE;
    }

    if(!VulkanDetail::ValidateTextureViewShape(m_desc, dimension, resolvedSubresources)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view: invalid view shape"));
        return VK_NULL_HANDLE;
    }

    const VkImageAspectFlags aspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(format));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = viewType;
    viewInfo.format = vkFormat;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = resolvedSubresources.baseMipLevel;
    viewInfo.subresourceRange.levelCount = resolvedSubresources.numMipLevels;
    viewInfo.subresourceRange.baseArrayLayer = resolvedSubresources.baseArraySlice;
    viewInfo.subresourceRange.layerCount = resolvedSubresources.numArraySlices;

    VkImageView view = VK_NULL_HANDLE;
    res = vkCreateImageView(m_context.device, &viewInfo, m_context.allocationCallbacks, &view);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: {}"), ResultToString(res));
        return VK_NULL_HANDLE;
    }

    m_views.emplace(key, view);
    return view;
}

Object Texture::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Image)
        return Object(m_image);
    return Object(nullptr);
}

Object Texture::getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool isReadOnlyDSV){
    if(objectType == ObjectTypes::VK_ImageView)
        return getView(subresources, dimension, format, isReadOnlyDSV);
    if(objectType == ObjectTypes::VK_Image)
        return getNativeHandle(objectType);
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTexture::StagingTexture(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<IStagingTexture>(context.threadPool)
    , m_context(context)
    , m_allocator(allocator)
{}
StagingTexture::~StagingTexture(){
    if(m_buffer != VK_NULL_HANDLE){
        vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
        m_buffer = VK_NULL_HANDLE;
    }

    if(m_memory != VK_NULL_HANDLE){
        if(m_mappedMemory){
            vkUnmapMemory(m_context.device, m_memory);
            m_mappedMemory = nullptr;
        }
        vkFreeMemory(m_context.device, m_memory, m_context.allocationCallbacks);
        m_memory = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextureHandle Device::createTexture(const TextureDesc& d){
    VkResult res = VK_SUCCESS;

    if(d.isTiled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create tiled texture: virtual/tiled resources are not supported by this backend"));
        return nullptr;
    }
    if(d.width == 0 || d.height == 0 || d.depth == 0 || d.mipLevels == 0 || d.arraySize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture: dimensions, mip count, and array size must be nonzero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create texture: dimensions, mip count, and array size must be nonzero"));
        return nullptr;
    }
    if(d.dimension == TextureDimension::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture: texture dimension is unknown"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create texture: texture dimension is unknown"));
        return nullptr;
    }
    if(ConvertFormat(d.format) == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture: format is unsupported"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create texture: format is unsupported"));
        return nullptr;
    }
    if(!VulkanDetail::IsSupportedSampleCount(d.sampleCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture: sample count is unsupported"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create texture: sample count is unsupported"));
        return nullptr;
    }
    if((d.dimension == TextureDimension::Texture1D || d.dimension == TextureDimension::Texture1DArray) && (d.height != 1 || d.depth != 1)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create 1D texture: height and depth must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create 1D texture: height and depth must be 1"));
        return nullptr;
    }
    if(d.dimension != TextureDimension::Texture3D && d.depth != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create non-3D texture: depth must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create non-3D texture: depth must be 1"));
        return nullptr;
    }
    if(d.dimension == TextureDimension::Texture3D && d.arraySize != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create 3D texture: array size must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create 3D texture: array size must be 1"));
        return nullptr;
    }
    if(d.dimension == TextureDimension::Texture3D && d.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create 3D texture: sample count must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create 3D texture: sample count must be 1"));
        return nullptr;
    }
    if(d.sampleCount != 1 && d.mipLevels != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create multisampled texture: mip levels must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create multisampled texture: mip levels must be 1"));
        return nullptr;
    }
    if(!VulkanDetail::ValidateTextureShape(d, NWB_TEXT("create texture"))){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create texture: invalid texture shape"));
        return nullptr;
    }

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = d;

    VkImageType imageType = VulkanDetail::TextureDimensionToImageType(d.dimension);
    VkFormat format = ConvertFormat(d.format);
    VkImageUsageFlags usage = VulkanDetail::PickImageUsage(d);
    VkImageCreateFlags flags = VulkanDetail::PickImageFlags(d);
    VkSampleCountFlagBits sampleCount = VulkanDetail::GetSampleCountFlagBits(d.sampleCount);

    texture->m_imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    texture->m_imageInfo.imageType = imageType;
    texture->m_imageInfo.extent.width = d.width;
    texture->m_imageInfo.extent.height = d.height;
    texture->m_imageInfo.extent.depth = d.depth;
    texture->m_imageInfo.mipLevels = d.mipLevels;
    texture->m_imageInfo.arrayLayers = d.arraySize;
    texture->m_imageInfo.format = format;
    texture->m_imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    texture->m_imageInfo.usage = usage;
    texture->m_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    texture->m_imageInfo.samples = sampleCount;
    texture->m_imageInfo.flags = flags;
    texture->m_imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

    res = vkCreateImage(m_context.device, &texture->m_imageInfo, m_context.allocationCallbacks, &texture->m_image);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, texture);
        return nullptr;
    }

    if(!d.isVirtual){
        res = m_allocator.allocateTextureMemory(texture);
        if(res != VK_SUCCESS){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate texture memory"));
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate texture memory: {}"), ResultToString(res));
            DestroyArenaObject(m_context.objectArena, texture);
            return nullptr;
        }
    }

    return TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

MemoryRequirements Device::getTextureMemoryRequirements(ITexture* textureResource){
    if(!textureResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: texture is null"));
        return {};
    }

    auto* texture = static_cast<Texture*>(textureResource);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture->m_image, &memRequirements);

    MemoryRequirements result;
    result.size = memRequirements.size;
    result.alignment = memRequirements.alignment;
    return result;
}

bool Device::bindTextureMemory(ITexture* textureResource, IHeap* heap, u64 offset){
    VkResult res = VK_SUCCESS;

    if(!textureResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture is null"));
        return false;
    }

    auto* texture = static_cast<Texture*>(textureResource);
    if(!texture->m_desc.isVirtual){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture was not created as virtual"));
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture->m_image, &memRequirements);
    Heap* vkHeap = nullptr;
    if(!validateHeapMemoryBinding(heap, memRequirements, offset, NWB_TEXT("bind texture memory"), NWB_TEXT("texture"), vkHeap))
        return false;

    texture->m_memory = VK_NULL_HANDLE;

    res = vkBindImageMemory(m_context.device, texture->m_image, vkHeap->m_memory, offset);
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
    if(desc.width == 0 || desc.height == 0 || desc.depth == 0 || desc.mipLevels == 0 || desc.arraySize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: dimensions, mip count, and array size must be nonzero"));
        return nullptr;
    }
    if(desc.dimension == TextureDimension::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: texture dimension is unknown"));
        return nullptr;
    }
    if(ConvertFormat(desc.format) == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: format is unsupported"));
        return nullptr;
    }
    if(!VulkanDetail::IsSupportedSampleCount(desc.sampleCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: sample count is unsupported"));
        return nullptr;
    }
    if((desc.dimension == TextureDimension::Texture1D || desc.dimension == TextureDimension::Texture1DArray) && (desc.height != 1 || desc.depth != 1)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native 1D texture: height and depth must be 1"));
        return nullptr;
    }
    if(desc.dimension != TextureDimension::Texture3D && desc.depth != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native non-3D texture: depth must be 1"));
        return nullptr;
    }
    if(desc.dimension == TextureDimension::Texture3D && desc.arraySize != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native 3D texture: array size must be 1"));
        return nullptr;
    }
    if(desc.dimension == TextureDimension::Texture3D && desc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native 3D texture: sample count must be 1"));
        return nullptr;
    }
    if(desc.sampleCount != 1 && desc.mipLevels != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native multisampled texture: mip levels must be 1"));
        return nullptr;
    }
    if(!VulkanDetail::ValidateTextureShape(desc, NWB_TEXT("create texture handle for native texture")))
        return nullptr;

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = desc;
    texture->m_image = nativeImage;
    texture->m_managed = false;
    texture->m_keepInitialStateKnown = desc.keepInitialState;

    texture->m_imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    texture->m_imageInfo.imageType = VulkanDetail::TextureDimensionToImageType(desc.dimension);
    texture->m_imageInfo.extent.width = desc.width;
    texture->m_imageInfo.extent.height = desc.height;
    texture->m_imageInfo.extent.depth = desc.depth;
    texture->m_imageInfo.mipLevels = desc.mipLevels;
    texture->m_imageInfo.arrayLayers = desc.arraySize;
    texture->m_imageInfo.format = ConvertFormat(desc.format);
    texture->m_imageInfo.samples = VulkanDetail::GetSampleCountFlagBits(desc.sampleCount);

    return TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SamplerHandle Device::createSampler(const SamplerDesc& d){
    VkResult res = VK_SUCCESS;

    SamplerDesc normalizedDesc = d;
    const f32 maxSupportedAnisotropy = Max(m_context.physicalDeviceProperties.limits.maxSamplerAnisotropy, 1.f);
    if(!(normalizedDesc.maxAnisotropy >= 1.f))
        normalizedDesc.maxAnisotropy = 1.f;
    if(normalizedDesc.maxAnisotropy > maxSupportedAnisotropy)
        normalizedDesc.maxAnisotropy = maxSupportedAnisotropy;

    auto* sampler = NewArenaObject<Sampler>(m_context.objectArena, m_context);
    sampler->m_desc = normalizedDesc;

    const VkSamplerCreateInfo samplerInfo = VulkanDetail::BuildSamplerCreateInfo(normalizedDesc);

    res = vkCreateSampler(m_context.device, &samplerInfo, m_context.allocationCallbacks, &sampler->m_sampler);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create sampler"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, sampler);
        return nullptr;
    }

    return SamplerHandle(sampler, SamplerHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::clearTextureFloat(ITexture* textureResource, TextureSubresourceSet subresources, const Color& clearColor){
    Texture* texture = nullptr;
    VkImageSubresourceRange range{};
    if(!VulkanDetail::PrepareColorTextureClear(textureResource, subresources, NWB_TEXT("color value"), texture, range))
        return;

    VkClearColorValue clearValue;
    clearValue.float32[0] = clearColor.r;
    clearValue.float32[1] = clearColor.g;
    clearValue.float32[2] = clearColor.b;
    clearValue.float32[3] = clearColor.a;

    setTextureState(textureResource, subresources, ResourceStates::CopyDest);
    vkCmdClearColorImage(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    retainResource(textureResource);
}

void CommandList::clearDepthStencilTexture(ITexture* textureResource, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil){
    auto* texture = checked_cast<Texture*>(textureResource);
    if(!clearDepth && !clearStencil)
        return;
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: texture is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: texture is null"));
        return;
    }
    const FormatInfo& formatInfo = GetFormatInfo(texture->m_desc.format);
    if((clearDepth && !formatInfo.hasDepth) || (clearStencil && !formatInfo.hasStencil)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: requested aspect is not present in the texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: requested aspect is not present in the texture format"));
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture->m_desc, false);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: invalid subresource range"));
        return;
    }

    VkClearDepthStencilValue clearValue{};
    clearValue.depth = depth;
    clearValue.stencil = stencil;

    VkImageAspectFlags aspectMask = 0;
    if(clearDepth)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(clearStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    const VkImageSubresourceRange range = VulkanDetail::BuildImageSubresourceRange(resolvedSubresources, aspectMask);

    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
    vkCmdClearDepthStencilImage(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    retainResource(textureResource);
}

void CommandList::clearTextureUInt(ITexture* textureResource, TextureSubresourceSet subresources, u32 clearColor){
    Texture* texture = nullptr;
    VkImageSubresourceRange range{};
    if(!VulkanDetail::PrepareColorTextureClear(textureResource, subresources, NWB_TEXT("integer value"), texture, range))
        return;

    VkClearColorValue clearValue;
    clearValue.uint32[0] = clearColor;
    clearValue.uint32[1] = clearColor;
    clearValue.uint32[2] = clearColor;
    clearValue.uint32[3] = clearColor;

    setTextureState(textureResource, subresources, ResourceStates::CopyDest);
    vkCmdClearColorImage(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    retainResource(textureResource);
}

void CommandList::copyTexture(ITexture* destResource, const TextureSlice& destSlice, ITexture* srcResource, const TextureSlice& srcSlice){
    auto* dest = checked_cast<Texture*>(destResource);
    auto* src = checked_cast<Texture*>(srcResource);
    if(!dest || !src){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture: resource is invalid"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture: resource is invalid"));
        return;
    }
    if(dest->m_desc.sampleCount != src->m_desc.sampleCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture: source and destination sample counts do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture: source and destination sample counts do not match"));
        return;
    }
    if(!VulkanDetail::IsTextureSliceInBounds(dest->m_desc, destSlice) || !VulkanDetail::IsTextureSliceInBounds(src->m_desc, srcSlice)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture: slice is outside the texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture: slice is outside the texture"));
        return;
    }

    const TextureSlice resolvedDst = destSlice.resolve(dest->m_desc);
    const TextureSlice resolvedSrc = srcSlice.resolve(src->m_desc);
    if(resolvedDst.width != resolvedSrc.width || resolvedDst.height != resolvedSrc.height || resolvedDst.depth != resolvedSrc.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture: source and destination extents do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture: source and destination extents do not match"));
        return;
    }

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(src->m_desc.format));
    region.srcSubresource.mipLevel = resolvedSrc.mipLevel;
    region.srcSubresource.baseArrayLayer = resolvedSrc.arraySlice;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = { static_cast<int32_t>(resolvedSrc.x), static_cast<int32_t>(resolvedSrc.y), static_cast<int32_t>(resolvedSrc.z) };
    region.dstSubresource.aspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(dest->m_desc.format));
    region.dstSubresource.mipLevel = resolvedDst.mipLevel;
    region.dstSubresource.baseArrayLayer = resolvedDst.arraySlice;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = { static_cast<int32_t>(resolvedDst.x), static_cast<int32_t>(resolvedDst.y), static_cast<int32_t>(resolvedDst.z) };
    region.extent = { resolvedDst.width, resolvedDst.height, resolvedDst.depth };

    setTextureState(srcResource, TextureSubresourceSet(resolvedSrc.mipLevel, 1u, resolvedSrc.arraySlice, 1u), ResourceStates::CopySource);
    setTextureState(destResource, TextureSubresourceSet(resolvedDst.mipLevel, 1u, resolvedDst.arraySlice, 1u), ResourceStates::CopyDest);

    vkCmdCopyImage(m_currentCmdBuf->m_cmdBuf, src->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    retainResource(srcResource);
    retainResource(destResource);
}

void CommandList::copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice){
    StagingTexture* staging = nullptr;
    Texture* texture = nullptr;
    VkBufferImageCopy region{};
    if(
        !VulkanDetail::PrepareStagingTextureCopy(
            dest,
            destSlice,
            src,
            srcSlice,
            NWB_TEXT("copy texture to staging texture"),
            NWB_TEXT("source texture must be single-sampled"),
            staging,
            texture,
            region
        )
    )
        return;

    setTextureState(src, TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u), ResourceStates::CopySource);

    vkCmdCopyImageToBuffer(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->m_buffer, 1, &region);

    retainResource(src);
    retainResource(dest);
}

void CommandList::copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice){
    StagingTexture* staging = nullptr;
    Texture* texture = nullptr;
    VkBufferImageCopy region{};
    if(
        !VulkanDetail::PrepareStagingTextureCopy(
            src,
            srcSlice,
            dest,
            destSlice,
            NWB_TEXT("copy staging texture to texture"),
            NWB_TEXT("destination texture must be single-sampled"),
            staging,
            texture,
            region
        )
    )
        return;

    setTextureState(dest, TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u), ResourceStates::CopyDest);

    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, staging->m_buffer, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    retainResource(dest);
    retainResource(src);
}

void CommandList::writeTexture(ITexture* destResource, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch){
    auto* dest = checked_cast<Texture*>(destResource);
    if(!dest){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: destination texture is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: destination texture is null"));
        return;
    }
    const TextureDesc& texDesc = dest->m_desc;
    if(texDesc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: destination texture must be single-sampled"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: destination texture must be single-sampled"));
        return;
    }

    if(!data){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: source data is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: source data is null"));
        return;
    }

    if(mipLevel >= texDesc.mipLevels || arraySlice >= texDesc.arraySize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: subresource is out of bounds"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: subresource is out of bounds"));
        return;
    }

    auto width = Max<u32>(1u, texDesc.width >> mipLevel);
    auto height = Max<u32>(1u, texDesc.height >> mipLevel);
    auto depth = Max<u32>(1u, texDesc.depth >> mipLevel);

    const FormatInfo& formatInfo = GetFormatInfo(texDesc.format);
    VkImageAspectFlags copyAspectMask = 0;
    if(!VulkanDetail::GetBufferImageCopyAspectMask(formatInfo, NWB_TEXT("write texture"), copyAspectMask)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: combined depth/stencil buffer-image copies are not supported"));
        return;
    }
    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);
    if(formatBlockWidth == 0 || formatBlockHeight == 0 || formatInfo.bytesPerBlock == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: invalid texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: invalid texture format"));
        return;
    }

    const u64 blockCountX = DivideUp(static_cast<u64>(width), static_cast<u64>(formatBlockWidth));
    const u64 blockCountY = DivideUp(static_cast<u64>(height), static_cast<u64>(formatBlockHeight));
    if(blockCountX > Limit<u64>::s_Max / formatInfo.bytesPerBlock){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: natural row pitch overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: natural row pitch overflows"));
        return;
    }

    const u64 naturalRowPitch = blockCountX * formatInfo.bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0 ? static_cast<u64>(rowPitch) : naturalRowPitch;
    if(effectiveRowPitch == 0 || blockCountY > UINT64_MAX / effectiveRowPitch){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: texture pitch size overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: texture pitch size overflows"));
        return;
    }
    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0 ? static_cast<u64>(depthPitch) : packedSlicePitch;

    if(effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % formatInfo.bytesPerBlock) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: invalid row pitch"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: invalid row pitch"));
        return;
    }

    if(effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: invalid depth pitch"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: invalid depth pitch"));
        return;
    }

    const u64 bufferRowBlocks = effectiveRowPitch / formatInfo.bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(bufferRowBlocks > UINT64_MAX / formatBlockWidth || bufferImageBlocks > UINT64_MAX / formatBlockHeight){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: row pitch or depth pitch exceeds Vulkan buffer image copy limits"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: row pitch or depth pitch exceeds Vulkan buffer image copy limits"));
        return;
    }
    const u64 bufferRowLength = bufferRowBlocks * formatBlockWidth;
    const u64 bufferImageHeight = bufferImageBlocks * formatBlockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: row pitch or depth pitch exceeds Vulkan buffer image copy limits"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: row pitch or depth pitch exceeds Vulkan buffer image copy limits"));
        return;
    }

    if(depth > 1 && static_cast<u64>(depth - 1) > (UINT64_MAX - packedSlicePitch) / effectiveDepthPitch){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: upload size overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: upload size overflows"));
        return;
    }
    const u64 dataSize = depth > 1 ? static_cast<u64>(effectiveDepthPitch) * (depth - 1) + packedSlicePitch : packedSlicePitch;
    if(dataSize > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: upload size exceeds addressable memory"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: upload size exceeds addressable memory"));
        return;
    }

    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    const usize uploadSize = static_cast<usize>(dataSize);
    if(!prepareUploadStaging(data, uploadSize, NWB_TEXT("writeTexture"), stagingBuffer, stagingOffset))
        return;

    setTextureState(destResource, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), ResourceStates::CopyDest);

    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;
    region.bufferRowLength = static_cast<u32>(bufferRowLength);
    region.bufferImageHeight = static_cast<u32>(bufferImageHeight);
    region.imageSubresource.aspectMask = copyAspectMask;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, depth };

    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, dest->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    retainResource(destResource);
    retainStagingBuffer(stagingBuffer);
}

void CommandList::resolveTexture(ITexture* destResource, const TextureSubresourceSet& dstSubresources, ITexture* srcResource, const TextureSubresourceSet& srcSubresources){
    auto* dest = checked_cast<Texture*>(destResource);
    auto* src = checked_cast<Texture*>(srcResource);
    if(!dest || !src){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: resource is invalid"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: resource is invalid"));
        return;
    }
    if(src->m_desc.sampleCount <= 1 || dest->m_desc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source must be multisampled and destination must be single-sampled"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: invalid sample counts"));
        return;
    }
    if(ConvertFormat(src->m_desc.format) != ConvertFormat(dest->m_desc.format)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination formats do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination formats do not match"));
        return;
    }
    const FormatInfo& formatInfo = GetFormatInfo(src->m_desc.format);
    if(formatInfo.hasDepth || formatInfo.hasStencil){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: depth/stencil resolves are not supported by this path"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: depth/stencil resolves are not supported by this path"));
        return;
    }

    const TextureSubresourceSet resolvedSrc = srcSubresources.resolve(src->m_desc, false);
    const TextureSubresourceSet resolvedDst = dstSubresources.resolve(dest->m_desc, false);
    if(resolvedSrc.numMipLevels == 0 || resolvedSrc.numArraySlices == 0 || resolvedDst.numMipLevels == 0 || resolvedDst.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: invalid subresource range"));
        return;
    }
    if(resolvedSrc.numMipLevels != resolvedDst.numMipLevels || resolvedSrc.numArraySlices != resolvedDst.numArraySlices){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination subresources do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination subresources do not match"));
        return;
    }

    Alloc::ScratchArena<> scratchArena;
    Vector<VkImageResolve, Alloc::ScratchAllocator<VkImageResolve>> regions{ Alloc::ScratchAllocator<VkImageResolve>(scratchArena) };
    regions.reserve(resolvedSrc.numMipLevels);

    for(MipLevel mipOffset = 0; mipOffset < resolvedSrc.numMipLevels; ++mipOffset){
        const MipLevel srcMipLevel = resolvedSrc.baseMipLevel + mipOffset;
        const MipLevel dstMipLevel = resolvedDst.baseMipLevel + mipOffset;
        const u32 srcWidth = Max<u32>(1u, src->m_desc.width >> srcMipLevel);
        const u32 srcHeight = Max<u32>(1u, src->m_desc.height >> srcMipLevel);
        const u32 srcDepth = Max<u32>(1u, src->m_desc.depth >> srcMipLevel);
        const u32 dstWidth = Max<u32>(1u, dest->m_desc.width >> dstMipLevel);
        const u32 dstHeight = Max<u32>(1u, dest->m_desc.height >> dstMipLevel);
        const u32 dstDepth = Max<u32>(1u, dest->m_desc.depth >> dstMipLevel);
        if(srcWidth != dstWidth || srcHeight != dstHeight || srcDepth != dstDepth){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination mip extents do not match"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination mip extents do not match"));
            return;
        }

        VkImageResolve region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = srcMipLevel;
        region.srcSubresource.baseArrayLayer = resolvedSrc.baseArraySlice;
        region.srcSubresource.layerCount = resolvedSrc.numArraySlices;
        region.srcOffset = { 0, 0, 0 };
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = dstMipLevel;
        region.dstSubresource.baseArrayLayer = resolvedDst.baseArraySlice;
        region.dstSubresource.layerCount = resolvedDst.numArraySlices;
        region.dstOffset = { 0, 0, 0 };
        region.extent = { srcWidth, srcHeight, srcDepth };
        regions.push_back(region);
    }

    if(!regions.empty()){
        vkCmdResolveImage(
            m_currentCmdBuf->m_cmdBuf,
            src->m_image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dest->m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<u32>(regions.size()),
            regions.data()
        );
    }

    retainResource(srcResource);
    retainResource(destResource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

