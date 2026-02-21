// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipeline::MeshletPipeline(const VulkanContext& context)
    : m_context(context)
{}
MeshletPipeline::~MeshletPipeline(){
    if(pipeline){
        vkDestroyPipeline(m_context.device, pipeline, m_context.allocationCallbacks);
        pipeline = VK_NULL_HANDLE;
    }
}
Object MeshletPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipelineHandle Device::createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* pso = NewArenaObject<MeshletPipeline>(*m_context.objectArena, m_context);
    pso->desc = desc;

    Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>> shaderStages{ Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>(scratchArena) };
    Vector<VkSpecializationInfo, Alloc::ScratchAllocator<VkSpecializationInfo>> specInfos{ Alloc::ScratchAllocator<VkSpecializationInfo>(scratchArena) };
    shaderStages.reserve(3); // Task (optional), Mesh, Fragment
    specInfos.reserve(3);

    auto addShaderStage = [&](IShader* iShader, VkShaderStageFlagBits vkStage){
        auto* s = checked_cast<Shader*>(iShader);
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = vkStage;
        stageInfo.module = s->shaderModule;
        stageInfo.pName = s->desc.entryName.c_str();

        if(!s->specializationEntries.empty()){
            VkSpecializationInfo specInfo{};
            specInfo.mapEntryCount = static_cast<u32>(s->specializationEntries.size());
            specInfo.pMapEntries = s->specializationEntries.data();
            specInfo.dataSize = s->specializationData.size();
            specInfo.pData = s->specializationData.data();
            specInfos.push_back(specInfo);
            stageInfo.pSpecializationInfo = &specInfos.back();
        }

        shaderStages.push_back(stageInfo);
    };

    if(desc.AS)
        addShaderStage(desc.AS.get(), VK_SHADER_STAGE_TASK_BIT_EXT);

    if(desc.MS)
        addShaderStage(desc.MS.get(), VK_SHADER_STAGE_MESH_BIT_EXT);
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader is required for meshlet pipeline"));
        DestroyArenaObject(*m_context.objectArena, pso);
        return nullptr;
    }

    if(desc.PS)
        addShaderStage(desc.PS.get(), VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.bindingLayouts.empty() && desc.bindingLayouts[0]){
        auto* layout = checked_cast<BindingLayout*>(desc.bindingLayouts[0].get());
        pipelineLayout = layout->pipelineLayout;
        pso->pipelineLayout = pipelineLayout;
    }

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

    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(fbinfo.sampleCount);
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.alphaToCoverageEnable = desc.renderState.blendState.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = desc.renderState.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.renderState.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = desc.renderState.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;

    Vector<VkPipelineColorBlendAttachmentState, Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>> blendAttachments{ Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>(scratchArena) };
    blendAttachments.reserve(fbinfo.colorFormats.empty() ? 1 : fbinfo.colorFormats.size());
    for(u32 i = 0; i < fbinfo.colorFormats.size(); ++i){
        VkPipelineColorBlendAttachmentState attachment = {};
        attachment.blendEnable = VK_FALSE;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(attachment);
    }
    if(blendAttachments.empty()){
        VkPipelineColorBlendAttachmentState attachment = {};
        attachment.blendEnable = VK_FALSE;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(attachment);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.attachmentCount = static_cast<u32>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = static_cast<u32>(LengthOf(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    Vector<VkFormat, Alloc::ScratchAllocator<VkFormat>> colorFormats{ Alloc::ScratchAllocator<VkFormat>(scratchArena) };
    colorFormats.reserve(fbinfo.colorFormats.size());
    for(const auto& format : fbinfo.colorFormats){
        colorFormats.push_back(ConvertFormat(format));
    }

    VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingInfo.colorAttachmentCount = static_cast<u32>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = ConvertFormat(fbinfo.depthFormat);
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

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

    res = vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create meshlet pipeline: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, pso);
        return nullptr;
    }

    return MeshletPipelineHandle(pso, MeshletPipelineHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setMeshletState(const MeshletState& state){
    currentMeshletState = state;

    auto* pipeline = checked_cast<MeshletPipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

    if(state.bindings.size() > 0){
        for(usize i = 0; i < state.bindings.size(); ++i){
            if(state.bindings[i]){
                auto* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->descriptorSets.empty())
                    vkCmdBindDescriptorSets(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipelineLayout, static_cast<u32>(i),
                        static_cast<u32>(bindingSet->descriptorSets.size()),
                        bindingSet->descriptorSets.data(), 0, nullptr);
            }
        }
    }
}

void CommandList::dispatchMesh(u32 groupsX, u32 groupsY, u32 groupsZ){
    vkCmdDrawMeshTasksEXT(currentCmdBuf->cmdBuf, groupsX, groupsY, groupsZ);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

