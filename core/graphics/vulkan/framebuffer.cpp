// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_framebuffer.h"

#include "backend.h"
#include "command_validation.h"
#include "texture_image_policy.h"
#include "texture_resource_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Framebuffer::Framebuffer(const VulkanContext& context)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_resources(context.objectArena)
    , m_context(context)
{}

Expected<VkImageView> Framebuffer::resolveAttachmentView(const FramebufferAttachment& attachment, CachedAttachmentView& cached){
    static_assert(Atomic<VkImageView>::is_always_lock_free, "Cached VkImageView atomic must remain lock-free");

    if(!attachment.texture)
        return MakeUnexpected(Failure{});
    Texture& tex = *attachment.texture;
    const b8 canUseCache = tex.m_managed || tex.m_gpuReady.load(MemoryOrder::acquire);
    if(canUseCache){
        const VkImageView cachedView = cached.view.load(MemoryOrder::acquire);
        if(cachedView != VK_NULL_HANDLE)
            return cachedView;
    }

    const TextureSubresourceSet resolvedSubresources = attachment.subresources.resolve(tex.m_creationDesc, TextureSubresourceMipResolve::Range);
    const TextureDimension::Enum viewDimension = VulkanDetail::GetFramebufferAttachmentViewDimension(tex.m_creationDesc, resolvedSubresources);
    const auto view = tex.getView(attachment.subresources, viewDimension, attachment.format);
    if(!view)
        return MakeUnexpected(view.error());

    if(tex.m_managed || tex.m_gpuReady.load(MemoryOrder::acquire))
        cached.view.store(*view, MemoryOrder::release);
    return *view;
}

Expected<VkImageView> Framebuffer::resolveColorAttachmentView(const u32 index){
    if(index >= m_desc.colorAttachments.size() || index >= m_cachedColorViews.size())
        return MakeUnexpected(Failure{});
    return resolveAttachmentView(m_desc.colorAttachments[index], m_cachedColorViews[index]);
}

Expected<VkImageView> Framebuffer::resolveDepthAttachmentView(){
    return resolveAttachmentView(m_desc.depthAttachment, m_cachedDepthView);
}

