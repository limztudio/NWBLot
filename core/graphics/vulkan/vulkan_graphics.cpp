// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr VkPrimitiveTopology ConvertPrimitiveTopology(PrimitiveType::Enum primType){
    switch(primType){
    case PrimitiveType::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveType::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveType::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveType::TriangleFan:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case PrimitiveType::TriangleListWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
    case PrimitiveType::TriangleStripWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
    case PrimitiveType::PatchList:     return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

constexpr VkCullModeFlags ConvertCullMode(RasterCullMode::Enum cullMode){
    switch(cullMode){
    case RasterCullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    case RasterCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RasterCullMode::None:  return VK_CULL_MODE_NONE;
    default: return VK_CULL_MODE_BACK_BIT;
    }
}

constexpr VkPolygonMode ConvertFillMode(RasterFillMode::Enum fillMode){
    switch(fillMode){
    case RasterFillMode::Solid:     return VK_POLYGON_MODE_FILL;
    case RasterFillMode::Wireframe: return VK_POLYGON_MODE_LINE;
    default: return VK_POLYGON_MODE_FILL;
    }
}

constexpr VkCompareOp ConvertCompareOp(ComparisonFunc::Enum compareFunc){
    switch(compareFunc){
    case ComparisonFunc::Never:        return VK_COMPARE_OP_NEVER;
    case ComparisonFunc::Less:         return VK_COMPARE_OP_LESS;
    case ComparisonFunc::Equal:        return VK_COMPARE_OP_EQUAL;
    case ComparisonFunc::LessOrEqual:  return VK_COMPARE_OP_LESS_OR_EQUAL;
    case ComparisonFunc::Greater:      return VK_COMPARE_OP_GREATER;
    case ComparisonFunc::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case ComparisonFunc::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case ComparisonFunc::Always:       return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

constexpr VkStencilOp ConvertStencilOp(StencilOp::Enum stencilOp){
    switch(stencilOp){
    case StencilOp::Keep:              return VK_STENCIL_OP_KEEP;
    case StencilOp::Zero:              return VK_STENCIL_OP_ZERO;
    case StencilOp::Replace:           return VK_STENCIL_OP_REPLACE;
    case StencilOp::IncrementAndClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::DecrementAndClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::Invert:            return VK_STENCIL_OP_INVERT;
    case StencilOp::IncrementAndWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOp::DecrementAndWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;
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

constexpr VkStencilOpState ConvertStencilOpState(const DepthStencilState& dsState, const DepthStencilState::StencilOpDesc& stencilDesc){
    VkStencilOpState state = {};
    state.failOp = ConvertStencilOp(stencilDesc.failOp);
    state.passOp = ConvertStencilOp(stencilDesc.passOp);
    state.depthFailOp = ConvertStencilOp(stencilDesc.depthFailOp);
    state.compareOp = ConvertCompareOp(stencilDesc.stencilFunc);
    state.compareMask = dsState.stencilReadMask;
    state.writeMask = dsState.stencilWriteMask;
    state.reference = dsState.stencilRefValue;
    return state;
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
    if(target.colorWriteMask & ColorMask::Red)   state.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if(target.colorWriteMask & ColorMask::Green) state.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if(target.colorWriteMask & ColorMask::Blue)  state.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if(target.colorWriteMask & ColorMask::Alpha) state.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsPipeline::GraphicsPipeline(const VulkanContext& context)
    : m_context(context)
{}
GraphicsPipeline::~GraphicsPipeline(){
    if(pipeline){
        vkDestroyPipeline(m_context.device, pipeline, m_context.allocationCallbacks);
        pipeline = VK_NULL_HANDLE;
    }
}
Object GraphicsPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc){
    auto* fb = NewArenaObject<Framebuffer>(*m_context.objectArena, m_context);
    fb->desc = desc;

    for(u32 i = 0; i < static_cast<u32>(desc.colorAttachments.size()); ++i){
        if(desc.colorAttachments[i].texture){
            fb->resources.push_back(desc.colorAttachments[i].texture);
            auto* tex = checked_cast<Texture*>(desc.colorAttachments[i].texture);
            fb->framebufferInfo.colorFormats.push_back(tex->desc.format);

            if(fb->framebufferInfo.width == 0)
                fb->framebufferInfo.width = tex->desc.width;
            if(fb->framebufferInfo.height == 0)
                fb->framebufferInfo.height = tex->desc.height;
        }
    }

    if(desc.depthAttachment.texture){
        fb->resources.push_back(desc.depthAttachment.texture);
        auto* depthTex = checked_cast<Texture*>(desc.depthAttachment.texture);
        fb->framebufferInfo.depthFormat = depthTex->desc.format;

        if(fb->framebufferInfo.width == 0)
            fb->framebufferInfo.width = depthTex->desc.width;
        if(fb->framebufferInfo.height == 0)
            fb->framebufferInfo.height = depthTex->desc.height;
    }

    if(!fb->resources.empty()){
        auto* tex = checked_cast<Texture*>(fb->resources[0].get());
        fb->framebufferInfo.sampleCount = tex->desc.sampleCount;
    }

    return FramebufferHandle(fb, FramebufferHandle::deleter_type(m_context.objectArena), AdoptRef);
}


GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena(2048);

    auto* pso = NewArenaObject<GraphicsPipeline>(*m_context.objectArena, m_context);
    pso->desc = desc;
    pso->framebufferInfo = fbinfo;

    // Step 1: Collect shader stages
    Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>> shaderStages{ Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>(scratchArena) };
    Vector<VkSpecializationInfo, Alloc::ScratchAllocator<VkSpecializationInfo>> specInfos{ Alloc::ScratchAllocator<VkSpecializationInfo>(scratchArena) };
    shaderStages.reserve(5);
    specInfos.reserve(5);

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

    if(desc.VS)
        addShaderStage(desc.VS.get(), VK_SHADER_STAGE_VERTEX_BIT);
    if(desc.HS)
        addShaderStage(desc.HS.get(), VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    if(desc.DS)
        addShaderStage(desc.DS.get(), VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    if(desc.GS)
        addShaderStage(desc.GS.get(), VK_SHADER_STAGE_GEOMETRY_BIT);
    if(desc.PS)
        addShaderStage(desc.PS.get(), VK_SHADER_STAGE_FRAGMENT_BIT);

    // Step 2: Vertex input state from InputLayout
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    if(desc.inputLayout){
        auto* layout = checked_cast<InputLayout*>(desc.inputLayout.get());

        vertexInputInfo.vertexBindingDescriptionCount = (u32)layout->bindings.size();
        vertexInputInfo.pVertexBindingDescriptions = layout->bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = (u32)layout->vkAttributes.size();
        vertexInputInfo.pVertexAttributeDescriptions = layout->vkAttributes.data();
    }

    // Step 3: Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = __hidden_vulkan::ConvertPrimitiveTopology(desc.primType);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineTessellationStateCreateInfo tessellationState = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
    tessellationState.patchControlPoints = desc.patchControlPoints;

    // Step 4: Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Step 5: Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = desc.renderState.rasterState.depthClipEnable ? VK_FALSE : VK_TRUE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = __hidden_vulkan::ConvertFillMode(desc.renderState.rasterState.fillMode);
    rasterizer.cullMode = __hidden_vulkan::ConvertCullMode(desc.renderState.rasterState.cullMode);
    rasterizer.frontFace = desc.renderState.rasterState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = desc.renderState.rasterState.depthBias != 0 ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = (f32)desc.renderState.rasterState.depthBias;
    rasterizer.depthBiasClamp = desc.renderState.rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.renderState.rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = 1.0f;

    // Step 6: Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = __hidden_vulkan::GetSampleCount(fbinfo.sampleCount);
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.alphaToCoverageEnable = desc.renderState.blendState.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;

    // Step 7: Depth/stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = desc.renderState.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.renderState.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = __hidden_vulkan::ConvertCompareOp(desc.renderState.depthStencilState.depthFunc);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = desc.renderState.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;
    depthStencil.front = __hidden_vulkan::ConvertStencilOpState(desc.renderState.depthStencilState, desc.renderState.depthStencilState.frontFaceStencil);
    depthStencil.back = __hidden_vulkan::ConvertStencilOpState(desc.renderState.depthStencilState, desc.renderState.depthStencilState.backFaceStencil);

    // Step 8: Color blend state
    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.logicOpEnable = VK_FALSE;
    Vector<VkPipelineColorBlendAttachmentState, Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>> blendAttachments{ Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>(scratchArena) };
    blendAttachments.reserve(fbinfo.colorFormats.size());
    for(u32 i = 0; i < (u32)fbinfo.colorFormats.size(); ++i){
        blendAttachments.push_back(__hidden_vulkan::ConvertBlendState(desc.renderState.blendState.targets[i]));
    }
    colorBlending.attachmentCount = (u32)blendAttachments.size();
    colorBlending.pAttachments = blendAttachments.data();

    // Step 9: Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };

    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(LengthOf(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    // Step 10: Pipeline layout from binding layouts
    pso->pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> allDescriptorSetLayouts{ Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena) };
    for(u32 i = 0; i < (u32)desc.bindingLayouts.size(); ++i){
        auto* bl = checked_cast<BindingLayout*>(desc.bindingLayouts[i].get());
        for(auto& dsl : bl->descriptorSetLayouts){
            allDescriptorSetLayouts.push_back(dsl);
        }
    }
    if(desc.bindingLayouts.size() == 1){
        auto* bl = checked_cast<BindingLayout*>(desc.bindingLayouts[0].get());
        pso->pipelineLayout = bl->pipelineLayout;
    } else if(desc.bindingLayouts.size() > 1){
        VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = (u32)allDescriptorSetLayouts.size();
        layoutInfo.pSetLayouts = allDescriptorSetLayouts.data();
        res = vkCreatePipelineLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &pso->pipelineLayout);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for graphics pipeline: {}"), ResultToString(res));
            DestroyArenaObject(*m_context.objectArena, pso);
            return nullptr;
        }
    }

    // Step 11: Dynamic rendering info
    VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    Vector<VkFormat, Alloc::ScratchAllocator<VkFormat>> colorFormats{ Alloc::ScratchAllocator<VkFormat>(scratchArena) };
    colorFormats.reserve(fbinfo.colorFormats.size());
    for(u32 i = 0; i < (u32)fbinfo.colorFormats.size(); ++i)
        colorFormats.push_back(ConvertFormat(fbinfo.colorFormats[i]));
    renderingInfo.colorAttachmentCount = (u32)colorFormats.size();
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    if(fbinfo.depthFormat != Format::UNKNOWN){
        const FormatInfo& depthFormatInfo = GetFormatInfo(fbinfo.depthFormat);
        if(depthFormatInfo.hasDepth)
            renderingInfo.depthAttachmentFormat = ConvertFormat(fbinfo.depthFormat);
        if(depthFormatInfo.hasStencil)
            renderingInfo.stencilAttachmentFormat = ConvertFormat(fbinfo.depthFormat);
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    if(m_context.extensions.KHR_dynamic_rendering)
        pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = (u32)shaderStages.size();
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = (desc.patchControlPoints > 0) ? &tessellationState : nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pso->pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    res = vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, pso);
        return nullptr;
    }

    return GraphicsPipelineHandle(pso, GraphicsPipelineHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginRenderPass(IFramebuffer* _framebuffer, const RenderPassParameters& params){
    auto* fb = checked_cast<Framebuffer*>(_framebuffer);
    const FramebufferDesc& fbDesc = fb->desc;

    // Dynamic rendering (VK_KHR_dynamic_rendering)
    VkRenderingAttachmentInfo colorAttachments[8] = {};
    u32 numColorAttachments = 0;

    for(u32 i = 0; i < static_cast<u32>(fbDesc.colorAttachments.size()); ++i){
        if(fbDesc.colorAttachments[i].texture){
            auto* tex = checked_cast<Texture*>(fbDesc.colorAttachments[i].texture);

            TextureDimension::Enum viewDimension = tex->desc.dimension;
            if(fbDesc.colorAttachments[i].subresources.numArraySlices == 1)
                viewDimension = TextureDimension::Texture2D;
            VkImageView view = tex->getView(fbDesc.colorAttachments[i].subresources, viewDimension, Format::UNKNOWN);

            colorAttachments[numColorAttachments].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachments[numColorAttachments].imageView = view;
            colorAttachments[numColorAttachments].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            if(params.clearColorTargets && params.clearColorTarget(i)){
                colorAttachments[numColorAttachments].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                const Color& clr = params.colorClearValues[i];
                colorAttachments[numColorAttachments].clearValue.color = { clr.r, clr.g, clr.b, clr.a };
            }
            else
                colorAttachments[numColorAttachments].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

            colorAttachments[numColorAttachments].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            numColorAttachments++;
        }
    }

    VkRenderingAttachmentInfo depthAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingAttachmentInfo stencilAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    bool hasDepth = false;
    bool hasStencil = false;

    if(fbDesc.depthAttachment.texture){
        auto* depthTex = checked_cast<Texture*>(fbDesc.depthAttachment.texture);
        VkImageView depthView = depthTex->getView(fbDesc.depthAttachment.subresources, TextureDimension::Texture2D, Format::UNKNOWN, true);

        const FormatInfo& formatInfo = GetFormatInfo(depthTex->desc.format);
        if(formatInfo.hasDepth){
            depthAttachment.imageView = depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = params.clearDepthTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil.depth = params.depthClearValue;
            hasDepth = true;
        }
        if(formatInfo.hasStencil){
            stencilAttachment.imageView = depthView;
            stencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAttachment.loadOp = params.clearStencilTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            stencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            stencilAttachment.clearValue.depthStencil.stencil = params.stencilClearValue;
            hasStencil = true;
        }
    }

    VkRenderingInfo renderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = { fb->framebufferInfo.width, fb->framebufferInfo.height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = numColorAttachments;
    renderingInfo.pColorAttachments = colorAttachments;
    if(hasDepth)
        renderingInfo.pDepthAttachment = &depthAttachment;
    if(hasStencil)
        renderingInfo.pStencilAttachment = &stencilAttachment;

    vkCmdBeginRendering(currentCmdBuf->cmdBuf, &renderingInfo);
}

void CommandList::endRenderPass(){
    vkCmdEndRendering(currentCmdBuf->cmdBuf);
}

void CommandList::setGraphicsState(const GraphicsState& state){
    currentGraphicsState = state;

    auto* pipeline = checked_cast<GraphicsPipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

    // Bind descriptor sets
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

    // Set viewport and scissor
    if(!state.viewport.viewports.empty()){
        const auto& vp = state.viewport.viewports[0];
        VkViewport viewport{};
        viewport.x = vp.minX;
        viewport.y = vp.maxY;
        viewport.width = vp.maxX - vp.minX;
        viewport.height = -(vp.maxY - vp.minY);
        viewport.minDepth = vp.minZ;
        viewport.maxDepth = vp.maxZ;
        vkCmdSetViewport(currentCmdBuf->cmdBuf, 0, 1, &viewport);

        VkRect2D scissor{};
        if(!state.viewport.scissorRects.empty()){
            const auto& sr = state.viewport.scissorRects[0];
            scissor.offset = { sr.minX, sr.minY };
            scissor.extent = { static_cast<u32>(sr.maxX - sr.minX), static_cast<u32>(sr.maxY - sr.minY) };
        }
        else{
            scissor.offset = { (i32)vp.minX, (i32)vp.minY };
            scissor.extent = { (u32)(vp.maxX - vp.minX), (u32)(vp.maxY - vp.minY) };
        }
        vkCmdSetScissor(currentCmdBuf->cmdBuf, 0, 1, &scissor);
    }

    // Bind vertex buffers
    if(!state.vertexBuffers.empty()){
        constexpr u32 kMaxVertexBuffers = 16;
        VkBuffer vertexBuffers[kMaxVertexBuffers];
        VkDeviceSize offsets[kMaxVertexBuffers];
        auto count = Min(static_cast<u32>(state.vertexBuffers.size()), kMaxVertexBuffers);
        for(u32 i = 0; i < count; ++i){
            auto* vb = checked_cast<Buffer*>(state.vertexBuffers[i].buffer);
            vertexBuffers[i] = vb->buffer;
            offsets[i] = state.vertexBuffers[i].offset;
        }
        vkCmdBindVertexBuffers(currentCmdBuf->cmdBuf, 0, count, vertexBuffers, offsets);
    }

    // Bind index buffer
    if(state.indexBuffer.buffer){
        auto* ib = checked_cast<Buffer*>(state.indexBuffer.buffer);
        VkIndexType indexType = (state.indexBuffer.format == Format::R16_UINT) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(currentCmdBuf->cmdBuf, ib->buffer, state.indexBuffer.offset, indexType);
    }
}

void CommandList::draw(const DrawArguments& args){
    vkCmdDraw(currentCmdBuf->cmdBuf, args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndexed(const DrawArguments& args){
    vkCmdDrawIndexed(currentCmdBuf->cmdBuf, args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndirect(u32 offsetBytes, u32 drawCount){
    if(!currentGraphicsState.indirectParams){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: No indirect buffer bound for drawIndirect"));
        return;
    }
    auto* indirectBuffer = checked_cast<Buffer*>(currentGraphicsState.indirectParams);
    vkCmdDrawIndirect(currentCmdBuf->cmdBuf, indirectBuffer->buffer, offsetBytes, drawCount, sizeof(DrawIndirectArguments));
    currentCmdBuf->referencedResources.push_back(currentGraphicsState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

