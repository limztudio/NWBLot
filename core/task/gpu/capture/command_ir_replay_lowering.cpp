// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/backend_selection/resource_validation.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_replay_lowering{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u8 s_BufferResourceKind = 1u;
inline constexpr u8 s_TextureResourceKind = 2u;

template<u8 ResourceKind, typename ResourceT>
[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateCachedBuiltinResource(
    const GpuGraphResourceId resourceID,
    ResourceT& resource,
    const ResourceStates::Mask requiredState,
    const GpuPhysicalQueueInfo& commandQueue,
    CommandList& commandList,
    GpuCommandIrDetail::BuiltinBackendResourceCache& resourceCache
)noexcept{
    const auto isResourceReady = [&]()noexcept{
        if constexpr(ResourceKind == s_BufferResourceKind)
            return GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &resource, requiredState);
        else
            return GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &resource, requiredState);
    };

    const usize slotIndex = resourceID.index & (GpuCommandIrDetail::BuiltinBackendResourceCache::s_SlotCount - 1u);
    const u16 slotBit = static_cast<u16>(1u << slotIndex);
    auto& slot = resourceCache.slots[slotIndex];
    const b8 matchesSlot =
        (resourceCache.populatedMask & slotBit) != 0u
        && slot.resourceIndex == resourceID.index
        && slot.resourceGeneration == resourceID.generation
        && slot.resourceKind == ResourceKind
    ;
    if(matchesSlot){
        if(slot.hasValidatedState && slot.validatedState == requiredState)
            return GpuCommandIrReplayError::None;
        if(slot.permanentStateKnown && slot.permanentState != ResourceStates::Unknown && slot.permanentState != requiredState){
            if(!isResourceReady() || !ResourceQueueAdmissionAdmitsQueue(resource.getQueueAdmissionSnapshot(), commandQueue))
                return GpuCommandIrReplayError::BackendResourceNotReady;
            return GpuCommandIrReplayError::PermanentResourceStateMismatch;
        }
    }
    if(!isResourceReady() || !ResourceQueueAdmissionAdmitsQueue(resource.getQueueAdmissionSnapshot(), commandQueue))
        return GpuCommandIrReplayError::BackendResourceNotReady;

    ResourceStates::Mask permanentState = ResourceStates::Unknown;
    if(matchesSlot && slot.permanentStateKnown)
        permanentState = slot.permanentState;
    else{
        if constexpr(ResourceKind == s_BufferResourceKind)
            permanentState = commandList.getPermanentBufferState(&resource);
        else
            permanentState = commandList.getPermanentTextureState(&resource);
        slot.resourceGeneration = resourceID.generation;
        slot.resourceIndex = resourceID.index;
        slot.resourceKind = ResourceKind;
        slot.permanentState = permanentState;
        slot.permanentStateKnown = true;
        slot.hasValidatedState = false;
        resourceCache.populatedMask |= slotBit;
    }
    if(permanentState != ResourceStates::Unknown && permanentState != requiredState)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    slot.validatedState = requiredState;
    slot.hasValidatedState = true;
    return GpuCommandIrReplayError::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateReplayCommandListQueue(const CommandListParameters& commandListDescription, const GpuPhysicalQueueId& packetQueue, const CommandQueue::Enum packetQueueClass)noexcept{
    if(commandListDescription.physicalQueue != packetQueue)
        return GpuCommandIrReplayError::CommandListQueueMismatch;
    if(commandListDescription.queueType != packetQueueClass)
        return GpuCommandIrReplayError::CommandListQueueMismatch;
    return GpuCommandIrReplayError::None;
}

