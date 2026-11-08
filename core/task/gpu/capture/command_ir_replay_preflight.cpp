// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"
#include "command_ir_builtin_preflight.h"

#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const GpuTaskResourceUse* FindTaskResourceUse(const GpuTaskGraphTaskView& task, const GpuGraphResourceId resource, const ResourceStates::Mask state, const GpuTaskResourceAccess::Enum access)noexcept{
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = task.resourceUses[useIndex];
        if(use.resource == resource && use.requiredState == state && use.access == access)
            return &use;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_replay_preflight{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Finish the syntax walk first, so malformed later bytes still report InvalidStream before this semantic error.
[[nodiscard]] static GpuCommandIrReplayResult SemanticFailure(GpuCommandIrStreamReader& reader, const GpuCommandIrReplayError::Enum error, const u64 recordIndex = Limit<u64>::s_Max)noexcept{
    while(reader.next()){}
    const GpuCommandIrStreamValidationResult& streamValidation = reader.validation();
    if(!streamValidation.valid())
        return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::InvalidStream, streamValidation, streamValidation.recordIndex);
    return GpuCommandIrDetail::ReplayFailure(error, streamValidation, recordIndex);
}

[[nodiscard]] static b8 QueueHasTransferCapability(const GpuPhysicalQueueInfo& queue)noexcept{
    return (static_cast<u8>(queue.capabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u;
}

[[nodiscard]] static b8 QueueHasGraphicsCapability(const GpuPhysicalQueueInfo& queue)noexcept{
    return (static_cast<u8>(queue.capabilities) & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateRecordContext(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet,
    const GpuCompiledPacketView& packetView,
    const GpuPhysicalQueueInfo& queue,
    u32& inOutPreviousTaskOrder,
    b8& inOutHasPreviousTask
)noexcept{
    NWB_ASSERT(record.packet == packet);
    if(record.queue != queue.id)
        return GpuCommandIrReplayError::RecordQueueMismatch;
    if(!graph.validTask(record.task))
        return GpuCommandIrReplayError::InvalidTask;

    const GpuCompiledTaskView compiledTask = compiledGraph.findTask(record.task);
    if(!compiledTask.valid() || compiledTask.plan->packet != packet || compiledTask.plan->queue != queue.id)
        return GpuCommandIrReplayError::CompiledTaskMismatch;

    NWB_ASSERT(packetView.valid());
    if(packetView.plan->taskCount == 0u)
        return GpuCommandIrReplayError::CompiledTaskMismatch;

    const u32 searchBegin = inOutHasPreviousTask ? inOutPreviousTaskOrder : 0u;
    u32 taskOrder = searchBegin;
    while(taskOrder < packetView.plan->taskCount && packetView.tasks[taskOrder] != record.task)
        ++taskOrder;
    if(taskOrder == packetView.plan->taskCount){
        // Only a rejected backward record needs the earlier prefix to preserve its specific diagnostic.
        for(u32 taskIndex = 0u; taskIndex < searchBegin; ++taskIndex){
            if(packetView.tasks[taskIndex] == record.task)
                return GpuCommandIrReplayError::TaskOrderMismatch;
        }
        return GpuCommandIrReplayError::CompiledTaskMismatch;
    }

    inOutPreviousTaskOrder = taskOrder;
    inOutHasPreviousTask = true;
    return GpuCommandIrReplayError::None;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuCommandIrReplayResult PreflightPacketImpl(
    const BinaryByteView bytes,
    const GpuCommandIrOwnedStream* const ownedStream,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet
)noexcept{
    // One reader walk admits syntax and packet semantics; a syntax failure anywhere in the stream wins over a semantic failure.
    GpuCommandIrStreamReader reader(bytes);
    if(reader.validation().failed())
        return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::InvalidStream, reader.validation(), reader.validation().recordIndex);
    if(!compiledGraph.validFor(graph))
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, GpuCommandIrReplayError::InvalidCompiledGraph);

    const GpuCompiledPacketView packetView = compiledGraph.packet(packet);
    if(!packetView.valid())
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, GpuCommandIrReplayError::InvalidPacket);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetView.plan->queue);
    if(!queue || queue->queueClass >= CommandQueue::kCount)
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, GpuCommandIrReplayError::PacketQueueUnavailable);
    if(reader.recordCount() != 0u && reader.graphGeneration() != graph.generation())
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, GpuCommandIrReplayError::GraphGenerationMismatch);
    if(reader.recordCount() != 0u && reader.planGeneration() != compiledGraph.planGeneration())
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, GpuCommandIrReplayError::PlanGenerationMismatch);

    u32 previousTaskOrder = 0u;
    b8 hasPreviousTask = false;
    u64 recordIndex = 0u;
    GpuCommandIrDetail::RasterReplayState rasterState;
    for(;;){
        const auto decodedResult = reader.next();
        if(!decodedResult && decodedResult.error() == GpuCommandIrStreamReadStatus::End){
            if(rasterState.active)
                return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::InvalidRasterState, reader.validation(), recordIndex);
            return GpuCommandIrReplayResult{ .streamValidation = reader.validation(), .recordIndex = recordIndex, };
        }
        if(!decodedResult)
            return GpuCommandIrDetail::ReplayFailure(GpuCommandIrReplayError::InvalidStream, reader.validation(), reader.validation().recordIndex);
        const auto& decoded = *decodedResult;

        const b8 raster = GpuCommandIrDetail::IsRasterOpcode(decoded.opcode);
        const GpuSubmissionPacketId recordPacket = raster ? decoded.raster.packet : decoded.builtin.packet;
        // The packet is a self-contained scope; other packets only pass the reader's syntax checks.
        if(recordPacket != packet){
            ++recordIndex;
            continue;
        }
        if(raster && !ownedStream)
            return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, GpuCommandIrReplayError::MissingOwnedSidecar, recordIndex);

        GpuCommandIrBuiltinTaskRecord contextRecord = decoded.builtin;
        if(raster){
            contextRecord.task = decoded.raster.task;
            contextRecord.packet = decoded.raster.packet;
            contextRecord.queue = decoded.raster.queue;
        }

        const GpuCommandIrReplayError::Enum contextError = __hidden_gpu_command_ir_replay_preflight::ValidateRecordContext(contextRecord, graph, compiledGraph, packet, packetView, *queue, previousTaskOrder, hasPreviousTask);
        if(contextError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, contextError, recordIndex);

        const GpuTaskGraphTaskView task = graph.taskAt(contextRecord.task.index);
        GpuCommandIrReplayError::Enum operationError = GpuCommandIrReplayError::None;
        if(raster){
            if(!__hidden_gpu_command_ir_replay_preflight::QueueHasGraphicsCapability(*queue))
                operationError = GpuCommandIrReplayError::MissingGraphicsCapability;
            else{
                operationError = GpuCommandIrDetail::ValidateRasterGraphOperation(decoded.raster, graph, task, rasterState);
            }
        }
        else{
            if(!__hidden_gpu_command_ir_replay_preflight::QueueHasTransferCapability(*queue))
                operationError = GpuCommandIrReplayError::MissingTransferCapability;
            else{
                operationError = GpuCommandIrDetail::ValidateRasterBuiltinBoundary(contextRecord.task, rasterState);
                if(operationError == GpuCommandIrReplayError::None){
                    operationError = GpuCommandIrBuiltinPreflight::Validate(decoded.builtin, reader.blobBytes(), graph, task, *queue);
                }
            }
        }
        if(operationError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, operationError, recordIndex);

        ++recordIndex;
    }
}

GpuCommandIrReplayResult PreflightGpuCommandIrPacket(const BinaryByteView bytes, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet)noexcept{
    return PreflightPacketImpl(bytes, nullptr, graph, compiledGraph, packet);
}

GpuCommandIrReplayResult PreflightGpuCommandIrPacket(const GpuCommandIrOwnedStream& stream, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet)noexcept{
    return PreflightPacketImpl(stream.bytes(), &stream, graph, compiledGraph, packet);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

