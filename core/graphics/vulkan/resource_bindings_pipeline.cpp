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
        const VkDescriptorSetLayout emptyLayout = getOrCreateEmptyDescriptorBufferSetLayout();
        if(emptyLayout == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: the required empty descriptor-buffer layout is unavailable."), operationName);
            return false;
        }
        if(!VulkanDetail::CreatePipelineLayout(m_context, &emptyLayout, 1u, 0u, outPipelineLayout, operationName))
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

        pushConstantByteSize = Max<u32>(
            pushConstantByteSize,
            VulkanDetail::GetPushConstantByteSize(layout->getBindingLayoutDesc())
        );
        if(layout->m_descriptorSetLayouts.size() > static_cast<usize>(Limit<u32>::s_Max) - descriptorSetLayoutCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set layout count exceeds u32 limits")
                , operationName
            );
            return false;
        }
        descriptorSetLayoutCount += layout->m_descriptorSetLayouts.size();
    }

    // Global heaps pin to reserved sets 8/9/10; local push-only layouts remain positional.
    const auto layoutSetIndex = [](const BindingLayout& layout, const u32 positional, bool& outExplicit) -> u32{
        if(const BindlessLayoutDesc* bindlessDesc = layout.getBindlessDesc()){
            outExplicit = true;
            return bindlessDesc->descriptorSetIndex;
        }
        outExplicit = false;
        return positional;
    };

    bool anyExplicitSet = false;
    for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()) && !anyExplicitSet; ++i){
        const BindingLayout* const layout = bindingLayouts[i].get();
        NWB_ASSERT(layout != nullptr);
        anyExplicitSet = layout->getBindlessDesc() != nullptr;
    }


    for(const auto& bindingLayout : bindingLayouts){
        const auto* layout = bindingLayout.get();
        if(!layout || !layout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: every binding layout must be descriptor-buffer-compatible."), operationName);
            return false;
        }
        if(const BindlessLayoutDesc* bindlessDesc = layout->getBindlessDesc()){
            if(bindlessDesc->descriptorSetIndex < s_MaxBindingLayouts){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: resource-bearing bindless layouts must use an explicit global-heap set at or above {}."), operationName, s_MaxBindingLayouts);
                return false;
            }
        }
    }

    if(bindingLayouts.size() == 1 && !anyExplicitSet){
        auto* layout = bindingLayouts[0].get();
        NWB_ASSERT(layout != nullptr);
        outPipelineLayout = layout->m_pipelineLayout;
        outPushConstantByteSize = layout->m_pushConstantByteSize;
        return true;
    }

    if(!anyExplicitSet){
        descriptorSetLayouts.reserve(descriptorSetLayoutCount);
        for(const auto& bindingLayout : bindingLayouts){
            auto* layout = bindingLayout.get();
            NWB_ASSERT(layout != nullptr);
            for(const auto& descriptorSetLayout : layout->m_descriptorSetLayouts)
                descriptorSetLayouts.push_back(descriptorSetLayout);
        }
        NWB_ASSERT(descriptorSetLayouts.size() == descriptorSetLayoutCount);
    }
    else{
        // Fill unused sets with empty descriptor-buffer layouts; Vulkan permits no gaps.
        const VkDescriptorSetLayout fillSetLayout = getOrCreateEmptyDescriptorBufferSetLayout();
        if(fillSetLayout == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer explicit-set placement needs the empty descriptor-buffer gap-set layout, which is unavailable"), operationName);
            return false;
        }

        u32 maxSetIndex = 0;
        for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
            const BindingLayout& layout = *bindingLayouts[i].get();
            const usize setCount = layout.m_descriptorSetLayouts.size();
            if(setCount == 0)
                continue;
            bool isExplicit = false;
            const u32 base = layoutSetIndex(layout, i, isExplicit);
            if(base > Limit<u32>::s_Max - static_cast<u32>(setCount - 1)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set index overflow"), operationName);
                return false;
            }
            maxSetIndex = Max<u32>(maxSetIndex, base + static_cast<u32>(setCount) - 1u);
        }

        const u32 totalSets = maxSetIndex + 1u;
        descriptorSetLayouts.reserve(totalSets);
        for(u32 s = 0; s < totalSets; ++s)
            descriptorSetLayouts.push_back(fillSetLayout);

        for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
            const BindingLayout& layout = *bindingLayouts[i].get();
            bool isExplicit = false;
            const u32 base = layoutSetIndex(layout, i, isExplicit);
            for(usize s = 0; s < layout.m_descriptorSetLayouts.size(); ++s){
                const u32 slot = base + static_cast<u32>(s);
                if(descriptorSetLayouts[slot] != fillSetLayout){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: two binding layouts map to descriptor set {}"), operationName, slot);
                    return false;
                }
                descriptorSetLayouts[slot] = layout.m_descriptorSetLayouts[s];
            }
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

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: required descriptor-buffer backend is unavailable."), operationName);
        return false;
    }

    return createPipelineLayoutForBindingLayouts(
        bindingLayouts,
        operationName,
        outBindings.m_pipelineLayout,
        outBindings.m_pushConstantByteSize,
        outBindings.m_ownsPipelineLayout,
        scratchArena
    );
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

