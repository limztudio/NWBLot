// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_validation.h"
#include "backend.h"
#include "buffer_resource_detail.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandList::validateGraphicsState(const GraphicsState& state){
    constexpr const tchar* s_OperationName = NWB_TEXT("set graphics state");
    if(state.pipeline && &state.pipeline->m_context != &m_context){
        rejectCommandRecording(s_OperationName, NWB_TEXT("graphics pipeline belongs to another device"));
        return false;
    }
    if(state.shadingRateState.enabled){
        rejectCommandRecording(s_OperationName, NWB_TEXT("variable-rate shading state is not implemented"));
        return false;
    }
    if(state.pipeline && !state.framebuffer){
        rejectCommandRecording(s_OperationName, NWB_TEXT("a graphics pipeline requires an explicit framebuffer"));
        return false;
    }
    if(!validateFramebufferForRendering(state.framebuffer, s_OperationName))
        return false;
    if(!validateViewportState(state.viewport, s_OperationName))
        return false;

    if(state.pipeline){
        if(state.pipeline->m_pipeline == VK_NULL_HANDLE || state.pipeline->m_pipelineLayout == VK_NULL_HANDLE){
            rejectCommandRecording(s_OperationName, NWB_TEXT("graphics pipeline has no native pipeline or layout"));
            return false;
        }
        if(state.pipeline->m_framebufferInfo != state.framebuffer->m_framebufferInfo){
            rejectCommandRecording(s_OperationName, NWB_TEXT("graphics pipeline and framebuffer are incompatible"));
            return false;
        }
        const FramebufferAttachment& depthAttachment = state.framebuffer->m_desc.depthAttachment;
        if(
            depthAttachment.valid()
            && depthAttachment.isReadOnly
            && !VulkanDetail::IsDepthStencilReadOnlyCompatible(
                state.pipeline->m_desc.renderState.depthStencilState,
                depthAttachment.texture->m_aspectMask
            )
        ){
            rejectCommandRecording(
                s_OperationName,
                NWB_TEXT("graphics pipeline writes a read-only depth/stencil attachment")
            );
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
            rejectCommandRecording(s_OperationName, NWB_TEXT("vertex-buffer binding is invalid"));
            return false;
        }
        for(usize priorIndex = 0u; priorIndex < bindingIndex; ++priorIndex){
            if(state.vertexBuffers[priorIndex].slot == binding.slot){
                rejectCommandRecording(s_OperationName, NWB_TEXT("vertex-buffer slot is bound more than once"));
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
            || !VulkanDetail::IsIndexFormatSupported(
                indexBinding.format,
                m_context.fullDrawIndexUint32FeatureEnabled
            )
            || (indexBinding.offset % indexBytes) != 0u
            || indexBinding.offset >= indexBinding.buffer->m_creationDesc.byteSize
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("index-buffer binding is invalid"));
            return false;
        }
    }
    else if(indexBinding.offset != 0u || indexBinding.format != Format::UNKNOWN){
        rejectCommandRecording(s_OperationName, NWB_TEXT("index-buffer metadata has no buffer"));
        return false;
    }

    if(
        state.indirectParams
        && (
            state.indirectParams->m_buffer == VK_NULL_HANDLE
            || !state.indirectParams->m_creationDesc.isDrawIndirectArgs
            || state.indirectParams->m_creationDesc.byteSize == 0u
        )
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("indirect-argument buffer is invalid"));
        return false;
    }

    struct BufferStateEntry{
        Buffer* buffer = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };
    BufferStateEntry requiredBufferStates[s_MaxVertexAttributes + 2u]{};
    u32 requiredBufferStateCount = 0u;

    const auto addRequiredBufferState = [&requiredBufferStates, &requiredBufferStateCount](
        Buffer* const buffer,
        const ResourceStates::Mask requiredState
    )noexcept -> bool{
        if(!buffer)
            return true;
        for(u32 stateIndex = 0u; stateIndex < requiredBufferStateCount; ++stateIndex){
            if(requiredBufferStates[stateIndex].buffer == buffer){
                requiredBufferStates[stateIndex].state |= requiredState;
                return true;
            }
            if(requiredBufferStates[stateIndex].buffer->m_buffer == buffer->m_buffer){
                return false;
            }
        }
        NWB_ASSERT(requiredBufferStateCount < LengthOf(requiredBufferStates));
        requiredBufferStates[requiredBufferStateCount].buffer = buffer;
        requiredBufferStates[requiredBufferStateCount].state = requiredState;
        ++requiredBufferStateCount;
        return true;
    };

    for(const VertexBufferBinding& binding : state.vertexBuffers){
        if(!addRequiredBufferState(binding.buffer, ResourceStates::VertexBuffer)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("distinct buffer objects alias the same native buffer"));
            return false;
        }
    }
    if(
        !addRequiredBufferState(state.indexBuffer.buffer, ResourceStates::IndexBuffer)
        || !addRequiredBufferState(state.indirectParams, ResourceStates::IndirectArgument)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("distinct buffer objects alias the same native buffer"));
        return false;
    }

    for(u32 stateIndex = 0u; stateIndex < requiredBufferStateCount; ++stateIndex){
        const BufferStateEntry& requiredBufferState = requiredBufferStates[stateIndex];
        if(!validateBufferForGpuState(requiredBufferState.buffer, requiredBufferState.state, s_OperationName))
            return false;
    }
    return true;
}

