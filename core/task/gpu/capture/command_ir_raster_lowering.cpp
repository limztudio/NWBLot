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
    const GpuCommandIrSetGraphicsStateRecord& wire,
    const u8* const vertexPtr,
    const GpuGraphPipelineId pipeline,
    const GpuGraphResourceId colorAttachment,
    const GpuGraphResourceId indexResource,
    const Viewport& viewport,
    const GpuCommandIrRasterStateOwner& owner,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    const u64 recordIndex,
    CommandList& commandList,
    GpuCommandIrDetail::BackendRasterReplayState& state
)noexcept{
    if(
        owner.recordIndex != recordIndex
        || !owner.pipeline
        || !owner.framebuffer
        || owner.pipeline.get() != graph.graphicsPipelineFor(pipeline)
        || owner.colorAttachment != colorAttachment
        || owner.vertexBuffers.size() != wire.vertexBindingCount
    )
        return GpuCommandIrReplayError::InvalidRasterOwner;

    Device& device = commandList.getDevice();
    if(!GraphicsBackend::IsViewportValid(viewport, device) || (wire.hasScissor == 0u && !GraphicsBackend::IsImplicitScissorValid(viewport)))
        return GpuCommandIrReplayError::InvalidRasterState;
    if(!GraphicsBackend::IsGraphicsPipelineReady(*owner.pipeline, device) || owner.pipeline->getFramebufferInfo() != owner.framebuffer->getFramebufferInfo())
        return GpuCommandIrReplayError::BackendResourceNotReady;

    const FramebufferDesc& framebuffer = owner.framebuffer->getDescription();
    if(framebuffer.colorAttachments.size() != 1u || framebuffer.depthAttachment.valid() || framebuffer.shadingRateAttachment.valid() || framebuffer.colorAttachments[0u].texture != graph.textureForResource(colorAttachment))
        return GpuCommandIrReplayError::InvalidRasterOwner;
    Texture* const color = framebuffer.colorAttachments[0u].texture;
    if(!color || !color->getDescription().isRenderTarget || !GraphicsBackend::IsFramebufferAttachmentSubresourceSetValid(color->getDescription(), framebuffer.colorAttachments[0u].subresources))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const GpuCommandIrReplayError::Enum colorError = ValidateColorAttachment(queue, *color, commandList);
    if(colorError != GpuCommandIrReplayError::None)
        return colorError;

    const u64 generation = colorAttachment.generation;
    for(u8 bindingIndex = 0u; bindingIndex < wire.vertexBindingCount; ++bindingIndex){
        GpuCommandIrRasterVertexWire vertexWire;
        FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + bindingIndex * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
        Buffer* const buffer = owner.vertexBuffers[bindingIndex].get();
        if(buffer != graph.bufferForResource(GpuGraphResourceId{ .generation = generation, .index = vertexWire.resourceIndex }))
            return GpuCommandIrReplayError::InvalidRasterOwner;
        if(!buffer)
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const BufferDesc& desc = buffer->getDescription();
        if(!desc.isVertexBuffer || vertexWire.offset >= desc.byteSize)
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const b8 sharedIndex = indexResource.valid() && buffer == owner.indexBuffer.get();
        const ResourceStates::Mask requiredState = sharedIndex ? ResourceStates::VertexBuffer | ResourceStates::IndexBuffer : ResourceStates::VertexBuffer;
        const GpuCommandIrReplayError::Enum bufferError = ValidateBuffer(requiredState, queue, *buffer, commandList);
        if(bufferError != GpuCommandIrReplayError::None)
            return bufferError;
        state.vertexBufferDescs[bindingIndex] = &desc;
    }

    if(!indexResource.valid()){
        if(owner.indexBuffer)
            return GpuCommandIrReplayError::InvalidRasterOwner;
        state.cachedIndexBufferDesc = nullptr;
        return GpuCommandIrReplayError::None;
    }
    Buffer* const index = owner.indexBuffer.get();
    if(index != graph.bufferForResource(indexResource))
        return GpuCommandIrReplayError::InvalidRasterOwner;
    if(!index)
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const BufferDesc& indexDesc = index->getDescription();
    if(!indexDesc.isIndexBuffer || wire.indexOffset >= indexDesc.byteSize)
        return GpuCommandIrReplayError::BackendResourceNotReady;
    state.cachedIndexBufferDesc = &indexDesc;
    for(const BufferHandle& vertex : owner.vertexBuffers){
        if(vertex.get() == index)
            return GpuCommandIrReplayError::None;
    }
    return ValidateBuffer(ResourceStates::IndexBuffer, queue, *index, commandList);
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateDraw(
    const GpuCommandIrWireOpcode::Enum opcode,
    const GpuCommandIrDrawRecord& draw,
    const GpuCommandIrDetail::BackendRasterReplayState& state
)noexcept{
    const b8 indexed = opcode == GpuCommandIrWireOpcode::DrawIndexed;
    if(indexed){
        FOUNDATION_ASSERT(state.cachedIndexBufferDesc);
        if(!GraphicsBackend::IsIndexDrawRangeValid(*state.cachedIndexBufferDesc, state.indexOffset, draw.startIndexLocation, draw.vertexCount, state.indexElementBytes))
            return GpuCommandIrReplayError::InvalidRasterDraw;
    }

    for(const GpuCommandIrDetail::CachedRasterVertexValidation& entry : state.cachedVertexValidations){
        if(indexed && !entry.instanceRate)
            continue;
        const u32 first = entry.instanceRate ? draw.startInstanceLocation : draw.startVertexLocation;
        const u32 count = entry.instanceRate ? draw.instanceCount : draw.vertexCount;
        FOUNDATION_ASSERT(entry.bufferDesc);
        if(!GraphicsBackend::IsStridedBufferRangeValid(*entry.bufferDesc, entry.offset, first, count, entry.stride, entry.requiredBytes))
            return GpuCommandIrReplayError::InvalidRasterDraw;
    }
    return state.cachedInputLayoutError;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateRasterWireBackendOperand(
    const GpuCommandIrWireOpcode::Enum opcode,
    const WireRecordStorage& record,
    const BinaryByteView blobBytes,
    const GpuTaskId taskId,
    const RasterOwnerTableView& owners,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    const u64 recordIndex,
    CommandList& commandList,
    BackendRasterReplayState& state
)noexcept{
    const u64 generation = taskId.generation;
    switch(opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrSetGraphicsStateRecord& wire = record.setGraphicsState;
        const GpuCommandIrRasterStateOwner* const owner = owners.rasterStateOwner(wire.framebufferOwnerIndex);
        if(!owner)
            return GpuCommandIrReplayError::InvalidRasterOwner;
        Device& device = commandList.getDevice();
        const u8* const vertexPtr = wire.vertexBindingCount != 0u ? blobBytes.data() + static_cast<usize>(wire.blobOffsetBytes) : nullptr;
        const GpuGraphPipelineId pipeline{ .generation = generation, .index = wire.pipelineIndex };
        const GpuGraphResourceId colorAttachment{ .generation = generation, .index = wire.colorAttachmentIndex };
        const GpuGraphResourceId indexResource = wire.indexResourceIndex != Limit<u32>::s_Max
            ? GpuGraphResourceId{ .generation = generation, .index = wire.indexResourceIndex }
            : GpuGraphResourceId{}
        ;
        const Viewport viewport{ wire.viewportMinX, wire.viewportMaxX, wire.viewportMinY, wire.viewportMaxY, wire.viewportMinZ, wire.viewportMaxZ };
        const Format::Enum indexFormat = static_cast<Format::Enum>(wire.indexFormat);

        const GpuCommandIrRasterStateOwner* const previousOwner = state.active && state.task == taskId ? state.cachedValidatedOwner : nullptr;
        b8 matchesCachedOwner = previousOwner != nullptr
            && owner->recordIndex == recordIndex
            && owner->pipeline.get() == previousOwner->pipeline.get()
            && owner->framebuffer.get() == previousOwner->framebuffer.get()
            && owner->colorAttachment == previousOwner->colorAttachment
            && colorAttachment == state.colorAttachment
            && pipeline == state.pipeline
            && owner->indexBuffer.get() == previousOwner->indexBuffer.get()
            && indexResource == state.indexResource
            && owner->vertexBuffers.size() == wire.vertexBindingCount
            && owner->vertexBuffers.size() == previousOwner->vertexBuffers.size()
            && state.vertexBindingCount == wire.vertexBindingCount
        ;
        if(matchesCachedOwner){
            for(u8 bindingIndex = 0u; bindingIndex < wire.vertexBindingCount; ++bindingIndex){
                GpuCommandIrRasterVertexWire vertexWire;
                FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + bindingIndex * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
                if(
                    owner->vertexBuffers[bindingIndex].get() != previousOwner->vertexBuffers[bindingIndex].get()
                    || vertexWire.resourceIndex != state.vertexBuffers[bindingIndex].resourceIndex
                    || generation != state.vertexBuffers[bindingIndex].resourceGeneration
                    || vertexWire.slot != state.vertexBuffers[bindingIndex].slot
                ){
                    matchesCachedOwner = false;
                    break;
                }
            }
        }
        if(matchesCachedOwner){
            if(!GraphicsBackend::IsViewportValid(viewport, device) || (wire.hasScissor == 0u && !GraphicsBackend::IsImplicitScissorValid(viewport)))
                return GpuCommandIrReplayError::InvalidRasterState;
            for(u8 bindingIndex = 0u; bindingIndex < wire.vertexBindingCount; ++bindingIndex){
                GpuCommandIrRasterVertexWire vertexWire;
                FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + bindingIndex * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
                FOUNDATION_ASSERT(state.vertexBufferDescs[bindingIndex]);
                if(vertexWire.offset >= state.vertexBufferDescs[bindingIndex]->byteSize)
                    return GpuCommandIrReplayError::BackendResourceNotReady;
            }
            if(indexResource.valid()){
                FOUNDATION_ASSERT(state.cachedIndexBufferDesc);
                if(wire.indexOffset >= state.cachedIndexBufferDesc->byteSize)
                    return GpuCommandIrReplayError::BackendResourceNotReady;
            }
            for(CachedRasterVertexValidation& entry : state.cachedVertexValidations){
                GpuCommandIrRasterVertexWire vertexWire;
                FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + entry.vertexBindingIndex * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
                entry.offset = vertexWire.offset;
            }
            state.task = taskId;
            state.pipeline = pipeline;
            state.colorAttachment = colorAttachment;
            state.indexResource = indexResource;
            state.framebufferOwnerIndex = wire.framebufferOwnerIndex;
            state.indexOffset = wire.indexOffset;
            state.indexFormat = indexFormat;
            state.indexElementBytes = indexResource.valid() ? GraphicsBackend::GetIndexElementByteSize(indexFormat) : 0u;
            state.active = true;
            state.heapBound = false;
            state.pushConstantsSet = false;
            state.cachedValidatedOwner = owner;
            return GpuCommandIrReplayError::None;
        }

        const GpuCommandIrReplayError::Enum error = __hidden_gpu_command_ir_raster_lowering::ValidateStateOwner(
            wire,
            vertexPtr,
            pipeline,
            colorAttachment,
            indexResource,
            viewport,
            *owner,
            graph,
            queue,
            recordIndex,
            commandList,
            state
        );
        if(error != GpuCommandIrReplayError::None)
            return error;

        state.task = taskId;
        state.pipeline = pipeline;
        state.colorAttachment = colorAttachment;
        state.indexResource = indexResource;
        state.framebufferOwnerIndex = wire.framebufferOwnerIndex;
        state.indexOffset = wire.indexOffset;
        state.indexFormat = indexFormat;
        state.indexElementBytes = indexResource.valid() ? GraphicsBackend::GetIndexElementByteSize(indexFormat) : 0u;
        state.vertexBindingCount = wire.vertexBindingCount;
        for(u8 bindingIndex = 0u; bindingIndex < wire.vertexBindingCount; ++bindingIndex){
            GpuCommandIrRasterVertexWire vertexWire;
            FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + bindingIndex * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
            state.vertexBuffers[bindingIndex] = RasterVertexCacheEntry{
                .resourceGeneration = generation,
                .resourceIndex = vertexWire.resourceIndex,
                .slot = vertexWire.slot,
            };
        }
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
                u8 bindingIndex = 0u;
                while(bindingIndex < wire.vertexBindingCount && state.vertexBuffers[bindingIndex].slot != required.slot)
                    ++bindingIndex;
                if(bindingIndex == wire.vertexBindingCount){
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
                GpuCommandIrRasterVertexWire vertexWire;
                FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + bindingIndex * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
                state.cachedVertexValidations.push_back(CachedRasterVertexValidation{
                    .bufferDesc = state.vertexBufferDescs[bindingIndex],
                    .offset = vertexWire.offset,
                    .stride = required.stride,
                    .requiredBytes = requiredBytes,
                    .vertexBindingIndex = bindingIndex,
                    .instanceRate = required.instanceRate,
                });
            }
        }
        state.cachedValidatedOwner = owner;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        const GpuCommandIrBindGraphicsHeapRecord& wire = record.bindGraphicsHeap;
        if(!state.active || state.pipeline != GpuGraphPipelineId{ .generation = generation, .index = wire.pipelineIndex } || !state.cachedValidatedOwner)
            return GpuCommandIrReplayError::InvalidRasterState;
        if(!state.cachedDescriptorHeap){
            state.cachedDescriptorHeap = &commandList.getDevice().getDescriptorHeap();
            state.cachedDescriptorHeapInitialized = state.cachedDescriptorHeap->isInitialized();
        }
        const GpuCommandIrRasterHeapOwner* const heapOwner = owners.rasterHeapOwner(wire.heapOwnerIndex);
        if(
            !heapOwner
            || heapOwner->recordIndex != recordIndex
            || !heapOwner->pipeline
            || heapOwner->pipeline.get() != state.cachedValidatedOwner->pipeline.get()
            || heapOwner->heap != state.cachedDescriptorHeap
            || !state.cachedDescriptorHeapInitialized
            || !heapOwner->lease.valid()
            || !heapOwner->descriptors.valid()
        )
            return GpuCommandIrReplayError::InvalidRasterOwner;
        state.heapBound = true;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::SetPushConstants:{
        if(!state.active || !state.cachedValidatedOwner)
            return GpuCommandIrReplayError::InvalidRasterState;
        FOUNDATION_ASSERT(state.cachedValidatedOwner->pipeline);
        if(record.setPushConstants.blobSizeBytes > state.cachedValidatedOwner->pipeline->pushConstantByteSize)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.pushConstantsSet = true;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:{
        if(!state.active || !state.heapBound || !state.pushConstantsSet)
            return GpuCommandIrReplayError::InvalidRasterState;
        return __hidden_gpu_command_ir_raster_lowering::ValidateDraw(opcode, record.draw, state);
    }
    case GpuCommandIrWireOpcode::EndRenderPass:
        state.active = false;
        state.heapBound = false;
        state.pushConstantsSet = false;
        state.cachedValidatedOwner = nullptr;
        state.cachedIndexBufferDesc = nullptr;
        state.cachedVertexValidations.clear();
        state.cachedInputLayoutError = GpuCommandIrReplayError::None;
        return GpuCommandIrReplayError::None;
    default:
        return GpuCommandIrReplayError::InvalidRasterState;
    }
}

b8 LowerRasterWireOperation(
    const GpuCommandIrWireOpcode::Enum opcode,
    const WireRecordStorage& record,
    const RasterOwnerTableView& owners,
    const BinaryByteView blobBytes,
    CommandList& commandList,
    GraphicsState& reusableState
){
    switch(opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrSetGraphicsStateRecord& wire = record.setGraphicsState;
        auto& owner = *MakeNotNull(owners.rasterStateOwner(wire.framebufferOwnerIndex));
        FOUNDATION_ASSERT(owner.pipeline && owner.framebuffer);
        reusableState.pipeline = owner.pipeline.get();
        reusableState.framebuffer = owner.framebuffer.get();
        reusableState.viewport.viewports.resize(1u);
        reusableState.viewport.viewports[0u] = Viewport{ wire.viewportMinX, wire.viewportMaxX, wire.viewportMinY, wire.viewportMaxY, wire.viewportMinZ, wire.viewportMaxZ };
        if(wire.hasScissor != 0u){
            reusableState.viewport.scissorRects.resize(1u);
            reusableState.viewport.scissorRects[0u] = Rect{ wire.scissor.minX, wire.scissor.maxX, wire.scissor.minY, wire.scissor.maxY };
        }
        else{
            reusableState.viewport.scissorRects.clear();
        }
        reusableState.blendConstantColor = Color(wire.blendConstantColor.r, wire.blendConstantColor.g, wire.blendConstantColor.b, wire.blendConstantColor.a);
        reusableState.dynamicStencilRefValue = wire.stencilRef;
        reusableState.vertexBuffers.resize(wire.vertexBindingCount);
        if(wire.vertexBindingCount != 0u){
            const u8* const vertexPtr = blobBytes.data() + static_cast<usize>(wire.blobOffsetBytes);
            for(u8 index = 0u; index < wire.vertexBindingCount; ++index){
                GpuCommandIrRasterVertexWire vertexWire;
                FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + index * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
                reusableState.vertexBuffers[index] = VertexBufferBinding{
                    .buffer = owner.vertexBuffers[index].get(),
                    .offset = vertexWire.offset,
                    .slot = vertexWire.slot,
                };
            }
        }
        if(wire.indexResourceIndex != Limit<u32>::s_Max){
            reusableState.indexBuffer = IndexBufferBinding{
                .buffer = owner.indexBuffer.get(),
                .offset = wire.indexOffset,
                .format = static_cast<Format::Enum>(wire.indexFormat),
            };
        }
        else{
            reusableState.indexBuffer = IndexBufferBinding{};
        }
        commandList.setGraphicsState(reusableState);
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        auto& owner = *MakeNotNull(owners.rasterHeapOwner(record.bindGraphicsHeap.heapOwnerIndex));
        FOUNDATION_ASSERT(owner.heap && owner.pipeline);
        owner.heap->bindGraphics(commandList, *owner.pipeline);
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::SetPushConstants:{
        const GpuCommandIrSetPushConstantsRecord& wire = record.setPushConstants;
        FOUNDATION_ASSERT(blobBytes.data() && wire.blobOffsetBytes <= static_cast<u64>(blobBytes.size()) && wire.blobSizeBytes <= static_cast<u64>(blobBytes.size()) - wire.blobOffsetBytes);
        commandList.setPushConstants(blobBytes.data() + static_cast<usize>(wire.blobOffsetBytes), static_cast<usize>(wire.blobSizeBytes));
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::Draw:{
        const GpuCommandIrDrawRecord& wire = record.draw;
        commandList.draw(DrawArguments{ wire.vertexCount, wire.instanceCount, wire.startIndexLocation, wire.startVertexLocation, wire.startInstanceLocation });
        return !commandList.commandRecordingFailed();
    }
    case GpuCommandIrWireOpcode::DrawIndexed:{
        const GpuCommandIrDrawRecord& wire = record.draw;
        commandList.drawIndexed(DrawArguments{ wire.vertexCount, wire.instanceCount, wire.startIndexLocation, wire.startVertexLocation, wire.startInstanceLocation });
        return !commandList.commandRecordingFailed();
    }
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

