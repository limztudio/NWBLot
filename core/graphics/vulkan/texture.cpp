// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "texture_resource_detail.h"

#include <global/algorithm.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsSupportedSampleCount(u32 sampleCount)noexcept{
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

bool ValidateTextureShape(const TextureDesc& desc, TStringView operationName){
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
    if((desc.dimension == TextureDimension::Texture1D || desc.dimension == TextureDimension::Texture2D || desc.dimension == TextureDimension::Texture2DMS) && desc.arraySize != 1u){
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
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: mip levels {} exceed maximum {} for texture dimensions {}x{}x{}"), operationName, desc.mipLevels, maxMipLevels, desc.width, desc.height, desc.depth);
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

VkImageAspectFlags GetImageAspectMask(const FormatInfo& formatInfo)noexcept{
    VkImageAspectFlags aspectMask = 0;
    if(formatInfo.hasDepth)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if(aspectMask == 0)
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    return aspectMask;
}

Expected<TextureFormatBlockLayout> GetTextureFormatBlockLayout(const FormatInfo& formatInfo)noexcept{
    const TextureFormatBlockLayout layout{ GetFormatBlockWidth(formatInfo), GetFormatBlockHeight(formatInfo), formatInfo.bytesPerBlock };
    if(layout.blockWidth == 0u || layout.blockHeight == 0u || layout.bytesPerBlock == 0u)
        return MakeUnexpected(Failure{});
    return layout;
}

Expected<u32> TryComputeUploadSuballocationAlignment(const u32 requiredAlignment)noexcept{
    return CommonAlignmentChecked<u32>(s_DefaultUploadSuballocationAlignment, requiredAlignment);
}

bool IsBufferImageCopyAspectMaskSupported(const VkImageAspectFlags aspectMask)noexcept{
    return (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 || (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) == 0;
}

VkExtent3D GetTextureMipExtent(const TextureDesc& desc, const MipLevel mipLevel)noexcept{
    VkExtent3D extent{};
    extent.width = Max<u32>(desc.width >> mipLevel, 1u);
    extent.height = Max<u32>(desc.height >> mipLevel, 1u);
    extent.depth = desc.dimension == TextureDimension::Texture3D ? Max<u32>(desc.depth >> mipLevel, 1u) : 1u;
    return extent;
}

Expected<BufferImageCopyLayout> BuildBufferImageCopyLayout(
    const VkExtent3D& extent,
    const TextureFormatBlockLayout& formatLayout,
    const u64 rowPitch,
    const u64 depthPitch,
    const BufferImageCopyRequiredSize::Enum requiredSizeMode,
    const BufferImageCopyPitchFields::Enum pitchFields,
    TStringView operationName
){
    BufferImageCopyLayout outLayout{};
    NWB_ASSERT(formatLayout.blockWidth != 0u && formatLayout.blockHeight != 0u && formatLayout.bytesPerBlock != 0u);

    const u64 blockCountX = Max<u64>(DivideUp(static_cast<u64>(extent.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 blockCountY = Max<u64>(DivideUp(static_cast<u64>(extent.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);

    const u64 naturalRowPitch = blockCountX * formatLayout.bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0 ? rowPitch : naturalRowPitch;
    if(blockCountY > UINT64_MAX / effectiveRowPitch){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture pitch size overflows"), operationName);
        return MakeUnexpected(Failure{});
    }
    if(effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % formatLayout.bytesPerBlock) != 0){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid row pitch"), operationName);
        return MakeUnexpected(Failure{});
    }

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0 ? depthPitch : packedSlicePitch;
    if(effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid depth pitch"), operationName);
        return MakeUnexpected(Failure{});
    }

    const u64 bufferRowBlocks = effectiveRowPitch / formatLayout.bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(bufferRowBlocks > UINT64_MAX / formatLayout.blockWidth || bufferImageBlocks > UINT64_MAX / formatLayout.blockHeight){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits"), operationName);
        return MakeUnexpected(Failure{});
    }

    const u64 bufferRowLength = bufferRowBlocks * formatLayout.blockWidth;
    const u64 bufferImageHeight = bufferImageBlocks * formatLayout.blockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits"), operationName);
        return MakeUnexpected(Failure{});
    }

    if(requiredSizeMode == BufferImageCopyRequiredSize::PaddedSlices){
        if(extent.depth > 1 && static_cast<u64>(extent.depth - 1) > (UINT64_MAX - packedSlicePitch) / effectiveDepthPitch){
            if(!operationName.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return MakeUnexpected(Failure{});
        }
        outLayout.requiredSize = extent.depth > 1 ? static_cast<u64>(effectiveDepthPitch) * (extent.depth - 1) + packedSlicePitch : packedSlicePitch;
    }
    else{
        const u64 depthOffset = static_cast<u64>(extent.depth - 1);
        if(depthOffset > UINT64_MAX / effectiveDepthPitch){
            if(!operationName.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return MakeUnexpected(Failure{});
        }
        const u64 depthBytes = depthOffset * effectiveDepthPitch;
        const u64 rowBytes = static_cast<u64>(blockCountY - 1) * effectiveRowPitch;
        if(depthBytes > UINT64_MAX - rowBytes || depthBytes + rowBytes > UINT64_MAX - naturalRowPitch){
            if(!operationName.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return MakeUnexpected(Failure{});
        }
        outLayout.requiredSize = depthBytes + rowBytes + naturalRowPitch;
    }

    outLayout.bufferRowLength = pitchFields == BufferImageCopyPitchFields::EmitExplicit || rowPitch != 0 ? static_cast<u32>(bufferRowLength) : 0u;
    outLayout.bufferImageHeight = pitchFields == BufferImageCopyPitchFields::EmitExplicit || depthPitch != 0 ? static_cast<u32>(bufferImageHeight) : 0u;
    return outLayout;
}

VkImageSubresourceLayers BuildImageSubresourceLayers(const VkImageAspectFlags aspectMask, const MipLevel mipLevel, const ArraySlice arraySlice, const ArraySlice layerCount)noexcept{
    VkImageSubresourceLayers layers{};
    layers.aspectMask = aspectMask;
    layers.mipLevel = mipLevel;
    layers.baseArrayLayer = arraySlice;
    layers.layerCount = layerCount;
    return layers;
}

VkImageSubresourceRange BuildImageSubresourceRange(const TextureSubresourceSet& subresources, const VkImageAspectFlags aspectMask)noexcept{
    VkImageSubresourceRange range{};
    range.aspectMask = aspectMask;
    range.baseMipLevel = subresources.baseMipLevel;
    range.levelCount = subresources.numMipLevels;
    range.baseArrayLayer = subresources.baseArraySlice;
    range.layerCount = subresources.numArraySlices;
    return range;
}

Expected<VkImageViewCreateInfo> BuildTextureImageViewCreateInfo(Texture& texture, const TextureSubresourceSet& resolvedSubresources, const TextureDimension::Enum dimension, const Format::Enum format, TStringView operationName, const bool assertFailure){
    VkImageViewCreateInfo outViewInfo{};
    const bool usesTextureFormat = format == texture.m_creationDesc.format;
    const VkFormat vkFormat = usesTextureFormat ? texture.m_imageInfo.format : ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: format is unsupported"), operationName);
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create {}: format is unsupported"), operationName);
        return MakeUnexpected(Failure{});
    }

    if(!VulkanTextureDetail::ValidateTextureViewShape(dimension, resolvedSubresources)){
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create {}: invalid view shape"), operationName);
        return MakeUnexpected(Failure{});
    }

    const VkImageAspectFlags aspectMask = usesTextureFormat ? texture.m_aspectMask : GetImageAspectMask(GetFormatInfo(format));

    outViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    outViewInfo.image = texture.m_image;
    outViewInfo.viewType = VulkanTextureDetail::TextureDimensionToViewType(dimension);
    outViewInfo.format = vkFormat;
    outViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.subresourceRange = BuildImageSubresourceRange(resolvedSubresources, aspectMask);
    return outViewInfo;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ImageQueueFamilyVector = Vector<u32, Alloc::GlobalArena>;


[[nodiscard]] static ImageQueueFamilyVector CopyImageQueueFamilyIndices(const VulkanContext& context, const VkImageCreateInfo& imageInfo){
    ImageQueueFamilyVector result(context.objectArena);
    if(imageInfo.queueFamilyIndexCount == 0u)
        return result;

    NWB_ASSERT(imageInfo.pQueueFamilyIndices != nullptr);
    result.assign(imageInfo.pQueueFamilyIndices, imageInfo.pQueueFamilyIndices + imageInfo.queueFamilyIndexCount);
    return result;
}

[[nodiscard]] static VkImageCreateInfo RetainImageCreateInfo(const VkImageCreateInfo& imageInfo, const ImageQueueFamilyVector& queueFamilyIndices)noexcept{
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


Texture::Texture(const VulkanContext& context, VulkanAllocator& allocator, const TextureDesc& creationDesc, const VkImageCreateInfo& imageInfo, const bool initialStateKnown)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_desc(creationDesc)
    , m_creationDesc(creationDesc)
    , m_imageQueueFamilyIndices(__hidden_texture::CopyImageQueueFamilyIndices(context, imageInfo))
    , m_imageInfo(__hidden_texture::RetainImageCreateInfo(imageInfo, m_imageQueueFamilyIndices))
    , m_views(0, TextureViewKeyHasher(), EqualTo<TextureViewKey>(), context.objectArena)
    , m_retainedSubresourceStates(context.objectArena)
    , m_creationInitialStateKnown(initialStateKnown)
    , m_context(context)
    , m_allocator(allocator)
{
    const usize subresourceCount = static_cast<usize>(m_creationDesc.mipLevels) * static_cast<usize>(m_creationDesc.arraySize);
    const bool retainedInitialStateKnown = m_creationDesc.keepInitialState && initialStateKnown;
    m_retainedSubresourceStates.assign(subresourceCount, retainedInitialStateKnown ? 1u : 0u);
}

Texture::~Texture()noexcept{
    const VkImage registeredNativeImage = m_image;

    for(const auto& [_, view] : m_views)
        m_context.deviceDispatch.vkDestroyImageView(m_context.device, view, m_context.allocationCallbacks);
    m_views.clear();

    {
        ScopedLock bindingLock(m_memoryBindingMutex);
        if(m_boundHeap){
            auto& boundHeap = *MakeNotNull(m_boundHeap.get());
            {
                ScopedLock heapLock(boundHeap.m_bindingMutex);
                if(m_image != VK_NULL_HANDLE){
                    m_context.deviceDispatch.vkDestroyImage(m_context.device, m_image, m_context.allocationCallbacks);
                    m_image = VK_NULL_HANDLE;
                }
                boundHeap.eraseBindingReservationLocked(MakeNotNull<const void*>(this));
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

ResourceStates::Mask Texture::resolveTaskGraphImportInitialState()const noexcept{
    if(!m_creationDesc.keepInitialState){
        return m_managed || m_creationInitialStateKnown ? m_creationDesc.initialState : ResourceStates::Unknown;
    }

    const usize subresourceCount = static_cast<usize>(m_creationDesc.mipLevels) * static_cast<usize>(m_creationDesc.arraySize);
    NWB_ASSERT(subresourceCount != 0u);

    ScopedLock lock(m_retainedSubresourceStatesMutex);
    NWB_ASSERT(subresourceCount == m_retainedSubresourceStates.size());
    bool hasKnownState = false;
    bool hasUnknownState = false;
    for(usize subresourceIndex = 0u; subresourceIndex < subresourceCount; ++subresourceIndex){
        if(m_retainedSubresourceStates[subresourceIndex] != 0u)
            hasKnownState = true;
        else
            hasUnknownState = true;
    }
    return !hasUnknownState || (!hasKnownState && m_managed) ? m_creationDesc.initialState : ResourceStates::Unknown;
}

Object Texture::getNativeHandle(ObjectType objectType)noexcept{
    if(objectType == ObjectTypes::s_Image){
        ScopedLock bindingLock(m_memoryBindingMutex);
        return Object(m_image);
    }
    return Object(nullptr);
}

Expected<Object> Texture::getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool){
    if(objectType == ObjectTypes::s_ImageView){
        const auto view = getView(subresources, dimension, format);
        if(!view)
            return MakeUnexpected(view.error());
        return Object(*view);
    }
    if(objectType == ObjectTypes::s_Image)
        return getNativeHandle(objectType);
    return Object(nullptr);
}

Expected<VkImageView> Texture::getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format){
    if(dimension == TextureDimension::Unknown)
        dimension = m_creationDesc.dimension;

    if(format == Format::UNKNOWN)
        format = m_creationDesc.format;

    TextureSubresourceSet resolvedSubresources = subresources.resolve(m_creationDesc, TextureSubresourceMipResolve::Range);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view: invalid subresource range"));
        return MakeUnexpected(Failure{});
    }

    TextureViewKey key{ resolvedSubresources, dimension, format };

    ScopedLock lock(m_viewsMutex);
    if(m_image == VK_NULL_HANDLE)
        return MakeUnexpected(Failure{});
    auto it = m_views.find(key);
    if(it != m_views.end())
        return it.value();

    const auto viewInfo = VulkanDetail::BuildTextureImageViewCreateInfo(*this, resolvedSubresources, dimension, format, NWB_TEXT("image view"), true);
    if(!viewInfo)
        return MakeUnexpected(Failure{});

    VkImageView view = VK_NULL_HANDLE;
    const VkResult res = m_context.deviceDispatch.vkCreateImageView(m_context.device, &*viewInfo, m_context.allocationCallbacks, &view);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create image view"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    ScopeExit destroyView([&]()noexcept{ m_context.deviceDispatch.vkDestroyImageView(m_context.device, view, m_context.allocationCallbacks); });
    if(!m_views.emplace(key, view).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    destroyView.release();
    return view;
}

bool Texture::canRevokeUnmanagedNativeImage(const VkImage expectedNativeImage)noexcept{
    if(expectedNativeImage == VK_NULL_HANDLE)
        return false;

    NothrowScopedLock bindingLock(m_memoryBindingMutex);
    if(m_managed || m_image != expectedNativeImage)
        return false;

    return true;
}

bool Texture::prepareRevokeUnmanagedNativeImage(const VkImage expectedNativeImage)noexcept{
    if(expectedNativeImage == VK_NULL_HANDLE)
        return false;

    NothrowScopedLock bindingLock(m_memoryBindingMutex);
    if(m_managed || m_image != expectedNativeImage)
        return false;
    if(m_preparedRevokedNativeImage == expectedNativeImage)
        return true;
    if(!m_allocator.isTextureNativeIdentityRegistered(expectedNativeImage, *this))
        return false;

    m_preparedRevokedNativeImage = expectedNativeImage;
    return true;
}

void Texture::commitRevokeUnmanagedNativeImage(const VkImage expectedNativeImage)noexcept{
    NothrowScopedLock bindingLock(m_memoryBindingMutex);
    if(expectedNativeImage == VK_NULL_HANDLE || m_managed || m_image != expectedNativeImage || m_preparedRevokedNativeImage != expectedNativeImage)
        TerminateInvariant();

    NothrowScopedLock viewsLock(m_viewsMutex);
    static_assert(noexcept(m_views.clear()), "prepared swapchain view release must remain non-throwing");
    for(const auto& [_, view] : m_views)
        m_context.deviceDispatch.vkDestroyImageView(m_context.device, view, m_context.allocationCallbacks);
    m_views.clear();
    m_image = VK_NULL_HANDLE;
}

void Texture::releasePreparedRevokeUnmanagedNativeImageIdentity(const VkImage expectedNativeImage)noexcept{
    NothrowScopedLock bindingLock(m_memoryBindingMutex);
    const bool identityMatches = expectedNativeImage != VK_NULL_HANDLE && !m_managed && m_image == VK_NULL_HANDLE && m_preparedRevokedNativeImage == expectedNativeImage;
    NWB_FATAL_ASSERT_MSG(identityMatches, "prepared swapchain Texture identity must outlive its native VkImage");

    m_allocator.unregisterTextureNativeIdentity(expectedNativeImage, *this);
    m_preparedRevokedNativeImage = VK_NULL_HANDLE;
}

bool Texture::isRetainedSubresourceStateKnown(const ArraySlice arraySlice, const MipLevel mipLevel)noexcept{
    if(!m_creationDesc.keepInitialState || arraySlice >= m_creationDesc.arraySize || mipLevel >= m_creationDesc.mipLevels)
        return false;

    const usize index = static_cast<usize>(arraySlice) * static_cast<usize>(m_creationDesc.mipLevels) + static_cast<usize>(mipLevel);
    NothrowScopedLock lock(m_retainedSubresourceStatesMutex);
    NWB_ASSERT(index < m_retainedSubresourceStates.size());
    return m_retainedSubresourceStates[index] != 0u;
}

void Texture::setRetainedSubresourceStateKnown(const ArraySlice arraySlice, const MipLevel mipLevel, const bool known)noexcept{
    if(!m_creationDesc.keepInitialState || arraySlice >= m_creationDesc.arraySize || mipLevel >= m_creationDesc.mipLevels)
        return;

    const usize index = static_cast<usize>(arraySlice) * static_cast<usize>(m_creationDesc.mipLevels) + static_cast<usize>(mipLevel);
    NothrowScopedLock lock(m_retainedSubresourceStatesMutex);
    NWB_ASSERT(index < m_retainedSubresourceStates.size());
    m_retainedSubresourceStates[index] = known ? 1u : 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTexture::StagingTexture(const VulkanContext& context, VulkanAllocator& allocator)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_mipLayouts(context.objectArena)
    , m_admittedQueueFamilies(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}

StagingTexture::~StagingTexture()noexcept{
    ScopedLock lock(m_mappingMutex);

    m_allocator.destroyStagingTexture(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

