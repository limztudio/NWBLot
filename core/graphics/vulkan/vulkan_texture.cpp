// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


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

VkSampleCountFlagBits GetSampleCount(u32 sampleCount){
    switch(sampleCount){
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture::Texture(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<ITexture>(context.threadPool)
    , m_views(0, Hasher<u64>(), EqualTo<u64>(), Alloc::CustomAllocator<Pair<const u64, VkImageView>>(context.objectArena))
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

u64 Texture::makeViewKey(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV)const{
    u64 key = 0;
    key |= static_cast<u64>(subresources.baseMipLevel) << s_TextureViewKeyBaseMipShift;
    key |= static_cast<u64>(subresources.numMipLevels) << s_TextureViewKeyMipCountShift;
    key |= static_cast<u64>(subresources.baseArraySlice) << s_TextureViewKeyBaseArraySliceShift;
    key |= static_cast<u64>(subresources.numArraySlices) << s_TextureViewKeyArraySliceCountShift;
    key |= static_cast<u64>(dimension) << s_TextureViewKeyDimensionShift;
    key |= static_cast<u64>(format) << s_TextureViewKeyFormatShift;
    key |= static_cast<u64>(isReadOnlyDSV ? 1 : 0) << s_TextureViewKeyReadOnlyDsvShift;
    return key;
}

VkImageView Texture::getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV){
    VkResult res = VK_SUCCESS;

    if(dimension == TextureDimension::Unknown)
        dimension = m_desc.dimension;

    if(format == Format::UNKNOWN)
        format = m_desc.format;

    TextureSubresourceSet resolvedSubresources = subresources.resolve(m_desc, false);
    u64 key = makeViewKey(resolvedSubresources, dimension, format, isReadOnlyDSV);

    auto it = m_views.find(key);
    if(it != m_views.end())
        return it.value();

    VkImageViewType viewType = __hidden_vulkan::TextureDimensionToViewType(dimension);
    VkFormat vkFormat = ConvertFormat(format);

    const FormatInfo& formatInfo = GetFormatInfo(format);
    VkImageAspectFlags aspectMask = 0;
    if(formatInfo.hasDepth)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if(!formatInfo.hasDepth && !formatInfo.hasStencil)
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

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

    m_views[key] = view;
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

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = d;

    VkImageType imageType = __hidden_vulkan::TextureDimensionToImageType(d.dimension);
    VkFormat format = ConvertFormat(d.format);
    VkImageUsageFlags usage = __hidden_vulkan::PickImageUsage(d);
    VkImageCreateFlags flags = __hidden_vulkan::PickImageFlags(d);
    VkSampleCountFlagBits sampleCount = __hidden_vulkan::GetSampleCount(d.sampleCount);

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

MemoryRequirements Device::getTextureMemoryRequirements(ITexture* _texture){
    if(!_texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture memory requirements: texture is null"));
        return {};
    }

    auto* texture = static_cast<Texture*>(_texture);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture->m_image, &memRequirements);

    MemoryRequirements result;
    result.size = memRequirements.size;
    result.alignment = memRequirements.alignment;
    return result;
}

bool Device::bindTextureMemory(ITexture* _texture, IHeap* heap, u64 offset){
    VkResult res = VK_SUCCESS;

    if(!_texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: texture is null"));
        return false;
    }

    auto* texture = static_cast<Texture*>(_texture);
    auto* vkHeap = checked_cast<Heap*>(heap);

    if(!vkHeap || vkHeap->m_memory == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: heap is invalid"));
        return false;
    }

    texture->m_memory = VK_NULL_HANDLE;

    res = vkBindImageMemory(m_context.device, texture->m_image, vkHeap->m_memory, offset);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind texture memory: {}"), ResultToString(res));
        return false;
    }

    return true;
}

TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object _texture, const TextureDesc& desc){
    if(objectType != ObjectTypes::VK_Image){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: object type is not VK_Image"));
        return nullptr;
    }

    auto* nativeImage = static_cast<VkImage>(static_cast<VkImage_T*>(_texture));
    if(nativeImage == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create texture handle for native texture: image handle is null"));
        return nullptr;
    }

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = desc;
    texture->m_image = nativeImage;
    texture->m_managed = false;

    texture->m_imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    texture->m_imageInfo.imageType = __hidden_vulkan::TextureDimensionToImageType(desc.dimension);
    texture->m_imageInfo.extent.width = desc.width;
    texture->m_imageInfo.extent.height = desc.height;
    texture->m_imageInfo.extent.depth = desc.depth;
    texture->m_imageInfo.mipLevels = desc.mipLevels;
    texture->m_imageInfo.arrayLayers = desc.arraySize;
    texture->m_imageInfo.format = ConvertFormat(desc.format);
    texture->m_imageInfo.samples = __hidden_vulkan::GetSampleCount(desc.sampleCount);

    return TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SamplerHandle Device::createSampler(const SamplerDesc& d){
    VkResult res = VK_SUCCESS;

    auto* sampler = NewArenaObject<Sampler>(m_context.objectArena, m_context);
    sampler->m_desc = d;

    VkFilter minFilter = d.minFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    VkFilter magFilter = d.magFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    VkSamplerMipmapMode mipFilter = d.mipFilter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;

    auto convertAddressMode = [](SamplerAddressMode::Enum mode) -> VkSamplerAddressMode {
        switch(mode){
            case SamplerAddressMode::Clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case SamplerAddressMode::Wrap: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case SamplerAddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case SamplerAddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case SamplerAddressMode::MirrorOnce: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
            default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
    };

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = magFilter;
    samplerInfo.minFilter = minFilter;
    samplerInfo.mipmapMode = mipFilter;
    samplerInfo.addressModeU = convertAddressMode(d.addressU);
    samplerInfo.addressModeV = convertAddressMode(d.addressV);
    samplerInfo.addressModeW = convertAddressMode(d.addressW);
    samplerInfo.mipLodBias = d.mipBias;
    samplerInfo.anisotropyEnable = d.maxAnisotropy > 1.f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = d.maxAnisotropy;
    samplerInfo.compareEnable = d.reductionType == SamplerReductionType::Comparison ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

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


void CommandList::clearTextureFloat(ITexture* _texture, TextureSubresourceSet subresources, const Color& clearColor){
    auto* texture = checked_cast<Texture*>(_texture);
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture->m_desc, false);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        return;
    }

    VkClearColorValue clearValue;
    clearValue.float32[0] = clearColor.r;
    clearValue.float32[1] = clearColor.g;
    clearValue.float32[2] = clearColor.b;
    clearValue.float32[3] = clearColor.a;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = resolvedSubresources.baseMipLevel;
    range.levelCount = resolvedSubresources.numMipLevels;
    range.baseArrayLayer = resolvedSubresources.baseArraySlice;
    range.layerCount = resolvedSubresources.numArraySlices;

    vkCmdClearColorImage(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    m_currentCmdBuf->m_referencedResources.push_back(_texture);
}

void CommandList::clearDepthStencilTexture(ITexture* _texture, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil){
    auto* texture = checked_cast<Texture*>(_texture);
    if(!clearDepth && !clearStencil)
        return;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture->m_desc, false);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: invalid subresource range"));
        return;
    }

    VkClearDepthStencilValue clearValue{};
    clearValue.depth = depth;
    clearValue.stencil = stencil;

    VkImageSubresourceRange range{};
    range.aspectMask = 0;
    if(clearDepth)
        range.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(clearStencil)
        range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    range.baseMipLevel = resolvedSubresources.baseMipLevel;
    range.levelCount = resolvedSubresources.numMipLevels;
    range.baseArrayLayer = resolvedSubresources.baseArraySlice;
    range.layerCount = resolvedSubresources.numArraySlices;

    vkCmdClearDepthStencilImage(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    m_currentCmdBuf->m_referencedResources.push_back(_texture);
}

void CommandList::clearTextureUInt(ITexture* _texture, TextureSubresourceSet subresources, u32 clearColor){
    auto* texture = checked_cast<Texture*>(_texture);
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture->m_desc, false);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        return;
    }

    VkClearColorValue clearValue;
    clearValue.uint32[0] = clearColor;
    clearValue.uint32[1] = clearColor;
    clearValue.uint32[2] = clearColor;
    clearValue.uint32[3] = clearColor;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = resolvedSubresources.baseMipLevel;
    range.levelCount = resolvedSubresources.numMipLevels;
    range.baseArrayLayer = resolvedSubresources.baseArraySlice;
    range.layerCount = resolvedSubresources.numArraySlices;

    vkCmdClearColorImage(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    m_currentCmdBuf->m_referencedResources.push_back(_texture);
}

