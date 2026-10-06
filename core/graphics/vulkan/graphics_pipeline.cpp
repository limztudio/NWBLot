// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "command_validation.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SetGraphicsDynamicState(
    const VolkDeviceTable& deviceDispatch,
    VkCommandBuffer commandBuffer,
    const GraphicsPipelineDesc& desc,
    const GraphicsState& state
){
    const RasterState& rasterState = desc.renderState.rasterState;
    const DepthStencilState& depthStencilState = desc.renderState.depthStencilState;

    deviceDispatch.vkCmdSetLineWidth(commandBuffer, s_DefaultRasterLineWidth);
    deviceDispatch.vkCmdSetDepthBias(commandBuffer, static_cast<f32>(rasterState.depthBias), rasterState.depthBiasClamp, rasterState.slopeScaledDepthBias);

    const f32 blendConstants[] = {
        state.blendConstantColor.r,
        state.blendConstantColor.g,
        state.blendConstantColor.b,
        state.blendConstantColor.a,
    };
    deviceDispatch.vkCmdSetBlendConstants(commandBuffer, blendConstants);
    deviceDispatch.vkCmdSetDepthBounds(commandBuffer, 0.f, 1.f);
    deviceDispatch.vkCmdSetStencilCompareMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilReadMask);
    deviceDispatch.vkCmdSetStencilWriteMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilWriteMask);

    const u8 stencilRef = depthStencilState.dynamicStencilRef ? state.dynamicStencilRefValue : depthStencilState.stencilRefValue;
    deviceDispatch.vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, stencilRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setViewportState(const ViewportState& viewportState){
    if(!viewportState.viewports.empty()){
        const auto& vp = viewportState.viewports[0u];
        const SIMDVector viewportMinimum = VectorSet(vp.minX, vp.minY, 0.0f, 0.0f);
        const SIMDVector viewportMaximum = VectorSet(vp.maxX, vp.maxY, 0.0f, 0.0f);
        const SIMDVector viewportExtent = VectorSubtract(viewportMaximum, viewportMinimum);
        const SIMDVector viewportOrigin = VectorPermute<0, 5, 0, 0>(viewportMinimum, viewportMaximum);
        const SIMDVector signedViewportExtent = VectorPermute<0, 5, 0, 0>(
            viewportExtent,
            VectorNegate(viewportExtent)
        );
        const SIMDVector viewportXYWH = VectorPermute<0, 1, 4, 5>(viewportOrigin, signedViewportExtent);
        Float4U viewportValues;
        StoreFloat(viewportXYWH, viewportValues);

        VkViewport viewport{};
        viewport.x = viewportValues.x;
        viewport.y = viewportValues.y;
        viewport.width = viewportValues.z;
        viewport.height = viewportValues.w;
        viewport.minDepth = vp.minZ;
        viewport.maxDepth = vp.maxZ;
        m_context.deviceDispatch.vkCmdSetViewport(m_currentCmdBuf->m_cmdBuf, 0u, 1u, &viewport);
    }

    if(!viewportState.scissorRects.empty()){
        VkRect2D scissor{};
        const auto& sr = viewportState.scissorRects[0];
        scissor.offset = { static_cast<int32_t>(sr.minX), static_cast<int32_t>(sr.minY) };
        scissor.extent = { static_cast<uint32_t>(sr.maxX - sr.minX), static_cast<uint32_t>(sr.maxY - sr.minY) };
        m_context.deviceDispatch.vkCmdSetScissor(m_currentCmdBuf->m_cmdBuf, 0u, 1u, &scissor);
    }
    else if(!viewportState.viewports.empty()){
        VkRect2D scissor{};
        const bool implicitScissorBuilt = VulkanDetail::BuildImplicitScissor(
            viewportState.viewports[0u],
            scissor
        );
        if(!implicitScissorBuilt)
            return;
        m_context.deviceDispatch.vkCmdSetScissor(m_currentCmdBuf->m_cmdBuf, 0u, 1u, &scissor);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandList::beginDynamicRendering(Framebuffer& framebuffer, const RenderPassParameters& params){
    if(framebuffer.m_framebufferInfo.width == 0 || framebuffer.m_framebufferInfo.height == 0 || framebuffer.m_framebufferInfo.arraySize == 0){
        NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to begin dynamic rendering: framebuffer dimensions are invalid"));
        GLB_ASSERT_MSG(false, GLB_TEXT("Vulkan: Failed to begin dynamic rendering: framebuffer dimensions are invalid"));
        return false;
    }

    const FramebufferDesc& fbDesc = framebuffer.m_desc;

    // Dynamic rendering (VK_KHR_dynamic_rendering)
    constexpr u32 s_MaxColorAttachments = s_MaxRenderTargets;
    VkRenderingAttachmentInfo colorAttachments[s_MaxColorAttachments] = {};
    const u32 numColorAttachments = static_cast<u32>(fbDesc.colorAttachments.size());
    GLB_ASSERT(numColorAttachments <= s_MaxColorAttachments);

    for(u32 i = 0u; i < numColorAttachments; ++i){
        auto* const tex = fbDesc.colorAttachments[i].texture;
        GLB_ASSERT(tex);

        const TextureSubresourceSet resolvedColorSubresources = fbDesc.colorAttachments[i].subresources.resolve(
            tex->m_creationDesc,
            TextureSubresourceMipResolve::Single
        );
        const TextureDimension::Enum viewDimension = VulkanDetail::GetFramebufferAttachmentViewDimension(
            tex->m_creationDesc,
            resolvedColorSubresources
        );
        VkImageView view = tex->getView(
            fbDesc.colorAttachments[i].subresources,
            viewDimension,
            fbDesc.colorAttachments[i].format
        );
        if(view == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
            GLB_ASSERT_MSG(false, GLB_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
            return false;
        }

        colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView = view;
        colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const VulkanDetail::RenderPassAttachmentOperations operations =
            VulkanDetail::ConvertRenderPassAttachmentActions(params.colorAttachmentActions[i])
        ;
        colorAttachments[i].loadOp = operations.loadOp;
        colorAttachments[i].storeOp = operations.storeOp;
        if(operations.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR){
            const Color& clr = params.colorClearValues[i];
            colorAttachments[i].clearValue.color = {{ clr.r, clr.g, clr.b, clr.a }};
        }
    }

    auto depthAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    auto stencilAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    bool hasDepth = false;
    bool hasStencil = false;

    if(fbDesc.depthAttachment.texture){
        auto* depthTex = fbDesc.depthAttachment.texture;
        const TextureSubresourceSet resolvedDepthSubresources = fbDesc.depthAttachment.subresources.resolve(
            depthTex->m_creationDesc,
            TextureSubresourceMipResolve::Single
        );
        const TextureDimension::Enum depthViewDimension = VulkanDetail::GetFramebufferAttachmentViewDimension(
            depthTex->m_creationDesc,
            resolvedDepthSubresources
        );
        VkImageView depthView = depthTex->getView(
            fbDesc.depthAttachment.subresources,
            depthViewDimension,
            fbDesc.depthAttachment.format
        );
        if(depthView == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            GLB_ASSERT_MSG(false, GLB_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            return false;
        }

        const VkImageLayout depthStencilLayout =
            fbDesc.depthAttachment.isReadOnly
            ? VulkanDetail::s_ReadOnlyImageLayout
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        ;
        if((depthTex->m_aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0){
            const VulkanDetail::RenderPassAttachmentOperations operations =
                VulkanDetail::ConvertRenderPassAttachmentActions(params.depthAttachmentActions)
            ;
            depthAttachment.imageView = depthView;
            depthAttachment.imageLayout = depthStencilLayout;
            depthAttachment.loadOp = operations.loadOp;
            depthAttachment.storeOp = operations.storeOp;
            depthAttachment.clearValue.depthStencil.depth = params.depthClearValue;
            hasDepth = true;
        }
        if((depthTex->m_aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0){
            const VulkanDetail::RenderPassAttachmentOperations operations =
                VulkanDetail::ConvertRenderPassAttachmentActions(params.stencilAttachmentActions)
            ;
            stencilAttachment.imageView = depthView;
            stencilAttachment.imageLayout = depthStencilLayout;
            stencilAttachment.loadOp = operations.loadOp;
            stencilAttachment.storeOp = operations.storeOp;
            stencilAttachment.clearValue.depthStencil.stencil = params.stencilClearValue;
            hasStencil = true;
        }
    }

    auto renderingInfo = VulkanDetail::MakeVkStruct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = { framebuffer.m_framebufferInfo.width, framebuffer.m_framebufferInfo.height };
    renderingInfo.layerCount = framebuffer.m_framebufferInfo.arraySize;
    renderingInfo.colorAttachmentCount = numColorAttachments;
    renderingInfo.pColorAttachments = colorAttachments;
    if(hasDepth)
        renderingInfo.pDepthAttachment = &depthAttachment;
    if(hasStencil)
        renderingInfo.pStencilAttachment = &stencilAttachment;

    m_context.deviceDispatch.vkCmdBeginRendering(m_currentCmdBuf->m_cmdBuf, &renderingInfo);
    return true;
}

void CommandList::endDynamicRendering(){
    m_context.deviceDispatch.vkCmdEndRendering(m_currentCmdBuf->m_cmdBuf);
}

void CommandList::beginRenderPass(Framebuffer& framebuffer, const RenderPassParameters& params){
    constexpr TStringView s_OperationName = GLB_TEXT("begin render pass");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(!validateRenderPassBegin(framebuffer, params, s_OperationName))
        return;
    if(!prepareFramebufferForRendering(&framebuffer, s_OperationName))
        return;

    // Match automatic graphics-state passes: compiler-owned recording may supply exact attachment barriers.
    if(m_enableAutomaticBarriers){
        setResourceStatesForFramebuffer(framebuffer);
        if(m_commandRecordingFailed)
            return;
        commitBarriers();
        if(m_commandRecordingFailed)
            return;
    }
    if(!beginDynamicRendering(framebuffer, params)){
        rejectCommandRecording(s_OperationName, GLB_TEXT("dynamic rendering could not begin"));
        return;
    }

    retainResource(&framebuffer);
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_renderPassActive = true;
    m_renderPassFramebuffer = &framebuffer;
}

void CommandList::endRenderPass(){
    if(!publicCommandStateAccessible())
        return;
    if(!m_renderPassActive)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, GLB_TEXT("end render pass")))
        return;
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
        if(m_commandRecordingFailed)
            return false;
    }

    RenderPassParameters params = {};
    if(!beginDynamicRendering(*framebuffer, params)){
        rejectCommandRecording(GLB_TEXT("begin graphics render pass"), GLB_TEXT("dynamic rendering could not begin"));
        return false;
    }
    retainResource(framebuffer);
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
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, GLB_TEXT("set graphics state")))
        return;
    if(!validateGraphicsState(state))
        return;
    if(!prepareFramebufferForRendering(state.framebuffer, GLB_TEXT("set graphics state")))
        return;

    setResourceStatesForGraphicsBuffers(state);
    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;

    commitBarriers();
    if(m_commandRecordingFailed)
        return;
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_currentGraphicsState = state;

    auto* pipeline = state.pipeline;
    if(pipeline){
        m_context.deviceDispatch.vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipeline);
        retainResource(pipeline);
        VulkanDetail::SetGraphicsDynamicState(m_context.deviceDispatch, m_currentCmdBuf->m_cmdBuf, pipeline->m_desc, state);
    }

    setViewportState(state.viewport);

    for(const VertexBufferBinding& binding : state.vertexBuffers){
        VkBuffer vertexBuffer = binding.buffer->m_buffer;
        VkDeviceSize offset = binding.offset;
        m_context.deviceDispatch.vkCmdBindVertexBuffers(m_currentCmdBuf->m_cmdBuf, binding.slot, 1, &vertexBuffer, &offset);
        retainResource(binding.buffer);
    }

    if(state.indexBuffer.buffer){
        auto* ib = state.indexBuffer.buffer;
        const VkIndexType indexType = state.indexBuffer.format == Format::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        m_context.deviceDispatch.vkCmdBindIndexBuffer(m_currentCmdBuf->m_cmdBuf, ib->m_buffer, state.indexBuffer.offset, indexType);
        retainResource(ib);
    }
    retainResource(state.indirectParams);
}

