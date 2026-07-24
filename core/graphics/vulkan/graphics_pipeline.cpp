// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_MaxGraphicsPipelineShaderStageCount = 5u;


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
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_context(context)
{}
GraphicsPipeline::~GraphicsPipeline(){
    VulkanDetail::DestroyPipelineResource(m_context, *this, m_pipeline);
}
Object GraphicsPipeline::getNativeHandle(ObjectType objectType){
    return VulkanDetail::GetPipelineNativeHandle(m_pipeline, objectType);
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
        if(desc.colorAttachments[i].texture)
            fb->m_resources.emplace_back(desc.colorAttachments[i].texture, TextureHandle::deleter_type(&m_context.objectArena));
    }

    if(desc.depthAttachment.texture)
        fb->m_resources.emplace_back(desc.depthAttachment.texture, TextureHandle::deleter_type(&m_context.objectArena));

    return FramebufferHandle(fb, FramebufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo){
    if(!m_context.extensions.KHR_dynamic_rendering){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Dynamic rendering extension is required to create graphics pipelines."));
        return nullptr;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_GraphicsPipelineArena, s_GraphicsPipelineScratchArenaBytes);

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
    const bool usesTessellation = hasTessellationControlShader || hasTessellationEvaluationShader || desc.primType == PrimitiveType::PatchList || desc.patchControlPoints > 0;

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
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: patch control point count {} exceeds device limit {}")
                , desc.patchControlPoints
                , m_context.physicalDeviceProperties.limits.maxTessellationPatchSize
            );
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
    }

    // Step 1: Collect shader stages
    PipelineShaderStageVector shaderStages{ scratchArena };
    PipelineSpecializationInfoVector specInfos{ scratchArena };
    PipelineDescriptorHeapScratch descriptorHeapScratch{ scratchArena };
    shaderStages.reserve(VulkanDetail::s_MaxGraphicsPipelineShaderStageCount);
    specInfos.reserve(VulkanDetail::s_MaxGraphicsPipelineShaderStageCount);

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

    if(!configurePipelineBindingsOrDestroy(
        desc.bindingLayouts,
        NWB_TEXT("graphics pipeline"),
        shaderStages,
        descriptorHeapScratch,
        pso,
        scratchArena
    ))
        return nullptr;

    // Step 2: Vertex input state from InputLayout
    auto vertexInputInfo = VulkanDetail::MakeVkStruct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    if(desc.inputLayout){
        auto* layout = desc.inputLayout.get();

        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(layout->m_bindings.size());
        vertexInputInfo.pVertexBindingDescriptions = layout->m_bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(layout->m_vkAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = layout->m_vkAttributes.data();
    }

    // Step 3: Input assembly
    auto inputAssembly = VulkanDetail::MakeVkStruct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    inputAssembly.topology = VulkanDetail::ConvertPrimitiveTopology(desc.primType);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    auto tessellationState = VulkanDetail::MakeVkStruct<VkPipelineTessellationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO);
    tessellationState.patchControlPoints = desc.patchControlPoints;

    // Step 4: Rasterization state
    const RasterState& rasterState = desc.renderState.rasterState;
    auto rasterizer = VulkanDetail::BuildPipelineRasterizationState(
        rasterState,
        VulkanDetail::ConvertFillMode(rasterState.fillMode),
        rasterState.depthClipEnable ? VK_FALSE : VK_TRUE
    );

    // Step 5: Fixed-function state
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
    VulkanDetail::GraphicsPipelineFixedState fixedState{ scratchArena };
    if(!buildGraphicsPipelineFixedStateOrDestroy(
        fbinfo,
        desc.renderState,
        VulkanDetail::PipelineStencilFaceMode::IncludeStencilFaces,
        dynamicStates,
        static_cast<u32>(LengthOf(dynamicStates)),
        NWB_TEXT("graphics pipeline"),
        pso,
        fixedState
    ))
        return nullptr;

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    VulkanDetail::AttachPipelineBindingState(pipelineInfo, descriptorHeapScratch, *pso, &fixedState.renderingInfo);
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = (desc.patchControlPoints > 0) ? &tessellationState : nullptr;
    VulkanDetail::AttachGraphicsPipelineFixedState(pipelineInfo, rasterizer, fixedState);
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    if(!createPipelineOrDestroy(NWB_TEXT("graphics pipeline"), pso, pipelineInfo))
        return nullptr;

    return GraphicsPipelineHandle(pso, GraphicsPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandList::beginDynamicRendering(Framebuffer* framebuffer, const RenderPassParameters& params){
    auto* fb = framebuffer;
    NWB_ASSERT(fb);

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
            auto* tex = fbDesc.colorAttachments[i].texture;

            const TextureSubresourceSet resolvedColorSubresources = fbDesc.colorAttachments[i].subresources.resolve(tex->m_desc, TextureSubresourceMipResolve::Single);
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

    auto depthAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    auto stencilAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    bool hasDepth = false;
    bool hasStencil = false;

    if(fbDesc.depthAttachment.texture){
        auto* depthTex = fbDesc.depthAttachment.texture;
        const TextureSubresourceSet resolvedDepthSubresources = fbDesc.depthAttachment.subresources.resolve(depthTex->m_desc, TextureSubresourceMipResolve::Single);
        const TextureDimension::Enum depthViewDimension =
            resolvedDepthSubresources.numArraySlices == 1
            ? TextureDimension::Texture2D
            : depthTex->m_desc.dimension
        ;
        VkImageView depthView = depthTex->getView(fbDesc.depthAttachment.subresources, depthViewDimension, Format::UNKNOWN);
        if(depthView == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            return false;
        }

        const VkImageLayout depthStencilLayout =
            fbDesc.depthAttachment.isReadOnly
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        ;
        if((depthTex->m_aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0){
            depthAttachment.imageView = depthView;
            depthAttachment.imageLayout = depthStencilLayout;
            depthAttachment.loadOp = params.clearDepthTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil.depth = params.depthClearValue;
            hasDepth = true;
        }
        if((depthTex->m_aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0){
            stencilAttachment.imageView = depthView;
            stencilAttachment.imageLayout = depthStencilLayout;
            stencilAttachment.loadOp = params.clearStencilTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            stencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            stencilAttachment.clearValue.depthStencil.stencil = params.stencilClearValue;
            hasStencil = true;
        }
    }

    auto renderingInfo = VulkanDetail::MakeVkStruct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
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

bool CommandList::ensureGraphicsRenderPass(Framebuffer* framebuffer){
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
    setResourceStatesForBindingSets(state.bindings);
    setResourceStatesForGraphicsBuffers(state);
    commitBarriers();

    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;

    commitBarriers();
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_currentGraphicsState = state;

    auto* pipeline = state.pipeline;
    if(pipeline){
        vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipeline);
        VulkanDetail::SetGraphicsDynamicState(m_currentCmdBuf->m_cmdBuf, pipeline->m_desc, state);
    }

    if(pipeline)
        bindPipelineBindingSets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipelineLayout, pipeline->m_usesDescriptorHeap, pipeline->m_usesDescriptorBuffer, pipeline->m_descriptorHeapPushRanges, pipeline->m_descriptorHeapPushDataSize, state.bindings);

    setViewportState(state.viewport);

    if(!state.vertexBuffers.empty()){
        for(u32 i = 0; i < static_cast<u32>(state.vertexBuffers.size()); ++i){
            const auto& binding = state.vertexBuffers[i];
            auto* vb = binding.buffer;
#if defined(NWB_DEBUG)
            if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("bind vertex buffer"), NWB_TEXT("buffer is null"), vb)){
                m_currentGraphicsState = {};
                return;
            }
            if(binding.slot >= s_MaxVertexAttributes){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind vertex buffer: slot is out of range"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind vertex buffer: slot is out of range"));
                m_currentGraphicsState = {};
                return;
            }

            if(!vb->m_desc.isVertexBuffer){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind vertex buffer: buffer was not created with vertex-buffer usage"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to bind vertex buffer: buffer was not created with vertex-buffer usage"));
                m_currentGraphicsState = {};
                return;
            }
            if(!VulkanDetail::DebugValidateBufferRange(vb->m_desc, binding.offset, 1, NWB_TEXT("bind vertex buffer"), NWB_TEXT("vertex buffer"))){
                m_currentGraphicsState = {};
                return;
            }
