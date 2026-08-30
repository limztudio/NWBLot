// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::createPipelineLayoutForBindingLayouts(
    const BindingLayoutVector& bindingLayouts,
    const tchar* operationName,
    VkPipelineLayout& outPipelineLayout,
    u32& outPushConstantByteSize,
    bool& outOwnsPipelineLayout,
    Alloc::ScratchArena& scratchArena
)const{
    outPipelineLayout = VK_NULL_HANDLE;
    outPushConstantByteSize = 0;
    outOwnsPipelineLayout = false;

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer backend is unavailable."), operationName);
        return false;
    }

    if(bindingLayouts.empty()){
        if(!VulkanDetail::CreatePipelineLayout(m_context, nullptr, 0u, 0u, outPipelineLayout, operationName))
            return false;

        outOwnsPipelineLayout = true;
        return true;
    }

    Vector<VkDescriptorSetLayout, Alloc::ScratchArena> descriptorSetLayouts{scratchArena};
    u32 pushConstantByteSize = 0;
    usize descriptorSetLayoutCount = 0;

    for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
        auto* layout = bindingLayouts[i].get();
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: binding layout {} is invalid"), operationName, i);
            return false;
        }
        if(&layout->m_context != &m_context){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create {}: binding layout {} belongs to another device"),
                operationName,
                i
            );
            return false;
        }

        pushConstantByteSize = Max<u32>(pushConstantByteSize, layout->m_pushConstantByteSize);
        if(layout->m_descriptorSetLayouts.size() > static_cast<usize>(Limit<u32>::s_Max) - descriptorSetLayoutCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set layout count exceeds u32 limits")
                , operationName
            );
            return false;
        }
        descriptorSetLayoutCount += layout->m_descriptorSetLayouts.size();
    }

    for(const auto& bindingLayout : bindingLayouts){
        const auto* layout = bindingLayout.get();
        if(!layout || !layout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: every binding layout must be descriptor-buffer-compatible."), operationName);
            return false;
        }
        const BindlessLayoutDesc* const bindlessDesc = layout->getBindlessDesc();
        if(!layout->m_descriptorSetLayouts.empty() && !bindlessDesc){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} needs explicit set metadata for every descriptor layout.")
                , operationName
            );
            return false;
        }
        if(bindlessDesc && layout->m_descriptorSetLayouts.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: a bindless layout owns no descriptor-set layout.")
                , operationName
            );
            return false;
        }
        if(bindlessDesc && bindlessDesc->descriptorSetIndex == Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} bindless resource layouts require an explicit set index.")
                , operationName
            );
            return false;
        }
    }

    if(bindingLayouts.size() == 1 && descriptorSetLayoutCount == 0u){
        auto* layout = bindingLayouts[0].get();
        NWB_ASSERT(layout != nullptr);
        outPipelineLayout = layout->m_pipelineLayout;
        outPushConstantByteSize = layout->m_pushConstantByteSize;
        return true;
    }

    if(descriptorSetLayoutCount == 0u){
        if(!VulkanDetail::CreatePipelineLayout(m_context, nullptr, 0u, pushConstantByteSize, outPipelineLayout, operationName))
            return false;

        outPushConstantByteSize = pushConstantByteSize;
        outOwnsPipelineLayout = true;
        return true;
    }

    u32 maxSetIndex = 0;
    for(const auto& bindingLayout : bindingLayouts){
        const BindingLayout& layout = *bindingLayout.get();
        const usize setCount = layout.m_descriptorSetLayouts.size();
        if(setCount == 0u)
            continue;
        const BindlessLayoutDesc* const bindlessDesc = layout.getBindlessDesc();
        NWB_ASSERT(bindlessDesc != nullptr);
        const u32 base = bindlessDesc->descriptorSetIndex;
        if(base > Limit<u32>::s_Max - static_cast<u32>(setCount - 1u)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set index overflow"), operationName);
            return false;
        }
        maxSetIndex = Max<u32>(maxSetIndex, base + static_cast<u32>(setCount) - 1u);
    }

    if(maxSetIndex >= m_context.physicalDeviceProperties.limits.maxBoundDescriptorSets){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create {}: descriptor set {} exceeds maxBoundDescriptorSets {}")
            , operationName
            , maxSetIndex
            , m_context.physicalDeviceProperties.limits.maxBoundDescriptorSets
        );
        return false;
    }

    const u32 totalSets = maxSetIndex + 1u;
    descriptorSetLayouts.reserve(totalSets);
    for(u32 setIndex = 0; setIndex < totalSets; ++setIndex)
        descriptorSetLayouts.push_back(VK_NULL_HANDLE);

    for(const auto& bindingLayout : bindingLayouts){
        const BindingLayout& layout = *bindingLayout.get();
        if(layout.m_descriptorSetLayouts.empty())
            continue;
        const BindlessLayoutDesc* const bindlessDesc = layout.getBindlessDesc();
        NWB_ASSERT(bindlessDesc != nullptr);
        const u32 base = bindlessDesc->descriptorSetIndex;
        for(usize localSetIndex = 0; localSetIndex < layout.m_descriptorSetLayouts.size(); ++localSetIndex){
            const u32 setIndex = base + static_cast<u32>(localSetIndex);
            if(descriptorSetLayouts[setIndex] != VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: two binding layouts map to descriptor set {}")
                    , operationName
                    , setIndex
                );
                return false;
            }
            descriptorSetLayouts[setIndex] = layout.m_descriptorSetLayouts[localSetIndex];
        }
    }

    for(u32 setIndex = 0; setIndex < totalSets; ++setIndex){
        if(descriptorSetLayouts[setIndex] == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} descriptor layouts must be dense from set 0; set {} is missing")
                , operationName
                , setIndex
            );
            return false;
        }
    }

    if(!VulkanDetail::CreatePipelineLayout(
        m_context,
        descriptorSetLayouts.data(),
        static_cast<u32>(descriptorSetLayouts.size()),
        pushConstantByteSize,
        outPipelineLayout,
        operationName
    ))
        return false;

    outPushConstantByteSize = pushConstantByteSize;
    outOwnsPipelineLayout = true;
    return true;
}

