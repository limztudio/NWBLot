// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/task_graph/compiled_graph.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


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
static void LowerOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    CommandList& commandList
)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        commandList.copyBuffer(
            graph.bufferForResource(record.destination),
            record.destinationOffsetBytes,
            graph.bufferForResource(record.source),
            record.sourceOffsetBytes,
            record.dataSizeBytes
        );
        return;
    case GpuCommandIrOpcode::CopyTexture:
        commandList.copyTexture(
            graph.textureForResource(record.destination),
            record.destinationSlice,
            graph.textureForResource(record.source),
            record.sourceSlice
        );
        return;
    case GpuCommandIrOpcode::ClearBuffer:
        commandList.clearBufferUInt(graph.bufferForResource(record.destination), record.uintClearValue.r);
        return;
    case GpuCommandIrOpcode::ClearTexture:
        switch(record.clearTextureValueType){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(
                graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.floatClearValue
            );
            return;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(
                graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.uintClearValue
            );
            return;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(
                graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.intClearValue
            );
            return;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(
                graph.textureForResource(record.destination),
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
            graph.textureForResource(record.destination),
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

// Direct Vulkan lowering is intentionally a prototype with a single opcode. Scan the complete selected packet
// after graph-aware preflight and before issuing any native command so an unsupported later record cannot leave an
// earlier copy in the command buffer.
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
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList
)noexcept{
    GpuCommandIrReplayResult result = PreflightGpuCommandIrPacket(bytes, graph, compiledGraph, packet);
    if(!result.valid())
        return result;

    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
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
    if(commandList.isRenderPassActive()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListRenderPassActive,
            result.streamValidation
        );
    }
    const CommandListParameters& commandListDescription = commandList.getDescription();
    if(commandListDescription.physicalQueue.deviceGeneration != compiledGraph.deviceGeneration()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }
    if(commandListDescription.queueType != queue->queueClass){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }

    // Preflight above establishes all graph/resource legality before any void Core::CommandList operation can
    // mutate its state tracker. The byte view and graph are caller-stable for this tooling call, so a second walk
    // can lower without allocating a duplicate command list or per-command object graph.
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

        // The graph and bytes are immutable for the call. These resolution checks defend the release build if a
        // caller violates that contract before replay begins; no revalidation is possible after a native command
        // has been emitted, so concurrent mutation remains unsupported by design.
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
        ++recordIndex;
    }
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacketDirectVulkan(
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
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

    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
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
    if(commandList.isRenderPassActive()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListRenderPassActive,
            result.streamValidation
        );
    }
    const CommandListParameters& commandListDescription = commandList.getDescription();
    if(commandListDescription.physicalQueue.deviceGeneration != compiledGraph.deviceGeneration()){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }
    if(commandListDescription.queueType != queue->queueClass){
        return __hidden_gpu_command_ir_replay_lowering::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }

    // The caller has already lowered the graph-owned state seed and packet barriers into commandList. This second
    // reader walk deliberately bypasses CommandList::copyBuffer, so it can measure/directly exercise only Vulkan
    // command emission without resurrecting automatic state tracking in the replay path.
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

        // Preflight above and the direct-opcode scan establish the normal contract. Repeat only the resolution
        // checks needed to turn caller mutation into a diagnostic instead of dereferencing a stale graph ID.
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
        if(!commandList.recordPreflightedCopyBufferDirectVulkan(
            graph.bufferForResource(record.destination),
            record.destinationOffsetBytes,
            graph.bufferForResource(record.source),
            record.sourceOffsetBytes,
            record.dataSizeBytes
        )){
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

