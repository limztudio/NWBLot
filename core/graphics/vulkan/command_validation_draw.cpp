// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_validation.h"
#include "backend.h"
#include "framebuffer_rendering_preparation.h"
#include "buffer_resource_detail.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CommandList::CollectGraphicsBufferStates(const GraphicsState& state, GraphicsBufferStateTable& outTable)noexcept{
    outTable.count = 0u;
    const auto addRequiredBufferState = [&outTable](Buffer* const buffer, const ResourceStates::Mask requiredState)noexcept -> b8{
        if(!buffer)
            return true;
        for(u32 stateIndex = 0u; stateIndex < outTable.count; ++stateIndex){
            if(outTable.entries[stateIndex].buffer == buffer){
                outTable.entries[stateIndex].state |= requiredState;
                return true;
            }
            if(outTable.entries[stateIndex].buffer->m_buffer == buffer->m_buffer)
                return false;
        }
        FOUNDATION_ASSERT(outTable.count < LengthOf(outTable.entries));
        outTable.entries[outTable.count].buffer = buffer;
        outTable.entries[outTable.count].state = requiredState;
        ++outTable.count;
        return true;
    };

    for(const VertexBufferBinding& binding : state.vertexBuffers){
        if(!addRequiredBufferState(binding.buffer, ResourceStates::VertexBuffer))
            return false;
    }
    return addRequiredBufferState(state.indexBuffer.buffer, ResourceStates::IndexBuffer) && addRequiredBufferState(state.indirectParams, ResourceStates::IndirectArgument);
}

b8 CommandList::validateGraphicsState(const GraphicsState& state, GraphicsBufferStateTable& outRequiredBufferStates){
    if(state.pipeline && &state.pipeline->m_context != &m_context){
        rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline belongs to another device"));
        return false;
    }
    if(state.shadingRateState.enabled){
        rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("variable-rate shading state is not implemented"));
        return false;
    }
    if(state.pipeline && !state.framebuffer){
        rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("a graphics pipeline requires an explicit framebuffer"));
        return false;
    }
    if(!FramebufferRenderingPreparation(*this).validateFramebuffer(state.framebuffer, s_SetGraphicsStateOperation))
        return false;
    if(!validateViewportState(state.viewport, s_SetGraphicsStateOperation))
        return false;

    if(state.pipeline){
        if(state.pipeline->m_pipeline == VK_NULL_HANDLE || state.pipeline->pipelineLayout == VK_NULL_HANDLE){
            rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline has no native pipeline or layout"));
            return false;
        }
        if(state.pipeline->m_framebufferInfo != state.framebuffer->m_framebufferInfo){
            rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline and framebuffer are incompatible"));
            return false;
        }
        const FramebufferAttachment& depthAttachment = state.framebuffer->m_desc.depthAttachment;
        if(depthAttachment.valid() && depthAttachment.isReadOnly && !VulkanDetail::IsDepthStencilReadOnlyCompatible(state.pipeline->m_desc.renderState.depthStencilState, depthAttachment.texture->m_aspectMask)){
            rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("graphics pipeline writes a read-only depth/stencil attachment"));
            return false;
        }
    }

    for(usize bindingIndex = 0u; bindingIndex < state.vertexBuffers.size(); ++bindingIndex){
        const VertexBufferBinding& binding = state.vertexBuffers[bindingIndex];
        if(
            !binding.buffer
            || binding.buffer->m_buffer == VK_NULL_HANDLE
            || !binding.buffer->m_creationDesc.isVertexBuffer
            || binding.offset >= binding.buffer->m_creationDesc.byteSize
            || binding.slot >= m_context.physicalDeviceProperties.limits.maxVertexInputBindings
        ){
            rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("vertex-buffer binding is invalid"));
            return false;
        }
        for(usize priorIndex = 0u; priorIndex < bindingIndex; ++priorIndex){
            if(state.vertexBuffers[priorIndex].slot == binding.slot){
                rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("vertex-buffer slot is bound more than once"));
                return false;
            }
        }
    }

    const IndexBufferBinding& indexBinding = state.indexBuffer;
    if(indexBinding.buffer){
        const u32 indexBytes = VulkanDetail::GetIndexElementByteSize(indexBinding.format);
        if(
            indexBinding.buffer->m_buffer == VK_NULL_HANDLE
            || !indexBinding.buffer->m_creationDesc.isIndexBuffer
            || !VulkanDetail::IsIndexFormatSupported(indexBinding.format, m_context.fullDrawIndexUint32FeatureEnabled)
            || (indexBinding.offset % indexBytes) != 0u
            || indexBinding.offset >= indexBinding.buffer->m_creationDesc.byteSize
        ){
            rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("index-buffer binding is invalid"));
            return false;
        }
    }
    else if(indexBinding.offset != 0u || indexBinding.format != Format::UNKNOWN){
        rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("index-buffer metadata has no buffer"));
        return false;
    }

    if(state.indirectParams && (state.indirectParams->m_buffer == VK_NULL_HANDLE || !state.indirectParams->m_creationDesc.isDrawIndirectArgs || state.indirectParams->m_creationDesc.byteSize == 0u)){
        rejectCommandRecording(s_SetGraphicsStateOperation, FOUNDATION_TEXT("indirect-argument buffer is invalid"));
        return false;
    }

    if(!CollectGraphicsBufferStates(state, outRequiredBufferStates)){
        rejectCommandRecording(s_SetGraphicsStateOperation, s_NativeBufferAliasReason);
        return false;
    }

    for(u32 stateIndex = 0u; stateIndex < outRequiredBufferStates.count; ++stateIndex){
        const GraphicsBufferStateEntry& requiredBufferState = outRequiredBufferStates.entries[stateIndex];
        if(!validateBufferForGpuStateValidated(requiredBufferState.buffer, requiredBufferState.state, s_SetGraphicsStateOperation))
            return false;
    }
    return true;
}

