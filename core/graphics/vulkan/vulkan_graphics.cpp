// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


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

constexpr VkPolygonMode ConvertFillMode(RasterFillMode::Enum fillMode){
    switch(fillMode){
    case RasterFillMode::Solid:     return VK_POLYGON_MODE_FILL;
    case RasterFillMode::Wireframe: return VK_POLYGON_MODE_LINE;
    default: return VK_POLYGON_MODE_FILL;
    }
}

void SetGraphicsDynamicState(VkCommandBuffer commandBuffer, const GraphicsPipelineDesc& desc, const GraphicsState& state){
    const RasterState& rasterState = desc.renderState.rasterState;
    const DepthStencilState& depthStencilState = desc.renderState.depthStencilState;

    vkCmdSetLineWidth(commandBuffer, s_DefaultRasterLineWidth);
    vkCmdSetDepthBias(commandBuffer, static_cast<f32>(rasterState.depthBias), rasterState.depthBiasClamp, rasterState.slopeScaledDepthBias);

    const f32 blendConstants[] = {
        state.blendConstantColor.r,
        state.blendConstantColor.g,
        state.blendConstantColor.b,
        state.blendConstantColor.a,
    };
    vkCmdSetBlendConstants(commandBuffer, blendConstants);
    vkCmdSetDepthBounds(commandBuffer, 0.f, 1.f);
    vkCmdSetStencilCompareMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilReadMask);
    vkCmdSetStencilWriteMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilWriteMask);

    const u8 stencilRef = depthStencilState.dynamicStencilRef ? state.dynamicStencilRefValue : depthStencilState.stencilRefValue;
    vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, stencilRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsPipeline::GraphicsPipeline(const VulkanContext& context)
    : RefCounter<IGraphicsPipeline>(context.threadPool)
    , m_context(context)
{}
GraphicsPipeline::~GraphicsPipeline(){
    VulkanDetail::DestroyPipelineAndOwnedLayout(
        m_context.device,
        m_context.allocationCallbacks,
        m_pipeline,
        m_pipelineLayout,
        m_ownsPipelineLayout
    );
}
Object GraphicsPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(m_pipeline);
    return Object(nullptr);
}

