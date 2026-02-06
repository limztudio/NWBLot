// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


using __hidden::checked_cast;
using __hidden::convertFormat;
using namespace __hidden_vulkan;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions for format conversion


namespace __hidden_vulkan{


VkImageType textureDimensionToImageType(TextureDimension::Enum dimension){
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

VkImageViewType textureDimensionToViewType(TextureDimension::Enum dimension){
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

VkSampleCountFlagBits getSampleCount(u32 sampleCount){
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

VkImageUsageFlags pickImageUsage(const TextureDesc& desc){
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

VkImageCreateFlags pickImageFlags(const TextureDesc& desc){
    VkImageCreateFlags flags = 0;
    
    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray)
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    
    if(desc.dimension == TextureDimension::Texture3D && desc.isRenderTarget)
        flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    
    return flags;
}


} // namespace __hidden_vulkan


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


Texture::Texture(const VulkanContext& context, VulkanAllocator& allocator)
    : m_context(context)
    , m_allocator(allocator)
{}

Texture::~Texture(){
    // Destroy all cached views
    for(auto& pair : views)
        vkDestroyImageView(m_context.device, pair.second, m_context.allocationCallbacks);
    views.clear();
    
    if(image != VK_NULL_HANDLE){
        vkDestroyImage(m_context.device, image, m_context.allocationCallbacks);
        image = VK_NULL_HANDLE;
    }
    
    m_allocator.freeTextureMemory(this);
}

u64 Texture::makeViewKey(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV)const{
    u64 key = 0;
    key |= static_cast<u64>(subresources.baseMipLevel) << 0;
    key |= static_cast<u64>(subresources.numMipLevels) << 8;
    key |= static_cast<u64>(subresources.baseArraySlice) << 16;
    key |= static_cast<u64>(subresources.numArraySlices) << 24;
    key |= static_cast<u64>(dimension) << 32;
    key |= static_cast<u64>(format) << 40;
    key |= static_cast<u64>(isReadOnlyDSV ? 1 : 0) << 48;
    return key;
}

VkImageView Texture::getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV){
    u64 key = makeViewKey(subresources, dimension, format, isReadOnlyDSV);
    
    auto it = views.find(key);
    if(it != views.end())
        return it->second;
    
    // Resolve dimension
    if(dimension == TextureDimension::Unknown)
        dimension = desc.dimension;
    
    // Resolve format
    if(format == Format::UNKNOWN)
        format = desc.format;
    
    // Resolve subresources
    TextureSubresourceSet resolvedSubresources = subresources.resolve(desc, false);
    
    VkImageViewType viewType = textureDimensionToViewType(dimension);
    VkFormat vkFormat = convertFormat(format);
    
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
    viewInfo.image = image;
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
    VkResult res = vkCreateImageView(m_context.device, &viewInfo, m_context.allocationCallbacks, &view);
    assert(res == VK_SUCCESS);
    
    views[key] = view;
    return view;
}

Object Texture::getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool isReadOnlyDSV){
    if(objectType == ObjectTypes::NWB_VK_Device)
        return getView(subresources, dimension, format, isReadOnlyDSV);
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging Texture


StagingTexture::StagingTexture(const VulkanContext& context, VulkanAllocator& allocator)
    : m_context(context)
    , m_allocator(allocator)
{}

StagingTexture::~StagingTexture(){
    if(buffer != VK_NULL_HANDLE){
        vkDestroyBuffer(m_context.device, buffer, m_context.allocationCallbacks);
        buffer = VK_NULL_HANDLE;
    }
    
    if(memory != VK_NULL_HANDLE){
        if(mappedMemory){
            vkUnmapMemory(m_context.device, memory);
            mappedMemory = nullptr;
        }
        vkFreeMemory(m_context.device, memory, m_context.allocationCallbacks);
        memory = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device - Texture Implementation


TextureHandle Device::createTexture(const TextureDesc& d){
    Texture* texture = new Texture(m_context, m_allocator);
    texture->desc = d;
    
    VkImageType imageType = textureDimensionToImageType(d.dimension);
    VkFormat format = convertFormat(d.format);
    VkImageUsageFlags usage = pickImageUsage(d);
    VkImageCreateFlags flags = pickImageFlags(d);
    VkSampleCountFlagBits sampleCount = getSampleCount(d.sampleCount);
    
    texture->imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    texture->imageInfo.imageType = imageType;
    texture->imageInfo.extent.width = d.width;
    texture->imageInfo.extent.height = d.height;
    texture->imageInfo.extent.depth = d.depth;
    texture->imageInfo.mipLevels = d.mipLevels;
    texture->imageInfo.arrayLayers = d.arraySize;
    texture->imageInfo.format = format;
    texture->imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    texture->imageInfo.usage = usage;
    texture->imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    texture->imageInfo.samples = sampleCount;
    texture->imageInfo.flags = flags;
    texture->imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    
    VkResult res = vkCreateImage(m_context.device, &texture->imageInfo, m_context.allocationCallbacks, &texture->image);
    assert(res == VK_SUCCESS);
    
    // Allocate memory if not virtual
    if(!d.isVirtual){
        VkResult res = m_allocator.allocateTextureMemory(texture);
        assert(res == VK_SUCCESS);
    }
    
    return RefCountPtr<ITexture, BlankDeleter<ITexture>>(texture, AdoptRef);
}

MemoryRequirements Device::getTextureMemoryRequirements(ITexture* _texture){
    Texture* texture = static_cast<Texture*>(_texture);
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.device, texture->image, &memRequirements);
    
    MemoryRequirements result;
    result.size = memRequirements.size;
    result.alignment = memRequirements.alignment;
    return result;
}

bool Device::bindTextureMemory(ITexture* _texture, IHeap* heap, u64 offset){
    // TODO: Implement heap binding
    return false;
}

TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc){
    // TODO: Implement native texture wrapping
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device - Sampler Implementation


SamplerHandle Device::createSampler(const SamplerDesc& d){
    Sampler* sampler = new Sampler(m_context);
    sampler->desc = d;
    
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
    
    VkResult res = vkCreateSampler(m_context.device, &samplerInfo, m_context.allocationCallbacks, &sampler->sampler);
    assert(res == VK_SUCCESS);
    
    return RefCountPtr<ISampler, BlankDeleter<ISampler>>(sampler, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommandList - Texture operations


void CommandList::clearTextureFloat(ITexture* _texture, TextureSubresourceSet subresources, const Color& clearColor){
    Texture* texture = checked_cast<Texture*>(_texture);
    
    VkClearColorValue clearValue;
    clearValue.float32[0] = clearColor.r;
    clearValue.float32[1] = clearColor.g;
    clearValue.float32[2] = clearColor.b;
    clearValue.float32[3] = clearColor.a;
    
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = subresources.baseMipLevel;
    range.levelCount = subresources.numMipLevels;
    range.baseArrayLayer = subresources.baseArraySlice;
    range.layerCount = subresources.numArraySlices;
    
    vkCmdClearColorImage(currentCmdBuf->cmdBuf, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    currentCmdBuf->referencedResources.push_back(_texture);
}

void CommandList::clearDepthStencilTexture(ITexture* _texture, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil){
    Texture* texture = checked_cast<Texture*>(_texture);
    
    VkClearDepthStencilValue clearValue{};
    clearValue.depth = depth;
    clearValue.stencil = stencil;
    
    VkImageSubresourceRange range{};
    range.aspectMask = 0;
    if(clearDepth)
        range.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(clearStencil)
        range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    range.baseMipLevel = subresources.baseMipLevel;
    range.levelCount = subresources.numMipLevels;
    range.baseArrayLayer = subresources.baseArraySlice;
    range.layerCount = subresources.numArraySlices;
    
    vkCmdClearDepthStencilImage(currentCmdBuf->cmdBuf, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    currentCmdBuf->referencedResources.push_back(_texture);
}

void CommandList::clearTextureUInt(ITexture* _texture, TextureSubresourceSet subresources, u32 clearColor){
    Texture* texture = checked_cast<Texture*>(_texture);
    
    VkClearColorValue clearValue;
    clearValue.uint32[0] = clearColor;
    clearValue.uint32[1] = clearColor;
    clearValue.uint32[2] = clearColor;
    clearValue.uint32[3] = clearColor;
    
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = subresources.baseMipLevel;
    range.levelCount = subresources.numMipLevels;
    range.baseArrayLayer = subresources.baseArraySlice;
    range.layerCount = subresources.numArraySlices;
    
    vkCmdClearColorImage(currentCmdBuf->cmdBuf, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    currentCmdBuf->referencedResources.push_back(_texture);
}

void CommandList::copyTexture(ITexture* _dest, const TextureSlice& destSlice, ITexture* _src, const TextureSlice& srcSlice){
    Texture* dest = checked_cast<Texture*>(_dest);
    Texture* src = checked_cast<Texture*>(_src);
    
    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = srcSlice.mipLevel;
    region.srcSubresource.baseArrayLayer = srcSlice.arraySlice;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = { srcSlice.x, srcSlice.y, srcSlice.z };
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = destSlice.mipLevel;
    region.dstSubresource.baseArrayLayer = destSlice.arraySlice;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = { destSlice.x, destSlice.y, destSlice.z };
    region.extent = { destSlice.width, destSlice.height, destSlice.depth };
    
    vkCmdCopyImage(currentCmdBuf->cmdBuf, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(_src);
    currentCmdBuf->referencedResources.push_back(_dest);
}

void CommandList::writeTexture(ITexture* _dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch){
    Texture* dest = checked_cast<Texture*>(_dest);
    const TextureDesc& desc = dest->desc;
    
    // Calculate required staging buffer size
    u32 width = Max(1u, desc.width >> mipLevel);
    u32 height = Max(1u, desc.height >> mipLevel);
    u32 depth = Max(1u, desc.depth >> mipLevel);
    
    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    u64 dataSize = u64(rowPitch) * height * depth;
    
    // Allocate staging buffer
    UploadManager* uploadMgr = m_device->getUploadManager();
    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    void* cpuVA = nullptr;
    
    if(!uploadMgr->suballocateBuffer(dataSize, &stagingBuffer, &stagingOffset, &cpuVA, 0)){
        NWB_ASSERT(false && "Failed to suballocate staging buffer for writeTexture");
        return;
    }
    
    // Copy data to staging
    NWB_MEMCPY(cpuVA, dataSize, data, dataSize);
    
    // Transition image to TRANSFER_DST
    VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = dest->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = arraySlice;
    barrier.subresourceRange.layerCount = 1;
    
    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(currentCmdBuf->cmdBuf, &depInfo);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;
    region.bufferRowLength = 0; // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, depth };
    
    vkCmdCopyBufferToImage(currentCmdBuf->cmdBuf, stagingBuffer->buffer, dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(_dest);
    currentCmdBuf->referencedStagingBuffers.push_back(stagingBuffer);
}

void CommandList::resolveTexture(ITexture* _dest, const TextureSubresourceSet& dstSubresources, ITexture* _src, const TextureSubresourceSet& srcSubresources){
    Texture* dest = checked_cast<Texture*>(_dest);
    Texture* src = checked_cast<Texture*>(_src);
    
    VkImageResolve region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = srcSubresources.baseMipLevel;
    region.srcSubresource.baseArrayLayer = srcSubresources.baseArraySlice;
    region.srcSubresource.layerCount = srcSubresources.numArraySlices;
    region.srcOffset = { 0, 0, 0 };
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = dstSubresources.baseMipLevel;
    region.dstSubresource.baseArrayLayer = dstSubresources.baseArraySlice;
    region.dstSubresource.layerCount = dstSubresources.numArraySlices;
    region.dstOffset = { 0, 0, 0 };
    region.extent = { Max(1u, src->desc.width >> srcSubresources.baseMipLevel),
                      Max(1u, src->desc.height >> srcSubresources.baseMipLevel), 1 };
    
    vkCmdResolveImage(currentCmdBuf->cmdBuf, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(_src);
    currentCmdBuf->referencedResources.push_back(_dest);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
