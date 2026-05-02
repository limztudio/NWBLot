// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unsupported Sampler Feedback


void CommandList::clearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture){
    static_cast<void>(texture);
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear sampler feedback texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
}

void CommandList::decodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, Format::Enum format){
    static_cast<void>(buffer);
    static_cast<void>(texture);
    static_cast<void>(format);
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to decode sampler feedback texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
}

void CommandList::setSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, ResourceStates::Mask stateBits){
    static_cast<void>(texture);
    static_cast<void>(stateBits);
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to set sampler feedback texture state: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackTexture(ITexture* pairedTexture, const SamplerFeedbackTextureDesc& desc){
    static_cast<void>(pairedTexture);
    static_cast<void>(desc);
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler feedback texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
    return nullptr;
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackForNativeTexture(ObjectType objectType, Object texture, ITexture* pairedTexture){
    static_cast<void>(objectType);
    static_cast<void>(texture);
    static_cast<void>(pairedTexture);
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler feedback texture for native texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