bool Device::configurePipelineBindings(
    const BindingLayoutVector& bindingLayouts,
    const tchar* operationName,
    PipelineBindingState& outBindings,
    Alloc::ScratchArena& scratchArena
)const{
    outBindings.m_pipelineLayout = VK_NULL_HANDLE;
    outBindings.m_ownsPipelineLayout = false;
    outBindings.m_pushConstantByteSize = 0;
    outBindings.m_bindingLayoutsAtCreation.clear();
    outBindings.m_bindingLayoutSetIndicesAtCreation = {};

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: required descriptor-buffer backend is unavailable."), operationName);
        return false;
    }

    if(!createPipelineLayoutForBindingLayouts(
        bindingLayouts,
        operationName,
        outBindings.m_pipelineLayout,
        outBindings.m_pushConstantByteSize,
        outBindings.m_ownsPipelineLayout,
        scratchArena
    ))
        return false;

    outBindings.m_bindingLayoutsAtCreation = bindingLayouts;
    for(u32 layoutIndex = 0u; layoutIndex < static_cast<u32>(bindingLayouts.size()); ++layoutIndex){
        const BindingLayout* const layout = bindingLayouts[layoutIndex].get();
        NWB_ASSERT(layout != nullptr);
        const BindlessLayoutDesc* const bindlessDesc = layout->getBindlessDesc();
        outBindings.m_bindingLayoutSetIndicesAtCreation[layoutIndex] = bindlessDesc
            ? bindlessDesc->descriptorSetIndex
            : Limit<u32>::s_Max
        ;
    }
    return true;
}


void Device::appendPipelineShaderStage(
    Shader* shader,
    const VkShaderStageFlagBits stage,
    PipelineSpecializationInfoVector& specializationInfos,
    PipelineShaderStageVector& shaderStages
)const{
    auto* s = shader;
    auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stageInfo.stage = stage;
    stageInfo.module = s->m_shaderModule;
    stageInfo.pName = s->m_entryPointName.c_str();

    if(!s->m_specializationEntries.empty()){
        specializationInfos.push_back(s->makeSpecializationInfo());
        stageInfo.pSpecializationInfo = &specializationInfos.back();
    }

    shaderStages.push_back(stageInfo);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

