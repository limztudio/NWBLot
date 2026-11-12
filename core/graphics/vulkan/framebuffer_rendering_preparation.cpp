// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "framebuffer_rendering_preparation.h"
#include "command_validation.h"
#include "backend.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


b8 FramebufferRenderingPreparation::validateFramebuffer(Framebuffer* const framebuffer, const TStringView operationName){
    if(!framebuffer)
        return true;
    if(&framebuffer->m_context != &m_commandList.m_context){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer belongs to another device"));
        return false;
    }

    const FramebufferDesc& framebufferDesc = framebuffer->m_desc;
    const FramebufferInfoEx& framebufferInfo = framebuffer->m_framebufferInfo;
    if(framebufferInfo.width == 0u || framebufferInfo.height == 0u || framebufferInfo.arraySize == 0u){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer dimensions are invalid"));
        return false;
    }
    const VkPhysicalDeviceLimits& limits = m_commandList.m_context.physicalDeviceProperties.limits;
    if(framebufferInfo.width > limits.maxFramebufferWidth || framebufferInfo.height > limits.maxFramebufferHeight || framebufferInfo.arraySize > limits.maxFramebufferLayers){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer dimensions exceed Vulkan device limits"));
        return false;
    }
    if(framebufferDesc.shadingRateAttachment.valid()){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("shading-rate framebuffer attachments are not implemented"));
        return false;
    }
    if(!framebufferDesc.depthAttachment.valid() && (framebufferDesc.depthAttachment.isReadOnly || framebufferDesc.depthAttachment.format != Format::UNKNOWN || framebufferDesc.depthAttachment.subresources != FramebufferAttachment{}.subresources)){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("depth/stencil metadata has no attachment texture"));
        return false;
    }
    if(framebufferDesc.colorAttachments.size() > s_MaxRenderTargets || framebufferDesc.colorAttachments.size() > limits.maxColorAttachments){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer color count exceeds the render-target limit"));
        return false;
    }
    if(framebufferDesc.colorAttachments.empty() && !framebufferDesc.depthAttachment.valid()){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer has no color or depth/stencil attachment"));
        return false;
    }

    const auto validateAttachment = [this, operationName, &framebufferInfo](
        const FramebufferAttachment& attachment,
        const ResourceStates::Mask requiredState,
        const b8 requireRenderTargetUsage,
        const b8 requireShadingRateUsage,
        const b8 requireDepthStencilFormat,
        const b8 requireFramebufferExtent,
        const Format::Enum expectedFormat
    ) -> b8{
        Texture* const texture = attachment.texture;
        if(!m_commandList.isTextureReadyForCommandQueue(texture)){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment is not ready for this exact command queue"));
            return false;
        }
        if(requireRenderTargetUsage && !texture->m_creationDesc.isRenderTarget){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment lacks render-target usage"));
            return false;
        }
        if(requireShadingRateUsage && !texture->m_creationDesc.isShadingRateSurface){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment lacks shading-rate usage"));
            return false;
        }
        if(!requireDepthStencilFormat && attachment.isReadOnly){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("only depth/stencil attachments may be read-only"));
            return false;
        }
        if(!VulkanDetail::IsFramebufferAttachmentSubresourceSetValid(texture->m_creationDesc, attachment.subresources)){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment subresource metadata is invalid"));
            return false;
        }

        const TextureSubresourceSet resolved = attachment.subresources.resolve(texture->m_creationDesc, TextureSubresourceMipResolve::Range);
        if(resolved.numMipLevels != 1u || resolved.numArraySlices == 0u){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment subresource range is invalid"));
            return false;
        }

        const Format::Enum format = attachment.format == Format::UNKNOWN ? texture->m_creationDesc.format : attachment.format;
        if(format != texture->m_creationDesc.format){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment format overrides are unsupported"));
            return false;
        }
        if(VulkanDetail::ConvertFormat(format) == VK_FORMAT_UNDEFINED){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment format is unsupported"));
            return false;
        }
        const FormatInfo& formatInfo = GetFormatInfo(format);
        const b8 depthStencilFormat = formatInfo.hasDepth || formatInfo.hasStencil;
        const b8 depthStencilImage = (texture->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u;
        if(depthStencilFormat != requireDepthStencilFormat || depthStencilImage != requireDepthStencilFormat){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment format class is incompatible"));
            return false;
        }
        if(expectedFormat != Format::UNKNOWN && format != expectedFormat){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment format metadata is inconsistent"));
            return false;
        }

        if(requireFramebufferExtent){
            const u32 width = Max(texture->m_creationDesc.width >> resolved.baseMipLevel, 1u);
            const u32 height = Max(texture->m_creationDesc.height >> resolved.baseMipLevel, 1u);
            if(
                width != framebufferInfo.width
                || height != framebufferInfo.height
                || resolved.numArraySlices != framebufferInfo.arraySize
                || texture->m_creationDesc.sampleCount != framebufferInfo.sampleCount
                || texture->m_creationDesc.sampleQuality != framebufferInfo.sampleQuality
            ){
                m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment extent or sampling is inconsistent"));
                return false;
            }
        }

        const TextureDimension::Enum viewDimension = VulkanDetail::GetFramebufferAttachmentViewDimension(texture->m_creationDesc, resolved);
        if(viewDimension == TextureDimension::Unknown){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment texture dimension is unsupported"));
            return false;
        }
        const auto viewInfo = VulkanDetail::BuildTextureImageViewCreateInfo(*texture, resolved, viewDimension, format, FOUNDATION_TEXT("framebuffer attachment image view"), false);
        if(!viewInfo){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment view description is invalid"));
            return false;
        }
        const ResourceStates::Mask permanentState = m_commandList.getPermanentTextureState(texture);
        if(permanentState != ResourceStates::Unknown && permanentState != requiredState){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment permanent state is incompatible"));
            return false;
        }
        return true;
    };

    for(usize colorIndex = 0u; colorIndex < framebufferDesc.colorAttachments.size(); ++colorIndex){
        if(!validateAttachment(framebufferDesc.colorAttachments[colorIndex], ResourceStates::RenderTarget, true, false, false, true, framebufferInfo.colorFormats[colorIndex]))
            return false;
    }

    if(framebufferDesc.depthAttachment.valid()){
        if(!validateAttachment(framebufferDesc.depthAttachment, framebufferDesc.depthAttachment.isReadOnly ? ResourceStates::DepthRead : ResourceStates::DepthWrite, true, false, true, true, framebufferInfo.depthFormat))
            return false;
    }

    for(usize colorIndex = 0u; colorIndex < framebufferDesc.colorAttachments.size(); ++colorIndex){
        const FramebufferAttachment& colorAttachment = framebufferDesc.colorAttachments[colorIndex];
        const TextureSubresourceSet colorRange = colorAttachment.subresources.resolve(colorAttachment.texture->m_creationDesc, TextureSubresourceMipResolve::Range);
        for(usize priorIndex = 0u; priorIndex < colorIndex; ++priorIndex){
            const FramebufferAttachment& priorAttachment = framebufferDesc.colorAttachments[priorIndex];
            if(priorAttachment.texture != colorAttachment.texture)
                continue;
            const TextureSubresourceSet priorRange = priorAttachment.subresources.resolve(priorAttachment.texture->m_creationDesc, TextureSubresourceMipResolve::Range);
            if(VulkanDetail::TextureSubresourceRangesOverlap(colorRange, priorRange)){
                m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("color attachments overlap the same image subresources"));
                return false;
            }
        }
        if(framebufferDesc.depthAttachment.texture == colorAttachment.texture){
            const TextureSubresourceSet depthRange = framebufferDesc.depthAttachment.subresources.resolve(framebufferDesc.depthAttachment.texture->m_creationDesc, TextureSubresourceMipResolve::Range);
            if(VulkanDetail::TextureSubresourceRangesOverlap(colorRange, depthRange)){
                m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("color and depth/stencil attachments overlap the same image subresources"));
                return false;
            }
        }
    }

    return true;
}

