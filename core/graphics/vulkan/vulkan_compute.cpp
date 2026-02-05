// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// ComputePipeline - Compute pipeline state object
//-----------------------------------------------------------------------------

ComputePipeline::~ComputePipeline(){
    if(pipeline){
        const VulkanContext& vk = *m_Context;
        vk.vkDestroyPipeline(vk.device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}

Object ComputePipeline::getNativeObject(ObjectType objectType){
    if(objectType == ObjectType::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}

//-----------------------------------------------------------------------------
// Device - Compute pipeline creation
//-----------------------------------------------------------------------------

ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc){
    ComputePipeline* pso = new ComputePipeline(m_Context);
    pso->desc = desc;
    
    const VulkanContext& vk = m_Context;
    
    // Compute pipeline requires a compute shader
    if(!desc.CS){
        delete pso;
        return nullptr;
    }
    
    Shader* cs = checked_cast<Shader*>(desc.CS.Get());
    
    // Shader stage
    VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = cs->shaderModule;
    shaderStage.pName = cs->desc.entryName.c_str();
    
    // Get pipeline layout from binding layouts
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.bindingLayouts.empty() && desc.bindingLayouts[0]){
        BindingLayout* layout = checked_cast<BindingLayout*>(desc.bindingLayouts[0].Get());
        pipelineLayout = layout->pipelineLayout;
        pso->pipelineLayout = pipelineLayout;
    }
    
    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;
    
    VkResult res = vkCreateComputePipelines(vk.device, vk.pipelineCache, 1, &pipelineInfo, vk.allocationCallbacks, &pso->pipeline);
    
    if(res != VK_SUCCESS){
        delete pso;
        return nullptr;
    }
    
    return ComputePipelineHandle::Create(pso);
}

//-----------------------------------------------------------------------------
// CommandList - Compute
//-----------------------------------------------------------------------------

ComputeState& CommandList::getComputeState(){
    return stateTracker->computeState;
}

void CommandList::setComputeState(const ComputeState& state){
    stateTracker->computeState = state;
    
    const VulkanContext& vk = *m_Context;
    
    ComputePipeline* pipeline = checked_cast<ComputePipeline*>(state.pipeline.Get());
    if(pipeline)
        vk.vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    
    // Bind descriptor sets
    if(state.bindings.size() > 0){
        for(usize i = 0; i < state.bindings.size(); i++){
            if(state.bindings[i].bindings){
                BindingSet* bindingSet = checked_cast<BindingSet*>(state.bindings[i].bindings.Get());
                if(bindingSet->descriptorSet != VK_NULL_HANDLE)
                    vk.vkCmdBindDescriptorSets(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline->pipelineLayout, static_cast<u32>(i), 1, &bindingSet->descriptorSet, 0, nullptr);
            }
        }
    }
}

void CommandList::dispatch(u32 groupsX, u32 groupsY, u32 groupsZ){
    const VulkanContext& vk = *m_Context;
    vk.vkCmdDispatch(currentCmdBuf->cmdBuf, groupsX, groupsY, groupsZ);
}

void CommandList::dispatchIndirect(IBuffer* _buffer, u64 offsetBytes){
    Buffer* buffer = checked_cast<Buffer*>(_buffer);
    const VulkanContext& vk = *m_Context;
    
    vk.vkCmdDispatchIndirect(currentCmdBuf->cmdBuf, buffer->buffer, offsetBytes);
    currentCmdBuf->referencedResources.push_back(_buffer);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
