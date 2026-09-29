// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/graphics/vulkan/command_validation.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_raster_lowering{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBuffer(
    Buffer* const buffer,
    const ResourceStates::Mask state,
    const VkBufferUsageFlags usage,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList
)noexcept{
    if(!buffer || !commandList.getDevice().isBufferReadyForGpuUse(buffer, usage))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    if(!ResourceQueueAdmissionAdmitsQueue(buffer->getQueueAdmissionSnapshot(), queue))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != state)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateColorAttachment(
    Texture* const texture,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList
)noexcept{
    if(!texture || !commandList.getDevice().isTextureReadyForGpuUse(texture, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    if(!ResourceQueueAdmissionAdmitsQueue(texture->getQueueAdmissionSnapshot(), queue))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(texture);
    if(permanentState != ResourceStates::Unknown && permanentState != ResourceStates::RenderTarget)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateStateOwner(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrRasterStateOwner& owner,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList,
    const u64 recordIndex
)noexcept{
    if(
        owner.recordIndex != recordIndex
        || !owner.pipeline
        || !owner.framebuffer
        || owner.pipeline.get() != graph.graphicsPipelineFor(record.pipeline)
        || owner.colorAttachment != record.colorAttachment
        || owner.vertexBuffers.size() != record.vertexBuffers.size()
    )
        return GpuCommandIrReplayError::InvalidRasterOwner;

    Device& device = commandList.getDevice();
    if(
        !GraphicsBackend::VulkanDetail::IsViewportValid(record.viewport, device.getPhysicalDeviceLimits())
        || (!record.hasScissor && !GraphicsBackend::VulkanDetail::IsImplicitScissorValid(record.viewport))
    )
        return GpuCommandIrReplayError::InvalidRasterState;
    if(
        owner.pipeline->getDeviceGeneration() != device.getDeviceGeneration()
        || owner.pipeline->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Pipeline).integer == 0u
        || owner.pipeline->m_pipelineLayout == VK_NULL_HANDLE
        || owner.pipeline->getFramebufferInfo() != owner.framebuffer->getFramebufferInfo()
    )
        return GpuCommandIrReplayError::BackendResourceNotReady;

    const FramebufferDesc& framebuffer = owner.framebuffer->getDescription();
    if(
        framebuffer.colorAttachments.size() != 1u
        || framebuffer.depthAttachment.valid()
        || framebuffer.shadingRateAttachment.valid()
        || framebuffer.colorAttachments[0u].texture != graph.textureForResource(record.colorAttachment)
    )
        return GpuCommandIrReplayError::InvalidRasterOwner;
    Texture* const color = framebuffer.colorAttachments[0u].texture;
    if(
        !color
        || !color->getDescription().isRenderTarget
        || !GraphicsBackend::VulkanDetail::IsFramebufferAttachmentSubresourceSetValid(
            color->getDescription(), framebuffer.colorAttachments[0u].subresources
        )
    )
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const GpuCommandIrReplayError::Enum colorError = ValidateColorAttachment(color, queue, commandList);
    if(colorError != GpuCommandIrReplayError::None)
        return colorError;

    for(usize bindingIndex = 0u; bindingIndex < record.vertexBuffers.size(); ++bindingIndex){
        const GpuCommandIrRasterVertexBinding& binding = record.vertexBuffers[bindingIndex];
        Buffer* const buffer = owner.vertexBuffers[bindingIndex].get();
        if(buffer != graph.bufferForResource(binding.resource))
            return GpuCommandIrReplayError::InvalidRasterOwner;
        if(!buffer || !buffer->getDescription().isVertexBuffer || binding.offset >= buffer->getDescription().byteSize)
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const bool sharedIndex = record.indexResource.valid() && buffer == owner.indexBuffer.get();
        const ResourceStates::Mask requiredState = sharedIndex
            ? ResourceStates::VertexBuffer | ResourceStates::IndexBuffer
            : ResourceStates::VertexBuffer
        ;
        const VkBufferUsageFlags requiredUsage = sharedIndex
            ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        ;
        const GpuCommandIrReplayError::Enum bufferError = ValidateBuffer(
            buffer, requiredState, requiredUsage, queue, commandList
        );
        if(bufferError != GpuCommandIrReplayError::None)
            return bufferError;
    }

    if(!record.indexResource.valid()){
        if(owner.indexBuffer)
            return GpuCommandIrReplayError::InvalidRasterOwner;
        return GpuCommandIrReplayError::None;
    }
    Buffer* const index = owner.indexBuffer.get();
    if(index != graph.bufferForResource(record.indexResource))
        return GpuCommandIrReplayError::InvalidRasterOwner;
    if(!index || !index->getDescription().isIndexBuffer || record.indexOffset >= index->getDescription().byteSize)
        return GpuCommandIrReplayError::BackendResourceNotReady;
    for(const BufferHandle& vertex : owner.vertexBuffers){
        if(vertex.get() == index)
            return GpuCommandIrReplayError::None;
    }
    return ValidateBuffer(index, ResourceStates::IndexBuffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, queue, commandList);
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateDraw(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrRasterTaskRecord& stateRecord,
    const GpuCommandIrRasterStateOwner& owner
)noexcept{
    const bool indexed = record.opcode == GpuCommandIrWireOpcode::DrawIndexed;
    if(indexed){
        Buffer* const index = owner.indexBuffer.get();
        if(!index)
            return GpuCommandIrReplayError::InvalidRasterDraw;
        const u32 indexBytes = GraphicsBackend::VulkanDetail::GetIndexElementByteSize(stateRecord.indexFormat);
        if(!GraphicsBackend::VulkanDetail::IsIndexDrawRangeValid(
            index->getDescription(), stateRecord.indexOffset,
            record.drawArguments.startIndexLocation, record.drawArguments.vertexCount, indexBytes
        ))
            return GpuCommandIrReplayError::InvalidRasterDraw;
    }

    InputLayout* const inputLayout = owner.pipeline->getDescription().inputLayout.get();
    if(!inputLayout)
        return GpuCommandIrReplayError::None;

    for(u32 requiredIndex = 0u; requiredIndex < inputLayout->getNumBindings(); ++requiredIndex){
        const VkVertexInputBindingDescription* const required = inputLayout->getBindingDescription(requiredIndex);
        if(!required)
            return GpuCommandIrReplayError::BackendResourceNotReady;

        usize bindingIndex = 0u;
        while(
            bindingIndex < stateRecord.vertexBuffers.size()
            && stateRecord.vertexBuffers[bindingIndex].slot != required->binding
        )
            ++bindingIndex;
        if(bindingIndex == stateRecord.vertexBuffers.size())
            return GpuCommandIrReplayError::InvalidRasterDraw;

        u32 requiredBytes = 0u;
        bool foundAttribute = false;
        for(u32 matchingIndex = 0u; matchingIndex < inputLayout->getNumAttributes(); ++matchingIndex){
            const VertexAttributeDesc* const matching = inputLayout->getAttributeDescription(matchingIndex);
            if(!matching || matching->bufferIndex != required->binding)
                continue;
            foundAttribute = true;
            const FormatInfo& format = GetFormatInfo(matching->format);
            const u64 extent = static_cast<u64>(matching->offset)
                + static_cast<u64>(format.bytesPerBlock) * matching->arraySize
            ;
            if(extent > Limit<u32>::s_Max)
                return GpuCommandIrReplayError::InvalidRasterDraw;
            requiredBytes = Max(requiredBytes, static_cast<u32>(extent));
        }
        if(!foundAttribute)
            return GpuCommandIrReplayError::InvalidRasterDraw;
        const bool instanceRate = required->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE;
        if(indexed && !instanceRate)
            continue;
        const u32 first = instanceRate
            ? record.drawArguments.startInstanceLocation
            : record.drawArguments.startVertexLocation
        ;
        const u32 count = instanceRate
            ? record.drawArguments.instanceCount
            : record.drawArguments.vertexCount
        ;
        const GpuCommandIrRasterVertexBinding& binding = stateRecord.vertexBuffers[bindingIndex];
        if(!GraphicsBackend::VulkanDetail::IsStridedBufferRangeValid(
            owner.vertexBuffers[bindingIndex]->getDescription(), binding.offset,
            first, count, required->stride, requiredBytes
        ))
            return GpuCommandIrReplayError::InvalidRasterDraw;
    }
    return GpuCommandIrReplayError::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateRasterBackendOperand(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrOwnedStream& stream,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList,
    const u64 recordIndex,
    RasterReplayState& state
)noexcept{
    switch(record.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrRasterStateOwner* const owner = stream.rasterStateOwner(record.framebufferOwnerIndex);
        if(!owner)
            return GpuCommandIrReplayError::InvalidRasterOwner;
        const GpuCommandIrReplayError::Enum error = __hidden_gpu_command_ir_raster_lowering::ValidateStateOwner(
            record, *owner, graph, queue, commandList, recordIndex
        );
        if(error != GpuCommandIrReplayError::None)
            return error;
        state.graphicsState = record;
        state.task = record.task;
        state.active = true;
        state.heapBound = false;
        state.pushConstantsSet = false;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        if(!state.active || state.graphicsState.pipeline != record.pipeline)
            return GpuCommandIrReplayError::InvalidRasterState;
        const GpuCommandIrRasterHeapOwner* const heapOwner = stream.rasterHeapOwner(record.heapOwnerIndex);
        if(
            !heapOwner
            || heapOwner->recordIndex != recordIndex
            || !heapOwner->pipeline
            || heapOwner->pipeline.get() != graph.graphicsPipelineFor(record.pipeline)
            || heapOwner->heap != &commandList.getDevice().getDescriptorHeap()
            || !heapOwner->heap->isInitialized()
            || !heapOwner->lease.valid()
            || !heapOwner->descriptors.valid()
        )
            return GpuCommandIrReplayError::InvalidRasterOwner;
        state.heapBound = true;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::SetPushConstants:{
        if(!state.active)
            return GpuCommandIrReplayError::InvalidRasterState;
        const GpuCommandIrRasterStateOwner* const owner = stream.rasterStateOwner(
            state.graphicsState.framebufferOwnerIndex
        );
        if(!owner || !owner->pipeline || record.blobSizeBytes > owner->pipeline->m_pushConstantByteSize)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.pushConstantsSet = true;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:{
        if(!state.active || !state.heapBound || !state.pushConstantsSet)
            return GpuCommandIrReplayError::InvalidRasterState;
        const GpuCommandIrRasterStateOwner* const owner = stream.rasterStateOwner(
            state.graphicsState.framebufferOwnerIndex
        );
        if(!owner)
            return GpuCommandIrReplayError::InvalidRasterOwner;
        return __hidden_gpu_command_ir_raster_lowering::ValidateDraw(record, state.graphicsState, *owner);
    }
    case GpuCommandIrWireOpcode::EndRenderPass:
        state.active = false;
        state.heapBound = false;
        state.pushConstantsSet = false;
        return GpuCommandIrReplayError::None;
    default:
        return GpuCommandIrReplayError::InvalidRasterState;
    }
}

bool LowerRasterOperation(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrOwnedStream& stream,
    const BinaryByteView blobBytes,
    CommandList& commandList
)noexcept{
    switch(record.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrRasterStateOwner* const owner = stream.rasterStateOwner(record.framebufferOwnerIndex);
        if(!owner || !owner->pipeline || !owner->framebuffer)
            return false;
        ViewportState viewport;
        viewport.addViewport(record.viewport);
        if(record.hasScissor)
            viewport.addScissorRect(record.scissor);
        GraphicsState state;
        state.setPipeline(owner->pipeline.get())
            .setFramebuffer(owner->framebuffer.get())
            .setViewport(viewport)
            .setBlendColor(record.blendConstantColor)
            .setDynamicStencilRefValue(record.dynamicStencilRefValue)
        ;
        for(usize index = 0u; index < record.vertexBuffers.size(); ++index){
            const GpuCommandIrRasterVertexBinding& binding = record.vertexBuffers[index];
            state.addVertexBuffer(
                VertexBufferBinding().setBuffer(owner->vertexBuffers[index].get()).setSlot(binding.slot).setOffset(binding.offset)
            );
        }
        if(record.indexResource.valid()){
            state.setIndexBuffer(
                IndexBufferBinding().setBuffer(owner->indexBuffer.get()).setFormat(record.indexFormat).setOffset(record.indexOffset)
            );
        }
        commandList.setGraphicsState(state);
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        const GpuCommandIrRasterHeapOwner* const owner = stream.rasterHeapOwner(record.heapOwnerIndex);
        if(!owner || !owner->heap || !owner->pipeline)
            return false;
        owner->heap->bindGraphics(commandList, *owner->pipeline);
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::SetPushConstants:
        if(
            !blobBytes.data()
            || record.blobOffsetBytes > static_cast<u64>(blobBytes.size())
            || record.blobSizeBytes > static_cast<u64>(blobBytes.size()) - record.blobOffsetBytes
        )
            return false;
        commandList.setPushConstants(
            blobBytes.data() + static_cast<usize>(record.blobOffsetBytes), static_cast<usize>(record.blobSizeBytes)
        );
        return !commandList.commandRecordingFailed();
    case GpuCommandIrWireOpcode::Draw:
        commandList.draw(record.drawArguments);
        return !commandList.commandRecordingFailed();
    case GpuCommandIrWireOpcode::DrawIndexed:
        commandList.drawIndexed(record.drawArguments);
        return !commandList.commandRecordingFailed();
    case GpuCommandIrWireOpcode::EndRenderPass:
        commandList.endRenderPass();
        return !commandList.commandRecordingFailed();
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

