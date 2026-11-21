// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "framebuffer_rendering_preparation.h"
#include "arena_names.h"
#include "command_validation.h"
#include "state_tracking_detail.h"
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


b8 VertexBufferBindingsEqual(const FixedVector<VertexBufferBinding, s_MaxVertexAttributes>& lhs, const FixedVector<VertexBufferBinding, s_MaxVertexAttributes>& rhs)noexcept{
    if(lhs.size() != rhs.size())
        return false;
    const VertexBufferBinding* const lhsData = lhs.data();
    const VertexBufferBinding* const rhsData = rhs.data();
    for(usize i = 0u; i < lhs.size(); ++i){
        if(!(lhsData[i] == rhsData[i]))
            return false;
    }
    return true;
}

void CopyVertexBufferBindingsCompact(FixedVector<VertexBufferBinding, s_MaxVertexAttributes>& dst, const FixedVector<VertexBufferBinding, s_MaxVertexAttributes>& src)noexcept{
    dst.resize(src.size());
    for(usize i = 0u; i < src.size(); ++i)
        dst.data()[i] = src.data()[i];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::bindVertexBuffersNative(const Span<const VertexBufferBinding> vertexBuffers)noexcept{
    b8 alreadyAscending = true;
    for(usize i = 1u; i < vertexBuffers.size(); ++i){
        if(vertexBuffers[i].slot <= vertexBuffers[i - 1u].slot){
            alreadyAscending = false;
            break;
        }
    }
    const auto emitBindings = [&](const VertexBufferBinding* const orderedData, const usize orderedCount)noexcept{
        usize startIndex = 0u;
        while(startIndex < orderedCount){
            const u32 firstSlot = orderedData[startIndex].slot;
            Array<VkBuffer, s_MaxVertexAttributes> nativeBuffers;
            Array<VkDeviceSize, s_MaxVertexAttributes> offsets;
            u32 count = 0u;
            while(startIndex + count < orderedCount && orderedData[startIndex + count].slot == firstSlot + count){
                nativeBuffers[count] = orderedData[startIndex + count].buffer->m_buffer;
                offsets[count] = orderedData[startIndex + count].offset;
                ++count;
            }
            m_context.deviceDispatch.vkCmdBindVertexBuffers(m_currentCmdBuf->m_cmdBuf, firstSlot, count, nativeBuffers.data(), offsets.data());
            startIndex += count;
        }
    };
    if(alreadyAscending){
        emitBindings(vertexBuffers.data(), vertexBuffers.size());
        return;
    }
    FixedVector<VertexBufferBinding, s_MaxVertexAttributes> sortedBindings;
    sortedBindings.resize(vertexBuffers.size());
    for(usize i = 0u; i < vertexBuffers.size(); ++i)
        sortedBindings.data()[i] = vertexBuffers[i];
    Sort(sortedBindings.begin(), sortedBindings.end(), [](const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{
        return lhs.slot < rhs.slot;
    });
    emitBindings(sortedBindings.data(), sortedBindings.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setGraphicsDynamicState(const GraphicsPipelineDesc& desc, const GraphicsState& state)noexcept{
    const RasterState& rasterState = desc.renderState.rasterState;
    const DepthStencilState& depthStencilState = desc.renderState.depthStencilState;
    const VkCommandBuffer commandBuffer = m_currentCmdBuf->m_cmdBuf;
    const f32 depthBiasConstant = static_cast<f32>(rasterState.depthBias);
    const u8 stencilRef = depthStencilState.dynamicStencilRef ? state.dynamicStencilRefValue : depthStencilState.stencilRefValue;

    if(!m_cachedDynamicGraphicsState.staticDefaultsBound){
        m_context.deviceDispatch.vkCmdSetLineWidth(commandBuffer, s_DefaultRasterLineWidth);
        m_context.deviceDispatch.vkCmdSetDepthBounds(commandBuffer, 0.f, 1.f);
        m_cachedDynamicGraphicsState.staticDefaultsBound = true;
    }
    if(!m_cachedDynamicGraphicsState.valid || m_cachedDynamicGraphicsState.depthBiasConstant != depthBiasConstant || m_cachedDynamicGraphicsState.depthBiasClamp != rasterState.depthBiasClamp || m_cachedDynamicGraphicsState.depthBiasSlope != rasterState.slopeScaledDepthBias){
        m_context.deviceDispatch.vkCmdSetDepthBias(commandBuffer, depthBiasConstant, rasterState.depthBiasClamp, rasterState.slopeScaledDepthBias);
        m_cachedDynamicGraphicsState.depthBiasConstant = depthBiasConstant;
        m_cachedDynamicGraphicsState.depthBiasClamp = rasterState.depthBiasClamp;
        m_cachedDynamicGraphicsState.depthBiasSlope = rasterState.slopeScaledDepthBias;
    }
    if(!m_cachedDynamicGraphicsState.valid || !(m_cachedDynamicGraphicsState.blendConstantColor == state.blendConstantColor)){
        const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
        m_context.deviceDispatch.vkCmdSetBlendConstants(commandBuffer, blendConstants.data());
        m_cachedDynamicGraphicsState.blendConstantColor = state.blendConstantColor;
    }
    if(!m_cachedDynamicGraphicsState.valid || m_cachedDynamicGraphicsState.stencilReadMask != depthStencilState.stencilReadMask){
        m_context.deviceDispatch.vkCmdSetStencilCompareMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilReadMask);
        m_cachedDynamicGraphicsState.stencilReadMask = depthStencilState.stencilReadMask;
    }
    if(!m_cachedDynamicGraphicsState.valid || m_cachedDynamicGraphicsState.stencilWriteMask != depthStencilState.stencilWriteMask){
        m_context.deviceDispatch.vkCmdSetStencilWriteMask(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, depthStencilState.stencilWriteMask);
        m_cachedDynamicGraphicsState.stencilWriteMask = depthStencilState.stencilWriteMask;
    }
    if(!m_cachedDynamicGraphicsState.valid || m_cachedDynamicGraphicsState.stencilRef != stencilRef){
        m_context.deviceDispatch.vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, stencilRef);
        m_cachedDynamicGraphicsState.stencilRef = stencilRef;
    }
    m_cachedDynamicGraphicsState.valid = true;
}

void CommandList::setGraphicsBlendConstantsIfChanged(const Color& blendConstantColor)noexcept{
    if(!m_cachedDynamicGraphicsState.valid || !(m_cachedDynamicGraphicsState.blendConstantColor == blendConstantColor)){
        const Array<f32, 4> blendConstants = {{ blendConstantColor.r, blendConstantColor.g, blendConstantColor.b, blendConstantColor.a }};
        m_context.deviceDispatch.vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants.data());
        m_cachedDynamicGraphicsState.blendConstantColor = blendConstantColor;
    }
}

void CommandList::setGraphicsStencilReferenceIfChanged(const u8 stencilRef)noexcept{
    if(!m_cachedDynamicGraphicsState.valid || m_cachedDynamicGraphicsState.stencilRef != stencilRef){
        m_context.deviceDispatch.vkCmdSetStencilReference(m_currentCmdBuf->m_cmdBuf, VK_STENCIL_FACE_FRONT_AND_BACK, stencilRef);
        m_cachedDynamicGraphicsState.stencilRef = stencilRef;
    }
}


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
        setResourceStatesForFramebufferValidated(framebuffer);
        if(m_commandRecordingFailed)
            return;
        if(!m_pendingImageBarriers.empty() || !m_pendingBufferBarriers.empty()){
            commitBarriers();
            if(m_commandRecordingFailed)
                return;
        }
    }
    if(!beginDynamicRendering(framebuffer, params)){
        rejectCommandRecording(s_OperationName, __hidden_graphics_pipeline::s_DynamicRenderingBeginFailedReason);
        return;
    }

    retainResource(&framebuffer);
    clearDirtyPipelineStates();
    invalidateGraphicsDrawValidationCache();
    updateActivePushConstantPipeline(s_PipelineKindNone, VK_NULL_HANDLE, 0u, GpuQueueCapability::None);
    m_graphicsBuffersValidatedInPass = false;
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

    const b8 hasPreviousGraphicsState = m_hasBoundGraphicsState;
    const b8 sameActiveRenderPass = m_renderPassActive && state.framebuffer && m_renderPassFramebuffer == state.framebuffer;
    const b8 sameFramebuffer = hasPreviousGraphicsState && m_currentGraphicsState.framebuffer == state.framebuffer;
    const b8 pipelineChanged = !hasPreviousGraphicsState || m_currentGraphicsState.pipeline != state.pipeline;
    const b8 indexBufferChanged = !hasPreviousGraphicsState || !(m_currentGraphicsState.indexBuffer == state.indexBuffer);
    const b8 indirectParamsChanged = !hasPreviousGraphicsState || m_currentGraphicsState.indirectParams != state.indirectParams;
    const b8 vertexBuffersEqual = hasPreviousGraphicsState && __hidden_graphics_pipeline::VertexBufferBindingsEqual(m_currentGraphicsState.vertexBuffers, state.vertexBuffers);

    b8 canFastHandleBuffers = vertexBuffersEqual && !indexBufferChanged;
    const b8 samePassFastEligible =
        hasPreviousGraphicsState
        && sameActiveRenderPass
        && sameFramebuffer
        && m_graphicsBuffersValidatedInPass
        && !state.shadingRateState.enabled
        && !indirectParamsChanged
        && (
            !pipelineChanged
            || (
                state.framebuffer->m_staticStructureValid
                && state.framebuffer->m_allAttachmentsManagedNonVirtual
                && state.framebuffer->m_allAttachmentsExclusiveSharing
                && !m_stateTracker.hasPermanentTextureStates()
            )
        );
    b8 onlyVertexOffsetsChanged = false;
    if(!canFastHandleBuffers && samePassFastEligible){
        const auto bufferReadyNoBarrier = [&](Buffer* const buffer, const ResourceStates::Mask requiredState)noexcept -> b8{
            if(!buffer || !buffer->m_managed || buffer->m_creationDesc.isVirtual || &buffer->m_context != &m_context || buffer->m_buffer == VK_NULL_HANDLE)
                return false;
            if(!m_hasCachedPhysicalQueueInfo || (buffer->m_bufferInfo.sharingMode == VK_SHARING_MODE_CONCURRENT && !isBufferAdmittedToCommandQueue(*buffer)))
                return false;
            if(!buffer->m_gpuReady.load(MemoryOrder::acquire))
                return false;
            const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
            if(permanentState != ResourceStates::Unknown)
                return permanentState == requiredState && !VulkanStateTrackingDetail::NeedsResourceStateBarrier(permanentState, requiredState, false, false);
            const StateTracker::BufferRangeStates* const ranges = m_stateTracker.findBufferRangesCached(*buffer);
            if(!ranges || ranges->size() != 1u)
                return false;
            const StateTracker::BufferRangeState& entry = (*ranges)[0u];
            return entry.range.byteOffset == 0u && entry.range.byteSize == buffer->m_creationDesc.byteSize && entry.state == requiredState;
        };
        const auto checkIncrementalBuffers = [&]()noexcept -> b8{
            onlyVertexOffsetsChanged = state.vertexBuffers.size() == m_currentGraphicsState.vertexBuffers.size();
            for(usize i = 0u; i < state.vertexBuffers.size(); ++i){
                const VertexBufferBinding& binding = state.vertexBuffers[i];
                if(
                    !binding.buffer
                    || binding.buffer->m_buffer == VK_NULL_HANDLE
                    || !binding.buffer->m_creationDesc.isVertexBuffer
                    || binding.offset >= binding.buffer->m_creationDesc.byteSize
                    || binding.slot >= m_context.physicalDeviceProperties.limits.maxVertexInputBindings
                )
                    return false;
                for(usize j = 0u; j < i; ++j){
                    if(state.vertexBuffers[j].slot == binding.slot)
                        return false;
                    if(state.vertexBuffers[j].buffer != binding.buffer && state.vertexBuffers[j].buffer->m_buffer == binding.buffer->m_buffer)
                        return false;
                }
                if(onlyVertexOffsetsChanged){
                    const VertexBufferBinding& prev = m_currentGraphicsState.vertexBuffers[i];
                    if(prev.buffer != binding.buffer || prev.slot != binding.slot)
                        onlyVertexOffsetsChanged = false;
                }
                if(state.indexBuffer.buffer == binding.buffer || state.indirectParams == binding.buffer)
                    return false;
                if(!vertexBuffersEqual && !(i < m_currentGraphicsState.vertexBuffers.size() && state.vertexBuffers[i] == m_currentGraphicsState.vertexBuffers[i])){
                    const b8 sameBufSlot = i < m_currentGraphicsState.vertexBuffers.size()
                        && state.vertexBuffers[i].buffer == m_currentGraphicsState.vertexBuffers[i].buffer
                        && m_currentGraphicsState.indexBuffer.buffer != binding.buffer
                        && m_currentGraphicsState.indirectParams != binding.buffer
                    ;
                    if(!sameBufSlot && !bufferReadyNoBarrier(binding.buffer, ResourceStates::VertexBuffer))
                        return false;
                }
            }
            if(!state.indexBuffer.buffer){
                if(state.indexBuffer.offset != 0u || state.indexBuffer.format != Format::UNKNOWN)
                    return false;
            }
            else{
                const IndexBufferBinding& ib = state.indexBuffer;
                const u32 indexBytes = VulkanDetail::GetIndexElementByteSize(ib.format);
                if(
                    ib.buffer->m_buffer == VK_NULL_HANDLE
                    || !ib.buffer->m_creationDesc.isIndexBuffer
                    || !VulkanDetail::IsIndexFormatSupported(ib.format, m_context.fullDrawIndexUint32FeatureEnabled)
                    || (ib.offset % indexBytes) != 0u
                    || ib.offset >= ib.buffer->m_creationDesc.byteSize
                )
                    return false;
                if(state.indirectParams == ib.buffer)
                    return false;
                for(const VertexBufferBinding& vb : state.vertexBuffers){
                    if(vb.buffer->m_buffer == ib.buffer->m_buffer)
                        return false;
                }
                if(ib.buffer != m_currentGraphicsState.indexBuffer.buffer){
                    if(m_currentGraphicsState.indirectParams && m_currentGraphicsState.indirectParams->m_buffer == ib.buffer->m_buffer)
                        return false;
                    if(!bufferReadyNoBarrier(ib.buffer, ResourceStates::IndexBuffer))
                        return false;
                }
            }
            if(state.indirectParams){
                for(const VertexBufferBinding& vb : state.vertexBuffers){
                    if(vb.buffer->m_buffer == state.indirectParams->m_buffer)
                        return false;
                }
                if(state.indexBuffer.buffer && state.indexBuffer.buffer->m_buffer == state.indirectParams->m_buffer)
                    return false;
            }
            return true;
        };
        canFastHandleBuffers = checkIncrementalBuffers();
    }

    if(samePassFastEligible && canFastHandleBuffers){
        if(pipelineChanged && state.pipeline){
            if(&state.pipeline->m_context != &m_context){
                rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline belongs to another device"));
                return;
            }
            if(state.pipeline->m_pipeline == VK_NULL_HANDLE || state.pipeline->pipelineLayout == VK_NULL_HANDLE){
                rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline has no native pipeline or layout"));
                return;
            }
            if(state.pipeline->m_framebufferInfo != state.framebuffer->m_framebufferInfo){
                rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline and framebuffer are incompatible"));
                return;
            }
            const FramebufferAttachment& depthAttachment = state.framebuffer->m_desc.depthAttachment;
            if(depthAttachment.valid() && depthAttachment.isReadOnly && !VulkanDetail::IsDepthStencilReadOnlyCompatible(state.pipeline->m_desc.renderState.depthStencilState, depthAttachment.texture->m_aspectMask)){
                rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline writes a read-only depth/stencil attachment"));
                return;
            }
        }

        const b8 viewportsEqual = VulkanDetail::ViewportsEqual(m_currentGraphicsState.viewport.viewports, state.viewport.viewports);
        const b8 explicitScissorsEqual = VulkanDetail::ScissorsEqual(m_currentGraphicsState.viewport.scissorRects, state.viewport.scissorRects);
        const b8 updateViewport = !viewportsEqual;
        const b8 updateScissor = !explicitScissorsEqual || (state.viewport.scissorRects.empty() && !viewportsEqual);
        if(updateViewport || updateScissor){
            if(!validateViewportState(state.viewport, s_SetGraphicsStateOperation))
                return;
            VulkanDetail::CopyViewportStateCompact(m_currentGraphicsState.viewport, state.viewport);
        }
        else if(!explicitScissorsEqual)
            VulkanDetail::CopyViewportStateCompact(m_currentGraphicsState.viewport, state.viewport);

        const b8 blendConstantsChanged = !(m_currentGraphicsState.blendConstantColor == state.blendConstantColor);
        const b8 dynamicStencilRefChanged = m_currentGraphicsState.dynamicStencilRefValue != state.dynamicStencilRefValue;
        m_currentGraphicsState.blendConstantColor = state.blendConstantColor;
        m_currentGraphicsState.dynamicStencilRefValue = state.dynamicStencilRefValue;

        if(!vertexBuffersEqual){
            if(!onlyVertexOffsetsChanged){
                for(const VertexBufferBinding& binding : state.vertexBuffers)
                    retainResource(binding.buffer);
            }
            __hidden_graphics_pipeline::CopyVertexBufferBindingsCompact(m_currentGraphicsState.vertexBuffers, state.vertexBuffers);
            if(!state.vertexBuffers.empty())
                bindVertexBuffersNative({ state.vertexBuffers.data(), state.vertexBuffers.size() });
        }
        if(indexBufferChanged){
            m_currentGraphicsState.indexBuffer = state.indexBuffer;
            if(state.indexBuffer.buffer){
                auto& ib = *MakeNotNull(state.indexBuffer.buffer);
                retainResource(&ib);
                const VkIndexType indexType = state.indexBuffer.format == Format::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                m_context.deviceDispatch.vkCmdBindIndexBuffer(m_currentCmdBuf->m_cmdBuf, ib.m_buffer, state.indexBuffer.offset, indexType);
            }
        }

        if(pipelineChanged){
            m_currentGraphicsState.pipeline = state.pipeline;
            if(state.pipeline){
                auto& pipeline = *MakeNotNull(state.pipeline);
                updateActivePushConstantPipeline(s_PipelineKindGraphics, pipeline.pipelineLayout, pipeline.pushConstantByteSize, GpuQueueCapability::Graphics);
                retainResource(&pipeline);
                m_context.deviceDispatch.vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipeline);
                setGraphicsDynamicState(pipeline.m_desc, state);
            }
            else
                updateActivePushConstantPipeline(s_PipelineKindNone, VK_NULL_HANDLE, 0u, GpuQueueCapability::None);

            if(updateViewport || updateScissor)
                setViewportState(state.viewport, updateViewport, updateScissor);
            rebuildGraphicsDrawValidationCache();
            return;
        }

        if(updateViewport || updateScissor){
            if(state.viewport.viewports.size() != 1u || state.viewport.scissorRects.size() > 1u)
                invalidateGraphicsDrawValidationCache();
            else if(!m_graphicsDrawStateValid)
                rebuildGraphicsDrawValidationCache();
            setViewportState(state.viewport, updateViewport, updateScissor);
        }

        if(!vertexBuffersEqual){
            if(onlyVertexOffsetsChanged && m_graphicsDrawStateValid){
                for(u32 i = 0u; i < m_resolvedVertexBindingCount; ++i)
                    m_resolvedVertexBindings[i].offset = state.vertexBuffers[m_resolvedVertexBindings[i].stateVertexIndex].offset;
                m_graphicsDrawIndexedValid = m_currentGraphicsState.indexBuffer.buffer != nullptr;
            }
            else
                rebuildGraphicsDrawValidationCache();
        }
        else if(indexBufferChanged && m_graphicsDrawStateValid)
            m_graphicsDrawIndexedValid = m_currentGraphicsState.indexBuffer.buffer != nullptr;

        if(state.pipeline){
            if(blendConstantsChanged)
                setGraphicsBlendConstantsIfChanged(state.blendConstantColor);
            if(dynamicStencilRefChanged && state.pipeline->m_desc.renderState.depthStencilState.dynamicStencilRef)
                setGraphicsStencilReferenceIfChanged(state.dynamicStencilRefValue);
        }
        return;
    }

    GraphicsBufferStateTable requiredBufferStates;
    if(!validateGraphicsState(state, requiredBufferStates))
        return;
    if((!sameActiveRenderPass || !state.framebuffer->m_allAttachmentsManagedNonVirtual) && !FramebufferRenderingPreparation(*this).prepareFramebuffer(state.framebuffer, s_SetGraphicsStateOperation))
        return;

    setResourceStatesForGraphicsBuffers(requiredBufferStates);
    if(!m_pendingImageBarriers.empty() || !m_pendingBufferBarriers.empty()){
        commitBarriers();
        if(m_commandRecordingFailed)
            return;
    }

    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;

    if(!sameActiveRenderPass && (!m_pendingImageBarriers.empty() || !m_pendingBufferBarriers.empty())){
        commitBarriers();
        if(m_commandRecordingFailed)
            return;
    }
    const b8 blendConstantsChanged = !(m_currentGraphicsState.blendConstantColor == state.blendConstantColor);
    const b8 dynamicStencilRefChanged = m_currentGraphicsState.dynamicStencilRefValue != state.dynamicStencilRefValue;
    const b8 viewportsEqual = hasPreviousGraphicsState && VulkanDetail::ViewportsEqual(m_currentGraphicsState.viewport.viewports, state.viewport.viewports);
    const b8 explicitScissorsEqual = hasPreviousGraphicsState && VulkanDetail::ScissorsEqual(m_currentGraphicsState.viewport.scissorRects, state.viewport.scissorRects);
    const b8 updateViewport = !viewportsEqual;
    const b8 updateScissor = !explicitScissorsEqual || (state.viewport.scissorRects.empty() && !viewportsEqual);

    clearDirtyPipelineStates(s_DirtyGraphicsState);
    m_currentGraphicsState.pipeline = state.pipeline;
    m_currentGraphicsState.framebuffer = state.framebuffer;
    if(updateViewport || !explicitScissorsEqual || !hasPreviousGraphicsState)
        VulkanDetail::CopyViewportStateCompact(m_currentGraphicsState.viewport, state.viewport);
    m_currentGraphicsState.shadingRateState = state.shadingRateState;
    m_currentGraphicsState.dynamicStencilRefValue = state.dynamicStencilRefValue;
    m_currentGraphicsState.blendConstantColor = state.blendConstantColor;
    if(!vertexBuffersEqual || !hasPreviousGraphicsState)
        __hidden_graphics_pipeline::CopyVertexBufferBindingsCompact(m_currentGraphicsState.vertexBuffers, state.vertexBuffers);
    m_currentGraphicsState.indexBuffer = state.indexBuffer;
    m_currentGraphicsState.indirectParams = state.indirectParams;
    m_dirtyPipelineStateMask |= s_DirtyGraphicsState;
    m_hasBoundGraphicsState = true;
    m_graphicsBuffersValidatedInPass = m_renderPassActive;

    if(state.pipeline){
        auto& pipeline = *MakeNotNull(state.pipeline);
        updateActivePushConstantPipeline(s_PipelineKindGraphics, pipeline.pipelineLayout, pipeline.pushConstantByteSize, GpuQueueCapability::Graphics);
        if(pipelineChanged){
            retainResource(&pipeline);
            m_context.deviceDispatch.vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipeline);
            setGraphicsDynamicState(pipeline.m_desc, state);
        }
        else{
            if(blendConstantsChanged)
                setGraphicsBlendConstantsIfChanged(state.blendConstantColor);
            if(pipeline.m_desc.renderState.depthStencilState.dynamicStencilRef && dynamicStencilRefChanged)
                setGraphicsStencilReferenceIfChanged(state.dynamicStencilRefValue);
        }
    }
    else
        updateActivePushConstantPipeline(s_PipelineKindNone, VK_NULL_HANDLE, 0u, GpuQueueCapability::None);

    if(updateViewport || updateScissor)
        setViewportState(state.viewport, updateViewport, updateScissor);

    rebuildGraphicsDrawValidationCache();

    if(!state.vertexBuffers.empty() && !vertexBuffersEqual){
        for(const VertexBufferBinding& binding : state.vertexBuffers)
            retainResource(binding.buffer);
        bindVertexBuffersNative({ state.vertexBuffers.data(), state.vertexBuffers.size() });
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

b8 CommandList::beginDynamicRenderingDefault(Framebuffer& framebuffer){
    FOUNDATION_ASSERT(framebuffer.m_framebufferInfo.width != 0u && framebuffer.m_framebufferInfo.height != 0u && framebuffer.m_framebufferInfo.arraySize != 0u);
    if(framebuffer.m_defaultRenderingCacheState.load(MemoryOrder::acquire) == 2u){
        m_context.deviceDispatch.vkCmdBeginRendering(m_currentCmdBuf->m_cmdBuf, &framebuffer.m_cachedDefaultRenderingInfo);
        return true;
    }

    const RenderPassParameters params{};
    return beginDynamicRendering(framebuffer, params);
}

b8 CommandList::beginDynamicRendering(Framebuffer& framebuffer, const RenderPassParameters& params){
    FOUNDATION_ASSERT(framebuffer.m_framebufferInfo.width != 0u && framebuffer.m_framebufferInfo.height != 0u && framebuffer.m_framebufferInfo.arraySize != 0u);

    const FramebufferDesc& fbDesc = framebuffer.m_desc;
    const b8 hasDefaultCache = framebuffer.m_defaultRenderingCacheState.load(MemoryOrder::acquire) == 2u;

    Array<VkRenderingAttachmentInfo, s_MaxRenderTargets> colorAttachments;
    const u32 numColorAttachments = static_cast<u32>(fbDesc.colorAttachments.size());
    FOUNDATION_ASSERT(numColorAttachments <= s_MaxRenderTargets);

    for(u32 i = 0u; i < numColorAttachments; ++i){
        VkImageView imageView = VK_NULL_HANDLE;
        if(hasDefaultCache){
            imageView = framebuffer.m_cachedDefaultColorAttachments[i].imageView;
        }
        else{
            const auto view = framebuffer.resolveColorAttachmentView(i);
            if(!view){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
                FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: color attachment view is invalid"));
                return false;
            }
            imageView = *view;
        }

        VkRenderingAttachmentInfo& colorInfo = colorAttachments[i];
        colorInfo = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        colorInfo.imageView = imageView;
        colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const VulkanDetail::RenderPassAttachmentOperations operations = VulkanDetail::ConvertRenderPassAttachmentActions(params.colorAttachmentActions[i]);
        colorInfo.loadOp = operations.loadOp;
        colorInfo.storeOp = operations.storeOp;
        if(operations.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR){
            const Color& clr = params.colorClearValues[i];
            colorInfo.clearValue.color = {{ clr.r, clr.g, clr.b, clr.a }};
        }
    }

    VkRenderingAttachmentInfo depthAttachment;
    VkRenderingAttachmentInfo stencilAttachment;
    b8 hasDepth = false;
    b8 hasStencil = false;

    if(fbDesc.depthAttachment.texture){
        auto& depthTex = *MakeNotNull(fbDesc.depthAttachment.texture);
        VkImageView depthViewHandle = VK_NULL_HANDLE;
        if(hasDefaultCache){
            depthViewHandle = (depthTex.m_aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0
                ? framebuffer.m_cachedDefaultDepthAttachment.imageView
                : framebuffer.m_cachedDefaultStencilAttachment.imageView
            ;
        }
        else{
            const auto depthView = framebuffer.resolveDepthAttachmentView();
            if(!depthView){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
                FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to begin dynamic rendering: depth/stencil attachment view is invalid"));
                return false;
            }
            depthViewHandle = *depthView;
        }

        const VkImageLayout depthStencilLayout = fbDesc.depthAttachment.isReadOnly ? VulkanDetail::s_ReadOnlyImageLayout : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        if((depthTex.m_aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0){
            const VulkanDetail::RenderPassAttachmentOperations operations = VulkanDetail::ConvertRenderPassAttachmentActions(params.depthAttachmentActions);
            depthAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
            depthAttachment.imageView = depthViewHandle;
            depthAttachment.imageLayout = depthStencilLayout;
            depthAttachment.loadOp = operations.loadOp;
            depthAttachment.storeOp = operations.storeOp;
            depthAttachment.clearValue.depthStencil.depth = params.depthClearValue;
            hasDepth = true;
        }
        if((depthTex.m_aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0){
            const VulkanDetail::RenderPassAttachmentOperations operations = VulkanDetail::ConvertRenderPassAttachmentActions(params.stencilAttachmentActions);
            stencilAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
            stencilAttachment.imageView = depthViewHandle;
            stencilAttachment.imageLayout = depthStencilLayout;
            stencilAttachment.loadOp = operations.loadOp;
            stencilAttachment.storeOp = operations.storeOp;
            stencilAttachment.clearValue.depthStencil.stencil = params.stencilClearValue;
            hasStencil = true;
        }
    }
    if(framebuffer.m_allAttachmentsManagedNonVirtual){
        if(!framebuffer.m_allViewsCached.load(MemoryOrder::relaxed))
            framebuffer.m_allViewsCached.store(true, MemoryOrder::release);
        u8 expectedCacheState = 0u;
        if(
            !hasDefaultCache
            && framebuffer.m_defaultRenderingCacheState.load(MemoryOrder::relaxed) == 0u
            && framebuffer.m_defaultRenderingCacheState.compare_exchange_strong(expectedCacheState, 1u, MemoryOrder::acq_rel, MemoryOrder::relaxed)
        ){
            for(u32 i = 0u; i < numColorAttachments; ++i){
                VkRenderingAttachmentInfo& cachedColor = framebuffer.m_cachedDefaultColorAttachments[i];
                cachedColor = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
                cachedColor.imageView = colorAttachments[i].imageView;
                cachedColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                cachedColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                cachedColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            }
            if(hasDepth){
                framebuffer.m_cachedDefaultDepthAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
                framebuffer.m_cachedDefaultDepthAttachment.imageView = depthAttachment.imageView;
                framebuffer.m_cachedDefaultDepthAttachment.imageLayout = depthAttachment.imageLayout;
                framebuffer.m_cachedDefaultDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                framebuffer.m_cachedDefaultDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                framebuffer.m_cachedDefaultDepthAttachment.clearValue.depthStencil.depth = s_DepthClearValue;
            }
            if(hasStencil){
                framebuffer.m_cachedDefaultStencilAttachment = VulkanDetail::MakeVkStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
                framebuffer.m_cachedDefaultStencilAttachment.imageView = stencilAttachment.imageView;
                framebuffer.m_cachedDefaultStencilAttachment.imageLayout = stencilAttachment.imageLayout;
                framebuffer.m_cachedDefaultStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                framebuffer.m_cachedDefaultStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                framebuffer.m_cachedDefaultStencilAttachment.clearValue.depthStencil.stencil = 0u;
            }
            auto& cachedInfo = framebuffer.m_cachedDefaultRenderingInfo;
            cachedInfo = VulkanDetail::MakeVkStruct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
            cachedInfo.renderArea.offset = { 0, 0 };
            cachedInfo.renderArea.extent = { framebuffer.m_framebufferInfo.width, framebuffer.m_framebufferInfo.height };
            cachedInfo.layerCount = framebuffer.m_framebufferInfo.arraySize;
            cachedInfo.colorAttachmentCount = numColorAttachments;
            cachedInfo.pColorAttachments = framebuffer.m_cachedDefaultColorAttachments.data();
            cachedInfo.pDepthAttachment = hasDepth ? &framebuffer.m_cachedDefaultDepthAttachment : nullptr;
            cachedInfo.pStencilAttachment = hasStencil ? &framebuffer.m_cachedDefaultStencilAttachment : nullptr;
            framebuffer.m_defaultRenderingCacheState.store(2u, MemoryOrder::release);
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
        setResourceStatesForFramebufferValidated(*framebuffer);
        if(!m_pendingImageBarriers.empty() || !m_pendingBufferBarriers.empty())
            commitBarriers();
        if(m_commandRecordingFailed)
            return false;
    }

    if(!beginDynamicRenderingDefault(*framebuffer)){
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
    invalidateGraphicsDrawValidationCache();
    m_renderPassActive = false;
    m_renderPassFramebuffer = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