b8 CommandList::validateMeshletState(const MeshletState& state){
    if(state.pipeline && &state.pipeline->m_context != &m_context){
        rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("meshlet pipeline belongs to another device"));
        return false;
    }
    if(state.pipeline && !state.framebuffer){
        rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("a meshlet pipeline requires an explicit framebuffer"));
        return false;
    }
    if(!FramebufferRenderingPreparation(*this).validateFramebuffer(state.framebuffer, s_SetMeshletStateOperation))
        return false;
    if(!validateViewportState(state.viewport, s_SetMeshletStateOperation))
        return false;

    if(state.pipeline){
        if(!m_context.extensions.extMeshShader || m_context.meshShaderFeatures.meshShader != VK_TRUE || !m_context.deviceDispatch.vkCmdDrawMeshTasksEXT){
            rejectCommandRecording(s_SetMeshletStateOperation, s_MeshShaderUnavailableReason);
            return false;
        }
        if(state.pipeline->m_desc.amplificationShader && m_context.meshShaderFeatures.taskShader != VK_TRUE){
            rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("task shader feature is unavailable"));
            return false;
        }
        if(state.pipeline->m_pipeline == VK_NULL_HANDLE || state.pipeline->pipelineLayout == VK_NULL_HANDLE){
            rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("meshlet pipeline has no native pipeline or layout"));
            return false;
        }
        if(state.pipeline->m_framebufferInfo != state.framebuffer->m_framebufferInfo){
            rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("meshlet pipeline and framebuffer are incompatible"));
            return false;
        }
        const FramebufferAttachment& depthAttachment = state.framebuffer->m_desc.depthAttachment;
        if(depthAttachment.valid() && depthAttachment.isReadOnly && !VulkanDetail::IsDepthStencilReadOnlyCompatible(state.pipeline->m_desc.renderState.depthStencilState, depthAttachment.texture->m_aspectMask)){
            rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("meshlet pipeline writes a read-only depth/stencil attachment"));
            return false;
        }
    }

    if(state.indirectParams && (state.indirectParams->m_buffer == VK_NULL_HANDLE || !state.indirectParams->m_creationDesc.isDrawIndirectArgs || state.indirectParams->m_creationDesc.byteSize == 0u)){
        rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("mesh indirect-argument buffer is invalid"));
        return false;
    }
    if(state.indirectParams && !validateBufferForGpuStateValidated(state.indirectParams, ResourceStates::IndirectArgument, s_SetMeshletStateOperation))
        return false;
    return true;
}

