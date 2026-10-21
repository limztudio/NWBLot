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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_replay_lowering{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuCommandIrReplayResult ReplayFailure(const GpuCommandIrReplayError::Enum error, const GpuCommandIrStreamValidationResult& streamValidation = {}, const u64 recordIndex = Limit<u64>::s_Max)noexcept{
    return GpuCommandIrReplayResult{ .error = error, .streamValidation = streamValidation, .recordIndex = recordIndex, };
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferBackendOperand(Buffer& buffer, const ResourceStates::Mask requiredState, CommandList& commandList, const GpuPhysicalQueueInfo& commandQueue)noexcept{
    if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &buffer, requiredState) || !ResourceQueueAdmissionAdmitsQueue(buffer.getQueueAdmissionSnapshot(), commandQueue))
        return GpuCommandIrReplayError::BackendResourceNotReady;

    const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(&buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != requiredState)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureBackendOperand(Texture& texture, const ResourceStates::Mask requiredState, CommandList& commandList, const GpuPhysicalQueueInfo& commandQueue)noexcept{
    if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &texture, requiredState) || !ResourceQueueAdmissionAdmitsQueue(texture.getQueueAdmissionSnapshot(), commandQueue))
        return GpuCommandIrReplayError::BackendResourceNotReady;

    const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(&texture);
    if(permanentState != ResourceStates::Unknown && permanentState != requiredState)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

// Semantic preflight admitted every backend resource here, and the graph stays caller-stable.
[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBackendOperands(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList, const GpuPhysicalQueueInfo& commandQueue)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:{
        auto& source = *MakeNotNull(graph.bufferForResource(record.source));
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        if(&source == &destination)
            return ValidateBufferBackendOperand(source, ResourceStates::CopySource | ResourceStates::CopyDest, commandList, commandQueue);

        const GpuCommandIrReplayError::Enum sourceError = ValidateBufferBackendOperand(source, ResourceStates::CopySource, commandList, commandQueue);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateBufferBackendOperand(destination, ResourceStates::CopyDest, commandList, commandQueue);
    }
    case GpuCommandIrOpcode::CopyTexture:{
        auto& source = *MakeNotNull(graph.textureForResource(record.source));
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        const GpuCommandIrReplayError::Enum sourceError = ValidateTextureBackendOperand(source, ResourceStates::CopySource, commandList, commandQueue);
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateTextureBackendOperand(destination, ResourceStates::CopyDest, commandList, commandQueue);
    }
    case GpuCommandIrOpcode::ClearBuffer:{
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        return ValidateBufferBackendOperand(destination, ResourceStates::CopyDest, commandList, commandQueue);
    }
    case GpuCommandIrOpcode::ClearTexture:
    case GpuCommandIrOpcode::ClearTextureRectUInt:{
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        return ValidateTextureBackendOperand(destination, ResourceStates::CopyDest, commandList, commandQueue);
    }
    case GpuCommandIrOpcode::UploadBuffer:
    case GpuCommandIrOpcode::UploadTexture:
        return GpuCommandIrDetail::ValidateUploadBackendOperand(record, graph, commandList, commandQueue);
    default:
        return GpuCommandIrReplayError::StreamChangedDuringReplay;
    }
}

