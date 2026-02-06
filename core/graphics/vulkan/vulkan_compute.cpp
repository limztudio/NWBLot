// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN

using __hidden::checked_cast;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// ComputePipeline - Compute pipeline state object
//-----------------------------------------------------------------------------

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

//-----------------------------------------------------------------------------
// Device - Compute pipeline creation
//-----------------------------------------------------------------------------

ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc){
    ComputePipeline* pso = new ComputePipeline(m_context);
    pso->desc = desc;
    
    // Compute pipeline requires a compute shader
    if(!desc.CS){
        delete pso;
        return nullptr;
    }
    
    Shader* cs = checked_cast<Shader*>(desc.CS.get());
    
    // Shader stage
    VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = cs->shaderModule;
    shaderStage.pName = cs->desc.entryName.c_str();
    
    // Get pipeline layout from binding layouts
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.bindingLayouts.empty() && desc.bindingLayouts[0]){
        BindingLayout* layout = checked_cast<BindingLayout*>(desc.bindingLayouts[0].get());
        pipelineLayout = layout->pipelineLayout;
        pso->pipelineLayout = pipelineLayout;
    }
    
    // Create compute pipeline
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

//-----------------------------------------------------------------------------
// CommandList - Compute
//-----------------------------------------------------------------------------

void CommandList::setComputeState(const ComputeState& state){
    currentComputeState = state;
    
    ComputePipeline* pipeline = checked_cast<ComputePipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    
    // Bind descriptor sets
    if(state.bindings.size() > 0){
        for(usize i = 0; i < state.bindings.size(); i++){
            if(state.bindings[i]){
                BindingSet* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
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
    // dispatchIndirect needs the indirect buffer from current compute state
    if(!currentComputeState.indirectParams){
        NWB_ASSERT(false && "No indirect buffer bound for dispatchIndirect");
        return;
    }
    Buffer* buffer = checked_cast<Buffer*>(currentComputeState.indirectParams);
    vkCmdDispatchIndirect(currentCmdBuf->cmdBuf, buffer->buffer, offsetBytes);
    currentCmdBuf->referencedResources.push_back(currentComputeState.indirectParams);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
