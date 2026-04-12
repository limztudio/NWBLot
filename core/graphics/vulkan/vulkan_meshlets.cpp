// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_meshlets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr VkCullModeFlags ConvertCullMode(RasterCullMode::Enum cullMode){
    switch(cullMode){
    case RasterCullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    case RasterCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RasterCullMode::None:  return VK_CULL_MODE_NONE;
    default: return VK_CULL_MODE_BACK_BIT;
    }
}

constexpr VkCompareOp ConvertCompareOp(ComparisonFunc::Enum compareFunc){
    switch(compareFunc){
    case ComparisonFunc::Never:          return VK_COMPARE_OP_NEVER;
    case ComparisonFunc::Less:           return VK_COMPARE_OP_LESS;
    case ComparisonFunc::Equal:          return VK_COMPARE_OP_EQUAL;
    case ComparisonFunc::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case ComparisonFunc::Greater:        return VK_COMPARE_OP_GREATER;
    case ComparisonFunc::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
    case ComparisonFunc::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case ComparisonFunc::Always:         return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

constexpr VkBlendFactor ConvertBlendFactor(BlendFactor::Enum blendFactor){
    switch(blendFactor){
    case BlendFactor::Zero:             return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:              return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor:         return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::InvSrcColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:         return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::InvDstAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DstColor:         return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::InvDstColor:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case BlendFactor::ConstantColor:    return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::InvConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::Src1Color:        return VK_BLEND_FACTOR_SRC1_COLOR;
    case BlendFactor::InvSrc1Color:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case BlendFactor::Src1Alpha:        return VK_BLEND_FACTOR_SRC1_ALPHA;
    case BlendFactor::InvSrc1Alpha:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    default: return VK_BLEND_FACTOR_ZERO;
    }
}

constexpr VkBlendOp ConvertBlendOp(BlendOp::Enum blendOp){
    switch(blendOp){
    case BlendOp::Add:             return VK_BLEND_OP_ADD;
    case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:             return VK_BLEND_OP_MIN;
    case BlendOp::Max:             return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

constexpr VkPipelineColorBlendAttachmentState ConvertBlendState(const BlendState::RenderTarget& target){
    VkPipelineColorBlendAttachmentState state = {};
    state.blendEnable = target.blendEnable ? VK_TRUE : VK_FALSE;
    state.srcColorBlendFactor = ConvertBlendFactor(target.srcBlend);
    state.dstColorBlendFactor = ConvertBlendFactor(target.destBlend);
    state.colorBlendOp = ConvertBlendOp(target.blendOp);
    state.srcAlphaBlendFactor = ConvertBlendFactor(target.srcBlendAlpha);
    state.dstAlphaBlendFactor = ConvertBlendFactor(target.destBlendAlpha);
    state.alphaBlendOp = ConvertBlendOp(target.blendOpAlpha);
    state.colorWriteMask = 0;
    if(target.colorWriteMask & ColorMask::Red)
        state.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if(target.colorWriteMask & ColorMask::Green)
        state.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if(target.colorWriteMask & ColorMask::Blue)
        state.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if(target.colorWriteMask & ColorMask::Alpha)
        state.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipeline::MeshletPipeline(const VulkanContext& context)
    : RefCounter<IMeshletPipeline>(context.threadPool)
    , m_context(context)
{}
MeshletPipeline::~MeshletPipeline(){
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
Object MeshletPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(m_pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipelineHandle Device::createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_dynamic_rendering){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Dynamic rendering extension is required to create meshlet pipelines."));
        return nullptr;
    }
    if(!m_context.extensions.EXT_mesh_shader){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader extension is required to create meshlet pipelines."));
        return nullptr;
    }

    Alloc::ScratchArena<> scratchArena;

    auto* pso = NewArenaObject<MeshletPipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;

    Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>> shaderStages{ Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>(scratchArena) };
    Vector<VkSpecializationInfo, Alloc::ScratchAllocator<VkSpecializationInfo>> specInfos{ Alloc::ScratchAllocator<VkSpecializationInfo>(scratchArena) };
    Vector<VkDescriptorSetAndBindingMappingEXT, Alloc::ScratchAllocator<VkDescriptorSetAndBindingMappingEXT>> descriptorHeapMappings{ Alloc::ScratchAllocator<VkDescriptorSetAndBindingMappingEXT>(scratchArena) };
    Vector<VkShaderDescriptorSetAndBindingMappingInfoEXT, Alloc::ScratchAllocator<VkShaderDescriptorSetAndBindingMappingInfoEXT>> descriptorHeapStageMappings{ Alloc::ScratchAllocator<VkShaderDescriptorSetAndBindingMappingInfoEXT>(scratchArena) };
    shaderStages.reserve(s_MeshletPipelineStageReserveCount); // Task (optional), Mesh, Fragment
    specInfos.reserve(s_MeshletPipelineStageReserveCount);

    auto addShaderStage = [&](IShader* iShader, VkShaderStageFlagBits vkStage){
        auto* s = checked_cast<Shader*>(iShader);
        VkPipelineShaderStageCreateInfo stageInfo = __hidden_vulkan::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stageInfo.stage = vkStage;
        stageInfo.module = s->m_shaderModule;
        stageInfo.pName = s->m_entryPointName.c_str();

        if(!s->m_specializationEntries.empty()){
            VkSpecializationInfo specInfo{};
            specInfo.mapEntryCount = static_cast<u32>(s->m_specializationEntries.size());
            specInfo.pMapEntries = s->m_specializationEntries.data();
            specInfo.dataSize = s->m_specializationData.size();
            specInfo.pData = s->m_specializationData.data();
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
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    if(desc.PS)
        addShaderStage(desc.PS.get(), VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineCreateFlags2CreateInfo descriptorHeapFlags2{};
    pso->m_usesDescriptorHeap = DescriptorHeapManager::tryEnablePipeline(
        m_context,
        desc.bindingLayouts,
        shaderStages,
        pso->m_descriptorHeapPushRanges,
        pso->m_descriptorHeapPushDataSize,
        descriptorHeapFlags2,
        descriptorHeapMappings,
        descriptorHeapStageMappings
    );

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!pso->m_usesDescriptorHeap){
        if(desc.bindingLayouts.empty()){
            if(!__hidden_vulkan::CreatePipelineLayout(m_context, nullptr, 0, 0, pipelineLayout, NWB_TEXT("meshlet pipeline"))){
                DestroyArenaObject(m_context.objectArena, pso);
                return nullptr;
            }
            pso->m_ownsPipelineLayout = true;
        }
        else if(desc.bindingLayouts.size() == 1){
            auto* layout = checked_cast<BindingLayout*>(desc.bindingLayouts[0].get());
            if(!layout){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create meshlet pipeline: binding layout is invalid"));
                DestroyArenaObject(m_context.objectArena, pso);
                return nullptr;
            }
            pipelineLayout = layout->m_pipelineLayout;
            pso->m_pushConstantByteSize = layout->m_pushConstantByteSize;
        }
        else{
            Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> allDescriptorSetLayouts{ Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena) };
            u32 pushConstantByteSize = 0;
            for(u32 i = 0; i < static_cast<u32>(desc.bindingLayouts.size()); ++i){
                auto* bl = checked_cast<BindingLayout*>(desc.bindingLayouts[i].get());
                if(!bl){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create meshlet pipeline: binding layout {} is invalid"), i);
                    DestroyArenaObject(m_context.objectArena, pso);
                    return nullptr;
                }
                const BindingLayoutDesc& bindingLayoutDesc = bl->getBindingLayoutDesc();
                pushConstantByteSize = Max<u32>(pushConstantByteSize, __hidden_vulkan::GetPushConstantByteSize(bindingLayoutDesc));
                for(const auto& dsl : bl->m_descriptorSetLayouts)
                    allDescriptorSetLayouts.push_back(dsl);
            }
            pso->m_pushConstantByteSize = pushConstantByteSize;

            if(!__hidden_vulkan::CreatePipelineLayout(
                m_context,
                allDescriptorSetLayouts.data(),
                static_cast<u32>(allDescriptorSetLayouts.size()),
                pushConstantByteSize,
                pipelineLayout,
                NWB_TEXT("meshlet pipeline")))
            {
                DestroyArenaObject(m_context.objectArena, pso);
                return nullptr;
            }
            pso->m_ownsPipelineLayout = true;
        }
    }
    pso->m_pipelineLayout = pipelineLayout;

    VkPipelineRasterizationStateCreateInfo rasterizer = __hidden_vulkan::MakeVkStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = __hidden_vulkan_meshlets::ConvertCullMode(desc.renderState.rasterState.cullMode);
    rasterizer.frontFace = desc.renderState.rasterState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = desc.renderState.rasterState.depthBias != 0 ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = static_cast<f32>(desc.renderState.rasterState.depthBias);
    rasterizer.depthBiasClamp = desc.renderState.rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.renderState.rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = s_DefaultRasterLineWidth;

    VkPipelineViewportStateCreateInfo viewportState = __hidden_vulkan::MakeVkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampling = __hidden_vulkan::MakeVkStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisampling.rasterizationSamples = __hidden_vulkan::GetSampleCount(fbinfo.sampleCount);
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.alphaToCoverageEnable = desc.renderState.blendState.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil = __hidden_vulkan::MakeVkStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    depthStencil.depthTestEnable = desc.renderState.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.renderState.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = __hidden_vulkan_meshlets::ConvertCompareOp(desc.renderState.depthStencilState.depthFunc);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = desc.renderState.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;

    Vector<VkPipelineColorBlendAttachmentState, Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>> blendAttachments{ Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>(scratchArena) };
    blendAttachments.reserve(fbinfo.colorFormats.empty() ? 1 : fbinfo.colorFormats.size());
    for(u32 i = 0; i < fbinfo.colorFormats.size(); ++i){
        blendAttachments.push_back(__hidden_vulkan_meshlets::ConvertBlendState(desc.renderState.blendState.targets[i]));
    }
    if(blendAttachments.empty()){
        VkPipelineColorBlendAttachmentState attachment = {};
        attachment.blendEnable = VK_FALSE;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(attachment);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = __hidden_vulkan::MakeVkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    colorBlending.attachmentCount = static_cast<u32>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState = __hidden_vulkan::MakeVkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamicState.dynamicStateCount = static_cast<u32>(LengthOf(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    Vector<VkFormat, Alloc::ScratchAllocator<VkFormat>> colorFormats{ Alloc::ScratchAllocator<VkFormat>(scratchArena) };
    colorFormats.reserve(fbinfo.colorFormats.size());
    for(const auto& format : fbinfo.colorFormats){
        colorFormats.push_back(ConvertFormat(format));
    }

    VkPipelineRenderingCreateInfo renderingInfo = __hidden_vulkan::MakeVkStruct<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    renderingInfo.colorAttachmentCount = static_cast<u32>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = ConvertFormat(fbinfo.depthFormat);
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineInfo = __hidden_vulkan::MakeVkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    if(pso->m_usesDescriptorHeap){
        descriptorHeapFlags2.pNext = &renderingInfo;
        pipelineInfo.pNext = &descriptorHeapFlags2;
    }
    else
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
    pipelineInfo.layout = pso->m_usesDescriptorHeap ? VK_NULL_HANDLE : pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    res = vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create meshlet pipeline: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    return MeshletPipelineHandle(pso, MeshletPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setMeshletState(const MeshletState& state){
    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;
    commitBarriers();
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentRayTracingState = {};
    m_currentMeshletState = state;

    auto* pipeline = checked_cast<MeshletPipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipeline);

    if(pipeline)
        retainBindingSets(state.bindings);

    if(pipeline && pipeline->m_usesDescriptorHeap)
        bindDescriptorHeapState(true, pipeline->m_descriptorHeapPushRanges, pipeline->m_descriptorHeapPushDataSize, state.bindings);
    else if(state.bindings.size() > 0 && pipeline && pipeline->m_pipelineLayout != VK_NULL_HANDLE){
        for(usize i = 0; i < state.bindings.size(); ++i){
            if(state.bindings[i]){
                auto* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->m_descriptorSets.empty())
                    vkCmdBindDescriptorSets(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->m_pipelineLayout, static_cast<u32>(i),
                        static_cast<u32>(bindingSet->m_descriptorSets.size()),
                        bindingSet->m_descriptorSets.data(), 0, nullptr);
            }
        }
    }

    if(!state.viewport.viewports.empty()){
        const auto& vp = state.viewport.viewports[0];
        VkViewport viewport{};
        viewport.x = vp.minX;
        viewport.y = vp.maxY;
        viewport.width = vp.maxX - vp.minX;
        viewport.height = -(vp.maxY - vp.minY);
        viewport.minDepth = vp.minZ;
        viewport.maxDepth = vp.maxZ;
        vkCmdSetViewport(m_currentCmdBuf->m_cmdBuf, 0, 1, &viewport);

        VkRect2D scissor{};
        if(!state.viewport.scissorRects.empty()){
            const auto& sr = state.viewport.scissorRects[0];
            scissor.offset = { static_cast<int32_t>(sr.minX), static_cast<int32_t>(sr.minY) };
            scissor.extent = { static_cast<uint32_t>(sr.maxX - sr.minX), static_cast<uint32_t>(sr.maxY - sr.minY) };
        }
        else{
            scissor.offset = { static_cast<int32_t>(vp.minX), static_cast<int32_t>(vp.minY) };
            scissor.extent = { static_cast<uint32_t>(vp.maxX - vp.minX), static_cast<uint32_t>(vp.maxY - vp.minY) };
        }
        vkCmdSetScissor(m_currentCmdBuf->m_cmdBuf, 0, 1, &scissor);
    }
}

void CommandList::dispatchMesh(u32 groupsX, u32 groupsY, u32 groupsZ){
    if(groupsX == 0 || groupsY == 0 || groupsZ == 0)
        return;
    if(!m_renderPassActive || !m_currentMeshletState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch mesh tasks: no meshlet pipeline and active render pass are bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch mesh tasks: no meshlet pipeline and active render pass are bound"));
        return;
    }
    if(!vkCmdDrawMeshTasksEXT){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader dispatch requested, but vkCmdDrawMeshTasksEXT is unavailable."));
        return;
    }

    vkCmdDrawMeshTasksEXT(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
