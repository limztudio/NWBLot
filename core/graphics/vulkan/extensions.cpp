// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Local Helpers


// Push Constants


void CommandList::setPushConstants(const void* data, usize byteSize){
    if(byteSize == 0)
        return;
#if defined(NWB_DEBUG)
    if(!data){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: data is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: data is null"));
        return;
    }
    if(byteSize > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds uint32 range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds uint32 range"));
        return;
    }
#endif

    const u32 pushConstantByteSize = static_cast<u32>(byteSize);
#if defined(NWB_DEBUG)
    if(!VulkanDetail::ValidatePushConstantByteSize(m_context, pushConstantByteSize, NWB_TEXT("set push constants")))
        return;
#endif

    VkPipelineLayout layout = VK_NULL_HANDLE;
#if defined(NWB_DEBUG)
    u32 pipelinePushConstantByteSize = 0;
#endif

    if(m_currentGraphicsState.pipeline){
        auto* gp = m_currentGraphicsState.pipeline;
        layout = gp->m_pipelineLayout;
#if defined(NWB_DEBUG)
        pipelinePushConstantByteSize = gp->m_pushConstantByteSize;
#endif
    }
    else if(m_currentComputeState.pipeline){
        auto* cp = m_currentComputeState.pipeline;
        layout = cp->m_pipelineLayout;
#if defined(NWB_DEBUG)
        pipelinePushConstantByteSize = cp->m_pushConstantByteSize;
#endif
    }
    else if(m_currentMeshletState.pipeline){
        auto* mp = m_currentMeshletState.pipeline;
        layout = mp->m_pipelineLayout;
#if defined(NWB_DEBUG)
        pipelinePushConstantByteSize = mp->m_pushConstantByteSize;
#endif
    }
    else if(m_currentRayTracingState.shaderTable){
        auto* rtp = m_currentRayTracingState.shaderTable->getPipeline();
        if(rtp){
            auto* rtpImpl = rtp;
            layout = rtpImpl->m_pipelineLayout;
#if defined(NWB_DEBUG)
            pipelinePushConstantByteSize = rtpImpl->m_pushConstantByteSize;
#endif
        }
    }

#if defined(NWB_DEBUG)
    if(layout == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: no active pipeline layout"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: no active pipeline layout"));
        return;
    }
    if(pipelinePushConstantByteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: active pipeline layout has no push constant range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: active pipeline layout has no push constant range"));
        return;
    }
    if(pushConstantByteSize > pipelinePushConstantByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size {} exceeds active pipeline push constant range {}")
            , pushConstantByteSize
            , pipelinePushConstantByteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds active pipeline push constant range"));
        return;
    }
#endif

    vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, pushConstantByteSize, data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

