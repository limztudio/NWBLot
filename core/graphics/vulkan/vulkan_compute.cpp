// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipeline::~ComputePipeline(){
    if(pipeline){
        vkDestroyPipeline(m_context.device, pipeline, m_context.allocationCallbacks);
        pipeline = VK_NULL_HANDLE;
    }
}

Object ComputePipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc){
    auto* pso = new ComputePipeline(m_context);
    pso->desc = desc;

    if(!desc.CS){
        delete pso;
        return nullptr;
    }

    auto* cs = checked_cast<Shader*>(desc.CS.get());

    VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = cs->shaderModule;
    shaderStage.pName = cs->desc.entryName.c_str();

    VkSpecializationInfo specInfo{};
    if(!cs->specializationEntries.empty()){
        specInfo.mapEntryCount = static_cast<u32>(cs->specializationEntries.size());
        specInfo.pMapEntries = cs->specializationEntries.data();
        specInfo.dataSize = cs->specializationData.size();
        specInfo.pData = cs->specializationData.data();
        shaderStage.pSpecializationInfo = &specInfo;
    }

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.bindingLayouts.empty() && desc.bindingLayouts[0]){
        auto* layout = checked_cast<BindingLayout*>(desc.bindingLayouts[0].get());
        pipelineLayout = layout->pipelineLayout;
        pso->pipelineLayout = pipelineLayout;
    }

    VkComputePipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;

    VkResult res = vkCreateComputePipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->pipeline);

    if(res != VK_SUCCESS){
        delete pso;
        return nullptr;
    }

    return ComputePipelineHandle(pso, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setComputeState(const ComputeState& state){
    currentComputeState = state;

    auto* pipeline = checked_cast<ComputePipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

    if(state.bindings.size() > 0){
        for(usize i = 0; i < state.bindings.size(); ++i){
            if(state.bindings[i]){
                auto* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->descriptorSets.empty())
                    vkCmdBindDescriptorSets(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline->pipelineLayout, static_cast<u32>(i), 
                        static_cast<u32>(bindingSet->descriptorSets.size()), bindingSet->descriptorSets.data(), 0, nullptr);
            }
        }
    }
}

void CommandList::dispatch(u32 groupsX, u32 groupsY, u32 groupsZ){
    vkCmdDispatch(currentCmdBuf->cmdBuf, groupsX, groupsY, groupsZ);
}

void CommandList::dispatchIndirect(u32 offsetBytes){
    if(!currentComputeState.indirectParams){
        NWB_ASSERT_MSG(false, NWB_TEXT("No indirect buffer bound for dispatchIndirect"));
        return;
    }
    auto* buffer = checked_cast<Buffer*>(currentComputeState.indirectParams);
    vkCmdDispatchIndirect(currentCmdBuf->cmdBuf, buffer->buffer, offsetBytes);
    currentCmdBuf->referencedResources.push_back(currentComputeState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

