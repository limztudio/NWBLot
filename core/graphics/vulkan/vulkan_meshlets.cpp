// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MeshletPipeline - Mesh shading pipeline state object
//-----------------------------------------------------------------------------

MeshletPipeline::~MeshletPipeline(){
    if(pipeline){
        const VulkanContext& vk = *m_Context;
        vk.vkDestroyPipeline(vk.device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}

Object MeshletPipeline::getNativeObject(ObjectType objectType){
    if(objectType == ObjectType::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}

//-----------------------------------------------------------------------------
// Device - Meshlet pipeline creation
//-----------------------------------------------------------------------------

MeshletPipelineHandle Device::createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo){
    // Meshlet pipeline uses mesh and task shaders (VK_EXT_mesh_shader)
    // Similar to graphics pipeline but with different shader stages
    
    MeshletPipeline* pso = new MeshletPipeline(m_Context);
    pso->desc = desc;
    
    const VulkanContext& vk = m_Context;
    
    // Collect shader stages
    Vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.reserve(3); // Task (optional), Mesh, Fragment
    
    // Task shader (optional - amplification)
    if(desc.AS){
        Shader* as = checked_cast<Shader*>(desc.AS.Get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
        stageInfo.module = as->shaderModule;
        stageInfo.pName = as->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    
    // Mesh shader (required)
    if(desc.MS){
        Shader* ms = checked_cast<Shader*>(desc.MS.Get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        stageInfo.module = ms->shaderModule;
        stageInfo.pName = ms->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    else{
        delete pso;
        return nullptr; // Mesh shader is required
    }
    
    // Fragment shader (optional)
    if(desc.PS){
        Shader* ps = checked_cast<Shader*>(desc.PS.Get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stageInfo.module = ps->shaderModule;
        stageInfo.pName = ps->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    
    // Get pipeline layout from binding layouts
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.bindingLayouts.empty() && desc.bindingLayouts[0]){
        BindingLayout* layout = checked_cast<BindingLayout*>(desc.bindingLayouts[0].Get());
        pipelineLayout = layout->pipelineLayout;
        pso->pipelineLayout = pipelineLayout;
    }
    
    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = desc.renderState.rasterState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = desc.renderState.rasterState.depthBias != 0 ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = static_cast<f32>(desc.renderState.rasterState.depthBias);
    rasterizer.depthBiasClamp = desc.renderState.rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.renderState.rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = 1.0f;
    
    // Viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(fbinfo.sampleCount);
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.alphaToCoverageEnable = desc.renderState.blendState.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
    
    // Depth/stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = desc.renderState.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.renderState.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = desc.renderState.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;
    
    // Color blend state
    Vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    for(u32 i = 0; i < fbinfo.colorFormats.size(); i++){
        VkPipelineColorBlendAttachmentState attachment = {};
        attachment.blendEnable = VK_FALSE;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(attachment);
    }
    if(blendAttachments.empty()){
        VkPipelineColorBlendAttachmentState attachment = {};
        attachment.blendEnable = VK_FALSE;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(attachment);
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.attachmentCount = static_cast<u32>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();
    
    // Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
    dynamicState.pDynamicStates = dynamicStates;
    
    // Dynamic rendering - fill color/depth formats from framebuffer
    Vector<VkFormat> colorFormats;
    for(const auto& format : fbinfo.colorFormats){
        colorFormats.push_back(ConvertFormat(format));
    }
    
    VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingInfo.colorAttachmentCount = static_cast<u32>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = ConvertFormat(fbinfo.depthFormat);
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<u32>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = nullptr; // Mesh shaders don't use vertex input
    pipelineInfo.pInputAssemblyState = nullptr; // Mesh shaders don't use input assembly
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    
    VkResult res = vkCreateGraphicsPipelines(vk.device, vk.pipelineCache, 1, &pipelineInfo, vk.allocationCallbacks, &pso->pipeline);
    
    if(res != VK_SUCCESS){
        delete pso;
        return nullptr;
    }
    
    return MeshletPipelineHandle::Create(pso);
}

//-----------------------------------------------------------------------------
// CommandList - Meshlet
//-----------------------------------------------------------------------------

MeshletState& CommandList::getMeshletState(){
    return stateTracker->meshletState;
}

void CommandList::setMeshletState(const MeshletState& state){
    stateTracker->meshletState = state;
    
    MeshletPipeline* pipeline = checked_cast<MeshletPipeline*>(state.pipeline.Get());
    const VulkanContext& vk = *m_Context;
    
    if(pipeline)
        vk.vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    
    // Bind descriptor sets
    if(state.bindings.size() > 0){
        for(usize i = 0; i < state.bindings.size(); i++){
            if(state.bindings[i].bindings){
                BindingSet* bindingSet = checked_cast<BindingSet*>(state.bindings[i].bindings.Get());
                if(bindingSet->descriptorSet != VK_NULL_HANDLE)
                    vk.vkCmdBindDescriptorSets(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipelineLayout, static_cast<u32>(i), 1, &bindingSet->descriptorSet, 0, nullptr);
            }
        }
    }
}

void CommandList::dispatchMesh(u32 groupsX, u32 groupsY, u32 groupsZ){
    const VulkanContext& vk = *m_Context;
    vk.vkCmdDrawMeshTasksEXT(currentCmdBuf->cmdBuf, groupsX, groupsY, groupsZ);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
