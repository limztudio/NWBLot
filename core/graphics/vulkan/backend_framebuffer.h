// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_forward.h"

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Framebuffer final : public RefCounter<GraphicsResource>{
    friend class Device;
    friend class CommandList;
    friend class CommandBufferResourceReferences;
    friend class FramebufferRenderingPreparation;


public:
    Framebuffer(const VulkanContext& context)noexcept;
    ~Framebuffer()noexcept;


public:
    [[nodiscard]] const FramebufferDesc& getDescription()const noexcept{ return m_desc; }

    [[nodiscard]] const FramebufferInfoEx& getFramebufferInfo()const noexcept{ return m_framebufferInfo; }


private:
    struct CachedAttachmentView{
        Atomic<VkImageView> view{ VK_NULL_HANDLE };
    };


private:
    [[nodiscard]] Expected<VkImageView> resolveAttachmentView(const FramebufferAttachment& attachment, CachedAttachmentView& cached);
    [[nodiscard]] Expected<VkImageView> resolveColorAttachmentView(u32 index);
    [[nodiscard]] Expected<VkImageView> resolveDepthAttachmentView();
    void cacheStaticValidationState(const VulkanAllocator& allocator)noexcept;


private:
    FramebufferDesc m_desc;
    FramebufferInfoEx m_framebufferInfo;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    Array<CachedAttachmentView, s_MaxRenderTargets> m_cachedColorViews{};
    CachedAttachmentView m_cachedDepthView{};
    Array<TextureSubresourceSet, s_MaxRenderTargets> m_cachedColorSubresources{};
    TextureSubresourceSet m_cachedDepthSubresources{};
    Array<VkRenderingAttachmentInfo, s_MaxRenderTargets> m_cachedDefaultColorAttachments{};
    VkRenderingAttachmentInfo m_cachedDefaultDepthAttachment{};
    VkRenderingAttachmentInfo m_cachedDefaultStencilAttachment{};
    VkRenderingInfo m_cachedDefaultRenderingInfo{};
    Atomic<u8> m_defaultRenderingCacheState{ 0u };
    Atomic<b8> m_allViewsCached{ false };
    b8 m_staticStructureValid = false;
    b8 m_allAttachmentsManagedNonVirtual = false;
    b8 m_allAttachmentsExclusiveSharing = false;

    Vector<TextureHandle, Alloc::GlobalArena> m_resources;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

