// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"
#include "command_ir_builtin_preflight.h"

#include <core/graphics/backend_selection/backend.h>
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
    const GpuTaskId recordTask,
    const GpuSubmissionPacketId recordPacket,
    const GpuPhysicalQueueId recordQueue,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet,
    const GpuCompiledPacketView& packetView,
    const GpuPhysicalQueueInfo& queue,
    u32& inOutPreviousTaskOrder,
    b8& inOutHasPreviousTask
)noexcept{
    FOUNDATION_ASSERT(recordPacket == packet);
    if(recordQueue != queue.id)
        return GpuCommandIrReplayError::RecordQueueMismatch;
    if(!graph.validTask(recordTask))
        return GpuCommandIrReplayError::InvalidTask;

    const GpuCompiledTaskView compiledTask = compiledGraph.findTask(recordTask);
    if(!compiledTask.valid() || compiledTask.plan->packet != packet || compiledTask.plan->queue != queue.id)
        return GpuCommandIrReplayError::CompiledTaskMismatch;

    FOUNDATION_ASSERT(packetView.valid());
    if(packetView.plan->taskCount == 0u)
        return GpuCommandIrReplayError::CompiledTaskMismatch;

    const u32 searchBegin = inOutHasPreviousTask ? inOutPreviousTaskOrder : 0u;
    u32 taskOrder = searchBegin;
    while(taskOrder < packetView.plan->taskCount && packetView.tasks[taskOrder] != recordTask)
        ++taskOrder;
    if(taskOrder == packetView.plan->taskCount){
        for(u32 taskIndex = 0u; taskIndex < searchBegin; ++taskIndex){
            if(packetView.tasks[taskIndex] == recordTask)
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


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayResult PreflightGpuCommandIrPacketStream(
    const BinaryByteView primaryBytes,
    const BinaryByteView secondaryBlobBytes,
    const b8 splitBuffers,
    const RasterOwnerTableView& owners,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList* const commandList
)noexcept{
    Optional<GpuCommandIrStreamReader> readerStorage;
    if(splitBuffers)
        readerStorage.emplace(primaryBytes, secondaryBlobBytes);
    else
        readerStorage.emplace(primaryBytes);
    GpuCommandIrStreamReader& reader = *readerStorage;

    if(reader.validation().failed())
        return ReplayFailure(GpuCommandIrReplayError::InvalidStream, reader.validation(), reader.validation().recordIndex);
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

    GpuCommandIrReplayError::Enum commandListError = GpuCommandIrReplayError::None;
    const GpuPhysicalQueueInfo* backendCommandQueue = nullptr;
    if(commandList){
        if(!commandList->isRecording())
            commandListError = GpuCommandIrReplayError::CommandListNotRecording;
        else if(commandList->commandRecordingFailed())
            commandListError = GpuCommandIrReplayError::CommandListRecordingFailed;
        else if(commandList->isRenderPassActive())
            commandListError = GpuCommandIrReplayError::CommandListRenderPassActive;
        else{
            const CommandListParameters commandListDescription = commandList->getResolvedDescription();
            commandListError = ValidateReplayCommandListQueue(commandListDescription, packetView.plan->queue, queue->queueClass);
            if(commandListError == GpuCommandIrReplayError::None){
                backendCommandQueue = commandList->getCachedPhysicalQueueInfo();
                if(!backendCommandQueue || backendCommandQueue->id != commandListDescription.physicalQueue)
                    backendCommandQueue = commandList->getDevice().getPhysicalQueueInfo(commandListDescription.physicalQueue);
                if(!backendCommandQueue)
                    commandListError = GpuCommandIrReplayError::BackendResourceNotReady;
            }
        }
    }

    u32 previousTaskOrder = 0u;
    b8 hasPreviousTask = false;
    GpuTaskId cachedRecordTask;
    GpuTaskGraphTaskView cachedTaskView;
    u64 recordIndex = 0u;
    RasterReplayState graphRasterState;
    RasterReplayState backendRasterState;
    GpuCommandIrReplayError::Enum firstBackendError = GpuCommandIrReplayError::None;
    u64 firstBackendErrorRecordIndex = Limit<u64>::s_Max;
    for(;;){
        const auto decodedResult = reader.next();
        if(!decodedResult && decodedResult.error() == GpuCommandIrStreamReadStatus::End){
            if(graphRasterState.active)
                return ReplayFailure(GpuCommandIrReplayError::InvalidRasterState, reader.validation(), recordIndex);
            if(commandListError != GpuCommandIrReplayError::None)
                return ReplayFailure(commandListError, reader.validation());
            if(firstBackendError != GpuCommandIrReplayError::None)
                return ReplayFailure(firstBackendError, reader.validation(), firstBackendErrorRecordIndex);
            return GpuCommandIrReplayResult{ .streamValidation = reader.validation(), .recordIndex = recordIndex, };
        }
        if(!decodedResult)
            return ReplayFailure(GpuCommandIrReplayError::InvalidStream, reader.validation(), reader.validation().recordIndex);
        const auto& decoded = *decodedResult;

        const b8 raster = IsRasterOpcode(decoded.opcode);
        const GpuSubmissionPacketId recordPacket = raster ? decoded.raster.packet : decoded.builtin.packet;
        if(recordPacket != packet){
            ++recordIndex;
            continue;
        }
        if(raster && !owners.hasOwnerSidecar)
            return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, GpuCommandIrReplayError::MissingOwnedSidecar, recordIndex);

        const GpuTaskId recordTask = raster ? decoded.raster.task : decoded.builtin.task;
        const GpuPhysicalQueueId recordQueue = raster ? decoded.raster.queue : decoded.builtin.queue;
        if(!hasPreviousTask || recordTask != cachedRecordTask || recordQueue != queue->id){
            const GpuCommandIrReplayError::Enum contextError = __hidden_gpu_command_ir_replay_preflight::ValidateRecordContext(
                recordTask,
                recordPacket,
                recordQueue,
                graph,
                compiledGraph,
                packet,
                packetView,
                *queue,
                previousTaskOrder,
                hasPreviousTask
            );
            if(contextError != GpuCommandIrReplayError::None)
                return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, contextError, recordIndex);
            cachedRecordTask = recordTask;
            cachedTaskView = graph.taskAt(recordTask.index);
        }

        const GpuTaskGraphTaskView& task = cachedTaskView;
        GpuCommandIrReplayError::Enum operationError = GpuCommandIrReplayError::None;
        if(raster){
            if(!__hidden_gpu_command_ir_replay_preflight::QueueHasGraphicsCapability(*queue))
                operationError = GpuCommandIrReplayError::MissingGraphicsCapability;
            else
                operationError = ValidateRasterGraphOperation(decoded.raster, graph, task, graphRasterState);
        }
        else{
            if(!__hidden_gpu_command_ir_replay_preflight::QueueHasTransferCapability(*queue))
                operationError = GpuCommandIrReplayError::MissingTransferCapability;
            else{
                operationError = ValidateRasterBuiltinBoundary(recordTask, graphRasterState);
                if(operationError == GpuCommandIrReplayError::None)
                    operationError = GpuCommandIrBuiltinPreflight::Validate(decoded.builtin, reader.blobBytes(), graph, task, *queue);
            }
        }
        if(operationError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(reader, operationError, recordIndex);

        if(commandList && commandListError == GpuCommandIrReplayError::None && firstBackendError == GpuCommandIrReplayError::None){
            FOUNDATION_ASSERT(backendCommandQueue);
            const GpuCommandIrReplayError::Enum operandError = raster
                ? ValidateRasterBackendOperand(decoded.raster, owners, graph, *backendCommandQueue, recordIndex, *commandList, backendRasterState)
                : ValidateBuiltinBackendOperand(decoded.builtin, graph, *backendCommandQueue, *commandList)
            ;
            if(operandError != GpuCommandIrReplayError::None){
                firstBackendError = operandError;
                firstBackendErrorRecordIndex = recordIndex;
            }
        }

        ++recordIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayResult PreflightGpuCommandIrPacket(const BinaryByteView bytes, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet)noexcept{
    return GpuCommandIrDetail::PreflightGpuCommandIrPacketStream(bytes, {}, false, GpuCommandIrDetail::RasterOwnerTableView::None(), graph, compiledGraph, packet, nullptr);
}

GpuCommandIrReplayResult PreflightGpuCommandIrPacket(const GpuCommandIrOwnedStream& stream, const GpuTaskGraphDeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledGraph, const GpuSubmissionPacketId packet)noexcept{
    return GpuCommandIrDetail::PreflightGpuCommandIrPacketStream(stream.bytes(), {}, false, GpuCommandIrDetail::RasterOwnerTableView::FromOwnedStream(stream), graph, compiledGraph, packet, nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