b8 FramebufferRenderingPreparation::validateRenderPassBegin(Framebuffer& framebuffer, const RenderPassParameters& params, const TStringView operationName){
    if(m_commandList.m_renderPassActive){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("a render pass is already active"));
        return false;
    }
    if(!validateFramebuffer(&framebuffer, operationName))
        return false;

    const FramebufferDesc& framebufferDesc = framebuffer.m_desc;
    const u32 colorAttachmentCount = static_cast<u32>(framebufferDesc.colorAttachments.size());
    for(u32 colorIndex = 0u; colorIndex < s_MaxRenderTargets; ++colorIndex){
        const RenderPassAttachmentActions& actions = params.colorAttachmentActions[colorIndex];
        if(!VulkanDetail::IsRenderPassAttachmentActionsValid(actions)){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("color attachment actions are invalid"));
            return false;
        }
        if(colorIndex >= colorAttachmentCount && (actions.loadAction != RenderPassLoadAction::Load || actions.storeAction != RenderPassStoreAction::Store)){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("color attachment actions select no framebuffer attachment"));
            return false;
        }
        if(colorIndex >= colorAttachmentCount || actions.loadAction != RenderPassLoadAction::Clear)
            continue;

        const FramebufferAttachment& attachment = framebufferDesc.colorAttachments[colorIndex];
        const Format::Enum format = attachment.format == Format::UNKNOWN ? attachment.texture->m_creationDesc.format : attachment.format;
        if(GetFormatInfo(format).kind == FormatKind::Integer){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("float color clear value targets an integer attachment"));
            return false;
        }
    }

    if(!VulkanDetail::IsRenderPassAttachmentActionsValid(params.depthAttachmentActions) || !VulkanDetail::IsRenderPassAttachmentActionsValid(params.stencilAttachmentActions)){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("depth/stencil attachment actions are invalid"));
        return false;
    }

    const b8 depthActionsRequested = params.depthAttachmentActions.loadAction != RenderPassLoadAction::Load || params.depthAttachmentActions.storeAction != RenderPassStoreAction::Store;
    const b8 stencilActionsRequested = params.stencilAttachmentActions.loadAction != RenderPassLoadAction::Load || params.stencilAttachmentActions.storeAction != RenderPassStoreAction::Store;
    const FramebufferAttachment& depthAttachment = framebufferDesc.depthAttachment;
    if(!depthAttachment.valid()){
        if(depthActionsRequested || stencilActionsRequested){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("depth/stencil attachment actions requested without an attachment"));
            return false;
        }
        return true;
    }

    const b8 clearDepth = params.depthAttachmentActions.loadAction == RenderPassLoadAction::Clear;
    const b8 clearStencil = params.stencilAttachmentActions.loadAction == RenderPassLoadAction::Clear;
    if(depthAttachment.isReadOnly && (clearDepth || clearStencil)){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("depth/stencil clear requested for a read-only attachment"));
        return false;
    }

    const VkImageAspectFlags attachmentAspects = depthAttachment.texture->m_aspectMask;
    if(depthActionsRequested && (attachmentAspects & VK_IMAGE_ASPECT_DEPTH_BIT) == 0u){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("depth attachment actions requested for an attachment without depth"));
        return false;
    }
    if(stencilActionsRequested && (attachmentAspects & VK_IMAGE_ASPECT_STENCIL_BIT) == 0u){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("stencil attachment actions requested for an attachment without stencil"));
        return false;
    }
    if(clearDepth && (!IsFinite(params.depthClearValue) || params.depthClearValue < 0.0f || params.depthClearValue > 1.0f)){
        m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("depth clear value is outside the normalized depth range"));
        return false;
    }
    return true;
}

b8 FramebufferRenderingPreparation::prepareFramebuffer(Framebuffer* const framebuffer, const TStringView operationName){
    if(!framebuffer)
        return true;

    const auto prepareAttachment = [this, operationName](const FramebufferAttachment& attachment) -> b8{
        if(!attachment.texture)
            return true;
        const TextureSubresourceSet resolved = attachment.subresources.resolve(attachment.texture->m_creationDesc, TextureSubresourceMipResolve::Range);
        const TextureDimension::Enum viewDimension = VulkanDetail::GetFramebufferAttachmentViewDimension(attachment.texture->m_creationDesc, resolved);
        if(!attachment.texture->getView(attachment.subresources, viewDimension, attachment.format)){
            m_commandList.rejectCommandRecording(operationName, FOUNDATION_TEXT("framebuffer attachment view could not be created"));
            return false;
        }
        return true;
    };

    for(const FramebufferAttachment& attachment : framebuffer->m_desc.colorAttachments){
        if(!prepareAttachment(attachment))
            return false;
    }
    if(!prepareAttachment(framebuffer->m_desc.depthAttachment))
        return false;
    return true;
}

NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