void CommandList::rebuildGraphicsDrawValidationCache()noexcept{
    invalidateGraphicsDrawValidationCache();

    GraphicsPipeline* const pipeline = m_currentGraphicsState.pipeline;
    if(!m_renderPassActive || !m_renderPassFramebuffer || !pipeline || pipeline->m_pipeline == VK_NULL_HANDLE || pipeline->m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo)
        return;
    if(m_currentGraphicsState.viewport.viewports.size() != 1u || m_currentGraphicsState.viewport.scissorRects.size() > 1u)
        return;

    InputLayout* const inputLayout = pipeline->m_desc.inputLayout.get();
    if(inputLayout){
        const auto& requiredBindings = inputLayout->m_bindings;
        const auto& requiredElementBytes = inputLayout->m_bindingRequiredElementBytes;
        const VertexBufferBinding* const vertexData = m_currentGraphicsState.vertexBuffers.data();
        const usize vertexCount = m_currentGraphicsState.vertexBuffers.size();
        const usize requiredCount = requiredBindings.size();
        if(requiredCount > s_MaxVertexAttributes)
            return;

        for(usize requiredIndex = 0u; requiredIndex < requiredCount; ++requiredIndex){
            const VkVertexInputBindingDescription& requiredBinding = requiredBindings[requiredIndex];
            const VertexBufferBinding* bound = nullptr;
            for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
                if(vertexData[vertexIndex].slot == requiredBinding.binding){
                    bound = &vertexData[vertexIndex];
                    break;
                }
            }
            if(!bound || !bound->buffer){
                m_resolvedVertexBindingCount = 0u;
                return;
            }
            const b8 instanceRate = requiredBinding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE;
            if(instanceRate)
                m_graphicsHasInstanceVertexBindings = true;
            m_resolvedVertexBindings[requiredIndex] = ResolvedVertexBinding{
                .bufferDesc = &bound->buffer->m_creationDesc,
                .offset = bound->offset,
                .stride = requiredBinding.stride,
                .requiredElementBytes = requiredElementBytes[requiredIndex],
                .instanceRate = instanceRate,
            };
        }
        m_resolvedVertexBindingCount = static_cast<u32>(requiredCount);
    }

    m_graphicsDrawStateValid = true;
    m_graphicsDrawIndexedValid = m_currentGraphicsState.indexBuffer.buffer != nullptr;
}

b8 CommandList::validateGraphicsDrawState(const TStringView operationName, const b8 indexed){
    GraphicsPipeline* const pipeline = m_currentGraphicsState.pipeline;
    if(!m_renderPassActive || !m_renderPassFramebuffer || !pipeline || pipeline->m_pipeline == VK_NULL_HANDLE || pipeline->m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("no compatible graphics pipeline and render pass are active"));
        return false;
    }
    if(m_currentGraphicsState.viewport.viewports.size() != 1u || m_currentGraphicsState.viewport.scissorRects.size() > 1u){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("draw requires one viewport and at most one explicit scissor"));
        return false;
    }

    InputLayout* const inputLayout = pipeline->m_desc.inputLayout.get();
    if(inputLayout){
        const VertexBufferBinding* const vertexData = m_currentGraphicsState.vertexBuffers.data();
        const usize vertexCount = m_currentGraphicsState.vertexBuffers.size();
        for(const VkVertexInputBindingDescription& requiredBinding : inputLayout->m_bindings){
            b8 bindingFound = false;
            for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
                if(vertexData[vertexIndex].slot == requiredBinding.binding){
                    bindingFound = true;
                    break;
                }
            }
            if(!bindingFound){
                rejectCommandRecording(operationName, FOUNDATION_TEXT("required vertex-buffer binding is missing"));
                return false;
            }
        }
    }
    if(indexed && !m_currentGraphicsState.indexBuffer.buffer){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("indexed draw has no index buffer"));
        return false;
    }
    return true;
}