bool CommandList::validateMeshletState(const MeshletState& state){
    constexpr const tchar* s_OperationName = NWB_TEXT("set meshlet state");
    if(state.pipeline && &state.pipeline->m_context != &m_context){
        rejectCommandRecording(s_OperationName, NWB_TEXT("meshlet pipeline belongs to another device"));
        return false;
    }
    if(state.pipeline && !state.framebuffer){
        rejectCommandRecording(s_OperationName, NWB_TEXT("a meshlet pipeline requires an explicit framebuffer"));
        return false;
    }
    if(!validateFramebufferForRendering(state.framebuffer, s_OperationName))
        return false;
    if(!validateViewportState(state.viewport, s_OperationName))
        return false;

    if(state.pipeline){
        if(
            !m_context.extensions.EXT_mesh_shader
            || m_context.meshShaderFeatures.meshShader != VK_TRUE
            || !m_context.deviceDispatch.vkCmdDrawMeshTasksEXT
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("mesh shader feature or entry point is unavailable"));
            return false;
        }
        if(state.pipeline->m_desc.AS && m_context.meshShaderFeatures.taskShader != VK_TRUE){
            rejectCommandRecording(s_OperationName, NWB_TEXT("task shader feature is unavailable"));
            return false;
        }
        if(state.pipeline->m_pipeline == VK_NULL_HANDLE || state.pipeline->m_pipelineLayout == VK_NULL_HANDLE){
            rejectCommandRecording(s_OperationName, NWB_TEXT("meshlet pipeline has no native pipeline or layout"));
            return false;
        }
        if(state.pipeline->m_framebufferInfo != state.framebuffer->m_framebufferInfo){
            rejectCommandRecording(s_OperationName, NWB_TEXT("meshlet pipeline and framebuffer are incompatible"));
            return false;
        }
        const FramebufferAttachment& depthAttachment = state.framebuffer->m_desc.depthAttachment;
        if(
            depthAttachment.valid()
            && depthAttachment.isReadOnly
            && !VulkanDetail::IsDepthStencilReadOnlyCompatible(
                state.pipeline->m_desc.renderState.depthStencilState,
                depthAttachment.texture->m_aspectMask
            )
        ){
            rejectCommandRecording(
                s_OperationName,
                NWB_TEXT("meshlet pipeline writes a read-only depth/stencil attachment")
            );
            return false;
        }
    }

    if(
        state.indirectParams
        && (
            state.indirectParams->m_buffer == VK_NULL_HANDLE
            || !state.indirectParams->m_creationDesc.isDrawIndirectArgs
            || state.indirectParams->m_creationDesc.byteSize == 0u
        )
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("mesh indirect-argument buffer is invalid"));
        return false;
    }
    if(
        state.indirectParams
        && !validateBufferForGpuState(
            state.indirectParams,
            ResourceStates::IndirectArgument,
            s_OperationName
        )
    )
        return false;
    return true;
}

bool CommandList::validateGraphicsDrawState(
    const tchar* const operationName,
    const bool indexed
){
    GraphicsPipeline* const pipeline = m_currentGraphicsState.pipeline;
    if(
        !m_renderPassActive
        || !m_renderPassFramebuffer
        || !pipeline
        || pipeline->m_pipeline == VK_NULL_HANDLE
        || pipeline->m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo
    ){
        rejectCommandRecording(operationName, NWB_TEXT("no compatible graphics pipeline and render pass are active"));
        return false;
    }
    if(
        m_currentGraphicsState.viewport.viewports.size() != 1u
        || m_currentGraphicsState.viewport.scissorRects.size() > 1u
    ){
        rejectCommandRecording(operationName, NWB_TEXT("draw requires one viewport and at most one explicit scissor"));
        return false;
    }

    InputLayout* const inputLayout = pipeline->m_desc.inputLayout.get();
    if(inputLayout){
        for(const VkVertexInputBindingDescription& requiredBinding : inputLayout->m_bindings){
            bool bindingFound = false;
            for(const VertexBufferBinding& binding : m_currentGraphicsState.vertexBuffers){
                if(binding.slot == requiredBinding.binding){
                    bindingFound = true;
                    break;
                }
            }
            if(!bindingFound){
                rejectCommandRecording(operationName, NWB_TEXT("required vertex-buffer binding is missing"));
                return false;
            }
        }
    }
    if(indexed && !m_currentGraphicsState.indexBuffer.buffer){
        rejectCommandRecording(operationName, NWB_TEXT("indexed draw has no index buffer"));
        return false;
    }
    return true;
}