#endif

            VkBuffer vertexBuffer = vb->m_buffer;
            VkDeviceSize offset = binding.offset;
            vkCmdBindVertexBuffers(m_currentCmdBuf->m_cmdBuf, binding.slot, 1, &vertexBuffer, &offset);
        }
    }

    if(state.indexBuffer.buffer){
        auto* ib = state.indexBuffer.buffer;
#if defined(NWB_DEBUG)
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
        if(!VulkanDetail::DebugValidateBufferRange(ib->m_desc, state.indexBuffer.offset, indexSizeBytes, NWB_TEXT("bind index buffer"), NWB_TEXT("index buffer"))){
            m_currentGraphicsState = {};
            return;
        }

        vkCmdBindIndexBuffer(m_currentCmdBuf->m_cmdBuf, ib->m_buffer, state.indexBuffer.offset, indexType);
#else
        const VkIndexType indexType = state.indexBuffer.format == Format::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(m_currentCmdBuf->m_cmdBuf, ib->m_buffer, state.indexBuffer.offset, indexType);
#endif
    }
}

void CommandList::draw(const DrawArguments& args){
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
#if defined(NWB_DEBUG)
    if(!m_renderPassActive || !m_currentGraphicsState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to draw: no graphics pipeline and active render pass are bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to draw: no graphics pipeline and active render pass are bound"));
        return;
    }
#endif

    vkCmdDraw(m_currentCmdBuf->m_cmdBuf, args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndexed(const DrawArguments& args){
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
#if defined(NWB_DEBUG)
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

    auto* ib = m_currentGraphicsState.indexBuffer.buffer;
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
    if(!VulkanDetail::DebugValidateBufferRange(ib->m_desc, indexByteOffset, indexByteSize, NWB_TEXT("draw indexed"), NWB_TEXT("index")))
        return;
#endif

    vkCmdDrawIndexed(m_currentCmdBuf->m_cmdBuf, args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndirect(u32 offsetBytes, u32 drawCount){
    Buffer* indirectBuffer = nullptr;
    if(!prepareDrawIndirect(
        offsetBytes,
        drawCount,
        sizeof(DrawIndirectArguments),
        NWB_TEXT("draw indirect"),
        NWB_TEXT("drawIndirect"),
        VulkanDetail::IndirectDrawIndexMode::NonIndexed,
        indirectBuffer
    ))
        return;

    vkCmdDrawIndirect(m_currentCmdBuf->m_cmdBuf, indirectBuffer->m_buffer, offsetBytes, drawCount, sizeof(DrawIndirectArguments));
    retainResource(m_currentGraphicsState.indirectParams);
}

void CommandList::drawIndexedIndirect(u32 offsetBytes, u32 drawCount){
    Buffer* indirectBuffer = nullptr;
    if(!prepareDrawIndirect(
        offsetBytes,
        drawCount,
        sizeof(DrawIndexedIndirectArguments),
        NWB_TEXT("draw indexed indirect"),
        NWB_TEXT("drawIndexedIndirect"),
        VulkanDetail::IndirectDrawIndexMode::Indexed,
        indirectBuffer
    ))
        return;

    vkCmdDrawIndexedIndirect(m_currentCmdBuf->m_cmdBuf, indirectBuffer->m_buffer, offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments));
    retainResource(m_currentGraphicsState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

