// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextureCreateMetadata{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VulkanDetail::TextureFormatBlockLayout formatLayout;
    VkImageAspectFlags aspectMask = 0;
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
};

inline u32 GetMaxMipLevels(const TextureDesc& desc){
    const u32 depth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
    u32 maxExtent = Max(Max(desc.width, desc.height), depth);
    u32 levels = 1;
    while(maxExtent > 1){
        maxExtent >>= 1;
        ++levels;
    }
    return levels;
}

inline VkImageType TextureDimensionToImageType(TextureDimension::Enum dimension){
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

inline VkImageViewType TextureDimensionToViewType(TextureDimension::Enum dimension){
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

inline VkImageUsageFlags PickImageUsage(const TextureDesc& desc, const VkImageAspectFlags aspectMask){
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

inline VkImageCreateFlags PickImageFlags(const TextureDesc& desc){
    VkImageCreateFlags flags = 0;

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray)
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if(desc.dimension == TextureDimension::Texture3D && desc.isRenderTarget)
        flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;

    return flags;
}

inline VkImageCreateInfo BuildTextureImageCreateInfo(const TextureDesc& desc, const TextureCreateMetadata& metadata){
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

inline bool ValidateTextureViewShape(const TextureDimension::Enum dimension, const TextureSubresourceSet& subresources){
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

inline bool ReportTextureCreateDescError(const tchar* operationName, const tchar* message, const bool assertFailure){
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    if(assertFailure)
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    return false;
}

inline bool ValidateTextureCreateDesc(
    const TextureDesc& desc,
    const tchar* operationName,
    const bool assertFailure,
    TextureCreateMetadata& outMetadata
){
    outMetadata = {};
    if(!VulkanDetail::ValidateTextureShape(desc, operationName)){
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: invalid texture shape"), operationName);
        return false;
    }

    outMetadata.format = VulkanDetail::ConvertFormat(desc.format);
    if(outMetadata.format == VK_FORMAT_UNDEFINED)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("format is unsupported"), assertFailure);

    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    if(!VulkanDetail::GetTextureFormatBlockLayout(formatInfo, outMetadata.formatLayout))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("invalid texture format"), assertFailure);

    outMetadata.aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    if(!VulkanDetail::IsSupportedSampleCount(desc.sampleCount))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("sample count is unsupported"), assertFailure);
    if(desc.dimension == TextureDimension::Texture3D && desc.sampleCount != 1)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("3D texture sample count must be 1"), assertFailure);
    if(desc.sampleCount != 1 && desc.mipLevels != 1)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("multisampled texture mip levels must be 1"), assertFailure);

    outMetadata.imageType = TextureDimensionToImageType(desc.dimension);
    outMetadata.usage = PickImageUsage(desc, outMetadata.aspectMask);
    outMetadata.flags = PickImageFlags(desc);
    outMetadata.sampleCount = VulkanDetail::GetSampleCountFlagBits(desc.sampleCount);
    return true;
}

