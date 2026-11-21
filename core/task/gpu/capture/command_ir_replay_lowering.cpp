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

GpuCommandIrReplayError::Enum ValidateBuiltinBackendOperand(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuPhysicalQueueInfo& commandQueue, CommandList& commandList)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:{
        auto& source = *MakeNotNull(graph.bufferForResource(record.source));
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        if(&source == &destination)
            return GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &source, ResourceStates::CopySource | ResourceStates::CopyDest) && ResourceQueueAdmissionAdmitsQueue(source.getQueueAdmissionSnapshot(), commandQueue)
                ? (commandList.getPermanentBufferState(&source) != ResourceStates::Unknown && commandList.getPermanentBufferState(&source) != (ResourceStates::CopySource | ResourceStates::CopyDest) ? GpuCommandIrReplayError::PermanentResourceStateMismatch : GpuCommandIrReplayError::None)
                : GpuCommandIrReplayError::BackendResourceNotReady
            ;

        const auto validateBuffer = [&](Buffer& buffer, const ResourceStates::Mask requiredState)noexcept -> GpuCommandIrReplayError::Enum{
            if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &buffer, requiredState) || !ResourceQueueAdmissionAdmitsQueue(buffer.getQueueAdmissionSnapshot(), commandQueue))
                return GpuCommandIrReplayError::BackendResourceNotReady;
            const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(&buffer);
            if(permanentState != ResourceStates::Unknown && permanentState != requiredState)
                return GpuCommandIrReplayError::PermanentResourceStateMismatch;
            return GpuCommandIrReplayError::None;
        };
        const GpuCommandIrReplayError::Enum sourceError = validateBuffer(source, ResourceStates::CopySource);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return validateBuffer(destination, ResourceStates::CopyDest);
    }
    case GpuCommandIrOpcode::CopyTexture:{
        auto& source = *MakeNotNull(graph.textureForResource(record.source));
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        const auto validateTexture = [&](Texture& texture, const ResourceStates::Mask requiredState)noexcept -> GpuCommandIrReplayError::Enum{
            if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &texture, requiredState) || !ResourceQueueAdmissionAdmitsQueue(texture.getQueueAdmissionSnapshot(), commandQueue))
                return GpuCommandIrReplayError::BackendResourceNotReady;
            const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(&texture);
            if(permanentState != ResourceStates::Unknown && permanentState != requiredState)
                return GpuCommandIrReplayError::PermanentResourceStateMismatch;
            return GpuCommandIrReplayError::None;
        };
        const GpuCommandIrReplayError::Enum sourceError = validateTexture(source, ResourceStates::CopySource);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return validateTexture(destination, ResourceStates::CopyDest);
    }
    case GpuCommandIrOpcode::ClearBuffer:{
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &destination, ResourceStates::CopyDest) || !ResourceQueueAdmissionAdmitsQueue(destination.getQueueAdmissionSnapshot(), commandQueue))
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(&destination);
        if(permanentState != ResourceStates::Unknown && permanentState != ResourceStates::CopyDest)
            return GpuCommandIrReplayError::PermanentResourceStateMismatch;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrOpcode::ClearTexture:
    case GpuCommandIrOpcode::ClearTextureRectUInt:{
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &destination, ResourceStates::CopyDest) || !ResourceQueueAdmissionAdmitsQueue(destination.getQueueAdmissionSnapshot(), commandQueue))
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(&destination);
        if(permanentState != ResourceStates::Unknown && permanentState != ResourceStates::CopyDest)
            return GpuCommandIrReplayError::PermanentResourceStateMismatch;
        return GpuCommandIrReplayError::None;
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
        const GpuCommandIrDetail::WireStepStatus::Enum step = cursor.nextReplayFast(recordPtr, wireOpcode, decodedContext, builtinRecord);
        if(step == GpuCommandIrDetail::WireStepStatus::End)
            return GpuCommandIrReplayResult{ .streamValidation = cursor.validation(), .recordIndex = recordIndex, };
        if(step == GpuCommandIrDetail::WireStepStatus::Error)
            return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, cursor.validation(), cursor.validation().recordIndex);

        if(decodedContext.packet != packet){
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
        if(!commandList.matchesRecordingLease(recordingLeaseSerial) || commandList.commandRecordingFailed()){
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