void Framebuffer::cacheStaticValidationState(const VulkanAllocator& allocator)noexcept{
    m_staticStructureValid = false;
    m_allAttachmentsManagedNonVirtual = false;
    m_allAttachmentsExclusiveSharing = false;

    if(m_framebufferInfo.width == 0u || m_framebufferInfo.height == 0u || m_framebufferInfo.arraySize == 0u)
        return;

    const VkPhysicalDeviceLimits& limits = m_context.physicalDeviceProperties.limits;
    if(m_framebufferInfo.width > limits.maxFramebufferWidth || m_framebufferInfo.height > limits.maxFramebufferHeight || m_framebufferInfo.arraySize > limits.maxFramebufferLayers)
        return;
    if(m_desc.shadingRateAttachment.valid())
        return;
    if(!m_desc.depthAttachment.valid() && (m_desc.depthAttachment.isReadOnly || m_desc.depthAttachment.format != Format::UNKNOWN || m_desc.depthAttachment.subresources != FramebufferAttachment{}.subresources))
        return;
    if(m_desc.colorAttachments.size() > s_MaxRenderTargets || m_desc.colorAttachments.size() > limits.maxColorAttachments)
        return;
    if(m_desc.colorAttachments.empty() && !m_desc.depthAttachment.valid())
        return;

    b8 allManagedNonVirtual = true;
    b8 allExclusiveSharing = true;

    const auto validateAttachmentStatic = [this, &allocator, &allManagedNonVirtual, &allExclusiveSharing](
        const FramebufferAttachment& attachment,
        const b8 requireDepthStencilFormat,
        const Format::Enum expectedFormat,
        TextureSubresourceSet& outResolved
    )noexcept -> b8{
        Texture* const texture = attachment.texture;
        if(!texture || &texture->m_context != &m_context || &texture->m_allocator != &allocator)
            return false;
        if(!texture->m_creationDesc.isRenderTarget)
            return false;
        if(!requireDepthStencilFormat && attachment.isReadOnly)
            return false;
        if(!VulkanDetail::IsFramebufferAttachmentSubresourceSetValid(texture->m_creationDesc, attachment.subresources))
            return false;

        const TextureSubresourceSet resolved = attachment.subresources.resolve(texture->m_creationDesc, TextureSubresourceMipResolve::Range);
        if(resolved.numMipLevels != 1u || resolved.numArraySlices == 0u)
            return false;

        const Format::Enum format = attachment.format == Format::UNKNOWN ? texture->m_creationDesc.format : attachment.format;
        if(format != texture->m_creationDesc.format)
            return false;
        if(VulkanDetail::ConvertFormat(format) == VK_FORMAT_UNDEFINED || texture->m_imageInfo.format == VK_FORMAT_UNDEFINED)
            return false;

        const FormatInfo& formatInfo = GetFormatInfo(format);
        const b8 depthStencilFormat = formatInfo.hasDepth || formatInfo.hasStencil;
        const b8 depthStencilImage = (texture->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u;
        if(depthStencilFormat != requireDepthStencilFormat || depthStencilImage != requireDepthStencilFormat)
            return false;
        if(expectedFormat != Format::UNKNOWN && format != expectedFormat)
            return false;

        const u32 width = Max(texture->m_creationDesc.width >> resolved.baseMipLevel, 1u);
        const u32 height = Max(texture->m_creationDesc.height >> resolved.baseMipLevel, 1u);
        if(
            width != m_framebufferInfo.width
            || height != m_framebufferInfo.height
            || resolved.numArraySlices != m_framebufferInfo.arraySize
            || texture->m_creationDesc.sampleCount != m_framebufferInfo.sampleCount
            || texture->m_creationDesc.sampleQuality != m_framebufferInfo.sampleQuality
        )
            return false;

        const TextureDimension::Enum viewDimension = VulkanDetail::GetFramebufferAttachmentViewDimension(texture->m_creationDesc, resolved);
        if(viewDimension == TextureDimension::Unknown)
            return false;
        if(viewDimension == TextureDimension::TextureCube && resolved.numArraySlices != VulkanTextureDetail::s_TextureCubeLayerCount)
            return false;
        if(viewDimension == TextureDimension::TextureCubeArray && (resolved.numArraySlices < VulkanTextureDetail::s_TextureCubeLayerCount || (resolved.numArraySlices % VulkanTextureDetail::s_TextureCubeLayerCount) != 0u))
            return false;

        if(!texture->m_managed || texture->m_creationDesc.isVirtual || !texture->m_gpuReady.load(MemoryOrder::acquire))
            allManagedNonVirtual = false;
        const ResourceQueueAdmissionSnapshot admission = texture->getQueueAdmissionSnapshot();
        if(!admission.valid() || admission.usesConcurrentSharing)
            allExclusiveSharing = false;
        outResolved = resolved;
        return true;
    };

    for(usize colorIndex = 0u; colorIndex < m_desc.colorAttachments.size(); ++colorIndex){
        if(!validateAttachmentStatic(m_desc.colorAttachments[colorIndex], false, m_framebufferInfo.colorFormats[colorIndex], m_cachedColorSubresources[colorIndex]))
            return;
    }

    if(m_desc.depthAttachment.valid()){
        if(!validateAttachmentStatic(m_desc.depthAttachment, true, m_framebufferInfo.depthFormat, m_cachedDepthSubresources))
            return;
    }

    for(usize colorIndex = 0u; colorIndex < m_desc.colorAttachments.size(); ++colorIndex){
        const FramebufferAttachment& colorAttachment = m_desc.colorAttachments[colorIndex];
        const TextureSubresourceSet& colorRange = m_cachedColorSubresources[colorIndex];
        for(usize priorIndex = 0u; priorIndex < colorIndex; ++priorIndex){
            if(m_desc.colorAttachments[priorIndex].texture != colorAttachment.texture)
                continue;
            if(VulkanDetail::TextureSubresourceRangesOverlap(colorRange, m_cachedColorSubresources[priorIndex]))
                return;
        }
        if(m_desc.depthAttachment.texture == colorAttachment.texture && VulkanDetail::TextureSubresourceRangesOverlap(colorRange, m_cachedDepthSubresources))
            return;
    }

    m_staticStructureValid = true;
    m_allAttachmentsManagedNonVirtual = allManagedNonVirtual;
    m_allAttachmentsExclusiveSharing = allExclusiveSharing;
}

Framebuffer::~Framebuffer()noexcept{
    if(m_framebuffer != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyFramebuffer(m_context.device, m_framebuffer, m_context.allocationCallbacks);
        m_framebuffer = VK_NULL_HANDLE;
    }

    if(m_renderPass != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyRenderPass(m_context.device, m_renderPass, m_context.allocationCallbacks);
        m_renderPass = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FramebufferHandle> Device::createFramebuffer(const FramebufferDesc& desc){
    for(u32 i = 0u; i < static_cast<u32>(desc.colorAttachments.size()); ++i){
        Texture* const texture = desc.colorAttachments[i].texture;
        if(texture && &texture->m_context != &m_context){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create framebuffer: color attachment {} belongs to another device."), i);
            return MakeUnexpected(Failure{});
        }
    }
    if(desc.depthAttachment.texture && &desc.depthAttachment.texture->m_context != &m_context){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create framebuffer: depth attachment belongs to another device."));
        return MakeUnexpected(Failure{});
    }
    if(desc.shadingRateAttachment.texture && &desc.shadingRateAttachment.texture->m_context != &m_context){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create framebuffer: shading-rate attachment belongs to another device."));
        return MakeUnexpected(Failure{});
    }

    FramebufferHandle fb(NewArenaObject<Framebuffer>(m_context.objectArena, m_context), FramebufferHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!fb)
        return MakeUnexpected(Failure{});
    fb->m_desc = desc;
    fb->m_framebufferInfo = FramebufferInfoEx(desc);

    const u32 colorAttachmentCount = Min<u32>(static_cast<u32>(desc.colorAttachments.size()), s_MaxRenderTargets);
    if(desc.colorAttachments.size() > s_MaxRenderTargets)
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Framebuffer has more than {} color attachments; truncating to {}."), s_MaxRenderTargets, s_MaxRenderTargets);

    fb->m_resources.reserve(static_cast<usize>(colorAttachmentCount) + (desc.depthAttachment.texture ? 1u : 0u) + (desc.shadingRateAttachment.texture ? 1u : 0u));
    for(u32 i = 0u; i < colorAttachmentCount; ++i){
        if(desc.colorAttachments[i].texture)
            fb->m_resources.emplace_back(desc.colorAttachments[i].texture, TextureHandle::deleter_type(&m_context.objectArena));
    }

    if(desc.depthAttachment.texture)
        fb->m_resources.emplace_back(desc.depthAttachment.texture, TextureHandle::deleter_type(&m_context.objectArena));
    if(desc.shadingRateAttachment.texture)
        fb->m_resources.emplace_back(desc.shadingRateAttachment.texture, TextureHandle::deleter_type(&m_context.objectArena));

    fb->cacheStaticValidationState(m_allocator);
    return fb;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

