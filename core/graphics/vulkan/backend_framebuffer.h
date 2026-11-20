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


private:
    FramebufferDesc m_desc;
    FramebufferInfoEx m_framebufferInfo;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    Array<CachedAttachmentView, s_MaxRenderTargets> m_cachedColorViews{};
    CachedAttachmentView m_cachedDepthView{};

    Vector<TextureHandle, Alloc::GlobalArena> m_resources;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

