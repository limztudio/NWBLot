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
    switch(desc.dimension){
    case TextureDimension::Unknown:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture dimension is unknown"), operationName);
        return false;
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
    case TextureDimension::Texture3D:
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture dimension is invalid"), operationName);
        return false;
    }
    if(
        (
            desc.dimension == TextureDimension::Texture1D
            || desc.dimension == TextureDimension::Texture2D
            || desc.dimension == TextureDimension::Texture2DMS
        )
        && desc.arraySize != 1u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: non-array texture array size must be 1"), operationName);
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

bool TryComputeCommonAlignment(
    const u32 firstAlignment,
    const u32 secondAlignment,
    u32& outAlignment
)noexcept{
    outAlignment = 0u;
    if(firstAlignment == 0u || secondAlignment == 0u)
        return false;

    u32 first = firstAlignment;
    u32 second = secondAlignment;
    while(second != 0u){
        const u32 remainder = first % second;
        first = second;
        second = remainder;
    }
    if(first == 0u)
        return false;

    return TryMultiply<u32>(firstAlignment / first, secondAlignment, outAlignment)
        && outAlignment != 0u
    ;
}

bool TryComputeUploadSuballocationAlignment(const u32 requiredAlignment, u32& outAlignment)noexcept{
    return TryComputeCommonAlignment(s_DefaultUploadSuballocationAlignment, requiredAlignment, outAlignment);
}

bool IsBufferImageCopyAspectMaskSupported(const VkImageAspectFlags aspectMask)noexcept{
    return (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 || (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) == 0;
}

bool ValidateBufferImageCopyAspectMask(const VkImageAspectFlags aspectMask, const tchar* operationName){
    if(!IsBufferImageCopyAspectMaskSupported(aspectMask)){
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
    BufferImageCopyLayout& outLayout
){
    return BuildBufferImageCopyLayout(
        extent,
        formatLayout,
        rowPitch,
        depthPitch,
        requiredSizeMode,
        pitchFields,
        nullptr,
        outLayout
    );
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
        if(operationName)
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid texture format"), operationName);
        return false;
    }

    const u64 blockCountX = Max<u64>(DivideUp(static_cast<u64>(extent.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 blockCountY = Max<u64>(DivideUp(static_cast<u64>(extent.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);
    if(blockCountX > Limit<u64>::s_Max / formatLayout.bytesPerBlock){
        if(operationName)
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: natural row pitch overflows"), operationName);
        return false;
    }

    const u64 naturalRowPitch = blockCountX * formatLayout.bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0 ? rowPitch : naturalRowPitch;
    if(effectiveRowPitch == 0 || blockCountY > UINT64_MAX / effectiveRowPitch){
        if(operationName)
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture pitch size overflows"), operationName);
        return false;
    }
    if(effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % formatLayout.bytesPerBlock) != 0){
        if(operationName)
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid row pitch"), operationName);
        return false;
    }

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0 ? depthPitch : packedSlicePitch;
    if(effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0){
        if(operationName)
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid depth pitch"), operationName);
        return false;
    }

    const u64 bufferRowBlocks = effectiveRowPitch / formatLayout.bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(bufferRowBlocks > UINT64_MAX / formatLayout.blockWidth || bufferImageBlocks > UINT64_MAX / formatLayout.blockHeight){
        if(operationName)
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits")
                , operationName
            );
        return false;
    }

    const u64 bufferRowLength = bufferRowBlocks * formatLayout.blockWidth;
    const u64 bufferImageHeight = bufferImageBlocks * formatLayout.blockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX){
        if(operationName)
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits")
                , operationName
            );
        return false;
    }

    if(requiredSizeMode == BufferImageCopyRequiredSize::PaddedSlices){
        if(extent.depth > 1 && static_cast<u64>(extent.depth - 1) > (UINT64_MAX - packedSlicePitch) / effectiveDepthPitch){
            if(operationName)
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return false;
        }
        outLayout.requiredSize = extent.depth > 1 ? static_cast<u64>(effectiveDepthPitch) * (extent.depth - 1) + packedSlicePitch : packedSlicePitch;
    }
    else{
        const u64 depthOffset = static_cast<u64>(extent.depth - 1);
        if(depthOffset > UINT64_MAX / effectiveDepthPitch){
            if(operationName)
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return false;
        }
        const u64 depthBytes = depthOffset * effectiveDepthPitch;
        const u64 rowBytes = static_cast<u64>(blockCountY - 1) * effectiveRowPitch;
        if(depthBytes > UINT64_MAX - rowBytes || depthBytes + rowBytes > UINT64_MAX - naturalRowPitch){
            if(operationName)
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
    const bool usesTextureFormat = format == texture.m_creationDesc.format;
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


namespace __hidden_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ImageQueueFamilyVector = Vector<u32, Alloc::GlobalArena>;


[[nodiscard]] static ImageQueueFamilyVector CopyImageQueueFamilyIndices(
    const VulkanContext& context,
    const VkImageCreateInfo& imageInfo
){
    ImageQueueFamilyVector result(context.objectArena);
    if(imageInfo.queueFamilyIndexCount == 0u)
        return result;

    NWB_ASSERT(imageInfo.pQueueFamilyIndices != nullptr);
    if(!imageInfo.pQueueFamilyIndices)
        return result;
    result.assign(
        imageInfo.pQueueFamilyIndices,
        imageInfo.pQueueFamilyIndices + imageInfo.queueFamilyIndexCount
    );
    return result;
}

[[nodiscard]] static VkImageCreateInfo RetainImageCreateInfo(
    const VkImageCreateInfo& imageInfo,
    const ImageQueueFamilyVector& queueFamilyIndices
){
    NWB_ASSERT(queueFamilyIndices.size() <= Limit<u32>::s_Max);
    VkImageCreateInfo result = imageInfo;
    result.pNext = nullptr;
    result.queueFamilyIndexCount = static_cast<u32>(queueFamilyIndices.size());
    result.pQueueFamilyIndices = queueFamilyIndices.empty() ? nullptr : queueFamilyIndices.data();
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture::Texture(
    const VulkanContext& context,
    VulkanAllocator& allocator,
    const TextureDesc& creationDesc,
    const VkImageCreateInfo& imageInfo,
    const bool initialStateKnown
)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(creationDesc)
    , m_creationDesc(creationDesc)
    , m_creationInitialStateKnown(initialStateKnown)
    , m_imageQueueFamilyIndices(__hidden_texture::CopyImageQueueFamilyIndices(context, imageInfo))
    , m_imageInfo(__hidden_texture::RetainImageCreateInfo(imageInfo, m_imageQueueFamilyIndices))
    , m_views(0, TextureViewKeyHasher(), EqualTo<TextureViewKey>(), context.objectArena)
    , m_retainedSubresourceStates(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{
    const usize subresourceCount = static_cast<usize>(m_creationDesc.mipLevels)
        * static_cast<usize>(m_creationDesc.arraySize)
    ;
    const bool retainedInitialStateKnown = m_creationDesc.keepInitialState && initialStateKnown;
    m_retainedSubresourceStates.assign(subresourceCount, retainedInitialStateKnown ? 1u : 0u);
}
Texture::~Texture(){
    const VkImage registeredNativeImage = m_image;

    for(const auto& [_, view] : m_views)
        m_context.deviceDispatch.vkDestroyImageView(m_context.device, view, m_context.allocationCallbacks);
    m_views.clear();

    {
        ScopedLock bindingLock(m_memoryBindingMutex);
        if(m_boundHeap){
            Heap* const boundHeap = m_boundHeap.get();
            {
                ScopedLock heapLock(boundHeap->m_bindingMutex);
                if(m_image != VK_NULL_HANDLE){
                    m_context.deviceDispatch.vkDestroyImage(m_context.device, m_image, m_context.allocationCallbacks);
                    m_image = VK_NULL_HANDLE;
                }
                boundHeap->eraseBindingReservationLocked(this);
            }
            m_heapBindingRange = {};
            m_boundHeap.reset();
        }
        else if(m_managed){
            if(m_creationDesc.isVirtual){
                if(m_image != VK_NULL_HANDLE){
                    m_context.deviceDispatch.vkDestroyImage(m_context.device, m_image, m_context.allocationCallbacks);
                    m_image = VK_NULL_HANDLE;
                }
            }
            else{
                m_allocator.destroyTexture(*this);
            }
        }
    }

    m_allocator.unregisterTextureNativeIdentity(registeredNativeImage, *this);
}

bool Texture::descriptionMatchesCreation()const noexcept{
    return VulkanTextureDetail::TextureDescriptionsEqual(m_desc, m_creationDesc);
}

ResourceStates::Mask Texture::resolveTaskGraphImportInitialState()const{
    if(!m_creationDesc.keepInitialState){
        return m_managed || m_creationInitialStateKnown
            ? m_creationDesc.initialState
            : ResourceStates::Unknown
        ;
    }

    const usize subresourceCount = static_cast<usize>(m_creationDesc.mipLevels)
        * static_cast<usize>(m_creationDesc.arraySize)
    ;
    if(subresourceCount == 0u)
        return ResourceStates::Unknown;

    ScopedLock lock(m_retainedSubresourceStatesMutex);
    const usize trackedSubresourceCount = Min(subresourceCount, m_retainedSubresourceStates.size());
    bool hasKnownState = false;
    bool hasUnknownState = trackedSubresourceCount < subresourceCount;
    for(usize subresourceIndex = 0u; subresourceIndex < trackedSubresourceCount; ++subresourceIndex){
        if(m_retainedSubresourceStates[subresourceIndex] != 0u)
            hasKnownState = true;
        else
            hasUnknownState = true;

    }
    return !hasUnknownState || (!hasKnownState && m_managed)
        ? m_creationDesc.initialState
        : ResourceStates::Unknown
    ;
}

Object Texture::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Image){
        ScopedLock bindingLock(m_memoryBindingMutex);
        return Object(m_image);
    }
    return Object(nullptr);
}

Object Texture::getNativeView(
    ObjectType objectType,
    Format::Enum format,
    TextureSubresourceSet subresources,
    TextureDimension::Enum dimension,
    bool
){
    if(objectType == ObjectTypes::VK_ImageView)
        return getView(subresources, dimension, format);
    if(objectType == ObjectTypes::VK_Image)
        return getNativeHandle(objectType);
    return nullptr;
}

VkImageView Texture::getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format){
    if(dimension == TextureDimension::Unknown)
        dimension = m_creationDesc.dimension;

    if(format == Format::UNKNOWN)
        format = m_creationDesc.format;

    TextureSubresourceSet resolvedSubresources = subresources.resolve(m_creationDesc, TextureSubresourceMipResolve::Range);
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

    ScopedLock lock(m_viewsMutex);
    if(m_image == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;
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
    const VkResult res = m_context.deviceDispatch.vkCreateImageView(m_context.device, &viewInfo, m_context.allocationCallbacks, &view);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: {}"), ResultToString(res));
        return VK_NULL_HANDLE;
    }

    m_views.emplace(key, view);
    return view;
}

bool Texture::canRevokeUnmanagedNativeImage(const VkImage expectedNativeImage)noexcept{
    if(expectedNativeImage == VK_NULL_HANDLE)
        return false;

    ScopedLock bindingLock(m_memoryBindingMutex);
    if(m_managed || m_image != expectedNativeImage)
        return false;

    return m_allocator.isTextureNativeIdentityRegistered(expectedNativeImage, *this);
}

bool Texture::revokeUnmanagedNativeImage(const VkImage expectedNativeImage)noexcept{
    if(expectedNativeImage == VK_NULL_HANDLE)
        return false;

    ScopedLock bindingLock(m_memoryBindingMutex);
    if(m_managed || m_image != expectedNativeImage)
        return false;

    ScopedLock viewsLock(m_viewsMutex);
    const bool identityRegistered = m_allocator.isTextureNativeIdentityRegistered(expectedNativeImage, *this);
    for(const auto& [_, view] : m_views)
        m_context.deviceDispatch.vkDestroyImageView(m_context.device, view, m_context.allocationCallbacks);
    m_views.clear();
    m_image = VK_NULL_HANDLE;
    return identityRegistered;
}

void Texture::releaseRevokedNativeImageIdentity(const VkImage expectedNativeImage)noexcept{
    if(expectedNativeImage == VK_NULL_HANDLE)
        return;

    {
        ScopedLock bindingLock(m_memoryBindingMutex);
        if(m_managed || m_image != VK_NULL_HANDLE){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Only a revoked unmanaged Texture can release a native identity"));
            return;
        }
    }
    m_allocator.unregisterTextureNativeIdentity(expectedNativeImage, *this);
}

bool Texture::isRetainedSubresourceStateKnown(const ArraySlice arraySlice, const MipLevel mipLevel){
    if(
        !m_creationDesc.keepInitialState
        || arraySlice >= m_creationDesc.arraySize
        || mipLevel >= m_creationDesc.mipLevels
    )
        return false;

    const usize index = static_cast<usize>(arraySlice) * static_cast<usize>(m_creationDesc.mipLevels)
        + static_cast<usize>(mipLevel)
    ;
    ScopedLock lock(m_retainedSubresourceStatesMutex);
    NWB_ASSERT(index < m_retainedSubresourceStates.size());
    return index < m_retainedSubresourceStates.size() && m_retainedSubresourceStates[index] != 0u;
}

void Texture::setRetainedSubresourceStateKnown(const ArraySlice arraySlice, const MipLevel mipLevel, const bool known)noexcept{
    if(
        !m_creationDesc.keepInitialState
        || arraySlice >= m_creationDesc.arraySize
        || mipLevel >= m_creationDesc.mipLevels
    )
        return;

    const usize index = static_cast<usize>(arraySlice) * static_cast<usize>(m_creationDesc.mipLevels)
        + static_cast<usize>(mipLevel)
    ;
    ScopedLock lock(m_retainedSubresourceStatesMutex);
    NWB_ASSERT(index < m_retainedSubresourceStates.size());
    if(index >= m_retainedSubresourceStates.size())
        return;

    m_retainedSubresourceStates[index] = known ? 1u : 0u;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTexture::StagingTexture(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_mipLayouts(context.objectArena)
    , m_admittedQueueFamilies(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}
StagingTexture::~StagingTexture(){
    ScopedLock lock(m_mappingMutex);

    m_allocator.destroyStagingTexture(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

