// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsSupportedSampleCount(u32 sampleCount){
    switch(sampleCount){
    case VK_SAMPLE_COUNT_1_BIT:
    case VK_SAMPLE_COUNT_2_BIT:
    case VK_SAMPLE_COUNT_4_BIT:
    case VK_SAMPLE_COUNT_8_BIT:
    case VK_SAMPLE_COUNT_16_BIT:
    case VK_SAMPLE_COUNT_32_BIT:
    case VK_SAMPLE_COUNT_64_BIT:
        return true;
    default:
        return false;
    }
}

bool ValidateTextureShape(const TextureDesc& desc, const tchar* operationName){
    if(desc.width == 0 || desc.height == 0 || desc.depth == 0 || desc.mipLevels == 0 || desc.arraySize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: dimensions, mip count, and array size must be nonzero"), operationName);
        return false;
    }
    if(desc.dimension == TextureDimension::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture dimension is unknown"), operationName);
        return false;
    }
    if((desc.dimension == TextureDimension::Texture1D || desc.dimension == TextureDimension::Texture1DArray) && (desc.height != 1 || desc.depth != 1)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: 1D texture height and depth must be 1"), operationName);
        return false;
    }
    if(desc.dimension != TextureDimension::Texture3D && desc.depth != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: non-3D texture depth must be 1"), operationName);
        return false;
    }
    if(desc.dimension == TextureDimension::Texture3D && desc.arraySize != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: 3D texture array size must be 1"), operationName);
        return false;
    }

    const u32 maxMipLevels = VulkanTextureDetail::GetMaxMipLevels(desc);
    if(desc.mipLevels > maxMipLevels){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: mip levels {} exceed maximum {} for texture dimensions {}x{}x{}")
            , operationName
            , desc.mipLevels
            , maxMipLevels
            , desc.width
            , desc.height
            , desc.depth
        );
        return false;
    }

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray){
        if(desc.width != desc.height){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube textures must have equal width and height"), operationName);
            return false;
        }
        if(desc.dimension == TextureDimension::TextureCube && desc.arraySize != VulkanTextureDetail::s_TextureCubeLayerCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube textures must have exactly 6 array layers"), operationName);
            return false;
        }
        if(desc.dimension == TextureDimension::TextureCubeArray && (desc.arraySize < VulkanTextureDetail::s_TextureCubeLayerCount || (desc.arraySize % VulkanTextureDetail::s_TextureCubeLayerCount) != 0)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube texture arrays must have a positive multiple of 6 array layers"), operationName);
            return false;
        }
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

bool GetTextureFormatBlockLayout(const FormatInfo& formatInfo, TextureFormatBlockLayout& outLayout){
    outLayout = {};
    outLayout.blockWidth = GetFormatBlockWidth(formatInfo);
    outLayout.blockHeight = GetFormatBlockHeight(formatInfo);
    outLayout.bytesPerBlock = formatInfo.bytesPerBlock;
    return outLayout.blockWidth != 0 && outLayout.blockHeight != 0 && outLayout.bytesPerBlock != 0;
}

bool ValidateBufferImageCopyAspectMask(const VkImageAspectFlags aspectMask, const tchar* operationName){
    if((aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 && (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: combined depth/stencil formats are not supported by buffer-image copy paths"), operationName);
        return false;
    }

    return true;
}

VkExtent3D GetTextureMipExtent(const TextureDesc& desc, const MipLevel mipLevel){
    VkExtent3D extent{};
    extent.width = Max<u32>(desc.width >> mipLevel, 1u);
    extent.height = Max<u32>(desc.height >> mipLevel, 1u);
    extent.depth = desc.dimension == TextureDimension::Texture3D ? Max<u32>(desc.depth >> mipLevel, 1u) : 1u;
    return extent;
}

bool BuildBufferImageCopyLayout(
    const VkExtent3D& extent,
    const TextureFormatBlockLayout& formatLayout,
    const u64 rowPitch,
    const u64 depthPitch,
    const BufferImageCopyRequiredSize::Enum requiredSizeMode,
    const BufferImageCopyPitchFields::Enum pitchFields,
    const tchar* operationName,
    BufferImageCopyLayout& outLayout
){
    outLayout = {};
    if(formatLayout.blockWidth == 0 || formatLayout.blockHeight == 0 || formatLayout.bytesPerBlock == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid texture format"), operationName);
        return false;
    }

    const u64 blockCountX = Max<u64>(DivideUp(static_cast<u64>(extent.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 blockCountY = Max<u64>(DivideUp(static_cast<u64>(extent.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);
    if(blockCountX > Limit<u64>::s_Max / formatLayout.bytesPerBlock){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: natural row pitch overflows"), operationName);
        return false;
    }

    const u64 naturalRowPitch = blockCountX * formatLayout.bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0 ? rowPitch : naturalRowPitch;
    if(effectiveRowPitch == 0 || blockCountY > UINT64_MAX / effectiveRowPitch){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture pitch size overflows"), operationName);
        return false;
    }
    if(effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % formatLayout.bytesPerBlock) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid row pitch"), operationName);
        return false;
    }

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0 ? depthPitch : packedSlicePitch;
    if(effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid depth pitch"), operationName);
        return false;
    }

    const u64 bufferRowBlocks = effectiveRowPitch / formatLayout.bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(bufferRowBlocks > UINT64_MAX / formatLayout.blockWidth || bufferImageBlocks > UINT64_MAX / formatLayout.blockHeight){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits"), operationName);
        return false;
    }

    const u64 bufferRowLength = bufferRowBlocks * formatLayout.blockWidth;
    const u64 bufferImageHeight = bufferImageBlocks * formatLayout.blockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits"), operationName);
        return false;
    }

    if(requiredSizeMode == BufferImageCopyRequiredSize::PaddedSlices){
        if(extent.depth > 1 && static_cast<u64>(extent.depth - 1) > (UINT64_MAX - packedSlicePitch) / effectiveDepthPitch){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return false;
        }
        outLayout.requiredSize = extent.depth > 1 ? static_cast<u64>(effectiveDepthPitch) * (extent.depth - 1) + packedSlicePitch : packedSlicePitch;
    }
    else{
        const u64 depthOffset = static_cast<u64>(extent.depth - 1);
        if(depthOffset > UINT64_MAX / effectiveDepthPitch){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return false;
        }
        const u64 depthBytes = depthOffset * effectiveDepthPitch;
        const u64 rowBytes = static_cast<u64>(blockCountY - 1) * effectiveRowPitch;
        if(depthBytes > UINT64_MAX - rowBytes || depthBytes + rowBytes > UINT64_MAX - naturalRowPitch){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return false;
        }
        outLayout.requiredSize = depthBytes + rowBytes + naturalRowPitch;
    }

    outLayout.bufferRowLength = pitchFields == BufferImageCopyPitchFields::EmitExplicit || rowPitch != 0 ? static_cast<u32>(bufferRowLength) : 0u;
    outLayout.bufferImageHeight = pitchFields == BufferImageCopyPitchFields::EmitExplicit || depthPitch != 0 ? static_cast<u32>(bufferImageHeight) : 0u;
    return true;
}

VkImageSubresourceLayers BuildImageSubresourceLayers(
    const VkImageAspectFlags aspectMask,
    const MipLevel mipLevel,
    const ArraySlice arraySlice,
    const ArraySlice layerCount
){
    VkImageSubresourceLayers layers{};
    layers.aspectMask = aspectMask;
    layers.mipLevel = mipLevel;
    layers.baseArrayLayer = arraySlice;
    layers.layerCount = layerCount;
    return layers;
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

bool BuildTextureImageViewCreateInfo(
    Texture& texture,
    const TextureSubresourceSet& resolvedSubresources,
    const TextureDimension::Enum dimension,
    const Format::Enum format,
    const tchar* operationName,
    const bool assertFailure,
    VkImageViewCreateInfo& outViewInfo
){
    const bool usesTextureFormat = format == texture.m_desc.format;
    const VkFormat vkFormat = usesTextureFormat ? texture.m_imageInfo.format : ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: format is unsupported"), operationName);
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create {}: format is unsupported"), operationName);
        return false;
    }

    if(!VulkanTextureDetail::ValidateTextureViewShape(dimension, resolvedSubresources)){
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create {}: invalid view shape"), operationName);
        return false;
    }

    const VkImageAspectFlags aspectMask = usesTextureFormat ? texture.m_aspectMask : GetImageAspectMask(GetFormatInfo(format));

    outViewInfo = {};
    outViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    outViewInfo.image = texture.m_image;
    outViewInfo.viewType = VulkanTextureDetail::TextureDimensionToViewType(dimension);
    outViewInfo.format = vkFormat;
    outViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.subresourceRange = BuildImageSubresourceRange(resolvedSubresources, aspectMask);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture::Texture(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_views(0, TextureViewKeyHasher(), EqualTo<TextureViewKey>(), context.objectArena)
    , m_retainedSubresourceStates(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}
Texture::~Texture(){
    for(const auto& [_, view] : m_views)
        vkDestroyImageView(m_context.device, view, m_context.allocationCallbacks);
    m_views.clear();

    if(m_managed){
        if(m_desc.isVirtual){
            if(m_image != VK_NULL_HANDLE){
                vkDestroyImage(m_context.device, m_image, m_context.allocationCallbacks);
                m_image = VK_NULL_HANDLE;
            }
        }
        else{
            m_allocator.destroyTexture(*this);
        }
    }
}

VkImageView Texture::getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format){
    if(dimension == TextureDimension::Unknown)
        dimension = m_desc.dimension;

    if(format == Format::UNKNOWN)
        format = m_desc.format;

    TextureSubresourceSet resolvedSubresources = subresources.resolve(m_desc, TextureSubresourceMipResolve::Range);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view: invalid subresource range"));
        return VK_NULL_HANDLE;
    }

    TextureViewKey key{
        resolvedSubresources,
        dimension,
        format
    };

    auto it = m_views.find(key);
    if(it != m_views.end())
        return it.value();

    VkImageViewCreateInfo viewInfo{};
    if(!VulkanDetail::BuildTextureImageViewCreateInfo(
        *this,
        resolvedSubresources,
        dimension,
        format,
        NWB_TEXT("image view"),
        true,
        viewInfo
    ))
        return VK_NULL_HANDLE;

    VkImageView view = VK_NULL_HANDLE;
    const VkResult res = vkCreateImageView(m_context.device, &viewInfo, m_context.allocationCallbacks, &view);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: {}"), ResultToString(res));
        return VK_NULL_HANDLE;
    }

    m_views.emplace(key, view);
    return view;
}

bool Texture::hasPartiallyKnownRetainedSubresourceState()const{
    if(!m_desc.keepInitialState)
        return false;

    const usize subresourceCount = static_cast<usize>(m_desc.mipLevels) * static_cast<usize>(m_desc.arraySize);
    if(subresourceCount == 0u)
        return false;

    ScopedLock lock(m_retainedSubresourceStatesMutex);
    const usize trackedSubresourceCount = Min(subresourceCount, m_retainedSubresourceStates.size());
    bool hasKnownState = false;
    bool hasUnknownState = trackedSubresourceCount < subresourceCount;
    for(usize subresourceIndex = 0u; subresourceIndex < trackedSubresourceCount; ++subresourceIndex){
        if(m_retainedSubresourceStates[subresourceIndex] != 0u)
            hasKnownState = true;
        else
            hasUnknownState = true;

        if(hasKnownState && hasUnknownState)
            return true;
    }
    return false;
}

void Texture::initializeRetainedSubresourceStates(const bool known){
    const usize subresourceCount = static_cast<usize>(m_desc.mipLevels) * static_cast<usize>(m_desc.arraySize);
    ScopedLock lock(m_retainedSubresourceStatesMutex);
    m_retainedSubresourceStates.assign(subresourceCount, known ? 1u : 0u);
}

bool Texture::isRetainedSubresourceStateKnown(const ArraySlice arraySlice, const MipLevel mipLevel){
    if(
        !m_desc.keepInitialState
        || arraySlice >= m_desc.arraySize
        || mipLevel >= m_desc.mipLevels
    )
        return false;

    const usize index = static_cast<usize>(arraySlice) * static_cast<usize>(m_desc.mipLevels) + static_cast<usize>(mipLevel);
    ScopedLock lock(m_retainedSubresourceStatesMutex);
    NWB_ASSERT(index < m_retainedSubresourceStates.size());
    return index < m_retainedSubresourceStates.size() && m_retainedSubresourceStates[index] != 0u;
}

void Texture::setRetainedSubresourceStateKnown(const ArraySlice arraySlice, const MipLevel mipLevel, const bool known){
    if(
        !m_desc.keepInitialState
        || arraySlice >= m_desc.arraySize
        || mipLevel >= m_desc.mipLevels
    )
        return;

    const usize index = static_cast<usize>(arraySlice) * static_cast<usize>(m_desc.mipLevels) + static_cast<usize>(mipLevel);
    ScopedLock lock(m_retainedSubresourceStatesMutex);
    NWB_ASSERT(index < m_retainedSubresourceStates.size());
    if(index >= m_retainedSubresourceStates.size())
        return;

    m_retainedSubresourceStates[index] = known ? 1u : 0u;
}

#if !defined(NWB_FINAL)
void VulkanDetail::MarkRetainedTextureSubresourceStateKnownForTesting(
    Texture& texture,
    const ArraySlice arraySlice,
    const MipLevel mipLevel
){
    if(
        !texture.m_desc.keepInitialState
        || arraySlice >= texture.m_desc.arraySize
        || mipLevel >= texture.m_desc.mipLevels
    )
        return;

    const usize subresourceCount = static_cast<usize>(texture.m_desc.mipLevels) * static_cast<usize>(texture.m_desc.arraySize);
    const usize index = static_cast<usize>(arraySlice) * static_cast<usize>(texture.m_desc.mipLevels) + static_cast<usize>(mipLevel);
    ScopedLock lock(texture.m_retainedSubresourceStatesMutex);
    if(texture.m_retainedSubresourceStates.size() != subresourceCount)
        texture.m_retainedSubresourceStates.assign(subresourceCount, 0u);
    texture.m_retainedSubresourceStates[index] = 1u;
}
#endif

Object Texture::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Image)
        return Object(m_image);
    return Object(nullptr);
}

Object Texture::getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool){
    if(objectType == ObjectTypes::VK_ImageView)
        return getView(subresources, dimension, format);
    if(objectType == ObjectTypes::VK_Image)
        return getNativeHandle(objectType);
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTexture::StagingTexture(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_mipLayouts(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}
StagingTexture::~StagingTexture(){
    m_allocator.destroyStagingTexture(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

