// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_framebuffer.h"

#include "backend.h"
#include "command_validation.h"
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

    return fb;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