void CommandList::setViewportState(const ViewportState& viewportState){
    if(viewportState.viewports.empty())
        return;

    const auto& vp = viewportState.viewports[0];
    VkViewport viewport{};
    viewport.x = vp.minX;
    viewport.y = vp.maxY;
    viewport.width = vp.maxX - vp.minX;
    viewport.height = -(vp.maxY - vp.minY);
    viewport.minDepth = vp.minZ;
    viewport.maxDepth = vp.maxZ;
    vkCmdSetViewport(m_currentCmdBuf->m_cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    if(!viewportState.scissorRects.empty()){
        const auto& sr = viewportState.scissorRects[0];
        scissor.offset = { static_cast<int32_t>(sr.minX), static_cast<int32_t>(sr.minY) };
        scissor.extent = { static_cast<uint32_t>(sr.maxX - sr.minX), static_cast<uint32_t>(sr.maxY - sr.minY) };
    }
    else{
        scissor.offset = { static_cast<int32_t>(vp.minX), static_cast<int32_t>(vp.minY) };
        scissor.extent = { static_cast<uint32_t>(vp.maxX - vp.minX), static_cast<uint32_t>(vp.maxY - vp.minY) };
    }
    vkCmdSetScissor(m_currentCmdBuf->m_cmdBuf, 0, 1, &scissor);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc){
    auto* fb = NewArenaObject<Framebuffer>(m_context.objectArena, m_context);
    fb->m_desc = desc;
    fb->m_framebufferInfo = FramebufferInfoEx(desc);

    constexpr u32 kMaxColorAttachments = s_MaxRenderTargets;
    const u32 colorAttachmentCount = Min<u32>(static_cast<u32>(desc.colorAttachments.size()), kMaxColorAttachments);
    if(desc.colorAttachments.size() > kMaxColorAttachments)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Framebuffer has more than {} color attachments; truncating to {}."), kMaxColorAttachments, kMaxColorAttachments);

    fb->m_resources.reserve(static_cast<usize>(colorAttachmentCount) + (desc.depthAttachment.texture ? 1u : 0u));
    for(u32 i = 0; i < colorAttachmentCount; ++i){
        if(desc.colorAttachments[i].texture){
            fb->m_resources.push_back(desc.colorAttachments[i].texture);
        }
    }

    if(desc.depthAttachment.texture){
        fb->m_resources.push_back(desc.depthAttachment.texture);
    }

    return FramebufferHandle(fb, FramebufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_dynamic_rendering){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Dynamic rendering extension is required to create graphics pipelines."));
        return nullptr;
    }

    Alloc::ScratchArena<> scratchArena(s_GraphicsPipelineScratchArenaBytes);

    auto* pso = NewArenaObject<GraphicsPipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;
    pso->m_framebufferInfo = fbinfo;

    if(!desc.VS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: vertex shader is required"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    const bool hasTessellationControlShader = static_cast<bool>(desc.HS);
    const bool hasTessellationEvaluationShader = static_cast<bool>(desc.DS);
    const bool usesTessellation = hasTessellationControlShader || hasTessellationEvaluationShader
        || desc.primType == PrimitiveType::PatchList
        || desc.patchControlPoints > 0;

    if(hasTessellationControlShader != hasTessellationEvaluationShader){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: tessellation control and evaluation shaders must both be provided"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }
    if(usesTessellation){
        if(!hasTessellationControlShader || !hasTessellationEvaluationShader){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: patch topology requires tessellation shaders"));
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
        if(desc.primType != PrimitiveType::PatchList){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: tessellation shaders require patch-list topology"));
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
        if(desc.patchControlPoints == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: tessellation patch control point count is zero"));
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
        if(desc.patchControlPoints > m_context.physicalDeviceProperties.limits.maxTessellationPatchSize){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create graphics pipeline: patch control point count {} exceeds device limit {}"),
                desc.patchControlPoints,
                m_context.physicalDeviceProperties.limits.maxTessellationPatchSize
            );
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
    }

    // Step 1: Collect shader stages
    PipelineShaderStageVector shaderStages{ Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>(scratchArena) };
    PipelineSpecializationInfoVector specInfos{ Alloc::ScratchAllocator<VkSpecializationInfo>(scratchArena) };
    PipelineDescriptorHeapScratch descriptorHeapScratch{ scratchArena };
    shaderStages.reserve(5);
    specInfos.reserve(5);

    if(desc.VS)
        appendPipelineShaderStage(desc.VS.get(), VK_SHADER_STAGE_VERTEX_BIT, specInfos, shaderStages);
    if(desc.HS)
        appendPipelineShaderStage(desc.HS.get(), VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, specInfos, shaderStages);
    if(desc.DS)
        appendPipelineShaderStage(desc.DS.get(), VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, specInfos, shaderStages);
    if(desc.GS)
        appendPipelineShaderStage(desc.GS.get(), VK_SHADER_STAGE_GEOMETRY_BIT, specInfos, shaderStages);
    if(desc.PS)
        appendPipelineShaderStage(desc.PS.get(), VK_SHADER_STAGE_FRAGMENT_BIT, specInfos, shaderStages);

    if(shaderStages.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: no shader stages provided"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    if(
        !configurePipelineBindings(
        desc.bindingLayouts,
        NWB_TEXT("graphics pipeline"),
        shaderStages,
        descriptorHeapScratch,
        *pso,
        scratchArena
        )
    ){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    // Step 2: Vertex input state from InputLayout
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = VulkanDetail::MakeVkStruct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);

    if(desc.inputLayout){
        auto* layout = checked_cast<InputLayout*>(desc.inputLayout.get());

        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(layout->m_bindings.size());
        vertexInputInfo.pVertexBindingDescriptions = layout->m_bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(layout->m_vkAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = layout->m_vkAttributes.data();
    }

    // Step 3: Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = VulkanDetail::MakeVkStruct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    inputAssembly.topology = VulkanDetail::ConvertPrimitiveTopology(desc.primType);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineTessellationStateCreateInfo tessellationState = VulkanDetail::MakeVkStruct<VkPipelineTessellationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO);
    tessellationState.patchControlPoints = desc.patchControlPoints;

    // Step 4: Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = VulkanDetail::MakeVkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Step 5: Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer = VulkanDetail::MakeVkStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rasterizer.depthClampEnable = desc.renderState.rasterState.depthClipEnable ? VK_FALSE : VK_TRUE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VulkanDetail::ConvertFillMode(desc.renderState.rasterState.fillMode);
    rasterizer.cullMode = VulkanDetail::ConvertCullMode(desc.renderState.rasterState.cullMode);
    rasterizer.frontFace = desc.renderState.rasterState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = desc.renderState.rasterState.depthBias != 0 ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = static_cast<f32>(desc.renderState.rasterState.depthBias);
    rasterizer.depthBiasClamp = desc.renderState.rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.renderState.rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = s_DefaultRasterLineWidth;

    // Step 6: Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling;
    if(
        !VulkanDetail::ConfigurePipelineMultisampleState(
        fbinfo.sampleCount,
        desc.renderState.blendState.alphaToCoverageEnable,
        multisampling,
        NWB_TEXT("graphics pipeline")
        )
    ){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    // Step 7: Depth/stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VulkanDetail::ConfigurePipelineDepthStencilState(desc.renderState.depthStencilState, true, depthStencil);

    // Step 8: Color blend state
    VkPipelineColorBlendStateCreateInfo colorBlending = VulkanDetail::MakeVkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    colorBlending.logicOpEnable = VK_FALSE;
    Vector<VkPipelineColorBlendAttachmentState, Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>> blendAttachments{ Alloc::ScratchAllocator<VkPipelineColorBlendAttachmentState>(scratchArena) };
    blendAttachments.reserve(fbinfo.colorFormats.size());
    for(u32 i = 0; i < static_cast<u32>(fbinfo.colorFormats.size()); ++i){
        blendAttachments.push_back(VulkanDetail::ConvertBlendState(desc.renderState.blendState.targets[i]));
    }
    colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
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

    VkPipelineDynamicStateCreateInfo dynamicState = VulkanDetail::MakeVkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamicState.dynamicStateCount = static_cast<uint32_t>(LengthOf(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    // Step 10: Dynamic rendering info
    VkPipelineRenderingCreateInfo renderingInfo = {};
    PipelineRenderingFormatVector colorFormats{ Alloc::ScratchAllocator<VkFormat>(scratchArena) };
    if(!VulkanDetail::BuildPipelineRenderingInfo(fbinfo, NWB_TEXT("graphics pipeline"), renderingInfo, colorFormats)){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = VulkanDetail::MakeVkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    if(pso->m_usesDescriptorHeap){
        pipelineInfo.pNext = descriptorHeapScratch.pNext(m_context.extensions.KHR_dynamic_rendering ? &renderingInfo : nullptr);
    }
    else if(m_context.extensions.KHR_dynamic_rendering)
        pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
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
    pipelineInfo.layout = pso->m_usesDescriptorHeap ? VK_NULL_HANDLE : pso->m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    res = vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    return GraphicsPipelineHandle(pso, GraphicsPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandList::beginDynamicRendering(IFramebuffer* framebuffer, const RenderPassParameters& params){
    auto* fb = checked_cast<Framebuffer*>(framebuffer);
    if(!fb)
        return false;

    if(fb->m_framebufferInfo.width == 0 || fb->m_framebufferInfo.height == 0 || fb->m_framebufferInfo.arraySize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to begin dynamic rendering: framebuffer dimensions are invalid"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin dynamic rendering: framebuffer dimensions are invalid"));
        return false;
    }

    const FramebufferDesc& fbDesc = fb->m_desc;

    // Dynamic rendering (VK_KHR_dynamic_rendering)
    constexpr u32 kMaxColorAttachments = s_MaxRenderTargets;
    VkRenderingAttachmentInfo colorAttachments[kMaxColorAttachments] = {};
    u32 numColorAttachments = 0;

    for(u32 i = 0; i < static_cast<u32>(fbDesc.colorAttachments.size()); ++i){
        if(numColorAttachments >= kMaxColorAttachments){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Render pass has more than {} color attachments; truncating to {}."), kMaxColorAttachments, kMaxColorAttachments);
            break;
        }

        if(fbDesc.colorAttachments[i].texture){
            auto* tex = checked_cast<Texture*>(fbDesc.colorAttachments[i].texture);

            const TextureSubresourceSet resolvedColorSubresources = fbDesc.colorAttachments[i].subresources.resolve(tex->m_desc, true);
            TextureDimension::Enum viewDimension = tex->m_desc.dimension;
            if(resolvedColorSubresources.numArraySlices == 1)
                viewDimension = TextureDimension::Texture2D;
            VkImageView view = tex->getView(fbDesc.colorAttachments[i].subresources, viewDimension, Format::UNKNOWN);
            if(view == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
                return false;
            }

            colorAttachments[numColorAttachments].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachments[numColorAttachments].imageView = view;
            colorAttachments[numColorAttachments].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            if(params.clearColorTargets && params.clearColorTarget(i)){
                colorAttachments[numColorAttachments].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                const Color& clr = params.colorClearValues[i];
                colorAttachments[numColorAttachments].clearValue.color = {{ clr.r, clr.g, clr.b, clr.a }};
            }
            else
                colorAttachments[numColorAttachments].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

            colorAttachments[numColorAttachments].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            ++numColorAttachments;
        }
    }

    VkRenderingAttachmentInfo depthAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    VkRenderingAttachmentInfo stencilAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    bool hasDepth = false;
    bool hasStencil = false;

    if(fbDesc.depthAttachment.texture){
        auto* depthTex = checked_cast<Texture*>(fbDesc.depthAttachment.texture);
        const TextureSubresourceSet resolvedDepthSubresources = fbDesc.depthAttachment.subresources.resolve(depthTex->m_desc, true);
        const TextureDimension::Enum depthViewDimension = resolvedDepthSubresources.numArraySlices == 1
            ? TextureDimension::Texture2D
            : depthTex->m_desc.dimension
        ;
        VkImageView depthView = depthTex->getView(fbDesc.depthAttachment.subresources, depthViewDimension, Format::UNKNOWN, true);
        if(depthView == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            return false;
        }

        const FormatInfo& formatInfo = GetFormatInfo(depthTex->m_desc.format);
        const VkImageLayout depthStencilLayout = fbDesc.depthAttachment.isReadOnly
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        ;
        if(formatInfo.hasDepth){
            depthAttachment.imageView = depthView;
            depthAttachment.imageLayout = depthStencilLayout;
            depthAttachment.loadOp = params.clearDepthTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil.depth = params.depthClearValue;
            hasDepth = true;
        }
        if(formatInfo.hasStencil){
            stencilAttachment.imageView = depthView;
            stencilAttachment.imageLayout = depthStencilLayout;
            stencilAttachment.loadOp = params.clearStencilTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            stencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            stencilAttachment.clearValue.depthStencil.stencil = params.stencilClearValue;
            hasStencil = true;
        }
    }

    VkRenderingInfo renderingInfo = VulkanDetail::MakeVkStruct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = { fb->m_framebufferInfo.width, fb->m_framebufferInfo.height };
    renderingInfo.layerCount = fb->m_framebufferInfo.arraySize;
    renderingInfo.colorAttachmentCount = numColorAttachments;
    renderingInfo.pColorAttachments = colorAttachments;
    if(hasDepth)
        renderingInfo.pDepthAttachment = &depthAttachment;
    if(hasStencil)
        renderingInfo.pStencilAttachment = &stencilAttachment;

    vkCmdBeginRendering(m_currentCmdBuf->m_cmdBuf, &renderingInfo);
    return true;
}

void CommandList::endDynamicRendering(){
    vkCmdEndRendering(m_currentCmdBuf->m_cmdBuf);
}

void CommandList::endRenderPass(){
    endActiveRenderPass();
}

bool CommandList::ensureGraphicsRenderPass(IFramebuffer* framebuffer){
    if(!framebuffer)
        return true;

    if(m_renderPassActive && m_renderPassFramebuffer == framebuffer)
        return true;

    endActiveRenderPass();

    if(m_enableAutomaticBarriers){
        setResourceStatesForFramebuffer(*framebuffer);
        commitBarriers();
    }

    RenderPassParameters params = {};
    if(!beginDynamicRendering(framebuffer, params))
        return false;
    m_renderPassActive = true;
    m_renderPassFramebuffer = framebuffer;
    return true;
}

void CommandList::endActiveRenderPass(){
    if(!m_renderPassActive)
        return;

    endDynamicRendering();
    m_renderPassActive = false;
    m_renderPassFramebuffer = nullptr;
}

void CommandList::setGraphicsState(const GraphicsState& state){
    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;
    commitBarriers();
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_currentGraphicsState = state;

    auto* pipeline = checked_cast<GraphicsPipeline*>(state.pipeline);
    if(pipeline){
        vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipeline);
        VulkanDetail::SetGraphicsDynamicState(m_currentCmdBuf->m_cmdBuf, pipeline->m_desc, state);
    }

    if(pipeline)
        bindPipelineBindingSets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipelineLayout, pipeline->m_usesDescriptorHeap, pipeline->m_descriptorHeapPushRanges, pipeline->m_descriptorHeapPushDataSize, state.bindings);

    setViewportState(state.viewport);

    // Bind vertex buffers
    if(!state.vertexBuffers.empty()){
        for(u32 i = 0; i < static_cast<u32>(state.vertexBuffers.size()); ++i){
            const auto& binding = state.vertexBuffers[i];
            if(!binding.buffer){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind vertex buffer: buffer is null"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind vertex buffer: buffer is null"));
                m_currentGraphicsState = {};
                return;
            }
            if(binding.slot >= s_MaxVertexAttributes){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind vertex buffer: slot is out of range"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind vertex buffer: slot is out of range"));
                m_currentGraphicsState = {};
                return;
            }

            auto* vb = checked_cast<Buffer*>(binding.buffer);
            if(!vb->m_desc.isVertexBuffer){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind vertex buffer: buffer was not created with vertex-buffer usage"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind vertex buffer: buffer was not created with vertex-buffer usage"));
                m_currentGraphicsState = {};
                return;
            }
            if(!VulkanDetail::IsBufferRangeInBounds(vb->m_desc, binding.offset, 1)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind vertex buffer: offset is outside the buffer"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind vertex buffer: offset is outside the buffer"));
                m_currentGraphicsState = {};
                return;
            }

            VkBuffer vertexBuffer = vb->m_buffer;
            VkDeviceSize offset = binding.offset;
            vkCmdBindVertexBuffers(m_currentCmdBuf->m_cmdBuf, binding.slot, 1, &vertexBuffer, &offset);
        }
    }

    // Bind index buffer
    if(state.indexBuffer.buffer){
        auto* ib = checked_cast<Buffer*>(state.indexBuffer.buffer);
        if(!ib->m_desc.isIndexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind index buffer: buffer was not created with index-buffer usage"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind index buffer: buffer was not created with index-buffer usage"));
            m_currentGraphicsState = {};
            return;
        }

        VkIndexType indexType;
        u32 indexSizeBytes;
        if(state.indexBuffer.format == Format::R16_UINT){
            indexType = VK_INDEX_TYPE_UINT16;
            indexSizeBytes = sizeof(u16);
        }
        else if(state.indexBuffer.format == Format::R32_UINT){
            indexType = VK_INDEX_TYPE_UINT32;
            indexSizeBytes = sizeof(u32);
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind index buffer: format must be R16_UINT or R32_UINT"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind index buffer: format must be R16_UINT or R32_UINT"));
            m_currentGraphicsState = {};
            return;
        }

        if((state.indexBuffer.offset % indexSizeBytes) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind index buffer: offset is not aligned to the index format"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind index buffer: offset is not aligned to the index format"));
            m_currentGraphicsState = {};
            return;
        }
        if(!VulkanDetail::IsBufferRangeInBounds(ib->m_desc, state.indexBuffer.offset, indexSizeBytes)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind index buffer: offset is outside the buffer"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind index buffer: offset is outside the buffer"));
            m_currentGraphicsState = {};
            return;
        }

        vkCmdBindIndexBuffer(m_currentCmdBuf->m_cmdBuf, ib->m_buffer, state.indexBuffer.offset, indexType);
    }
}

