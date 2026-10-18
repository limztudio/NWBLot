// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_sampler.h"

#include "backend.h"

#include <core/common/log.h>

#include <global/simd/vector_arithmetic.h>


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
    const f32 maxAnisotropy = VectorGetX(VectorMax(VectorReplicate(desc.maxAnisotropy), VectorSplatOne()));

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


Sampler::Sampler(const VulkanContext& context)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_context(context)
{}
Sampler::~Sampler()noexcept{
    if(m_sampler != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroySampler(m_context.device, m_sampler, m_context.allocationCallbacks);
        m_sampler = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SamplerHandle> Device::createSampler(const SamplerDesc& d){
    if(d.reductionType != SamplerReductionType::Standard && d.reductionType != SamplerReductionType::Comparison){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler: unsupported reduction type {}"), static_cast<u32>(d.reductionType));
        return MakeUnexpected(Failure{});
    }

    SamplerDesc normalizedDesc = d;
    // x: requested anisotropy, y: device limit. Both are raised to at least 1 before the limit applies.
    const SIMDVectorSingle bounded = VectorMax(VectorSet(normalizedDesc.maxAnisotropy, m_context.physicalDeviceProperties.limits.maxSamplerAnisotropy, 0.0f, 0.0f), VectorSplatOne());
    normalizedDesc.maxAnisotropy = VectorGetX(VectorMin(VectorSplatY(bounded), bounded));

    SamplerHandle sampler(NewArenaObject<Sampler>(m_context.objectArena, m_context), SamplerHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!sampler)
        return MakeUnexpected(Failure{});
    sampler->m_desc = normalizedDesc;

    const VkSamplerCreateInfo samplerInfo = __hidden_vulkan_sampler::BuildSamplerCreateInfo(normalizedDesc);

    const VkResult res = m_context.deviceDispatch.vkCreateSampler(m_context.device, &samplerInfo, m_context.allocationCallbacks, &sampler->m_sampler);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create sampler"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    return sampler;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

