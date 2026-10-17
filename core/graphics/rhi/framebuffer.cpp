// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "framebuffer.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_framebuffer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FramebufferAttachmentExtent{
    u32 width = 0u;
    u32 height = 0u;
    u32 arraySize = 0u;
};

[[nodiscard]] Expected<FramebufferAttachmentExtent> ResolveFramebufferAttachmentExtent(const FramebufferAttachment& attachment)noexcept{
    const TextureDesc& textureDesc = attachment.texture->getCreationDescription();
    const TextureSubresourceSet subresources = attachment.subresources.resolve(textureDesc, TextureSubresourceMipResolve::Single);
    if(subresources.numMipLevels == 0u || subresources.numArraySlices == 0u)
        return MakeUnexpected(Failure{});
    return FramebufferAttachmentExtent{ .width = Max(textureDesc.width >> subresources.baseMipLevel, static_cast<u32>(1)), .height = Max(textureDesc.height >> subresources.baseMipLevel, static_cast<u32>(1)), .arraySize = subresources.numArraySlices, };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FramebufferInfo::FramebufferInfo(const FramebufferDesc& desc)noexcept{
    const usize colorAttachmentCount = desc.colorAttachments.size();
    for(usize i = 0u; i < colorAttachmentCount; ++i){
        const FramebufferAttachment& attachment = desc.colorAttachments[i];
        const Format::Enum attachmentFormat = attachment.format == Format::UNKNOWN && attachment.texture ? attachment.texture->getCreationDescription().format : attachment.format;
        colorFormats.push_back(attachmentFormat);
    }

    if(desc.depthAttachment.valid()){
        const TextureDesc& textureDesc = desc.depthAttachment.texture->getCreationDescription();
        depthFormat = textureDesc.format;
        sampleCount = textureDesc.sampleCount;
        sampleQuality = textureDesc.sampleQuality;
    }
    else if(!desc.colorAttachments.empty() && desc.colorAttachments[0].valid()){
        const TextureDesc& textureDesc = desc.colorAttachments[0].texture->getCreationDescription();
        sampleCount = textureDesc.sampleCount;
        sampleQuality = textureDesc.sampleQuality;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FramebufferInfoEx::FramebufferInfoEx(const FramebufferDesc& desc)noexcept
    : FramebufferInfo(desc)
{
    const FramebufferAttachment* attachment = nullptr;
    if(desc.depthAttachment.valid())
        attachment = &desc.depthAttachment;
    else if(!desc.colorAttachments.empty() && desc.colorAttachments[0].valid())
        attachment = &desc.colorAttachments[0];
    if(!attachment)
        return;
    const auto extent = __hidden_graphics_framebuffer::ResolveFramebufferAttachmentExtent(*attachment);
    if(!extent){
        width = 0u;
        height = 0u;
        arraySize = 0u;
        return;
    }
    width = extent->width;
    height = extent->height;
    arraySize = extent->arraySize;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

