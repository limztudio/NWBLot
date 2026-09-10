// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateReplayCommandListQueue(
    const CommandListParameters& commandListDescription,
    const GpuPhysicalQueueId& packetQueue,
    const CommandQueue::Enum packetQueueClass
)noexcept{
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


[[nodiscard]] static GpuCommandIrReplayResult ReplayFailure(
    const GpuCommandIrReplayError::Enum error,
    const GpuCommandIrStreamValidationResult& streamValidation = {},
    const u64 recordIndex = Limit<u64>::s_Max
)noexcept{
    return GpuCommandIrReplayResult{
        .error = error,
        .streamValidation = streamValidation,
        .recordIndex = recordIndex,
    };
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferBackendOperand(
    Buffer* const buffer,
    const ResourceStates::Mask requiredState,
    const VkBufferUsageFlags requiredUsage,
    CommandList& commandList,
    const GpuPhysicalQueueInfo& commandQueue
)noexcept{
    if(!buffer)
        return GpuCommandIrReplayError::StreamChangedDuringReplay;
    if(
        !commandList.getDevice().isBufferReadyForGpuUse(buffer, requiredUsage)
        || !ResourceQueueAdmissionAdmitsQueue(buffer->getQueueAdmissionSnapshot(), commandQueue)
    )
        return GpuCommandIrReplayError::BackendResourceNotReady;

    const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != requiredState)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureBackendOperand(
    Texture* const texture,
    const ResourceStates::Mask requiredState,
    const VkImageUsageFlags requiredUsage,
    CommandList& commandList,
    const GpuPhysicalQueueInfo& commandQueue
)noexcept{
    if(!texture)
        return GpuCommandIrReplayError::StreamChangedDuringReplay;
    if(
        !commandList.getDevice().isTextureReadyForGpuUse(texture, requiredUsage)
        || !ResourceQueueAdmissionAdmitsQueue(texture->getQueueAdmissionSnapshot(), commandQueue)
    )
        return GpuCommandIrReplayError::BackendResourceNotReady;

    const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(texture);
    if(permanentState != ResourceStates::Unknown && permanentState != requiredState)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBackendOperands(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList,
    const GpuPhysicalQueueInfo& commandQueue
)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:{
        Buffer* const source = graph.bufferForResource(record.source);
        Buffer* const destination = graph.bufferForResource(record.destination);
        if(source == destination){
            return ValidateBufferBackendOperand(
                source,
                ResourceStates::CopySource | ResourceStates::CopyDest,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                commandList,
                commandQueue
            );
        }

        const GpuCommandIrReplayError::Enum sourceError = ValidateBufferBackendOperand(
            source,
            ResourceStates::CopySource,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            commandList,
            commandQueue
        );
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateBufferBackendOperand(
            destination,
            ResourceStates::CopyDest,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            commandList,
            commandQueue
        );
    }
    case GpuCommandIrOpcode::CopyTexture:{
        const GpuCommandIrReplayError::Enum sourceError = ValidateTextureBackendOperand(
            graph.textureForResource(record.source),
            ResourceStates::CopySource,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            commandList,
            commandQueue
        );
        if(sourceError != GpuCommandIrReplayError::None)
            return sourceError;
        return ValidateTextureBackendOperand(
            graph.textureForResource(record.destination),
            ResourceStates::CopyDest,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            commandList,
            commandQueue
        );
    }
    case GpuCommandIrOpcode::ClearBuffer:
        return ValidateBufferBackendOperand(
            graph.bufferForResource(record.destination),
            ResourceStates::CopyDest,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            commandList,
            commandQueue
        );
    case GpuCommandIrOpcode::ClearTexture:
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        return ValidateTextureBackendOperand(
            graph.textureForResource(record.destination),
            ResourceStates::CopyDest,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            commandList,
            commandQueue
        );
    default:
        return GpuCommandIrReplayError::StreamChangedDuringReplay;
    }
}

// Preflight cannot check native ownership, so scan the packet before taking the recording lease.
[[nodiscard]] static GpuCommandIrReplayResult ValidateBackendOperandPacket(
    const BinaryByteView bytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList,
    const GpuCommandIrStreamValidationResult& expectedStreamValidation
)noexcept{
    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    const GpuPhysicalQueueInfo* const commandQueue = commandList.getDevice().getPhysicalQueueInfo(
        commandListDescription.physicalQueue
    );
    if(!commandQueue)
        return ReplayFailure(GpuCommandIrReplayError::BackendResourceNotReady, expectedStreamValidation);

    GpuCommandIrStreamReader reader(bytes);
    if(reader.validation().failed()){
        return ReplayFailure(
            GpuCommandIrReplayError::StreamChangedDuringReplay,
            reader.validation(),
            reader.validation().recordIndex
        );
    }

    u64 recordIndex = 0u;
    GpuCommandIrBuiltinTaskRecord record;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End){
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        }
        if(status == GpuCommandIrStreamReadStatus::Error){
            return ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        if(record.packet == packet){
            const GpuCommandIrReplayError::Enum operandError = ValidateBackendOperands(
                record,
                graph,
                commandList,
                *commandQueue
            );
            if(operandError != GpuCommandIrReplayError::None)
                return ReplayFailure(operandError, expectedStreamValidation, recordIndex);
        }
        ++recordIndex;
    }
}

