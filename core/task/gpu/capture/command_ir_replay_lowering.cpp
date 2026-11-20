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
    Optional<GpuCommandIrStreamReader> readerStorage;
    if(splitBuffers)
        readerStorage.emplace(primaryBytes, secondaryBlobBytes);
    else
        readerStorage.emplace(primaryBytes);
    GpuCommandIrStreamReader& reader = *readerStorage;

    u64 recordIndex = 0u;
    for(;;){
        const auto decodedResult = reader.next();
        if(!decodedResult && decodedResult.error() == GpuCommandIrStreamReadStatus::End)
            return GpuCommandIrReplayResult{ .streamValidation = reader.validation(), .recordIndex = recordIndex, };
        if(!decodedResult){
            return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, reader.validation(), reader.validation().recordIndex);
        }
        const auto& decoded = *decodedResult;

        const b8 raster = GpuCommandIrDetail::IsRasterOpcode(decoded.opcode);
        const GpuSubmissionPacketId recordPacket = raster ? decoded.raster.packet : decoded.builtin.packet;
        if(recordPacket != packet){
            ++recordIndex;
            continue;
        }

        FOUNDATION_ASSERT(raster || (graph.validResource(decoded.builtin.destination) && ((decoded.builtin.opcode != GpuCommandIrOpcode::CopyBuffer && decoded.builtin.opcode != GpuCommandIrOpcode::CopyTexture) || graph.validResource(decoded.builtin.source))));

        if(raster){
            FOUNDATION_ASSERT(owners.hasOwnerSidecar);
            if(!GpuCommandIrDetail::LowerRasterOperation(decoded.raster, owners, reader.blobBytes(), commandList))
                return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
        }
        else if(decoded.builtin.opcode == GpuCommandIrOpcode::UploadBuffer || decoded.builtin.opcode == GpuCommandIrOpcode::UploadTexture){
            if(!GpuCommandIrDetail::LowerUploadOperation(decoded.builtin, reader.blobBytes(), graph, commandList)){
                return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
            }
        }
        else
            __hidden_gpu_command_ir_replay_lowering::LowerOperation(decoded.builtin, graph, commandList);
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

