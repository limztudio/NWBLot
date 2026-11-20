// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/backend_selection/resource_validation.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/graphics/backend_selection/raster_validation.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_raster_lowering{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBuffer(const ResourceStates::Mask state, const GpuPhysicalQueueInfo& queue, Buffer& buffer, CommandList& commandList)noexcept{
    if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &buffer, state))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    if(!ResourceQueueAdmissionAdmitsQueue(buffer.getQueueAdmissionSnapshot(), queue))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(&buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != state)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateColorAttachment(const GpuPhysicalQueueInfo& queue, Texture& texture, CommandList& commandList)noexcept{
    if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &texture, ResourceStates::RenderTarget))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    if(!ResourceQueueAdmissionAdmitsQueue(texture.getQueueAdmissionSnapshot(), queue))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(&texture);
    if(permanentState != ResourceStates::Unknown && permanentState != ResourceStates::RenderTarget)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateStateOwner(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrRasterStateOwner& owner,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    const u64 recordIndex,
    CommandList& commandList
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
    if(!GraphicsBackend::IsViewportValid(record.viewport, device) || (!record.hasScissor && !GraphicsBackend::IsImplicitScissorValid(record.viewport)))
        return GpuCommandIrReplayError::InvalidRasterState;
    if(!GraphicsBackend::IsGraphicsPipelineReady(*owner.pipeline, device) || owner.pipeline->getFramebufferInfo() != owner.framebuffer->getFramebufferInfo())
        return GpuCommandIrReplayError::BackendResourceNotReady;

    const FramebufferDesc& framebuffer = owner.framebuffer->getDescription();
    if(framebuffer.colorAttachments.size() != 1u || framebuffer.depthAttachment.valid() || framebuffer.shadingRateAttachment.valid() || framebuffer.colorAttachments[0u].texture != graph.textureForResource(record.colorAttachment))
        return GpuCommandIrReplayError::InvalidRasterOwner;
    Texture* const color = framebuffer.colorAttachments[0u].texture;
    if(!color || !color->getDescription().isRenderTarget || !GraphicsBackend::IsFramebufferAttachmentSubresourceSetValid(color->getDescription(), framebuffer.colorAttachments[0u].subresources))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const GpuCommandIrReplayError::Enum colorError = ValidateColorAttachment(queue, *color, commandList);
    if(colorError != GpuCommandIrReplayError::None)
        return colorError;

    for(usize bindingIndex = 0u; bindingIndex < record.vertexBuffers.size(); ++bindingIndex){
        const GpuCommandIrRasterVertexBinding& binding = record.vertexBuffers[bindingIndex];
        Buffer* const buffer = owner.vertexBuffers[bindingIndex].get();
        if(buffer != graph.bufferForResource(binding.resource))
            return GpuCommandIrReplayError::InvalidRasterOwner;
        if(!buffer || !buffer->getDescription().isVertexBuffer || binding.offset >= buffer->getDescription().byteSize)
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const b8 sharedIndex = record.indexResource.valid() && buffer == owner.indexBuffer.get();
        const ResourceStates::Mask requiredState = sharedIndex ? ResourceStates::VertexBuffer | ResourceStates::IndexBuffer : ResourceStates::VertexBuffer;
        const GpuCommandIrReplayError::Enum bufferError = ValidateBuffer(requiredState, queue, *buffer, commandList);
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
    return ValidateBuffer(ResourceStates::IndexBuffer, queue, *index, commandList);
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateDraw(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrDetail::RasterReplayState& state,
    const GpuCommandIrRasterStateOwner& owner
)noexcept{
    const b8 indexed = record.opcode == GpuCommandIrWireOpcode::DrawIndexed;
    if(indexed){
        auto& index = *MakeNotNull(owner.indexBuffer.get());
        const u32 indexBytes = GraphicsBackend::GetIndexElementByteSize(state.graphicsState.indexFormat);
        if(!GraphicsBackend::IsIndexDrawRangeValid(index.getDescription(), state.graphicsState.indexOffset, record.drawArguments.startIndexLocation, record.drawArguments.vertexCount, indexBytes))
            return GpuCommandIrReplayError::InvalidRasterDraw;
    }

    for(const GpuCommandIrDetail::CachedRasterVertexValidation& entry : state.cachedVertexValidations){
        if(indexed && !entry.instanceRate)
            continue;
        const u32 first = entry.instanceRate ? record.drawArguments.startInstanceLocation : record.drawArguments.startVertexLocation;
        const u32 count = entry.instanceRate ? record.drawArguments.instanceCount : record.drawArguments.vertexCount;
        const GpuCommandIrRasterVertexBinding& binding = state.graphicsState.vertexBuffers[entry.vertexBindingIndex];
        if(!GraphicsBackend::IsStridedBufferRangeValid(owner.vertexBuffers[entry.vertexBindingIndex]->getDescription(), binding.offset, first, count, entry.stride, entry.requiredBytes))
            return GpuCommandIrReplayError::InvalidRasterDraw;
    }
    return state.cachedInputLayoutError;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateRasterBackendOperand(
    const GpuCommandIrRasterTaskRecord& record,
    const RasterOwnerTableView& owners,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    const u64 recordIndex,
    CommandList& commandList,
    RasterReplayState& state
)noexcept{
    switch(record.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrRasterStateOwner* const owner = owners.rasterStateOwner(record.framebufferOwnerIndex);
        if(!owner)
            return GpuCommandIrReplayError::InvalidRasterOwner;
        Device& device = commandList.getDevice();
        const GpuCommandIrRasterStateOwner* const previousOwner = state.active && state.task == record.task ? state.cachedValidatedOwner : nullptr;
        b8 matchesCachedOwner = previousOwner != nullptr
            && owner->recordIndex == recordIndex
            && owner->pipeline.get() == previousOwner->pipeline.get()
            && owner->framebuffer.get() == previousOwner->framebuffer.get()
            && owner->colorAttachment == previousOwner->colorAttachment
            && record.colorAttachment == state.graphicsState.colorAttachment
            && record.pipeline == state.graphicsState.pipeline
            && owner->indexBuffer.get() == previousOwner->indexBuffer.get()
            && record.indexResource == state.graphicsState.indexResource
            && owner->vertexBuffers.size() == record.vertexBuffers.size()
            && owner->vertexBuffers.size() == previousOwner->vertexBuffers.size()
        ;
        if(matchesCachedOwner){
            for(usize bindingIndex = 0u; bindingIndex < record.vertexBuffers.size(); ++bindingIndex){
                if(
                    owner->vertexBuffers[bindingIndex].get() != previousOwner->vertexBuffers[bindingIndex].get()
                    || record.vertexBuffers[bindingIndex].resource != state.graphicsState.vertexBuffers[bindingIndex].resource
                    || record.vertexBuffers[bindingIndex].slot != state.graphicsState.vertexBuffers[bindingIndex].slot
                ){
                    matchesCachedOwner = false;
                    break;
                }
            }
        }
        if(matchesCachedOwner){
            if(!GraphicsBackend::IsViewportValid(record.viewport, device) || (!record.hasScissor && !GraphicsBackend::IsImplicitScissorValid(record.viewport)))
                return GpuCommandIrReplayError::InvalidRasterState;
            for(usize bindingIndex = 0u; bindingIndex < record.vertexBuffers.size(); ++bindingIndex){
                if(record.vertexBuffers[bindingIndex].offset >= owner->vertexBuffers[bindingIndex]->getDescription().byteSize)
                    return GpuCommandIrReplayError::BackendResourceNotReady;
            }
            if(record.indexResource.valid() && record.indexOffset >= owner->indexBuffer->getDescription().byteSize)
                return GpuCommandIrReplayError::BackendResourceNotReady;
            state.graphicsState = record;
            state.task = record.task;
            state.active = true;
            state.heapBound = false;
            state.pushConstantsSet = false;
            state.cachedValidatedOwner = owner;
            return GpuCommandIrReplayError::None;
        }

        const GpuCommandIrReplayError::Enum error = __hidden_gpu_command_ir_raster_lowering::ValidateStateOwner(record, *owner, graph, queue, recordIndex, commandList);
        if(error != GpuCommandIrReplayError::None)
            return error;
        state.graphicsState = record;
        state.task = record.task;
        state.active = true;
        state.heapBound = false;
        state.pushConstantsSet = false;
        state.cachedVertexValidations.clear();
        state.cachedInputLayoutError = GpuCommandIrReplayError::None;
        if(InputLayout* const inputLayout = owner->pipeline->getDescription().inputLayout.get()){
            const u32 numBindings = inputLayout->getNumBindings();
            const u32 numAttributes = inputLayout->getNumAttributes();
            for(u32 requiredIndex = 0u; requiredIndex < numBindings; ++requiredIndex){
                const GraphicsBackend::VertexInputBinding required = GraphicsBackend::GetVertexInputBinding(*inputLayout, requiredIndex);
                if(!required.valid){
                    state.cachedInputLayoutError = GpuCommandIrReplayError::BackendResourceNotReady;
                    break;
                }
                usize bindingIndex = 0u;
                while(bindingIndex < record.vertexBuffers.size() && record.vertexBuffers[bindingIndex].slot != required.slot)
                    ++bindingIndex;
                if(bindingIndex == record.vertexBuffers.size()){
                    state.cachedInputLayoutError = GpuCommandIrReplayError::InvalidRasterDraw;
                    break;
                }
                u32 requiredBytes = 0u;
                b8 foundAttribute = false;
                for(u32 matchingIndex = 0u; matchingIndex < numAttributes; ++matchingIndex){
                    const VertexAttributeDesc* const matching = inputLayout->getAttributeDescription(matchingIndex);
                    if(!matching || matching->bufferIndex != required.slot)
                        continue;
                    foundAttribute = true;
                    const FormatInfo& format = GetFormatInfo(matching->format);
                    const u64 extent = static_cast<u64>(matching->offset) + static_cast<u64>(format.bytesPerBlock) * matching->arraySize;
                    if(extent > Limit<u32>::s_Max){
                        state.cachedInputLayoutError = GpuCommandIrReplayError::InvalidRasterDraw;
                        break;
                    }
                    requiredBytes = Max(requiredBytes, static_cast<u32>(extent));
                }
                if(state.cachedInputLayoutError != GpuCommandIrReplayError::None)
                    break;
                if(!foundAttribute || state.cachedVertexValidations.size() >= s_MaxVertexAttributes){
                    state.cachedInputLayoutError = GpuCommandIrReplayError::InvalidRasterDraw;
                    break;
                }
                state.cachedVertexValidations.push_back(CachedRasterVertexValidation{
                    .stride = required.stride,
                    .requiredBytes = requiredBytes,
                    .vertexBindingIndex = static_cast<u8>(bindingIndex),
                    .instanceRate = required.instanceRate,
                });
            }
        }
        state.cachedValidatedOwner = owner;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        if(!state.active || state.graphicsState.pipeline != record.pipeline)
            return GpuCommandIrReplayError::InvalidRasterState;
        const GpuCommandIrRasterHeapOwner* const heapOwner = owners.rasterHeapOwner(record.heapOwnerIndex);
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
        auto& owner = *MakeNotNull(owners.rasterStateOwner(state.graphicsState.framebufferOwnerIndex));
        FOUNDATION_ASSERT(owner.pipeline);
        if(record.blobSizeBytes > owner.pipeline->pushConstantByteSize)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.pushConstantsSet = true;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:{
        if(!state.active || !state.heapBound || !state.pushConstantsSet)
            return GpuCommandIrReplayError::InvalidRasterState;
        auto& owner = *MakeNotNull(owners.rasterStateOwner(state.graphicsState.framebufferOwnerIndex));
        return __hidden_gpu_command_ir_raster_lowering::ValidateDraw(record, state, owner);
    }
    case GpuCommandIrWireOpcode::EndRenderPass:
        state.active = false;
        state.heapBound = false;
        state.pushConstantsSet = false;
        state.cachedValidatedOwner = nullptr;
        state.cachedVertexValidations.clear();
        state.cachedInputLayoutError = GpuCommandIrReplayError::None;
        return GpuCommandIrReplayError::None;
    default:
        return GpuCommandIrReplayError::InvalidRasterState;
    }
}

b8 LowerRasterOperation(const GpuCommandIrRasterTaskRecord& record, const RasterOwnerTableView& owners, const BinaryByteView blobBytes, CommandList& commandList){
    switch(record.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        auto& owner = *MakeNotNull(owners.rasterStateOwner(record.framebufferOwnerIndex));
        FOUNDATION_ASSERT(owner.pipeline && owner.framebuffer);
        ViewportState viewport;
        viewport.addViewport(record.viewport);
        if(record.hasScissor)
            viewport.addScissorRect(record.scissor);
        GraphicsState state;
        state.setPipeline(owner.pipeline.get()).setFramebuffer(owner.framebuffer.get()).setViewport(viewport).setBlendConstantColor(record.blendConstantColor).setDynamicStencilRefValue(record.dynamicStencilRefValue);
        for(usize index = 0u; index < record.vertexBuffers.size(); ++index){
            const GpuCommandIrRasterVertexBinding& binding = record.vertexBuffers[index];
            state.addVertexBuffer(VertexBufferBinding().setBuffer(owner.vertexBuffers[index].get()).setSlot(binding.slot).setOffset(binding.offset));
        }
        if(record.indexResource.valid()){
            state.setIndexBuffer(IndexBufferBinding().setBuffer(owner.indexBuffer.get()).setFormat(record.indexFormat).setOffset(record.indexOffset));
        }
        commandList.setGraphicsState(state);
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        auto& owner = *MakeNotNull(owners.rasterHeapOwner(record.heapOwnerIndex));
        FOUNDATION_ASSERT(owner.heap && owner.pipeline);
        owner.heap->bindGraphics(commandList, *owner.pipeline);
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::SetPushConstants:
        FOUNDATION_ASSERT(blobBytes.data() && record.blobOffsetBytes <= static_cast<u64>(blobBytes.size()) && record.blobSizeBytes <= static_cast<u64>(blobBytes.size()) - record.blobOffsetBytes);
        commandList.setPushConstants(blobBytes.data() + static_cast<usize>(record.blobOffsetBytes), static_cast<usize>(record.blobSizeBytes));
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