void CommandList::copyTexture(ITexture* _dest, const TextureSlice& destSlice, ITexture* _src, const TextureSlice& srcSlice){
    auto* dest = checked_cast<Texture*>(_dest);
    auto* src = checked_cast<Texture*>(_src);
    const TextureSlice resolvedDst = destSlice.resolve(dest->m_desc);
    const TextureSlice resolvedSrc = srcSlice.resolve(src->m_desc);

    VkImageCopy region{};
    const FormatInfo& srcFormatInfo = GetFormatInfo(src->m_desc.format);
    VkImageAspectFlags srcAspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if(srcFormatInfo.hasDepth)
        srcAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(srcFormatInfo.hasStencil)
        srcAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

    const FormatInfo& dstFormatInfo = GetFormatInfo(dest->m_desc.format);
    VkImageAspectFlags dstAspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if(dstFormatInfo.hasDepth)
        dstAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(dstFormatInfo.hasStencil)
        dstAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

    region.srcSubresource.aspectMask = srcAspect;
    region.srcSubresource.mipLevel = resolvedSrc.mipLevel;
    region.srcSubresource.baseArrayLayer = resolvedSrc.arraySlice;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = { static_cast<int32_t>(resolvedSrc.x), static_cast<int32_t>(resolvedSrc.y), static_cast<int32_t>(resolvedSrc.z) };
    region.dstSubresource.aspectMask = dstAspect;
    region.dstSubresource.mipLevel = resolvedDst.mipLevel;
    region.dstSubresource.baseArrayLayer = resolvedDst.arraySlice;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = { static_cast<int32_t>(resolvedDst.x), static_cast<int32_t>(resolvedDst.y), static_cast<int32_t>(resolvedDst.z) };
    region.extent = { resolvedDst.width, resolvedDst.height, resolvedDst.depth };

    vkCmdCopyImage(m_currentCmdBuf->m_cmdBuf, src->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    m_currentCmdBuf->m_referencedResources.push_back(_src);
    m_currentCmdBuf->m_referencedResources.push_back(_dest);
}

void CommandList::copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice){
    auto* staging = checked_cast<StagingTexture*>(dest);
    auto* texture = checked_cast<Texture*>(src);

    TextureSlice resolvedSrc = srcSlice.resolve(texture->m_desc);
    TextureSlice resolvedDst = destSlice.resolve(staging->m_desc);

    const FormatInfo& formatInfo = GetFormatInfo(texture->m_desc.format);
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
    const u64 bufferOffset = __hidden_vulkan::ComputeStagingTextureOffset(staging->m_desc, resolvedDst, nullptr, &bufferRowLength, &bufferImageHeight);

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if(formatInfo.hasDepth)
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = bufferRowLength;
    region.bufferImageHeight = bufferImageHeight;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = resolvedSrc.mipLevel;
    region.imageSubresource.baseArrayLayer = resolvedSrc.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { static_cast<i32>(resolvedSrc.x), static_cast<i32>(resolvedSrc.y), static_cast<i32>(resolvedSrc.z) };
    region.imageExtent = { resolvedSrc.width, resolvedSrc.height, resolvedSrc.depth };

    vkCmdCopyImageToBuffer(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->m_buffer, 1, &region);

    m_currentCmdBuf->m_referencedResources.push_back(src);
    m_currentCmdBuf->m_referencedResources.push_back(dest);
}