inline VkBufferImageCopy BuildStagingTextureCopyRegion(
    const TextureSlice& stagingSlice,
    const TextureSlice& imageSlice,
    const VkImageAspectFlags aspectMask,
    const VulkanDetail::StagingTextureMipLayout& stagingMipLayout,
    const VulkanDetail::TextureFormatBlockLayout& stagingFormatLayout,
    const u64 stagingArrayByteSize
){
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
    const u64 bufferOffset = VulkanDetail::ComputeStagingTextureOffset(
        stagingSlice,
        stagingMipLayout,
        stagingFormatLayout,
        stagingArrayByteSize,
        nullptr,
        &bufferRowLength,
        &bufferImageHeight,
        nullptr
    );

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = bufferRowLength;
    region.bufferImageHeight = bufferImageHeight;
    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(aspectMask, imageSlice.mipLevel, imageSlice.arraySlice);
    region.imageOffset = { static_cast<i32>(imageSlice.x), static_cast<i32>(imageSlice.y), static_cast<i32>(imageSlice.z) };
    region.imageExtent = { imageSlice.width, imageSlice.height, imageSlice.depth };
    return region;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    const u32 maxMipLevels = __hidden_vulkan_texture::GetMaxMipLevels(desc);
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

bool GetBufferImageCopyAspectMask(const VkImageAspectFlags aspectMask, const tchar* operationName, VkImageAspectFlags& outAspectMask){
    outAspectMask = aspectMask;
    if((outAspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 && (outAspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0){
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

    if(!__hidden_vulkan_texture::ValidateTextureViewShape(dimension, resolvedSubresources)){
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create {}: invalid view shape"), operationName);
        return false;
    }

    const VkImageAspectFlags aspectMask = usesTextureFormat ? texture.m_aspectMask : GetImageAspectMask(GetFormatInfo(format));

    outViewInfo = {};
    outViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    outViewInfo.image = texture.m_image;
    outViewInfo.viewType = __hidden_vulkan_texture::TextureDimensionToViewType(dimension);
    outViewInfo.format = vkFormat;
    outViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.subresourceRange = BuildImageSubresourceRange(resolvedSubresources, aspectMask);
    return true;
}

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
    : RefCounter<IStagingTexture>(context.threadPool)
    , m_mipLayouts(Alloc::CustomAllocator<VulkanDetail::StagingTextureMipLayout>(context.objectArena))
    , m_context(context)
    , m_allocator(allocator)
{}
StagingTexture::~StagingTexture(){
    m_allocator.destroyStagingTexture(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextureHandle Device::createTexture(const TextureDesc& d){
    if(d.isTiled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create tiled texture: virtual/tiled resources are not supported by this backend"));
        return nullptr;
    }
    __hidden_vulkan_texture::TextureCreateMetadata metadata;
    if(!__hidden_vulkan_texture::ValidateTextureCreateDesc(d, NWB_TEXT("create texture"), true, metadata))
        return nullptr;

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = d;
    texture->m_formatLayout = metadata.formatLayout;
    texture->m_aspectMask = metadata.aspectMask;

    texture->m_imageInfo = __hidden_vulkan_texture::BuildTextureImageCreateInfo(d, metadata);

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

    const VkResult res = m_allocator.bindHeapTextureMemory(*texture, *vkHeap, offset);
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
    __hidden_vulkan_texture::TextureCreateMetadata metadata;
    if(!__hidden_vulkan_texture::ValidateTextureCreateDesc(desc, NWB_TEXT("create texture handle for native texture"), false, metadata))
        return nullptr;

    auto* texture = NewArenaObject<Texture>(m_context.objectArena, m_context, m_allocator);
    texture->m_desc = desc;
    texture->m_formatLayout = metadata.formatLayout;
    texture->m_aspectMask = metadata.aspectMask;
    texture->m_image = nativeImage;
    texture->m_managed = false;
    texture->m_keepInitialStateKnown = desc.keepInitialState;

    texture->m_imageInfo = __hidden_vulkan_texture::BuildTextureImageCreateInfo(desc, metadata);

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


void CommandList::clearTextureFloat(ITexture* textureResource, TextureSubresourceSet subresources, const Color& clearColor){
    VkClearColorValue clearValue{};
    clearValue.float32[0] = clearColor.r;
    clearValue.float32[1] = clearColor.g;
    clearValue.float32[2] = clearColor.b;
    clearValue.float32[3] = clearColor.a;

    clearColorTexture(textureResource, subresources, NWB_TEXT("color value"), clearValue);
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
    if(
        (clearDepth && (texture->m_aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) == 0)
        || (clearStencil && (texture->m_aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) == 0)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: requested aspect is not present in the texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture: requested aspect is not present in the texture format"));
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture->m_desc, TextureSubresourceMipResolve::Range);
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
    VkClearColorValue clearValue{};
    clearValue.uint32[0] = clearColor;
    clearValue.uint32[1] = clearColor;
    clearValue.uint32[2] = clearColor;
    clearValue.uint32[3] = clearColor;

    clearColorTexture(textureResource, subresources, NWB_TEXT("integer value"), clearValue);
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
    TextureSlice resolvedDst;
    TextureSlice resolvedSrc;
    if(
        !VulkanDetail::IsTextureSliceInBounds(dest->m_desc, destSlice, dest->m_formatLayout, &resolvedDst)
        || !VulkanDetail::IsTextureSliceInBounds(src->m_desc, srcSlice, src->m_formatLayout, &resolvedSrc)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture: slice is outside the texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture: slice is outside the texture"));
        return;
    }

    if(
        resolvedDst.width != resolvedSrc.width
        || resolvedDst.height != resolvedSrc.height
        || resolvedDst.depth != resolvedSrc.depth
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture: source and destination extents do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture: source and destination extents do not match"));
        return;
    }

    VkImageCopy region{};
    region.srcSubresource = VulkanDetail::BuildImageSubresourceLayers(src->m_aspectMask, resolvedSrc.mipLevel, resolvedSrc.arraySlice);
    region.srcOffset = { static_cast<int32_t>(resolvedSrc.x), static_cast<int32_t>(resolvedSrc.y), static_cast<int32_t>(resolvedSrc.z) };
    region.dstSubresource = VulkanDetail::BuildImageSubresourceLayers(dest->m_aspectMask, resolvedDst.mipLevel, resolvedDst.arraySlice);
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
        !prepareStagingTextureCopy(
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
        !prepareStagingTextureCopy(
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

    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(texDesc, mipLevel);

    VkImageAspectFlags copyAspectMask = 0;
    if(!VulkanDetail::GetBufferImageCopyAspectMask(dest->m_aspectMask, NWB_TEXT("write texture"), copyAspectMask)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: combined depth/stencil buffer-image copies are not supported"));
        return;
    }
    VulkanDetail::BufferImageCopyLayout copyLayout;
    if(!VulkanDetail::BuildBufferImageCopyLayout(
        mipExtent,
        dest->m_formatLayout,
        static_cast<u64>(rowPitch),
        static_cast<u64>(depthPitch),
        VulkanDetail::BufferImageCopyRequiredSize::PaddedSlices,
        VulkanDetail::BufferImageCopyPitchFields::EmitExplicit,
        NWB_TEXT("write texture"),
        copyLayout
    )){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: invalid buffer-image copy layout"));
        return;
    }
    if(copyLayout.requiredSize > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: upload size exceeds addressable memory"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: upload size exceeds addressable memory"));
        return;
    }

    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    const usize uploadSize = static_cast<usize>(copyLayout.requiredSize);
    if(!prepareUploadStaging(data, uploadSize, NWB_TEXT("writeTexture"), stagingBuffer, stagingOffset))
        return;

    setTextureState(destResource, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), ResourceStates::CopyDest);

    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;
    region.bufferRowLength = copyLayout.bufferRowLength;
    region.bufferImageHeight = copyLayout.bufferImageHeight;
    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(copyAspectMask, mipLevel, arraySlice);
    region.imageExtent = mipExtent;

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
    if(src->m_imageInfo.format != dest->m_imageInfo.format){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination formats do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination formats do not match"));
        return;
    }
    if((src->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: depth/stencil resolves are not supported by this path"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: depth/stencil resolves are not supported by this path"));
        return;
    }

    const TextureSubresourceSet resolvedSrc = srcSubresources.resolve(src->m_desc, TextureSubresourceMipResolve::Range);
    const TextureSubresourceSet resolvedDst = dstSubresources.resolve(dest->m_desc, TextureSubresourceMipResolve::Range);
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
    Vector<VkImageResolve, Alloc::ScratchAllocator<VkImageResolve>> regions(resolvedSrc.numMipLevels, Alloc::ScratchAllocator<VkImageResolve>(scratchArena));

    for(MipLevel mipOffset = 0; mipOffset < resolvedSrc.numMipLevels; ++mipOffset){
        const MipLevel srcMipLevel = resolvedSrc.baseMipLevel + mipOffset;
        const MipLevel dstMipLevel = resolvedDst.baseMipLevel + mipOffset;
        const VkExtent3D srcExtent = VulkanDetail::GetTextureMipExtent(src->m_desc, srcMipLevel);
        const VkExtent3D dstExtent = VulkanDetail::GetTextureMipExtent(dest->m_desc, dstMipLevel);
        if(srcExtent.width != dstExtent.width || srcExtent.height != dstExtent.height || srcExtent.depth != dstExtent.depth){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination mip extents do not match"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination mip extents do not match"));
            return;
        }

        VkImageResolve region{};
        region.srcSubresource = VulkanDetail::BuildImageSubresourceLayers(
            VK_IMAGE_ASPECT_COLOR_BIT,
            srcMipLevel,
            resolvedSrc.baseArraySlice,
            resolvedSrc.numArraySlices
        );
        region.srcOffset = { 0, 0, 0 };
        region.dstSubresource = VulkanDetail::BuildImageSubresourceLayers(
            VK_IMAGE_ASPECT_COLOR_BIT,
            dstMipLevel,
            resolvedDst.baseArraySlice,
            resolvedDst.numArraySlices
        );
        region.dstOffset = { 0, 0, 0 };
        region.extent = srcExtent;
        regions[mipOffset] = region;
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


void CommandList::clearColorTexture(ITexture* textureResource, TextureSubresourceSet subresources, const tchar* valueName, const VkClearColorValue& clearValue){
    auto* texture = checked_cast<Texture*>(textureResource);
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture: texture is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture: texture is null"));
        return;
    }
    if((texture->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture with {}: texture format is depth/stencil"), valueName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture with {}: texture format is depth/stencil"), valueName);
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture->m_desc, TextureSubresourceMipResolve::Range);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture: invalid subresource range"));
        return;
    }

    const VkImageSubresourceRange range = VulkanDetail::BuildImageSubresourceRange(resolvedSubresources, VK_IMAGE_ASPECT_COLOR_BIT);
    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
    vkCmdClearColorImage(m_currentCmdBuf->m_cmdBuf, texture->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    retainResource(textureResource);
}

bool CommandList::prepareStagingTextureCopy(
    IStagingTexture* stagingResource,
    const TextureSlice& stagingSlice,
    ITexture* textureResource,
    const TextureSlice& textureSlice,
    const tchar* operationName,
    const tchar* singleSampleRequirement,
    StagingTexture*& outStaging,
    Texture*& outTexture,
    VkBufferImageCopy& outRegion
)const{
    outStaging = checked_cast<StagingTexture*>(stagingResource);
    outTexture = checked_cast<Texture*>(textureResource);
    if(!outStaging || !outTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: resource is invalid"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: resource is invalid"), operationName);
        return false;
    }
    const TextureDesc& stagingDesc = outStaging->m_desc;
    const TextureDesc& textureDesc = outTexture->m_desc;
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
    if(!VulkanDetail::GetBufferImageCopyAspectMask(outStaging->m_aspectMask, operationName, copyAspectMask)){
        NWB_ASSERT_MSG(
            false,
            NWB_TEXT("Vulkan: Failed to {}: combined depth/stencil buffer-image copies are not supported"),
            operationName
        );
        return false;
    }
    TextureSlice resolvedStaging;
    TextureSlice resolvedTexture;
    if(
        !VulkanDetail::IsTextureSliceInBounds(stagingDesc, stagingSlice, outStaging->m_formatLayout, &resolvedStaging)
        || !VulkanDetail::IsTextureSliceInBounds(textureDesc, textureSlice, outTexture->m_formatLayout, &resolvedTexture)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: slice is outside the texture"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: slice is outside the texture"), operationName);
        return false;
    }

    if(
        resolvedStaging.width != resolvedTexture.width
        || resolvedStaging.height != resolvedTexture.height
        || resolvedStaging.depth != resolvedTexture.depth
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: source and destination extents do not match"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: source and destination extents do not match"), operationName);
        return false;
    }

    outRegion = __hidden_vulkan_texture::BuildStagingTextureCopyRegion(
        resolvedStaging,
        resolvedTexture,
        copyAspectMask,
        outStaging->m_mipLayouts[resolvedStaging.mipLevel],
        outStaging->m_formatLayout,
        outStaging->m_arrayByteSize
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

