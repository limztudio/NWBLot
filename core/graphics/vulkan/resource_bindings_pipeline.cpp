// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CreatedPipelineLayout> Device::createPipelineLayoutForBindingLayouts(
    const BindingLayoutVector& bindingLayouts,
    TStringView operationName,
    Alloc::ScratchArena& scratchArena
)const{
    CreatedPipelineLayout result;

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer backend is unavailable."), operationName);
        return MakeUnexpected(Failure{});
    }

    if(bindingLayouts.empty()){
        const auto pipelineLayout = VulkanDetail::CreatePipelineLayout(m_context, nullptr, 0u, 0u, operationName);
        if(!pipelineLayout)
            return MakeUnexpected(Failure{});
        result.layout = *pipelineLayout;

        result.ownsLayout = true;
        return result;
    }

    Vector<VkDescriptorSetLayout, Alloc::ScratchArena> descriptorSetLayouts{scratchArena};
    u32 pushConstantByteSize = 0;
    usize descriptorSetLayoutCount = 0;

    for(u32 i = 0u; i < static_cast<u32>(bindingLayouts.size()); ++i){
        auto* layout = bindingLayouts[i].get();
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: binding layout {} is invalid"), operationName, i);
            return MakeUnexpected(Failure{});
        }
        if(&layout->m_context != &m_context){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create {}: binding layout {} belongs to another device"),
                operationName,
                i
            );
            return MakeUnexpected(Failure{});
        }

        pushConstantByteSize = Max<u32>(pushConstantByteSize, layout->m_pushConstantByteSize);
        if(layout->m_descriptorSetLayouts.size() > static_cast<usize>(Limit<u32>::s_Max) - descriptorSetLayoutCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set layout count exceeds u32 limits")
                , operationName
            );
            return MakeUnexpected(Failure{});
        }
        descriptorSetLayoutCount += layout->m_descriptorSetLayouts.size();
    }

    for(const auto& bindingLayout : bindingLayouts){
        const auto* layout = bindingLayout.get();
        if(!layout || !layout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: every binding layout must be descriptor-buffer-compatible."), operationName);
            return MakeUnexpected(Failure{});
        }
        const BindlessLayoutDesc* const bindlessDesc = layout->getBindlessDesc();
        if(!layout->m_descriptorSetLayouts.empty() && !bindlessDesc){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} needs explicit set metadata for every descriptor layout.")
                , operationName
            );
            return MakeUnexpected(Failure{});
        }
        if(bindlessDesc && layout->m_descriptorSetLayouts.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: a bindless layout owns no descriptor-set layout.")
                , operationName
            );
            return MakeUnexpected(Failure{});
        }
        if(bindlessDesc && bindlessDesc->descriptorSetIndex == Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} bindless resource layouts require an explicit set index.")
                , operationName
            );
            return MakeUnexpected(Failure{});
        }
    }

    if(bindingLayouts.size() == 1 && descriptorSetLayoutCount == 0u){
        auto* layoutPtr = bindingLayouts[0].get();
        if(!layoutPtr){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: binding layout 0 is invalid"), operationName);
            return MakeUnexpected(Failure{});
        }
        const BindingLayout& layout = *layoutPtr;
        result.layout = layout.m_pipelineLayout;
        result.pushConstantByteSize = layout.m_pushConstantByteSize;
        return result;
    }

    if(descriptorSetLayoutCount == 0u){
        const auto pipelineLayout = VulkanDetail::CreatePipelineLayout(m_context, nullptr, 0u, pushConstantByteSize, operationName);
        if(!pipelineLayout)
            return MakeUnexpected(Failure{});
        result.layout = *pipelineLayout;

        result.pushConstantByteSize = pushConstantByteSize;
        result.ownsLayout = true;
        return result;
    }

    u32 maxSetIndex = 0;
    for(const auto& bindingLayout : bindingLayouts){
        const BindingLayout& layout = *bindingLayout.get();
        const usize setCount = layout.m_descriptorSetLayouts.size();
        if(setCount == 0u)
            continue;
        const BindlessLayoutDesc* const bindlessDesc = layout.getBindlessDesc();
        if(!bindlessDesc){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: bindless layout descriptor is missing"), operationName);
            return MakeUnexpected(Failure{});
        }
        const u32 base = bindlessDesc->descriptorSetIndex;
        if(base > Limit<u32>::s_Max - static_cast<u32>(setCount - 1u)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set index overflow"), operationName);
            return MakeUnexpected(Failure{});
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
        return MakeUnexpected(Failure{});
    }

    const u32 totalSets = maxSetIndex + 1u;
    descriptorSetLayouts.reserve(totalSets);
    for(u32 setIndex = 0u; setIndex < totalSets; ++setIndex)
        descriptorSetLayouts.push_back(VK_NULL_HANDLE);

    for(const auto& bindingLayout : bindingLayouts){
        const BindingLayout& layout = *bindingLayout.get();
        if(layout.m_descriptorSetLayouts.empty())
            continue;
        const BindlessLayoutDesc* const bindlessDesc = layout.getBindlessDesc();
        if(!bindlessDesc){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: bindless layout descriptor is missing"), operationName);
            return MakeUnexpected(Failure{});
        }
        const u32 base = bindlessDesc->descriptorSetIndex;
        for(usize localSetIndex = 0u; localSetIndex < layout.m_descriptorSetLayouts.size(); ++localSetIndex){
            const u32 setIndex = base + static_cast<u32>(localSetIndex);
            if(descriptorSetLayouts[setIndex] != VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: two binding layouts map to descriptor set {}")
                    , operationName
                    , setIndex
                );
                return MakeUnexpected(Failure{});
            }
            descriptorSetLayouts[setIndex] = layout.m_descriptorSetLayouts[localSetIndex];
        }
    }

    for(u32 setIndex = 0u; setIndex < totalSets; ++setIndex){
        if(descriptorSetLayouts[setIndex] == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} descriptor layouts must be dense from set 0; set {} is missing")
                , operationName
                , setIndex
            );
            return MakeUnexpected(Failure{});
        }
    }

    const auto pipelineLayout = VulkanDetail::CreatePipelineLayout(
        m_context,
        descriptorSetLayouts.data(),
        static_cast<u32>(descriptorSetLayouts.size()),
        pushConstantByteSize,
        operationName
    );
    if(!pipelineLayout)
        return MakeUnexpected(Failure{});
    result.layout = *pipelineLayout;

    result.pushConstantByteSize = pushConstantByteSize;
    result.ownsLayout = true;
    return result;
}

