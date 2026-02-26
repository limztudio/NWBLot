// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipeline::ComputePipeline(const VulkanContext& context)
    : RefCounter<IComputePipeline>(context.threadPool)
    , m_context(context)
{}
ComputePipeline::~ComputePipeline(){
    if(m_pipeline){
        vkDestroyPipeline(m_context.device, m_pipeline, m_context.allocationCallbacks);
        m_pipeline = VK_NULL_HANDLE;
    }

    if(m_ownsPipelineLayout && m_pipelineLayout != VK_NULL_HANDLE){
        vkDestroyPipelineLayout(m_context.device, m_pipelineLayout, m_context.allocationCallbacks);
        m_pipelineLayout = VK_NULL_HANDLE;
        m_ownsPipelineLayout = false;
    }
}

Object ComputePipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(m_pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc){
    VkResult res = VK_SUCCESS;
    Alloc::ScratchArena<> scratchArena;

    auto* pso = NewArenaObject<ComputePipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;

    if(!desc.CS){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    auto* cs = checked_cast<Shader*>(desc.CS.get());

    VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = cs->m_shaderModule;
    shaderStage.pName = cs->m_desc.entryName.c_str();

    VkSpecializationInfo specInfo{};
    if(!cs->m_specializationEntries.empty()){
        specInfo.mapEntryCount = static_cast<u32>(cs->m_specializationEntries.size());
        specInfo.pMapEntries = cs->m_specializationEntries.data();
        specInfo.dataSize = cs->m_specializationData.size();
        specInfo.pData = cs->m_specializationData.data();
        shaderStage.pSpecializationInfo = &specInfo;
    }

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(desc.bindingLayouts.size() == 1){
        auto* layout = checked_cast<BindingLayout*>(desc.bindingLayouts[0].get());
        if(layout)
            pipelineLayout = layout->m_pipelineLayout;
    }
    else if(desc.bindingLayouts.size() > 1){
        Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> allDescriptorSetLayouts{ Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena) };
        u32 pushConstantByteSize = 0;
        for(u32 i = 0; i < static_cast<u32>(desc.bindingLayouts.size()); ++i){
            auto* bl = checked_cast<BindingLayout*>(desc.bindingLayouts[i].get());
            if(!bl)
                continue;
            for(const auto& item : bl->m_desc.bindings){
                if(item.type == ResourceType::PushConstants)
                    pushConstantByteSize = Max<u32>(pushConstantByteSize, item.size);
            }
            for(const auto& dsl : bl->m_descriptorSetLayouts)
                allDescriptorSetLayouts.push_back(dsl);
        }

        if(!allDescriptorSetLayouts.empty()){
            VkPushConstantRange pushConstantRange = {};
            if(pushConstantByteSize > 0){
                pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
                pushConstantRange.offset = 0;
                pushConstantRange.size = pushConstantByteSize;
            }

            VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layoutInfo.setLayoutCount = static_cast<u32>(allDescriptorSetLayouts.size());
            layoutInfo.pSetLayouts = allDescriptorSetLayouts.data();
            layoutInfo.pushConstantRangeCount = pushConstantByteSize > 0 ? 1u : 0u;
            layoutInfo.pPushConstantRanges = pushConstantByteSize > 0 ? &pushConstantRange : nullptr;
            res = vkCreatePipelineLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &pipelineLayout);
            if(res != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for compute pipeline: {}"), ResultToString(res));
                DestroyArenaObject(m_context.objectArena, pso);
                return nullptr;
            }
            pso->m_ownsPipelineLayout = true;
        }
    }
    pso->m_pipelineLayout = pipelineLayout;

    VkComputePipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;

    res = vkCreateComputePipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    return ComputePipelineHandle(pso, ComputePipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setComputeState(const ComputeState& state){
    endActiveRenderPass();
    m_currentComputeState = state;

    auto* pipeline = checked_cast<ComputePipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->m_pipeline);

    if(state.bindings.size() > 0 && pipeline && pipeline->m_pipelineLayout != VK_NULL_HANDLE){
        for(usize i = 0; i < state.bindings.size(); ++i){
            if(state.bindings[i]){
                auto* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->m_descriptorSets.empty())
                    vkCmdBindDescriptorSets(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline->m_pipelineLayout, static_cast<u32>(i), 
                        static_cast<u32>(bindingSet->m_descriptorSets.size()), bindingSet->m_descriptorSets.data(), 0, nullptr);
            }
        }
    }
}

void CommandList::dispatch(u32 groupsX, u32 groupsY, u32 groupsZ){
    vkCmdDispatch(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}

void CommandList::dispatchIndirect(u32 offsetBytes){
    if(!m_currentComputeState.indirectParams){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: No indirect buffer bound for dispatchIndirect"));
        return;
    }
    auto* buffer = checked_cast<Buffer*>(m_currentComputeState.indirectParams);
    vkCmdDispatchIndirect(m_currentCmdBuf->m_cmdBuf, buffer->m_buffer, offsetBytes);
    m_currentCmdBuf->m_referencedResources.push_back(m_currentComputeState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