void CommandList::draw(const DrawArguments& args){
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
    if(!m_renderPassActive || !m_currentGraphicsState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to draw: no graphics pipeline and active render pass are bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to draw: no graphics pipeline and active render pass are bound"));
        return;
    }

    vkCmdDraw(m_currentCmdBuf->m_cmdBuf, args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndexed(const DrawArguments& args){
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
    if(!m_renderPassActive || !m_currentGraphicsState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to draw indexed: no graphics pipeline and active render pass are bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to draw indexed: no graphics pipeline and active render pass are bound"));
        return;
    }
    if(!m_currentGraphicsState.indexBuffer.buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to draw indexed: no index buffer is bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to draw indexed: no index buffer is bound"));
        return;
    }

    auto* ib = checked_cast<Buffer*>(m_currentGraphicsState.indexBuffer.buffer);
    u32 indexSizeBytes = 0;
    if(m_currentGraphicsState.indexBuffer.format == Format::R16_UINT)
        indexSizeBytes = sizeof(u16);
    else if(m_currentGraphicsState.indexBuffer.format == Format::R32_UINT)
        indexSizeBytes = sizeof(u32);
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to draw indexed: index buffer format must be R16_UINT or R32_UINT"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to draw indexed: index buffer format must be R16_UINT or R32_UINT"));
        return;
    }

    const u64 startIndexByteOffset = static_cast<u64>(args.startIndexLocation) * indexSizeBytes;
    if(static_cast<u64>(m_currentGraphicsState.indexBuffer.offset) > Limit<u64>::s_Max - startIndexByteOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to draw indexed: index byte offset overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to draw indexed: index byte offset overflows"));
        return;
    }

    const u64 indexByteOffset = static_cast<u64>(m_currentGraphicsState.indexBuffer.offset) + startIndexByteOffset;
    const u64 indexByteSize = static_cast<u64>(args.vertexCount) * indexSizeBytes;
    if(!VulkanDetail::IsBufferRangeInBounds(ib->m_desc, indexByteOffset, indexByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to draw indexed: requested index range is outside the index buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to draw indexed: requested index range is outside the index buffer"));
        return;
    }

    vkCmdDrawIndexed(m_currentCmdBuf->m_cmdBuf, args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndirect(u32 offsetBytes, u32 drawCount){
    Buffer* indirectBuffer = nullptr;
    if(!prepareDrawIndirect(offsetBytes, drawCount, sizeof(DrawIndirectArguments), NWB_TEXT("draw indirect"), NWB_TEXT("drawIndirect"), false, indirectBuffer))
        return;

    vkCmdDrawIndirect(m_currentCmdBuf->m_cmdBuf, indirectBuffer->m_buffer, offsetBytes, drawCount, sizeof(DrawIndirectArguments));
    m_currentCmdBuf->m_referencedResources.push_back(m_currentGraphicsState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