static void LowerOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList
)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        commandList.copyBuffer(
            *graph.bufferForResource(record.destination),
            record.destinationOffsetBytes,
            *graph.bufferForResource(record.source),
            record.sourceOffsetBytes,
            record.dataSizeBytes
        );
        return;
    case GpuCommandIrOpcode::CopyTexture:
        commandList.copyTexture(
            *graph.textureForResource(record.destination),
            record.destinationSlice,
            *graph.textureForResource(record.source),
            record.sourceSlice
        );
        return;
    case GpuCommandIrOpcode::ClearBuffer:
        commandList.clearBufferUInt(*graph.bufferForResource(record.destination), record.uintClearValue.r);
        return;
    case GpuCommandIrOpcode::ClearTexture:
        switch(record.clearTextureValueType){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(
                *graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.floatClearValue
            );
            return;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(
                *graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.uintClearValue
            );
            return;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(
                *graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.intClearValue
            );
            return;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(
                *graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.clearDepth,
                record.depthClearValue,
                record.clearStencil,
                record.stencilClearValue
            );
            return;
        default:
            NWB_ASSERT_MSG(false, NWB_TEXT("Validated command IR clear type lost its lowerer"));
            return;
        }
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        commandList.clearTextureRectUInt(
            *graph.textureForResource(record.destination),
            record.destinationSubresources,
            record.clearRect,
            record.uintClearValue
        );
        return;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Validated command IR opcode lost its lowerer"));
        return;
    }
}