// Preflight cannot check native ownership, so scan the packet before taking the recording lease.
[[nodiscard]] static GpuCommandIrReplayResult ValidateBackendOperandPacket(
    const BinaryByteView bytes,
    const GpuCommandIrOwnedStream* const ownedStream,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList,
    const GpuCommandIrStreamValidationResult& expectedStreamValidation
)noexcept{
    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    const GpuPhysicalQueueInfo* const commandQueue = commandList.getDevice().getPhysicalQueueInfo(commandListDescription.physicalQueue);
    if(!commandQueue)
        return ReplayFailure(GpuCommandIrReplayError::BackendResourceNotReady, expectedStreamValidation);

    // A reader failure surfaces through next() below.
    GpuCommandIrStreamReader reader(bytes);
    u64 recordIndex = 0u;
    GpuCommandIrDetail::RasterReplayState rasterState;
    for(;;){
        const auto decodedResult = reader.next();
        if(!decodedResult && decodedResult.error() == GpuCommandIrStreamReadStatus::End){
            return GpuCommandIrReplayResult{ .streamValidation = reader.validation(), .recordIndex = recordIndex, };
        }
        if(!decodedResult){
            return ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, reader.validation(), reader.validation().recordIndex);
        }
        const auto& decoded = *decodedResult;

        const bool raster = GpuCommandIrDetail::IsRasterOpcode(decoded.opcode);
        const GpuSubmissionPacketId recordPacket = raster ? decoded.raster.packet : decoded.builtin.packet;
        if(recordPacket == packet){
            // Preflight rejected every raster record without the owned sidecar.
            NWB_ASSERT(!raster || ownedStream);
            const GpuCommandIrReplayError::Enum operandError = raster ? GpuCommandIrDetail::ValidateRasterBackendOperand(decoded.raster, *ownedStream, graph, *commandQueue, commandList, recordIndex, rasterState) : ValidateBackendOperands(decoded.builtin, graph, commandList, *commandQueue);
            if(operandError != GpuCommandIrReplayError::None)
                return ReplayFailure(operandError, expectedStreamValidation, recordIndex);
        }
        ++recordIndex;
    }
}

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
            NWB_ASSERT_MSG(false, NWB_TEXT("Validated command IR clear type lost its lowerer"));
            return;
        }
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        commandList.clearTextureRectUInt(*graph.textureForResource(record.destination), record.destinationSubresources, record.clearRect, record.uintClearValue);
        return;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Validated command IR opcode lost its lowerer"));
        return;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuCommandIrReplayResult ReplayPacketImpl(
    const BinaryByteView bytes,
    const GpuCommandIrOwnedStream* const ownedStream,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList
){
    GpuCommandIrReplayResult result = ownedStream ? PreflightGpuCommandIrPacket(*ownedStream, graph, compiledGraph, packet) : PreflightGpuCommandIrPacket(bytes, graph, compiledGraph, packet);
    if(!result.valid())
        return result;

    // Preflight admitted this packet and its queue on the same pinned plan.
    const GpuCompiledPacketView packetView = compiledGraph.packet(packet);
    NWB_ASSERT(packetView.valid());
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    auto& queue = *MakeNotNull(compiledGraph.queueInfo(packetPlan.queue));
    if(!commandList.isRecording()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(GpuCommandIrReplayError::CommandListNotRecording, result.streamValidation);
    }
    if(commandList.commandRecordingFailed()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation);
    }
    if(commandList.isRenderPassActive()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(GpuCommandIrReplayError::CommandListRenderPassActive, result.streamValidation);
    }
    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    const GpuCommandIrReplayError::Enum commandListQueueError = GpuCommandIrDetail::ValidateReplayCommandListQueue(commandListDescription, packetPlan.queue, queue.queueClass);
    if(commandListQueueError != GpuCommandIrReplayError::None){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(commandListQueueError, result.streamValidation);
    }
    const GpuCommandIrReplayResult backendPreflight = __hidden_gpu_command_ir_replay_lowering::ValidateBackendOperandPacket(bytes, ownedStream, graph, packet, commandList, result.streamValidation);
    if(!backendPreflight.valid())
        return backendPreflight;

    const u64 recordingLeaseSerial = commandList.recordingLeaseSerial();
    // Preflight already proved legality, so this walk can lower without a duplicate command list.
    GpuCommandIrStreamReader reader(bytes);
    u64 recordIndex = 0u;
    for(;;){
        const auto decodedResult = reader.next();
        if(!decodedResult && decodedResult.error() == GpuCommandIrStreamReadStatus::End)
            return GpuCommandIrReplayResult{ .streamValidation = reader.validation(), .recordIndex = recordIndex, };
        if(!decodedResult){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(GpuCommandIrReplayError::StreamChangedDuringReplay, reader.validation(), reader.validation().recordIndex);
        }
        const auto& decoded = *decodedResult;

        const bool raster = GpuCommandIrDetail::IsRasterOpcode(decoded.opcode);
        const GpuSubmissionPacketId recordPacket = raster ? decoded.raster.packet : decoded.builtin.packet;
        if(recordPacket != packet){
            ++recordIndex;
            continue;
        }

        // Preflight admitted these resources and the raster sidecar; the graph and bytes are caller-stable and concurrent mutation stays unsupported.
        NWB_ASSERT(raster || (graph.validResource(decoded.builtin.destination) && ((decoded.builtin.opcode != GpuCommandIrOpcode::CopyBuffer && decoded.builtin.opcode != GpuCommandIrOpcode::CopyTexture) || graph.validResource(decoded.builtin.source))));

        if(raster){
            NWB_ASSERT(ownedStream);
            if(!GpuCommandIrDetail::LowerRasterOperation(decoded.raster, *ownedStream, reader.blobBytes(), commandList))
                return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
        }
        else if(decoded.builtin.opcode == GpuCommandIrOpcode::UploadBuffer || decoded.builtin.opcode == GpuCommandIrOpcode::UploadTexture){
            if(!GpuCommandIrDetail::LowerUploadOperation(decoded.builtin, reader.blobBytes(), graph, commandList)){
                return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
            }
        }
        else
            __hidden_gpu_command_ir_replay_lowering::LowerOperation(decoded.builtin, graph, commandList);
        if(!commandList.matchesRecordingLease(recordingLeaseSerial) || commandList.commandRecordingFailed()){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(GpuCommandIrReplayError::CommandListRecordingFailed, result.streamValidation, recordIndex);
        }
        ++recordIndex;
    }
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacket(const BinaryByteView bytes, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet, CommandList& commandList){
    return ReplayPacketImpl(bytes, nullptr, graph, compiledGraph, packet, commandList);
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacket(const GpuCommandIrOwnedStream& stream, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet, CommandList& commandList){
    return ReplayPacketImpl(stream.bytes(), &stream, graph, compiledGraph, packet, commandList);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