GpuCommandIrReplayError::Enum ValidateBuiltinBackendOperand(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuPhysicalQueueInfo& commandQueue, CommandList& commandList, BuiltinBackendResourceCache& resourceCache)noexcept{
    using namespace __hidden_gpu_command_ir_replay_lowering;

    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:{
        auto& source = *MakeNotNull(graph.bufferForResource(record.source));
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        if(&source == &destination)
            return ValidateCachedBuiltinResource<s_BufferResourceKind>(record.source, source, ResourceStates::CopySource | ResourceStates::CopyDest, commandQueue, commandList, resourceCache);

        const GpuCommandIrReplayError::Enum sourceError = ValidateCachedBuiltinResource<s_BufferResourceKind>(record.source, source, ResourceStates::CopySource, commandQueue, commandList, resourceCache);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateCachedBuiltinResource<s_BufferResourceKind>(record.destination, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrOpcode::CopyTexture:{
        auto& source = *MakeNotNull(graph.textureForResource(record.source));
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        const GpuCommandIrReplayError::Enum sourceError = ValidateCachedBuiltinResource<s_TextureResourceKind>(record.source, source, ResourceStates::CopySource, commandQueue, commandList, resourceCache);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateCachedBuiltinResource<s_TextureResourceKind>(record.destination, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrOpcode::ClearBuffer:{
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        return ValidateCachedBuiltinResource<s_BufferResourceKind>(record.destination, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrOpcode::ClearTexture:
    case GpuCommandIrOpcode::ClearTextureRectUInt:{
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        return ValidateCachedBuiltinResource<s_TextureResourceKind>(record.destination, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrOpcode::UploadBuffer:
    case GpuCommandIrOpcode::UploadTexture:
        return ValidateUploadBackendOperand(record, graph, commandQueue, commandList);
    default:
        return GpuCommandIrReplayError::StreamChangedDuringReplay;
    }
}

GpuCommandIrReplayError::Enum ValidateBuiltinWireBackendOperand(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordPtr,
    const u64 graphGeneration,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& commandQueue,
    CommandList& commandList,
    BuiltinBackendResourceCache& resourceCache
)noexcept{
    using namespace __hidden_gpu_command_ir_replay_lowering;

    switch(opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        const auto wire = ReadWirePod<GpuCommandIrCopyBufferRecord>(recordPtr);
        const GpuGraphResourceId sourceId{ .generation = graphGeneration, .index = wire.sourceResourceIndex };
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        auto& source = *MakeNotNull(graph.bufferForResource(sourceId));
        auto& destination = *MakeNotNull(graph.bufferForResource(destinationId));
        if(&source == &destination)
            return ValidateCachedBuiltinResource<s_BufferResourceKind>(sourceId, source, ResourceStates::CopySource | ResourceStates::CopyDest, commandQueue, commandList, resourceCache);

        const GpuCommandIrReplayError::Enum sourceError = ValidateCachedBuiltinResource<s_BufferResourceKind>(sourceId, source, ResourceStates::CopySource, commandQueue, commandList, resourceCache);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateCachedBuiltinResource<s_BufferResourceKind>(destinationId, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrWireOpcode::CopyTexture:{
        const auto wire = ReadWirePod<GpuCommandIrCopyTextureRecord>(recordPtr);
        const GpuGraphResourceId sourceId{ .generation = graphGeneration, .index = wire.sourceResourceIndex };
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        auto& source = *MakeNotNull(graph.textureForResource(sourceId));
        auto& destination = *MakeNotNull(graph.textureForResource(destinationId));
        const GpuCommandIrReplayError::Enum sourceError = ValidateCachedBuiltinResource<s_TextureResourceKind>(sourceId, source, ResourceStates::CopySource, commandQueue, commandList, resourceCache);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateCachedBuiltinResource<s_TextureResourceKind>(destinationId, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        const auto wire = ReadWirePod<GpuCommandIrClearBufferRecord>(recordPtr);
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        auto& destination = *MakeNotNull(graph.bufferForResource(destinationId));
        return ValidateCachedBuiltinResource<s_BufferResourceKind>(destinationId, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        const auto wire = ReadWirePod<GpuCommandIrClearTextureRecord>(recordPtr);
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        auto& destination = *MakeNotNull(graph.textureForResource(destinationId));
        return ValidateCachedBuiltinResource<s_TextureResourceKind>(destinationId, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:{
        const auto wire = ReadWirePod<GpuCommandIrClearTextureRectUIntRecord>(recordPtr);
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        auto& destination = *MakeNotNull(graph.textureForResource(destinationId));
        return ValidateCachedBuiltinResource<s_TextureResourceKind>(destinationId, destination, ResourceStates::CopyDest, commandQueue, commandList, resourceCache);
    }
    case GpuCommandIrWireOpcode::UploadBuffer:
    case GpuCommandIrWireOpcode::UploadTexture:
        return ValidateUploadWireBackendOperand(opcode, recordPtr, graphGeneration, graph, commandQueue, commandList);
    default:
        return GpuCommandIrReplayError::StreamChangedDuringReplay;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_replay_lowering{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 LowerBuiltinWireOperation(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordPtr,
    const u64 graphGeneration,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList
){
    switch(opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrCopyBufferRecord>(recordPtr);
        const GpuGraphResourceId sourceId{ .generation = graphGeneration, .index = wire.sourceResourceIndex };
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        FOUNDATION_ASSERT(graph.validResource(destinationId) && graph.validResource(sourceId));
        commandList.copyBuffer(*graph.bufferForResource(destinationId), wire.destinationOffsetBytes, *graph.bufferForResource(sourceId), wire.sourceOffsetBytes, wire.dataSizeBytes);
        return true;
    }
    case GpuCommandIrWireOpcode::CopyTexture:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrCopyTextureRecord>(recordPtr);
        const GpuGraphResourceId sourceId{ .generation = graphGeneration, .index = wire.sourceResourceIndex };
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        FOUNDATION_ASSERT(graph.validResource(destinationId) && graph.validResource(sourceId));
        const TextureSlice sourceSlice{ .x = wire.sourceSlice.x, .y = wire.sourceSlice.y, .z = wire.sourceSlice.z, .width = wire.sourceSlice.width, .height = wire.sourceSlice.height, .depth = wire.sourceSlice.depth, .mipLevel = wire.sourceSlice.mipLevel, .arraySlice = wire.sourceSlice.arraySlice };
        const TextureSlice destinationSlice{ .x = wire.destinationSlice.x, .y = wire.destinationSlice.y, .z = wire.destinationSlice.z, .width = wire.destinationSlice.width, .height = wire.destinationSlice.height, .depth = wire.destinationSlice.depth, .mipLevel = wire.destinationSlice.mipLevel, .arraySlice = wire.destinationSlice.arraySlice };
        commandList.copyTexture(*graph.textureForResource(destinationId), destinationSlice, *graph.textureForResource(sourceId), sourceSlice);
        return true;
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrClearBufferRecord>(recordPtr);
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        FOUNDATION_ASSERT(graph.validResource(destinationId));
        commandList.clearBufferUInt(*graph.bufferForResource(destinationId), wire.clearValue);
        return true;
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrClearTextureRecord>(recordPtr);
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        FOUNDATION_ASSERT(graph.validResource(destinationId));
        const TextureSubresourceSet subresources(wire.destinationSubresources.baseMipLevel, wire.destinationSubresources.numMipLevels, wire.destinationSubresources.baseArraySlice, wire.destinationSubresources.numArraySlices);
        switch(static_cast<GpuClearTextureTaskValueType::Enum>(wire.clearTextureValueType)){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(*graph.textureForResource(destinationId), subresources, Color(wire.floatClearValue.r, wire.floatClearValue.g, wire.floatClearValue.b, wire.floatClearValue.a));
            return true;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(*graph.textureForResource(destinationId), subresources, UIntColor(wire.uintClearValue.r, wire.uintClearValue.g, wire.uintClearValue.b, wire.uintClearValue.a));
            return true;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(*graph.textureForResource(destinationId), subresources, IntColor(wire.intClearValue.r, wire.intClearValue.g, wire.intClearValue.b, wire.intClearValue.a));
            return true;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(
                *graph.textureForResource(destinationId),
                subresources,
                (static_cast<u8>(wire.clearFlags) & GpuCommandIrClearTextureFlag::ClearDepth) != 0u,
                wire.depthClearValue,
                (static_cast<u8>(wire.clearFlags) & GpuCommandIrClearTextureFlag::ClearStencil) != 0u,
                wire.stencilClearValue
            );
            return true;
        default:
            FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Validated command IR clear type lost its lowerer"));
            return true;
        }
    }
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrClearTextureRectUIntRecord>(recordPtr);
        const GpuGraphResourceId destinationId{ .generation = graphGeneration, .index = wire.destinationResourceIndex };
        FOUNDATION_ASSERT(graph.validResource(destinationId));
        const TextureSubresourceSet subresources(wire.destinationSubresources.baseMipLevel, wire.destinationSubresources.numMipLevels, wire.destinationSubresources.baseArraySlice, wire.destinationSubresources.numArraySlices);
        const Rect clearRect(wire.clearRect.minX, wire.clearRect.maxX, wire.clearRect.minY, wire.clearRect.maxY);
        commandList.clearTextureRectUInt(*graph.textureForResource(destinationId), subresources, clearRect, UIntColor(wire.uintClearValue.r, wire.uintClearValue.g, wire.uintClearValue.b, wire.uintClearValue.a));
        return true;
    }
    case GpuCommandIrWireOpcode::UploadBuffer:
    case GpuCommandIrWireOpcode::UploadTexture:
        return GpuCommandIrDetail::LowerUploadWireOperation(opcode, recordPtr, graphGeneration, blobBytes, graph, commandList);
    default:
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Validated command IR opcode lost its lowerer"));
        return true;
    }
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuCommandIrReplayResult ReplayPacketImpl(
    const BinaryByteView primaryBytes,
    const BinaryByteView secondaryBlobBytes,
    const b8 splitBuffers,
    const GpuCommandIrDetail::RasterOwnerTableView& owners,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList
){
    const GpuCommandIrReplayResult result = GpuCommandIrDetail::PreflightGpuCommandIrPacketStream(
        primaryBytes,
        secondaryBlobBytes,
        splitBuffers,
        owners,
        graph,
        compiledGraph,
        packet,
        &commandList
    );
    if(!result.valid())
        return result;

    const u64 recordingLeaseSerial = commandList.recordingLeaseSerial();
    GpuCommandIrDetail::WireStreamCursor cursor(primaryBytes, secondaryBlobBytes, splitBuffers);
    if(cursor.validation().failed() || cursor.recordCount() != result.streamValidation.recordIndex)
        return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, cursor.validation(), cursor.validation().recordIndex);

    GraphicsState reusableGraphicsState;
    u64 recordIndex = 0u;
    const u8* recordPtr = nullptr;
    GpuCommandIrWireOpcode::Enum wireOpcode = GpuCommandIrWireOpcode::kCount;
    GpuCommandIrDetail::DecodedRecordContext decodedContext;
    for(;;){
        b8 matchesPacket = false;
        const GpuCommandIrDetail::WireStepStatus::Enum step = cursor.nextReplayFastForPacket(packet, recordPtr, wireOpcode, decodedContext, matchesPacket);
        if(step == GpuCommandIrDetail::WireStepStatus::End)
            return GpuCommandIrReplayResult{ .streamValidation = cursor.validation(), .recordIndex = recordIndex, };
        if(step == GpuCommandIrDetail::WireStepStatus::Error)
            return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, cursor.validation(), cursor.validation().recordIndex);

        if(!matchesPacket){
            ++recordIndex;
            continue;
        }

        const b8 raster = GpuCommandIrDetail::IsRasterOpcode(wireOpcode);
        if(raster){
            FOUNDATION_ASSERT(owners.hasOwnerSidecar);
            switch(wireOpcode){
            case GpuCommandIrWireOpcode::SetGraphicsState:{
                const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrSetGraphicsStateRecord>(recordPtr);
                const GpuCommandIrRasterStateOwner* const owner = owners.rasterStateOwner(wire.framebufferOwnerIndex);
                if(
                    !owner
                    || !owner->pipeline
                    || !owner->framebuffer
                    || owner->vertexBuffers.size() != wire.vertexBindingCount
                    || (wire.indexResourceIndex != Limit<u32>::s_Max && !owner->indexBuffer)
                )
                    return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, result.streamValidation, recordIndex);
                GpuCommandIrDetail::LowerSetGraphicsStateWireRecord(recordPtr, *owner, cursor.blobBytes(), commandList, reusableGraphicsState);
                break;
            }
            case GpuCommandIrWireOpcode::BindGraphicsHeap:{
                const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrBindGraphicsHeapRecord>(recordPtr);
                const GpuCommandIrRasterHeapOwner* const owner = owners.rasterHeapOwner(wire.heapOwnerIndex);
                if(!owner || !owner->heap || !owner->pipeline)
                    return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, result.streamValidation, recordIndex);
                owner->heap->bindGraphics(commandList, *owner->pipeline);
                break;
            }
            case GpuCommandIrWireOpcode::SetPushConstants:{
                const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrSetPushConstantsRecord>(recordPtr);
                const BinaryByteView blobBytes = cursor.blobBytes();
                if(!blobBytes.data() || wire.blobOffsetBytes > static_cast<u64>(blobBytes.size()) || wire.blobSizeBytes > static_cast<u64>(blobBytes.size()) - wire.blobOffsetBytes)
                    return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, result.streamValidation, recordIndex);
                commandList.setPushConstants(blobBytes.data() + static_cast<usize>(wire.blobOffsetBytes), static_cast<usize>(wire.blobSizeBytes));
                break;
            }
            case GpuCommandIrWireOpcode::Draw:{
                const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrDrawRecord>(recordPtr);
                commandList.draw(DrawArguments{ wire.vertexCount, wire.instanceCount, wire.startIndexLocation, wire.startVertexLocation, wire.startInstanceLocation });
                break;
            }
            case GpuCommandIrWireOpcode::DrawIndexed:{
                const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrDrawRecord>(recordPtr);
                commandList.drawIndexed(DrawArguments{ wire.vertexCount, wire.instanceCount, wire.startIndexLocation, wire.startVertexLocation, wire.startInstanceLocation });
                break;
            }
            case GpuCommandIrWireOpcode::EndRenderPass:
                commandList.endRenderPass();
                break;
            default:
                return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, result.streamValidation, recordIndex);
            }
        }
        else{
            FOUNDATION_ASSERT(recordPtr != nullptr);
            if(!__hidden_gpu_command_ir_replay_lowering::LowerBuiltinWireOperation(wireOpcode, recordPtr, decodedContext.task.generation, cursor.blobBytes(), graph, commandList))
                return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
        }
        if(!commandList.isRecordingLeaseHealthy(recordingLeaseSerial)){
            return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
        }
        ++recordIndex;
    }
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacket(const GpuCommandIrOwnedStream& stream, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet, CommandList& commandList){
    return ReplayPacketImpl(stream.bytes(), {}, false, GpuCommandIrDetail::RasterOwnerTableView::FromOwnedStream(stream), graph, compiledGraph, packet, commandList);
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacket(const GpuCommandIrCapture& capture, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet, CommandList& commandList){
    return ReplayPacketImpl(capture.rawCommandBytes(), capture.rawBlobBytes(), true, GpuCommandIrDetail::RasterOwnerTableView::FromCapture(capture), graph, compiledGraph, packet, commandList);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