void CommandList::draw(const DrawArguments& args){
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, GLB_TEXT("draw")))
        return;
    if(!validateGraphicsDrawArguments(args, false, GLB_TEXT("draw")))
        return;

    m_context.deviceDispatch.vkCmdDraw(m_currentCmdBuf->m_cmdBuf, args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndexed(const DrawArguments& args){
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, GLB_TEXT("draw indexed")))
        return;
    if(!validateGraphicsDrawArguments(args, true, GLB_TEXT("draw indexed")))
        return;

    m_context.deviceDispatch.vkCmdDrawIndexed(
        m_currentCmdBuf->m_cmdBuf,
        args.vertexCount,
        args.instanceCount,
        args.startIndexLocation,
        static_cast<i32>(args.startVertexLocation),
        args.startInstanceLocation
    );
}

void CommandList::drawIndirect(u32 offsetBytes, u32 drawCount){
    if(drawCount == 0u)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, GLB_TEXT("draw indirect")))
        return;
    Buffer* indirectBuffer = nullptr;
    if(!prepareDrawIndirect(
        offsetBytes,
        drawCount,
        sizeof(DrawIndirectArguments),
        GLB_TEXT("draw indirect"),
        GLB_TEXT("drawIndirect"),
        VulkanDetail::IndirectDrawIndexMode::NonIndexed,
        indirectBuffer
    ))
        return;

    m_context.deviceDispatch.vkCmdDrawIndirect(m_currentCmdBuf->m_cmdBuf, indirectBuffer->m_buffer, offsetBytes, drawCount, sizeof(DrawIndirectArguments));
    retainResource(m_currentGraphicsState.indirectParams);
}

void CommandList::drawIndexedIndirect(u32 offsetBytes, u32 drawCount){
    if(drawCount == 0u)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, GLB_TEXT("draw indexed indirect")))
        return;
    Buffer* indirectBuffer = nullptr;
    if(!prepareDrawIndirect(
        offsetBytes,
        drawCount,
        sizeof(DrawIndexedIndirectArguments),
        GLB_TEXT("draw indexed indirect"),
        GLB_TEXT("drawIndexedIndirect"),
        VulkanDetail::IndirectDrawIndexMode::Indexed,
        indirectBuffer
    ))
        return;

    m_context.deviceDispatch.vkCmdDrawIndexedIndirect(m_currentCmdBuf->m_cmdBuf, indirectBuffer->m_buffer, offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments));
    retainResource(m_currentGraphicsState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

