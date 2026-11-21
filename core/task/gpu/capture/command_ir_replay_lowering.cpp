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
    const auto validateBuffer = [&](const GpuGraphResourceId resourceID, Buffer& buffer, const ResourceStates::Mask requiredState)noexcept -> GpuCommandIrReplayError::Enum{
        const usize slotIndex = resourceID.index & (BuiltinBackendResourceCache::s_SlotCount - 1u);
        const u16 slotBit = static_cast<u16>(1u << slotIndex);
        auto& slot = resourceCache.slots[slotIndex];
        const b8 matchesSlot = (resourceCache.populatedMask & slotBit) != 0u && slot.resourceIndex == resourceID.index && slot.resourceGeneration == resourceID.generation && slot.resourceKind == 1u;
        if(matchesSlot){
            if(slot.hasValidatedState && slot.validatedState == requiredState)
                return GpuCommandIrReplayError::None;
            if(slot.permanentStateKnown && slot.permanentState != ResourceStates::Unknown && slot.permanentState != requiredState){
                if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &buffer, requiredState) || !ResourceQueueAdmissionAdmitsQueue(buffer.getQueueAdmissionSnapshot(), commandQueue))
                    return GpuCommandIrReplayError::BackendResourceNotReady;
                return GpuCommandIrReplayError::PermanentResourceStateMismatch;
            }
        }
        if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &buffer, requiredState) || !ResourceQueueAdmissionAdmitsQueue(buffer.getQueueAdmissionSnapshot(), commandQueue))
            return GpuCommandIrReplayError::BackendResourceNotReady;
        ResourceStates::Mask permanentState = ResourceStates::Unknown;
        if(matchesSlot && slot.permanentStateKnown)
            permanentState = slot.permanentState;
        else{
            permanentState = commandList.getPermanentBufferState(&buffer);
            slot.resourceGeneration = resourceID.generation;
            slot.resourceIndex = resourceID.index;
            slot.resourceKind = 1u;
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
    };
    const auto validateTexture = [&](const GpuGraphResourceId resourceID, Texture& texture, const ResourceStates::Mask requiredState)noexcept -> GpuCommandIrReplayError::Enum{
        const usize slotIndex = resourceID.index & (BuiltinBackendResourceCache::s_SlotCount - 1u);
        const u16 slotBit = static_cast<u16>(1u << slotIndex);
        auto& slot = resourceCache.slots[slotIndex];
        const b8 matchesSlot = (resourceCache.populatedMask & slotBit) != 0u && slot.resourceIndex == resourceID.index && slot.resourceGeneration == resourceID.generation && slot.resourceKind == 2u;
        if(matchesSlot){
            if(slot.hasValidatedState && slot.validatedState == requiredState)
                return GpuCommandIrReplayError::None;
            if(slot.permanentStateKnown && slot.permanentState != ResourceStates::Unknown && slot.permanentState != requiredState){
                if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &texture, requiredState) || !ResourceQueueAdmissionAdmitsQueue(texture.getQueueAdmissionSnapshot(), commandQueue))
                    return GpuCommandIrReplayError::BackendResourceNotReady;
                return GpuCommandIrReplayError::PermanentResourceStateMismatch;
            }
        }
        if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &texture, requiredState) || !ResourceQueueAdmissionAdmitsQueue(texture.getQueueAdmissionSnapshot(), commandQueue))
            return GpuCommandIrReplayError::BackendResourceNotReady;
        ResourceStates::Mask permanentState = ResourceStates::Unknown;
        if(matchesSlot && slot.permanentStateKnown)
            permanentState = slot.permanentState;
        else{
            permanentState = commandList.getPermanentTextureState(&texture);
            slot.resourceGeneration = resourceID.generation;
            slot.resourceIndex = resourceID.index;
            slot.resourceKind = 2u;
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
    };

    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:{
        auto& source = *MakeNotNull(graph.bufferForResource(record.source));
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        if(&source == &destination)
            return validateBuffer(record.source, source, ResourceStates::CopySource | ResourceStates::CopyDest);

        const GpuCommandIrReplayError::Enum sourceError = validateBuffer(record.source, source, ResourceStates::CopySource);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return validateBuffer(record.destination, destination, ResourceStates::CopyDest);
    }
    case GpuCommandIrOpcode::CopyTexture:{
        auto& source = *MakeNotNull(graph.textureForResource(record.source));
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        const GpuCommandIrReplayError::Enum sourceError = validateTexture(record.source, source, ResourceStates::CopySource);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return validateTexture(record.destination, destination, ResourceStates::CopyDest);
    }
    case GpuCommandIrOpcode::ClearBuffer:{
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        return validateBuffer(record.destination, destination, ResourceStates::CopyDest);
    }
    case GpuCommandIrOpcode::ClearTexture:
    case GpuCommandIrOpcode::ClearTextureRectUInt:{
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        return validateTexture(record.destination, destination, ResourceStates::CopyDest);
    }
    case GpuCommandIrOpcode::UploadBuffer:
    case GpuCommandIrOpcode::UploadTexture:
        return ValidateUploadBackendOperand(record, graph, commandQueue, commandList);
    default:
        return GpuCommandIrReplayError::StreamChangedDuringReplay;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_replay_lowering{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void LowerOperation(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList){
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        commandList.copyBuffer(*graph.bufferForResource(record.destination), record.destinationOffsetBytes, *graph.bufferForResource(record.source), record.sourceOffsetBytes, record.dataSizeBytes);
        return;
    case GpuCommandIrOpcode::CopyTexture:
        commandList.copyTexture(*graph.textureForResource(record.destination), record.destinationSlice, *graph.textureForResource(record.source), record.sourceSlice);
        return;
    case GpuCommandIrOpcode::ClearBuffer:
        commandList.clearBufferUInt(*graph.bufferForResource(record.destination), record.uintClearValue.r);
        return;
    case GpuCommandIrOpcode::ClearTexture:
        switch(record.clearTextureValueType){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(*graph.textureForResource(record.destination), record.destinationSubresources, record.floatClearValue);
            return;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(*graph.textureForResource(record.destination), record.destinationSubresources, record.uintClearValue);
            return;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(*graph.textureForResource(record.destination), record.destinationSubresources, record.intClearValue);
            return;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(*graph.textureForResource(record.destination), record.destinationSubresources, record.clearDepth, record.depthClearValue, record.clearStencil, record.stencilClearValue);
            return;
        default:
            FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Validated command IR clear type lost its lowerer"));
            return;
        }
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        commandList.clearTextureRectUInt(*graph.textureForResource(record.destination), record.destinationSubresources, record.clearRect, record.uintClearValue);
        return;
    default:
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Validated command IR opcode lost its lowerer"));
        return;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    Optional<GpuCommandIrBuiltinTaskRecord> builtinRecord;
    GpuCommandIrWireOpcode::Enum wireOpcode = GpuCommandIrWireOpcode::Count;
    GpuCommandIrDetail::DecodedRecordContext decodedContext;
    for(;;){
        b8 matchesPacket = false;
        const GpuCommandIrDetail::WireStepStatus::Enum step = cursor.nextReplayFastForPacket(packet, recordPtr, wireOpcode, decodedContext, builtinRecord, matchesPacket);
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
            FOUNDATION_ASSERT(builtinRecord.has_value());
            const GpuCommandIrBuiltinTaskRecord& builtin = *builtinRecord;
            FOUNDATION_ASSERT(graph.validResource(builtin.destination) && ((builtin.opcode != GpuCommandIrOpcode::CopyBuffer && builtin.opcode != GpuCommandIrOpcode::CopyTexture) || graph.validResource(builtin.source)));
            if(builtin.opcode == GpuCommandIrOpcode::UploadBuffer || builtin.opcode == GpuCommandIrOpcode::UploadTexture){
                if(!GpuCommandIrDetail::LowerUploadOperation(builtin, cursor.blobBytes(), graph, commandList))
                    return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
            }
            else
                __hidden_gpu_command_ir_replay_lowering::LowerOperation(builtin, graph, commandList);
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