bool CommandList::validateGraphicsDrawArguments(
    const DrawArguments& arguments,
    const bool indexed,
    const tchar* const operationName
){
    if(!validateGraphicsDrawState(operationName, indexed))
        return false;

    if(indexed){
        if(arguments.startVertexLocation > static_cast<u32>(Limit<i32>::s_Max)){
            rejectCommandRecording(operationName, NWB_TEXT("indexed base vertex exceeds signed Vulkan range"));
            return false;
        }
        const IndexBufferBinding& indexBinding = m_currentGraphicsState.indexBuffer;
        const u32 indexBytes = VulkanDetail::GetIndexElementByteSize(indexBinding.format);
        if(!VulkanDetail::IsIndexDrawRangeValid(
            indexBinding.buffer->m_creationDesc,
            indexBinding.offset,
            arguments.startIndexLocation,
            arguments.vertexCount,
            indexBytes
        )){
            rejectCommandRecording(operationName, NWB_TEXT("indexed draw range exceeds the index buffer"));
            return false;
        }
    }

    InputLayout* const inputLayout = m_currentGraphicsState.pipeline->m_desc.inputLayout.get();
    if(!inputLayout)
        return true;

    for(const VkVertexInputBindingDescription& requiredBinding : inputLayout->m_bindings){
        const VertexBufferBinding* boundBuffer = nullptr;
        for(const VertexBufferBinding& binding : m_currentGraphicsState.vertexBuffers){
            if(binding.slot == requiredBinding.binding){
                boundBuffer = &binding;
                break;
            }
        }
        NWB_ASSERT(boundBuffer && boundBuffer->buffer);

        u32 requiredElementBytes = 0u;
        for(const VertexAttributeDesc& attribute : inputLayout->m_attributes){
            if(attribute.bufferIndex != requiredBinding.binding)
                continue;
            const u64 attributeBytes = static_cast<u64>(GetFormatInfo(attribute.format).bytesPerBlock)
                * attribute.arraySize
            ;
            const u64 attributeEnd = static_cast<u64>(attribute.offset) + attributeBytes;
            requiredElementBytes = Max(requiredElementBytes, static_cast<u32>(attributeEnd));
        }

        const bool instanceRate = requiredBinding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE;
        if(indexed && !instanceRate)
            continue;
        const u32 firstElement = instanceRate ? arguments.startInstanceLocation : arguments.startVertexLocation;
        const u32 elementCount = instanceRate ? arguments.instanceCount : arguments.vertexCount;
        if(!VulkanDetail::IsStridedBufferRangeValid(
            boundBuffer->buffer->m_creationDesc,
            boundBuffer->offset,
            firstElement,
            elementCount,
            requiredBinding.stride,
            requiredElementBytes
        )){
            rejectCommandRecording(operationName, NWB_TEXT("draw range exceeds a vertex buffer"));
            return false;
        }
    }
    return true;
}

bool CommandList::validateIndirectBuffer(
    Buffer* const buffer,
    const u64 offsetBytes,
    const u64 commandSizeBytes,
    const u32 commandCount,
    const tchar* const commandName
){
    if(!buffer){
        rejectCommandRecording(commandName, NWB_TEXT("no indirect-argument buffer is bound"));
        return false;
    }
    if(buffer->m_buffer == VK_NULL_HANDLE){
        rejectCommandRecording(commandName, NWB_TEXT("indirect-argument buffer has no native buffer"));
        return false;
    }
    if(!buffer->m_creationDesc.isDrawIndirectArgs){
        rejectCommandRecording(commandName, NWB_TEXT("buffer lacks indirect-argument usage"));
        return false;
    }
    if(!VulkanDetail::IsIndirectCommandRangeValid(
        buffer->m_creationDesc,
        offsetBytes,
        commandSizeBytes,
        commandCount
    )){
        rejectCommandRecording(commandName, NWB_TEXT("indirect-argument range is invalid"));
        return false;
    }
    return true;
}

bool CommandList::prepareDrawIndirect(
    const u32 offsetBytes,
    const u32 drawCount,
    const u64 commandSizeBytes,
    const tchar* const operationLabel,
    const tchar* const commandName,
    const VulkanDetail::IndirectDrawIndexMode::Enum indexMode,
    Buffer*& outIndirectBuffer
){
    outIndirectBuffer = nullptr;
    if(drawCount == 0u)
        return false;

    const bool indexed = indexMode == VulkanDetail::IndirectDrawIndexMode::Indexed;
    if(!validateGraphicsDrawState(operationLabel, indexed))
        return false;
    if(!m_context.drawIndirectFirstInstanceFeatureEnabled){
        rejectCommandRecording(operationLabel, NWB_TEXT("drawIndirectFirstInstance was not enabled"));
        return false;
    }
    if(!VulkanDetail::IsIndirectDrawCountValid(
        drawCount,
        m_context.physicalDeviceProperties.limits.maxDrawIndirectCount,
        m_context.multiDrawIndirectFeatureEnabled
    )){
        rejectCommandRecording(operationLabel, NWB_TEXT("indirect draw count is unsupported or exceeds the device limit"));
        return false;
    }
    if(!validateIndirectBuffer(
        m_currentGraphicsState.indirectParams,
        offsetBytes,
        commandSizeBytes,
        drawCount,
        commandName
    ))
        return false;

    outIndirectBuffer = m_currentGraphicsState.indirectParams;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

