// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_sampler.h"

#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_sampler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkSamplerAddressMode ConvertSamplerAddressMode(const SamplerAddressMode::Enum mode)noexcept{
    switch(mode){
    case SamplerAddressMode::Clamp:      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerAddressMode::Wrap:       return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::Border:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case SamplerAddressMode::Mirror:     return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::MirrorOnce: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:                             return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

VkSamplerCreateInfo BuildSamplerCreateInfo(const SamplerDesc& desc)noexcept{
    const f32 maxAnisotropy = desc.maxAnisotropy >= 1.f ? desc.maxAnisotropy : 1.f;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = desc.magFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.minFilter = desc.minFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = desc.mipFilter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = ConvertSamplerAddressMode(desc.addressU);
    samplerInfo.addressModeV = ConvertSamplerAddressMode(desc.addressV);
    samplerInfo.addressModeW = ConvertSamplerAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipBias;
    samplerInfo.anisotropyEnable = maxAnisotropy > 1.f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = maxAnisotropy;
    samplerInfo.compareEnable = desc.reductionType == SamplerReductionType::Comparison ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    return samplerInfo;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Sampler::Sampler(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_context(context)
{}
Sampler::~Sampler(){
    if(m_sampler != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroySampler(m_context.device, m_sampler, m_context.allocationCallbacks);
        m_sampler = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SamplerHandle Device::createSampler(const SamplerDesc& d){
    if(d.reductionType != SamplerReductionType::Standard && d.reductionType != SamplerReductionType::Comparison){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler: unsupported reduction type {}"), static_cast<u32>(d.reductionType));
        return nullptr;
    }

    SamplerDesc normalizedDesc = d;
    const f32 supported = m_context.physicalDeviceProperties.limits.maxSamplerAnisotropy;
    const f32 maximum = supported > 1.0f ? supported : 1.0f;
    const f32 requested = normalizedDesc.maxAnisotropy;
    const f32 lowerBounded = requested >= 1.0f ? requested : 1.0f;
    normalizedDesc.maxAnisotropy = lowerBounded > maximum ? maximum : lowerBounded;

    SamplerHandle sampler(NewArenaObject<Sampler>(m_context.objectArena, m_context), SamplerHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!sampler)
        return nullptr;
    sampler->m_desc = normalizedDesc;

    const VkSamplerCreateInfo samplerInfo = __hidden_vulkan_sampler::BuildSamplerCreateInfo(normalizedDesc);

    const VkResult res = m_context.deviceDispatch.vkCreateSampler(m_context.device, &samplerInfo, m_context.allocationCallbacks, &sampler->m_sampler);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create sampler"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler: {}"), ResultToString(res));
        return nullptr;
    }

    return sampler;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