b8 CommandList::validateGraphicsDrawArguments(const DrawArguments& arguments, const b8 indexed, const TStringView operationName){
    if(!(indexed ? m_graphicsDrawIndexedValid : m_graphicsDrawStateValid)){
        if(!validateGraphicsDrawState(operationName, indexed))
            return false;
        rebuildGraphicsDrawValidationCache();
    }

    if(indexed){
        if(arguments.startVertexLocation > static_cast<u32>(Limit<i32>::s_Max)){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("indexed base vertex exceeds signed Vulkan range"));
            return false;
        }
        const IndexBufferBinding& indexBinding = m_currentGraphicsState.indexBuffer;
        const u32 indexBytes = VulkanDetail::GetIndexElementByteSize(indexBinding.format);
        if(!VulkanDetail::IsIndexDrawRangeValid(indexBinding.buffer->m_creationDesc, indexBinding.offset, arguments.startIndexLocation, arguments.vertexCount, indexBytes)){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("indexed draw range exceeds the index buffer"));
            return false;
        }
        if(!m_graphicsHasInstanceVertexBindings)
            return true;
    }

    for(u32 bindingIndex = 0u; bindingIndex < m_resolvedVertexBindingCount; ++bindingIndex){
        const ResolvedVertexBinding& resolved = m_resolvedVertexBindings[bindingIndex];
        if(indexed && !resolved.instanceRate)
            continue;
        const u32 firstElement = resolved.instanceRate ? arguments.startInstanceLocation : arguments.startVertexLocation;
        const u32 elementCount = resolved.instanceRate ? arguments.instanceCount : arguments.vertexCount;
        if(!VulkanDetail::IsStridedBufferRangeValid(*resolved.bufferDesc, resolved.offset, firstElement, elementCount, resolved.stride, resolved.requiredElementBytes)){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("draw range exceeds a vertex buffer"));
            return false;
        }
    }
    return true;
}

b8 CommandList::validateIndirectBuffer(Buffer* const buffer, const u64 offsetBytes, const u64 commandSizeBytes, const u32 commandCount, const TStringView commandName){
    if(!buffer){
        rejectCommandRecording(commandName, FOUNDATION_TEXT("no indirect-argument buffer is bound"));
        return false;
    }
    if(buffer->m_buffer == VK_NULL_HANDLE){
        rejectCommandRecording(commandName, FOUNDATION_TEXT("indirect-argument buffer has no native buffer"));
        return false;
    }
    if(!buffer->m_creationDesc.isDrawIndirectArgs){
        rejectCommandRecording(commandName, FOUNDATION_TEXT("buffer lacks indirect-argument usage"));
        return false;
    }
    if(!VulkanDetail::IsIndirectCommandRangeValid(buffer->m_creationDesc, offsetBytes, commandSizeBytes, commandCount)){
        rejectCommandRecording(commandName, FOUNDATION_TEXT("indirect-argument range is invalid"));
        return false;
    }
    return true;
}

Expected<NotNull<Buffer*>> CommandList::prepareDrawIndirect(const u32 offsetBytes, const u32 drawCount, const u64 commandSizeBytes, const TStringView operationLabel, const TStringView commandName, const VulkanDetail::IndirectDrawIndexMode::Enum indexMode){
    if(drawCount == 0u)
        return MakeUnexpected(Failure{});

    const b8 indexed = indexMode == VulkanDetail::IndirectDrawIndexMode::Indexed;
    if(!validateGraphicsDrawState(operationLabel, indexed))
        return MakeUnexpected(Failure{});
    if(!m_context.drawIndirectFirstInstanceFeatureEnabled){
        rejectCommandRecording(operationLabel, FOUNDATION_TEXT("drawIndirectFirstInstance was not enabled"));
        return MakeUnexpected(Failure{});
    }
    if(!VulkanDetail::IsIndirectDrawCountValid(drawCount, m_context.physicalDeviceProperties.limits.maxDrawIndirectCount, m_context.multiDrawIndirectFeatureEnabled)){
        rejectCommandRecording(operationLabel, FOUNDATION_TEXT("indirect draw count is unsupported or exceeds the device limit"));
        return MakeUnexpected(Failure{});
    }
    if(!validateIndirectBuffer(m_currentGraphicsState.indirectParams, offsetBytes, commandSizeBytes, drawCount, commandName))
        return MakeUnexpected(Failure{});

    return MakeNotNull(m_currentGraphicsState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

