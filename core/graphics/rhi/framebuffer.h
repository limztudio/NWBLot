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
    bool isReadOnly = false;

    constexpr FramebufferAttachment& setTexture(Texture* t){ texture = t; return *this; }
    constexpr FramebufferAttachment& setSubresources(TextureSubresourceSet value){ subresources = value; return *this; }
    constexpr FramebufferAttachment& setArraySlice(ArraySlice index){ subresources.baseArraySlice = index; subresources.numArraySlices = 1; return *this; }
    constexpr FramebufferAttachment& setArraySliceRange(ArraySlice index, ArraySlice count){ subresources.baseArraySlice = index; subresources.numArraySlices = count; return *this; }
    constexpr FramebufferAttachment& setMipLevel(MipLevel level){ subresources.baseMipLevel = level; subresources.numMipLevels = 1; return *this; }
    constexpr FramebufferAttachment& setFormat(Format::Enum f){ format = f; return *this; }
    constexpr FramebufferAttachment& setReadOnly(bool val){ isReadOnly = val; return *this; }

    [[nodiscard]] constexpr bool valid()const{ return texture != nullptr; }
};

struct FramebufferDesc{
    FixedVector<FramebufferAttachment, s_MaxRenderTargets> colorAttachments;
    FramebufferAttachment depthAttachment;
    FramebufferAttachment shadingRateAttachment;

    constexpr FramebufferDesc& addColorAttachment(const FramebufferAttachment& a){ colorAttachments.push_back(a); return *this; }
    constexpr FramebufferDesc& addColorAttachment(Texture* texture){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture)); return *this; }
    constexpr FramebufferDesc& addColorAttachment(Texture* texture, TextureSubresourceSet subresources){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture).setSubresources(subresources)); return *this; }
    constexpr FramebufferDesc& setDepthAttachment(const FramebufferAttachment& d){ depthAttachment = d; return *this; }
    constexpr FramebufferDesc& setDepthAttachment(Texture* texture){ depthAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    constexpr FramebufferDesc& setDepthAttachment(Texture* texture, TextureSubresourceSet subresources){ depthAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(const FramebufferAttachment& d){ shadingRateAttachment = d; return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(Texture* texture){ shadingRateAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(Texture* texture, TextureSubresourceSet subresources){ shadingRateAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
};

struct FramebufferInfo{
    FixedVector<Format::Enum, s_MaxRenderTargets> colorFormats;
    Format::Enum depthFormat = Format::UNKNOWN;
    u32 sampleCount = 1;
    u32 sampleQuality = 0;

    FramebufferInfo() = default;
    FramebufferInfo(const FramebufferDesc& desc);

    constexpr FramebufferInfo& addColorFormat(Format::Enum format){ colorFormats.push_back(format); return *this; }
    constexpr FramebufferInfo& setDepthFormat(Format::Enum format){ depthFormat = format; return *this; }
    constexpr FramebufferInfo& setSampleCount(u32 count){ sampleCount = count; return *this; }
    constexpr FramebufferInfo& setSampleQuality(u32 quality){ sampleQuality = quality; return *this; }
};
inline bool operator==(const FramebufferInfo& lhs, const FramebufferInfo& rhs){
    if(lhs.sampleQuality != rhs.sampleQuality)
        return false;
    if(lhs.sampleCount != rhs.sampleCount)
        return false;
    if(lhs.depthFormat != rhs.depthFormat)
        return false;
    if(lhs.colorFormats.size() != rhs.colorFormats.size())
        return false;
    for(usize i = 0; i < lhs.colorFormats.size(); ++i){
        if(lhs.colorFormats[i] != rhs.colorFormats[i])
            return false;
    }
    return true;
}
inline bool operator!=(const FramebufferInfo& lhs, const FramebufferInfo& rhs){ return !(lhs == rhs); }

struct FramebufferInfoEx : FramebufferInfo{
    u32 width = 0;
    u32 height = 0;
    u32 arraySize = 1;

    FramebufferInfoEx() = default;
    FramebufferInfoEx(const FramebufferDesc& desc);

    constexpr FramebufferInfoEx& setWidth(u32 value){ width = value; return *this; }
    constexpr FramebufferInfoEx& setHeight(u32 value){ height = value; return *this; }
    constexpr FramebufferInfoEx& setArraySize(u32 value){ arraySize = value; return *this; }

    [[nodiscard]] constexpr Viewport getViewport(f32 minZ = 0.f, f32 maxZ = 1.f)const{ return Viewport(0, static_cast<f32>(width), 0, static_cast<f32>(height), minZ, maxZ); }
};

typedef GraphicsBackend::Handle<Framebuffer> FramebufferHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

