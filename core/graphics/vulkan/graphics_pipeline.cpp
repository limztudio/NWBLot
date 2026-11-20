// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "framebuffer_rendering_preparation.h"
#include "arena_names.h"
#include "command_validation.h"
#include "vulkan_viewport_policy.h"

#include <core/common/log.h>

#include <global/algorithm.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_DynamicRenderingBeginFailedReason = FOUNDATION_TEXT("dynamic rendering could not begin");


b8 ViewportsEqual(const FixedVector<Viewport, s_MaxViewports>& lhs, const FixedVector<Viewport, s_MaxViewports>& rhs)noexcept{
    if(lhs.size() != rhs.size())
        return false;
    for(usize i = 0u; i < lhs.size(); ++i){
        if(!(lhs[i] == rhs[i]))
            return false;
    }
    return true;
}

b8 ScissorsEqual(const FixedVector<Rect, s_MaxViewports>& lhs, const FixedVector<Rect, s_MaxViewports>& rhs)noexcept{
    if(lhs.size() != rhs.size())
        return false;
    for(usize i = 0u; i < lhs.size(); ++i){
        if(!(lhs[i] == rhs[i]))
            return false;
    }
    return true;
}

b8 VertexBufferBindingsEqual(const FixedVector<VertexBufferBinding, s_MaxVertexAttributes>& lhs, const FixedVector<VertexBufferBinding, s_MaxVertexAttributes>& rhs)noexcept{
    if(lhs.size() != rhs.size())
        return false;
    for(usize i = 0u; i < lhs.size(); ++i){
        if(!(lhs[i] == rhs[i]))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SetGraphicsDynamicState(const VolkDeviceTable& deviceDispatch, VkCommandBuffer commandBuffer, const GraphicsPipelineDesc& desc, const GraphicsState& state){
    const RasterState& rasterState = desc.renderState.rasterState;
    const DepthStencilState& depthStencilState = desc.renderState.depthStencilState;

    deviceDispatch.vkCmdSetLineWidth(commandBuffer, s_DefaultRasterLineWidth);
    deviceDispatch.vkCmdSetDepthBias(commandBuffer, static_cast<f32>(rasterState.depthBias), rasterState.depthBiasClamp, rasterState.slopeScaledDepthBias);

    const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
    deviceDispatch.vkCmdSetBlendConstants(commandBuffer, blendConstants.data());
    deviceDispatch.vkCmdSetDepthBounds(commandBuffer, 0.f, 1.f);
    deviceDispatch.vkCmdSetStencilCompareMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilReadMask);
    deviceDispatch.vkCmdSetStencilWriteMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilWriteMask);

    const u8 stencilRef = depthStencilState.dynamicStencilRef ? state.dynamicStencilRefValue : depthStencilState.stencilRefValue;
    deviceDispatch.vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, stencilRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginRenderPass(Framebuffer& framebuffer, const RenderPassParameters& params){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("begin render pass");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(!FramebufferRenderingPreparation(*this).validateRenderPassBegin(framebuffer, params, s_OperationName))
        return;
    if(!FramebufferRenderingPreparation(*this).prepareFramebuffer(&framebuffer, s_OperationName))
        return;

    if(m_enableAutomaticBarriers){
        setResourceStatesForFramebuffer(framebuffer);
        if(m_commandRecordingFailed)
            return;
        commitBarriers();
        if(m_commandRecordingFailed)
            return;
    }
    if(!beginDynamicRendering(framebuffer, params)){
        rejectCommandRecording(s_OperationName, __hidden_graphics_pipeline::s_DynamicRenderingBeginFailedReason);
        return;
    }

    retainResource(&framebuffer);
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_hasBoundGraphicsState = false;
    m_graphicsBuffersValidatedInPass = false;
    m_hasBoundComputeState = false;
    m_renderPassActive = true;
    m_renderPassFramebuffer = &framebuffer;
}

void CommandList::endRenderPass(){
    if(!publicCommandStateAccessible())
        return;
    if(!m_renderPassActive)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, FOUNDATION_TEXT("end render pass")))
        return;
    endActiveRenderPass();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setGraphicsState(const GraphicsState& state){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_SetGraphicsStateOperation))
        return;

    const b8 sameActiveRenderPass = m_renderPassActive && state.framebuffer && m_renderPassFramebuffer == state.framebuffer;
    if(
        m_hasBoundGraphicsState
        && sameActiveRenderPass
        && m_graphicsBuffersValidatedInPass
        && !state.shadingRateState.enabled
        && state.pipeline == m_currentGraphicsState.pipeline
        && state.framebuffer == m_currentGraphicsState.framebuffer
        && state.indexBuffer == m_currentGraphicsState.indexBuffer
        && state.indirectParams == m_currentGraphicsState.indirectParams
        && __hidden_graphics_pipeline::VertexBufferBindingsEqual(m_currentGraphicsState.vertexBuffers, state.vertexBuffers)
    ){
        if(!validateViewportState(state.viewport, s_SetGraphicsStateOperation))
            return;

        const b8 blendConstantsChanged = !(m_currentGraphicsState.blendConstantColor == state.blendConstantColor);
        const b8 dynamicStencilRefChanged = m_currentGraphicsState.dynamicStencilRefValue != state.dynamicStencilRefValue;
        const b8 viewportsEqual = __hidden_graphics_pipeline::ViewportsEqual(m_currentGraphicsState.viewport.viewports, state.viewport.viewports);
        const b8 explicitScissorsEqual = __hidden_graphics_pipeline::ScissorsEqual(m_currentGraphicsState.viewport.scissorRects, state.viewport.scissorRects);
        const b8 updateViewport = !viewportsEqual;
        const b8 updateScissor = !explicitScissorsEqual || (state.viewport.scissorRects.empty() && !viewportsEqual);

        m_currentGraphicsState.blendConstantColor = state.blendConstantColor;
        m_currentGraphicsState.dynamicStencilRefValue = state.dynamicStencilRefValue;
        m_currentGraphicsState.viewport = state.viewport;

        if(state.pipeline){
            if(blendConstantsChanged){
                const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
                m_context.deviceDispatch.vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants.data());
            }
            const DepthStencilState& depthStencilState = state.pipeline->m_desc.renderState.depthStencilState;
            if(depthStencilState.dynamicStencilRef && dynamicStencilRefChanged)
                m_context.deviceDispatch.vkCmdSetStencilReference(m_currentCmdBuf->m_cmdBuf, VK_STENCIL_FACE_FRONT_AND_BACK, state.dynamicStencilRefValue);
        }
        if(updateViewport || updateScissor)
            setViewportState(state.viewport, updateViewport, updateScissor);
        return;
    }

    if(!validateGraphicsState(state))
        return;
    if(!FramebufferRenderingPreparation(*this).prepareFramebuffer(state.framebuffer, s_SetGraphicsStateOperation))
        return;

    setResourceStatesForGraphicsBuffers(state);
    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;

    if(!sameActiveRenderPass){
        commitBarriers();
        if(m_commandRecordingFailed)
            return;
    }
    const b8 hasPreviousGraphicsState = m_hasBoundGraphicsState;
    const b8 pipelineChanged = !hasPreviousGraphicsState || m_currentGraphicsState.pipeline != state.pipeline;
    const b8 blendConstantsChanged = !(m_currentGraphicsState.blendConstantColor == state.blendConstantColor);
    const b8 dynamicStencilRefChanged = m_currentGraphicsState.dynamicStencilRefValue != state.dynamicStencilRefValue;
    const b8 viewportsEqual = hasPreviousGraphicsState && __hidden_graphics_pipeline::ViewportsEqual(m_currentGraphicsState.viewport.viewports, state.viewport.viewports);
    const b8 explicitScissorsEqual = hasPreviousGraphicsState && __hidden_graphics_pipeline::ScissorsEqual(m_currentGraphicsState.viewport.scissorRects, state.viewport.scissorRects);
    const b8 updateViewport = !viewportsEqual;
    const b8 updateScissor = !explicitScissorsEqual || (state.viewport.scissorRects.empty() && !viewportsEqual);
    const b8 vertexBuffersEqual = hasPreviousGraphicsState && __hidden_graphics_pipeline::VertexBufferBindingsEqual(m_currentGraphicsState.vertexBuffers, state.vertexBuffers);
    const b8 indexBufferChanged = !hasPreviousGraphicsState || !(m_currentGraphicsState.indexBuffer == state.indexBuffer);
    const b8 indirectParamsChanged = !hasPreviousGraphicsState || m_currentGraphicsState.indirectParams != state.indirectParams;

    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_hasBoundComputeState = false;
    m_currentGraphicsState = state;
    m_hasBoundGraphicsState = true;
    m_graphicsBuffersValidatedInPass = m_renderPassActive;

    if(state.pipeline){
        auto& pipeline = *MakeNotNull(state.pipeline);
        if(pipelineChanged){
            retainResource(&pipeline);
            m_context.deviceDispatch.vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipeline);
            VulkanDetail::SetGraphicsDynamicState(m_context.deviceDispatch, m_currentCmdBuf->m_cmdBuf, pipeline.m_desc, state);
        }
        else{
            if(blendConstantsChanged){
                const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
                m_context.deviceDispatch.vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants.data());
            }
            const DepthStencilState& depthStencilState = pipeline.m_desc.renderState.depthStencilState;
            if(depthStencilState.dynamicStencilRef && dynamicStencilRefChanged)
                m_context.deviceDispatch.vkCmdSetStencilReference(m_currentCmdBuf->m_cmdBuf, VK_STENCIL_FACE_FRONT_AND_BACK, state.dynamicStencilRefValue);
        }
    }

    if(updateViewport || updateScissor)
        setViewportState(state.viewport, updateViewport, updateScissor);

    if(!state.vertexBuffers.empty() && !vertexBuffersEqual){
        for(const VertexBufferBinding& binding : state.vertexBuffers)
            retainResource(binding.buffer);

        b8 alreadyAscending = true;
        for(usize i = 1u; i < state.vertexBuffers.size(); ++i){
            if(state.vertexBuffers[i].slot <= state.vertexBuffers[i - 1u].slot){
                alreadyAscending = false;
                break;
            }
        }
        const VertexBufferBinding* orderedData = state.vertexBuffers.data();
        const usize orderedCount = state.vertexBuffers.size();
        FixedVector<VertexBufferBinding, s_MaxVertexAttributes> sortedBindings;
        if(!alreadyAscending){
            sortedBindings = state.vertexBuffers;
            Sort(sortedBindings.begin(), sortedBindings.end(), [](const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{
                return lhs.slot < rhs.slot;
            });
            orderedData = sortedBindings.data();
        }

        usize startIndex = 0u;
        while(startIndex < orderedCount){
            const u32 firstSlot = orderedData[startIndex].slot;
            Array<VkBuffer, s_MaxVertexAttributes> nativeBuffers{};
            Array<VkDeviceSize, s_MaxVertexAttributes> offsets{};
            u32 count = 0u;
            while(startIndex + count < orderedCount && orderedData[startIndex + count].slot == firstSlot + count){
                nativeBuffers[count] = orderedData[startIndex + count].buffer->m_buffer;
                offsets[count] = orderedData[startIndex + count].offset;
                ++count;
            }
            m_context.deviceDispatch.vkCmdBindVertexBuffers(m_currentCmdBuf->m_cmdBuf, firstSlot, count, nativeBuffers.data(), offsets.data());
            startIndex += count;
        }
    }

    if(state.indexBuffer.buffer && indexBufferChanged){
        auto& ib = *MakeNotNull(state.indexBuffer.buffer);
        retainResource(&ib);
        const VkIndexType indexType = state.indexBuffer.format == Format::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        m_context.deviceDispatch.vkCmdBindIndexBuffer(m_currentCmdBuf->m_cmdBuf, ib.m_buffer, state.indexBuffer.offset, indexType);
    }
    if(indirectParamsChanged)
        retainResource(state.indirectParams);
}