void CommandList::copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice){
    auto* texture = checked_cast<Texture*>(dest);
    auto* staging = checked_cast<StagingTexture*>(src);

    TextureSlice resolvedDst = destSlice.resolve(texture->m_desc);
    TextureSlice resolvedSrc = srcSlice.resolve(staging->m_desc);

    const FormatInfo& formatInfo = GetFormatInfo(staging->m_desc.format);
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
    const u64 bufferOffset = __hidden_vulkan::ComputeStagingTextureOffset(staging->m_desc, resolvedSrc, nullptr, &bufferRowLength, &bufferImageHeight);

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if(formatInfo.hasDepth)
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = bufferRowLength;
    region.bufferImageHeight = bufferImageHeight;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = resolvedDst.mipLevel;
    region.imageSubresource.baseArrayLayer = resolvedDst.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { static_cast<i32>(resolvedDst.x), static_cast<i32>(resolvedDst.y), static_cast<i32>(resolvedDst.z) };
    region.imageExtent = { resolvedDst.width, resolvedDst.height, resolvedDst.depth };

    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, staging->m_buffer, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    m_currentCmdBuf->m_referencedResources.push_back(dest);
    m_currentCmdBuf->m_referencedResources.push_back(src);
}

void CommandList::writeTexture(ITexture* _dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch){
    auto* dest = checked_cast<Texture*>(_dest);
    const TextureDesc& texDesc = dest->m_desc;

    auto width = Max<u32>(1u, texDesc.width >> mipLevel);
    auto height = Max<u32>(1u, texDesc.height >> mipLevel);
    auto depth = Max<u32>(1u, texDesc.depth >> mipLevel);

    const FormatInfo& formatInfo = GetFormatInfo(texDesc.format);
    if(formatInfo.blockSize == 0 || formatInfo.bytesPerBlock == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: invalid texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: invalid texture format"));
        return;
    }

    const u32 blockWidth = (width + formatInfo.blockSize - 1) / formatInfo.blockSize;
    const u32 blockHeight = (height + formatInfo.blockSize - 1) / formatInfo.blockSize;
    const usize naturalRowPitch = static_cast<usize>(blockWidth) * formatInfo.bytesPerBlock;
    const usize effectiveRowPitch = rowPitch != 0 ? rowPitch : naturalRowPitch;
    const usize packedSlicePitch = effectiveRowPitch * blockHeight;
    const usize effectiveDepthPitch = depthPitch != 0 ? depthPitch : packedSlicePitch;

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

    const u64 dataSize = depth > 1
        ? static_cast<u64>(effectiveDepthPitch) * (depth - 1) + packedSlicePitch
        : packedSlicePitch;

    UploadManager* uploadMgr = m_device.m_uploadManager.get();
    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    void* cpuVA = nullptr;

    if(!uploadMgr->suballocateBuffer(dataSize, &stagingBuffer, &stagingOffset, &cpuVA, 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to suballocate staging buffer for writeTexture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to suballocate staging buffer for writeTexture"));
        return;
    }

    const usize uploadSize = static_cast<usize>(dataSize);
    __hidden_vulkan::CopyHostMemory(taskPool(), cpuVA, data, uploadSize);

    VkImageMemoryBarrier2 barrier = __hidden_vulkan::MakeVkStruct<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = dest->m_image;
    VkImageAspectFlags writeAspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if(formatInfo.hasDepth)
        writeAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        writeAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

    barrier.subresourceRange.aspectMask = writeAspect;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = arraySlice;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo depInfo = __hidden_vulkan::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &depInfo);

    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;
    region.bufferRowLength = static_cast<u32>((effectiveRowPitch / formatInfo.bytesPerBlock) * formatInfo.blockSize);
    region.bufferImageHeight = static_cast<u32>((effectiveDepthPitch / effectiveRowPitch) * formatInfo.blockSize);
    region.imageSubresource.aspectMask = writeAspect;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, depth };

    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, dest->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    m_currentCmdBuf->m_referencedResources.push_back(_dest);
    m_currentCmdBuf->m_referencedStagingBuffers.push_back(stagingBuffer);
}

void CommandList::resolveTexture(ITexture* _dest, const TextureSubresourceSet& dstSubresources, ITexture* _src, const TextureSubresourceSet& srcSubresources){
    auto* dest = checked_cast<Texture*>(_dest);
    auto* src = checked_cast<Texture*>(_src);

    const TextureSubresourceSet resolvedSrc = srcSubresources.resolve(src->m_desc, false);
    const TextureSubresourceSet resolvedDst = dstSubresources.resolve(dest->m_desc, false);
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
        region.extent = {
            Max<u32>(1u, src->m_desc.width >> srcMipLevel),
            Max<u32>(1u, src->m_desc.height >> srcMipLevel),
            Max<u32>(1u, src->m_desc.depth >> srcMipLevel)
        };
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
            regions.data());
    }

    m_currentCmdBuf->m_referencedResources.push_back(_src);
    m_currentCmdBuf->m_referencedResources.push_back(_dest);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
