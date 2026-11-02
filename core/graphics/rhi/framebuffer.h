// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "pipeline_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FramebufferAttachment{
    Texture* texture = nullptr;
    TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, 1);
    Format::Enum format = Format::UNKNOWN;
    b8 isReadOnly = false;


public:
    constexpr FramebufferAttachment& setTexture(Texture* t)noexcept{ texture = t; return *this; }

    constexpr FramebufferAttachment& setSubresources(TextureSubresourceSet value)noexcept{ subresources = value; return *this; }

    constexpr FramebufferAttachment& setArraySlice(ArraySlice index)noexcept{ subresources.baseArraySlice = index; subresources.numArraySlices = 1; return *this; }

    constexpr FramebufferAttachment& setMipLevel(MipLevel level)noexcept{ subresources.baseMipLevel = level; subresources.numMipLevels = 1; return *this; }

    constexpr FramebufferAttachment& setFormat(Format::Enum f)noexcept{ format = f; return *this; }

    constexpr FramebufferAttachment& setIsReadOnly(b8 val)noexcept{ isReadOnly = val; return *this; }

    [[nodiscard]] constexpr b8 valid()const noexcept{ return texture != nullptr; }
};

struct FramebufferDesc{
    FixedVector<FramebufferAttachment, s_MaxRenderTargets> colorAttachments;
    FramebufferAttachment depthAttachment;
    FramebufferAttachment shadingRateAttachment;


public:
    constexpr FramebufferDesc& addColorAttachment(const FramebufferAttachment& a){ colorAttachments.push_back(a); return *this; }

    constexpr FramebufferDesc& addColorAttachment(Texture* texture){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture)); return *this; }

    constexpr FramebufferDesc& addColorAttachment(Texture* texture, TextureSubresourceSet subresources){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture).setSubresources(subresources)); return *this; }

    constexpr FramebufferDesc& setDepthAttachment(const FramebufferAttachment& d)noexcept{ depthAttachment = d; return *this; }

    constexpr FramebufferDesc& setDepthAttachment(Texture* texture)noexcept{ depthAttachment = FramebufferAttachment().setTexture(texture); return *this; }

    constexpr FramebufferDesc& setDepthAttachment(Texture* texture, TextureSubresourceSet subresources)noexcept{ depthAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }

};

struct FramebufferInfo{
    FixedVector<Format::Enum, s_MaxRenderTargets> colorFormats;
    u32 sampleCount = 1;
    u32 sampleQuality = 0;
    Format::Enum depthFormat = Format::UNKNOWN;


public:
    FramebufferInfo()noexcept = default;
    FramebufferInfo(const FramebufferDesc& desc)noexcept;


public:
    constexpr FramebufferInfo& addColorFormat(Format::Enum format){ colorFormats.push_back(format); return *this; }

    constexpr FramebufferInfo& setDepthFormat(Format::Enum format)noexcept{ depthFormat = format; return *this; }

};
[[nodiscard]] b8 operator==(const FramebufferInfo& lhs, const FramebufferInfo& rhs)noexcept;

inline b8 operator!=(const FramebufferInfo& lhs, const FramebufferInfo& rhs)noexcept{ return !(lhs == rhs); }

struct FramebufferInfoEx : FramebufferInfo{
    u32 width = 0;
    u32 height = 0;
    u32 arraySize = 1;


public:
    FramebufferInfoEx()noexcept = default;
    FramebufferInfoEx(const FramebufferDesc& desc)noexcept;


public:
    constexpr FramebufferInfoEx& setWidth(u32 value)noexcept{ width = value; return *this; }

    constexpr FramebufferInfoEx& setHeight(u32 value)noexcept{ height = value; return *this; }

    constexpr FramebufferInfoEx& setArraySize(u32 value)noexcept{ arraySize = value; return *this; }

    [[nodiscard]] constexpr Viewport getViewport(f32 minZ = 0.f, f32 maxZ = 1.f)const noexcept{ return Viewport(0, static_cast<f32>(width), 0, static_cast<f32>(height), minZ, maxZ); }
};

typedef GraphicsBackend::Handle<Framebuffer> FramebufferHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