void CommandList::draw(const DrawArguments& args){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("draw");
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(!validateGraphicsDrawArguments(args, false, s_OperationName))
        return;

    m_context.deviceDispatch.vkCmdDraw(m_currentCmdBuf->m_cmdBuf, args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndexed(const DrawArguments& args){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("draw indexed");
    if(args.vertexCount == 0 || args.instanceCount == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(!validateGraphicsDrawArguments(args, true, s_OperationName))
        return;

    m_context.deviceDispatch.vkCmdDrawIndexed(m_currentCmdBuf->m_cmdBuf, args.vertexCount, args.instanceCount, args.startIndexLocation, static_cast<i32>(args.startVertexLocation), args.startInstanceLocation);
}

void CommandList::drawIndirect(u32 offsetBytes, u32 drawCount){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("draw indirect");
    if(drawCount == 0u)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    const auto indirectBuffer = prepareDrawIndirect(offsetBytes, drawCount, sizeof(DrawIndirectArguments), s_OperationName, FOUNDATION_TEXT("drawIndirect"), VulkanDetail::IndirectDrawIndexMode::NonIndexed);
    if(!indirectBuffer)
        return;

    m_context.deviceDispatch.vkCmdDrawIndirect(m_currentCmdBuf->m_cmdBuf, (*indirectBuffer)->m_buffer, offsetBytes, drawCount, sizeof(DrawIndirectArguments));
    retainResource(m_currentGraphicsState.indirectParams);
}

void CommandList::drawIndexedIndirect(u32 offsetBytes, u32 drawCount){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("draw indexed indirect");
    if(drawCount == 0u)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    const auto indirectBuffer = prepareDrawIndirect(offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments), s_OperationName, FOUNDATION_TEXT("drawIndexedIndirect"), VulkanDetail::IndirectDrawIndexMode::Indexed);
    if(!indirectBuffer)
        return;

    m_context.deviceDispatch.vkCmdDrawIndexedIndirect(m_currentCmdBuf->m_cmdBuf, (*indirectBuffer)->m_buffer, offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments));
    retainResource(m_currentGraphicsState.indirectParams);
}

void CommandList::setViewportState(const ViewportState& viewportState, const b8 updateViewport, const b8 updateScissor){
    if(updateViewport && !viewportState.viewports.empty()){
        const auto& vp = viewportState.viewports[0u];
        const SIMDVectorSingle extent = VectorSubtract(VectorSet(vp.maxX, vp.minY, vp.maxX, vp.minY), VectorSet(vp.minX, vp.maxY, vp.minX, vp.maxY));
        const VkViewport viewport{ vp.minX, vp.maxY, VectorGetX(extent), VectorGetY(extent), vp.minZ, vp.maxZ };
        m_context.deviceDispatch.vkCmdSetViewport(m_currentCmdBuf->m_cmdBuf, 0u, 1u, &viewport);
    }

    if(updateScissor){
        if(!viewportState.scissorRects.empty()){
            VkRect2D scissor{};
            const auto& sr = viewportState.scissorRects[0];
            scissor.offset = { static_cast<int32_t>(sr.minX), static_cast<int32_t>(sr.minY) };
            scissor.extent = { static_cast<uint32_t>(sr.maxX - sr.minX), static_cast<uint32_t>(sr.maxY - sr.minY) };
            m_context.deviceDispatch.vkCmdSetScissor(m_currentCmdBuf->m_cmdBuf, 0u, 1u, &scissor);
        }
        else if(!viewportState.viewports.empty()){
            const auto scissor = VulkanViewportPolicy::BuildImplicitScissor(viewportState.viewports[0u]);
            if(!scissor)
                return;
            m_context.deviceDispatch.vkCmdSetScissor(m_currentCmdBuf->m_cmdBuf, 0u, 1u, &*scissor);
        }
    }
}

b8 CommandList::beginDynamicRendering(Framebuffer& framebuffer, const RenderPassParameters& params){
    FOUNDATION_ASSERT(framebuffer.m_framebufferInfo.width != 0u && framebuffer.m_framebufferInfo.height != 0u && framebuffer.m_framebufferInfo.arraySize != 0u);

    const FramebufferDesc& fbDesc = framebuffer.m_desc;

    Array<VkRenderingAttachmentInfo, s_MaxRenderTargets> colorAttachments = {};
    const u32 numColorAttachments = static_cast<u32>(fbDesc.colorAttachments.size());
    FOUNDATION_ASSERT(numColorAttachments <= s_MaxRenderTargets);

    for(u32 i = 0u; i < numColorAttachments; ++i){
        const auto view = framebuffer.resolveColorAttachmentView(i);
        if(!view){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
            FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
            return false;
        }

        colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView = *view;
        colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const VulkanDetail::RenderPassAttachmentOperations operations = VulkanDetail::ConvertRenderPassAttachmentActions(params.colorAttachmentActions[i]);
        colorAttachments[i].loadOp = operations.loadOp;
        colorAttachments[i].storeOp = operations.storeOp;
        if(operations.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR){
            const Color& clr = params.colorClearValues[i];
            colorAttachments[i].clearValue.color = {{ clr.r, clr.g, clr.b, clr.a }};
        }
    }

    auto depthAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    auto stencilAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    b8 hasDepth = false;
    b8 hasStencil = false;

    if(fbDesc.depthAttachment.texture){
        auto& depthTex = *MakeNotNull(fbDesc.depthAttachment.texture);
        const auto depthView = framebuffer.resolveDepthAttachmentView();
        if(!depthView){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
            return false;
        }

        const VkImageLayout depthStencilLayout = fbDesc.depthAttachment.isReadOnly ? VulkanDetail::s_ReadOnlyImageLayout : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        if((depthTex.m_aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0){
            const VulkanDetail::RenderPassAttachmentOperations operations = VulkanDetail::ConvertRenderPassAttachmentActions(params.depthAttachmentActions);
            depthAttachment.imageView = *depthView;
            depthAttachment.imageLayout = depthStencilLayout;
            depthAttachment.loadOp = operations.loadOp;
            depthAttachment.storeOp = operations.storeOp;
            depthAttachment.clearValue.depthStencil.depth = params.depthClearValue;
            hasDepth = true;
        }
        if((depthTex.m_aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0){
            const VulkanDetail::RenderPassAttachmentOperations operations = VulkanDetail::ConvertRenderPassAttachmentActions(params.stencilAttachmentActions);
            stencilAttachment.imageView = *depthView;
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
    renderingInfo.pColorAttachments = colorAttachments.data();
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

b8 CommandList::ensureGraphicsRenderPass(Framebuffer* framebuffer){
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
        rejectCommandRecording(FOUNDATION_TEXT("begin graphics render pass"), __hidden_graphics_pipeline::s_DynamicRenderingBeginFailedReason);
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
    m_graphicsBuffersValidatedInPass = false;
    m_renderPassActive = false;
    m_renderPassFramebuffer = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