Expected<PipelineBindingState> Device::buildPipelineBindings(
    const BindingLayoutVector& bindingLayouts,
    TStringView operationName,
    Alloc::ScratchArena& scratchArena
)const{
    PipelineBindingState bindings;

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: required descriptor-buffer backend is unavailable."), operationName);
        return MakeUnexpected(Failure{});
    }

    const auto layout = createPipelineLayoutForBindingLayouts(bindingLayouts, operationName, scratchArena);
    if(!layout)
        return MakeUnexpected(Failure{});
    bindings.m_pipelineLayout = layout->layout;
    bindings.m_pushConstantByteSize = layout->pushConstantByteSize;
    bindings.m_ownsPipelineLayout = layout->ownsLayout;

    bindings.m_bindingLayoutsAtCreation = bindingLayouts;
    for(u32 layoutIndex = 0u; layoutIndex < static_cast<u32>(bindingLayouts.size()); ++layoutIndex){
        const BindingLayout* const layout = bindingLayouts[layoutIndex].get();
        NWB_ASSERT(layout != nullptr);
        const BindlessLayoutDesc* const bindlessDesc = layout->getBindlessDesc();
        bindings.m_bindingLayoutSetIndicesAtCreation[layoutIndex] = bindlessDesc
            ? bindlessDesc->descriptorSetIndex
            : Limit<u32>::s_Max
        ;
    }
    return bindings;
}


void Device::appendPipelineShaderStage(
    Shader& shader,
    const VkShaderStageFlagBits stage,
    PipelineSpecializationInfoVector& specializationInfos,
    PipelineShaderStageVector& shaderStages
)const{
    Shader& s = shader;
    auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stageInfo.stage = stage;
    stageInfo.module = s.m_shaderModule;
    stageInfo.pName = s.m_entryPointName.data();

    if(!s.m_specializationEntries.empty()){
        specializationInfos.push_back(s.makeSpecializationInfo());
        stageInfo.pSpecializationInfo = &specializationInfos.back();
    }

    shaderStages.push_back(stageInfo);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