// Direct lowering supports one opcode; scan the packet before emitting any native command.
[[nodiscard]] static GpuCommandIrReplayResult ValidateDirectVulkanOpcodeSupport(
    const BinaryByteView bytes,
    const GpuSubmissionPacketId packet,
    const GpuCommandIrStreamValidationResult& expectedStreamValidation
)noexcept{
    GpuCommandIrStreamReader reader(bytes);
    if(reader.validation().failed()){
        return ReplayFailure(
            GpuCommandIrReplayError::StreamChangedDuringReplay,
            reader.validation(),
            reader.validation().recordIndex
        );
    }

    u64 recordIndex = 0u;
    GpuCommandIrBuiltinTaskRecord record;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End){
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        }
        if(status == GpuCommandIrStreamReadStatus::Error){
            return ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        if(record.packet == packet && record.opcode != GpuCommandIrOpcode::CopyBuffer){
            return ReplayFailure(
                GpuCommandIrReplayError::UnsupportedDirectVulkanOpcode,
                expectedStreamValidation,
                recordIndex
            );
        }
        ++recordIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayResult ReplayGpuCommandIrPacket(
    const BinaryByteView bytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList
)noexcept{
    GpuCommandIrReplayResult result = PreflightGpuCommandIrPacket(bytes, graph, compiledGraph, packet);
    if(!result.valid())
        return result;

    const GpuCompiledPacketView packetView = compiledGraph.packet(packet);
    if(!packetView.valid()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::InvalidPacket,
            result.streamValidation
        );
    }
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetPlan.queue);
    if(!queue){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::PacketQueueUnavailable,
            result.streamValidation
        );
    }
    if(!commandList.isRecording()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListNotRecording,
            result.streamValidation
        );
    }
    if(commandList.commandRecordingFailed()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListRecordingFailed,
            result.streamValidation
        );
    }
    if(commandList.isRenderPassActive()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListRenderPassActive,
            result.streamValidation
        );
    }
    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    const GpuCommandIrReplayError::Enum commandListQueueError = GpuCommandIrDetail::ValidateReplayCommandListQueue(
        commandListDescription,
        packetPlan.queue,
        queue->queueClass
    );
    if(commandListQueueError != GpuCommandIrReplayError::None){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            commandListQueueError,
            result.streamValidation
        );
    }
    const GpuCommandIrReplayResult backendPreflight = __hidden_gpu_command_ir_replay_lowering::ValidateBackendOperandPacket(
        bytes,
        graph,
        packet,
        commandList,
        result.streamValidation
    );
    if(!backendPreflight.valid())
        return backendPreflight;

    const u64 recordingLeaseSerial = commandList.recordingLeaseSerial();
    // Preflight already proved legality, so this walk can lower without a duplicate command list.
    GpuCommandIrStreamReader reader(bytes);
    GpuCommandIrBuiltinTaskRecord record;
    u64 recordIndex = 0u;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End)
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        if(status == GpuCommandIrStreamReadStatus::Error){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        if(record.packet != packet){
            ++recordIndex;
            continue;
        }

        // The graph and bytes are caller-stable; concurrent mutation stays unsupported.
        if(!graph.validResource(record.destination) || (
            (record.opcode == GpuCommandIrOpcode::CopyBuffer || record.opcode == GpuCommandIrOpcode::CopyTexture)
            && !graph.validResource(record.source)
        )){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                result.streamValidation,
                recordIndex
            );
        }

        __hidden_gpu_command_ir_replay_lowering::LowerOperation(record, graph, commandList);
        if(!commandList.matchesRecordingLease(recordingLeaseSerial) || commandList.commandRecordingFailed()){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
                GpuCommandIrReplayError::CommandListRecordingFailed,
                result.streamValidation,
                recordIndex
            );
        }
        ++recordIndex;
    }
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacketDirectVulkan(
    const BinaryByteView bytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList
)noexcept{
    GpuCommandIrReplayResult result = PreflightGpuCommandIrPacket(bytes, graph, compiledGraph, packet);
    if(!result.valid())
        return result;

    result = __hidden_gpu_command_ir_replay_lowering::ValidateDirectVulkanOpcodeSupport(
        bytes,
        packet,
        result.streamValidation
    );
    if(!result.valid())
        return result;

    const GpuCompiledPacketView packetView = compiledGraph.packet(packet);
    if(!packetView.valid()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::InvalidPacket,
            result.streamValidation
        );
    }
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetPlan.queue);
    if(!queue){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::PacketQueueUnavailable,
            result.streamValidation
        );
    }
    if(!commandList.isRecording()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListNotRecording,
            result.streamValidation
        );
    }
    if(commandList.commandRecordingFailed()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListRecordingFailed,
            result.streamValidation
        );
    }
    if(commandList.isRenderPassActive()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListRenderPassActive,
            result.streamValidation
        );
    }
    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    const GpuCommandIrReplayError::Enum commandListQueueError = GpuCommandIrDetail::ValidateReplayCommandListQueue(
        commandListDescription,
        packetPlan.queue,
        queue->queueClass
    );
    if(commandListQueueError != GpuCommandIrReplayError::None){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            commandListQueueError,
            result.streamValidation
        );
    }
    const GpuCommandIrReplayResult backendPreflight =
        __hidden_gpu_command_ir_replay_lowering::ValidateBackendOperandPacket(
            bytes,
            graph,
            packet,
            commandList,
            result.streamValidation
        );
    if(!backendPreflight.valid())
        return backendPreflight;

    const u64 recordingLeaseSerial = commandList.recordingLeaseSerial();

    // Barriers are already lowered; bypass copyBuffer to exercise raw Vulkan emission only.
    GpuCommandIrStreamReader reader(bytes);
    GpuCommandIrBuiltinTaskRecord record;
    u64 recordIndex = 0u;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End){
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        }
        if(status == GpuCommandIrStreamReadStatus::Error){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        if(record.packet != packet){
            ++recordIndex;
            continue;
        }

        // Preflight proved the contract; repeat only resolution checks against stale IDs.
        if(
            record.opcode != GpuCommandIrOpcode::CopyBuffer
            || !graph.validResource(record.source)
            || !graph.validResource(record.destination)
        ){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                result.streamValidation,
                recordIndex
            );
        }
        const bool lowered = commandList.recordPreflightedCopyBufferDirectVulkan(
            *graph.bufferForResource(record.destination),
            record.destinationOffsetBytes,
            *graph.bufferForResource(record.source),
            record.sourceOffsetBytes,
            record.dataSizeBytes
        );
        if(!commandList.matchesRecordingLease(recordingLeaseSerial) || commandList.commandRecordingFailed()){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
                GpuCommandIrReplayError::CommandListRecordingFailed,
                result.streamValidation,
                recordIndex
            );
        }
        if(!lowered){
            return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
                GpuCommandIrReplayError::DirectVulkanLoweringFailed,
                result.streamValidation,
                recordIndex
            );
        }
        ++recordIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

