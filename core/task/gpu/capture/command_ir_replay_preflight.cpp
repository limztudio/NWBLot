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


[[nodiscard]] static GpuCommandIrReplayResult SemanticFailure(GpuCommandIrDetail::WireStreamCursor& cursor, const GpuCommandIrReplayError::Enum error, const u64 recordIndex = Limit<u64>::s_Max)noexcept{
    cursor.drain();
    const GpuCommandIrStreamValidationResult& streamValidation = cursor.validation();
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
    WireStreamCursor cursor(primaryBytes, secondaryBlobBytes, splitBuffers);

    if(cursor.validation().failed())
        return ReplayFailure(GpuCommandIrReplayError::InvalidStream, cursor.validation(), cursor.validation().recordIndex);
    if(!compiledGraph.validFor(graph))
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, GpuCommandIrReplayError::InvalidCompiledGraph);

    const GpuCompiledPacketView packetView = compiledGraph.packet(packet);
    if(!packetView.valid())
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, GpuCommandIrReplayError::InvalidPacket);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetView.plan->queue);
    if(!queue || queue->queueClass >= CommandQueue::kCount)
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, GpuCommandIrReplayError::PacketQueueUnavailable);
    if(cursor.recordCount() != 0u && cursor.graphGeneration() != graph.generation())
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, GpuCommandIrReplayError::GraphGenerationMismatch);
    if(cursor.recordCount() != 0u && cursor.planGeneration() != compiledGraph.planGeneration())
        return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, GpuCommandIrReplayError::PlanGenerationMismatch);

    GpuCommandIrReplayError::Enum commandListError = GpuCommandIrReplayError::None;
    const GpuPhysicalQueueInfo* backendCommandQueue = nullptr;
    Optional<BackendRasterReplayState> backendRasterState;
    Optional<BuiltinBackendResourceCache> builtinResourceCache;
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
                else{
                    backendRasterState.emplace();
                    builtinResourceCache.emplace();
                }
            }
        }
    }

    u32 previousTaskOrder = 0u;
    b8 hasPreviousTask = false;
    GpuTaskId cachedRecordTask;
    GpuTaskGraphTaskView cachedTaskView;
    u64 recordIndex = 0u;
    RasterReplayState graphRasterState;
    GpuCommandIrReplayError::Enum firstBackendError = GpuCommandIrReplayError::None;
    u64 firstBackendErrorRecordIndex = Limit<u64>::s_Max;
    const u8* recordPtr = nullptr;
    GpuCommandIrWireOpcode::Enum wireOpcode = GpuCommandIrWireOpcode::kCount;
    DecodedRecordContext decodedContext;
    for(;;){
        const WireStepStatus::Enum step = cursor.nextView(recordPtr, wireOpcode, decodedContext);
        if(step == WireStepStatus::End){
            if(graphRasterState.active)
                return ReplayFailure(GpuCommandIrReplayError::InvalidRasterState, cursor.validation(), recordIndex);
            if(commandListError != GpuCommandIrReplayError::None)
                return ReplayFailure(commandListError, cursor.validation());
            if(firstBackendError != GpuCommandIrReplayError::None)
                return ReplayFailure(firstBackendError, cursor.validation(), firstBackendErrorRecordIndex);
            return GpuCommandIrReplayResult{ .streamValidation = cursor.validation(), .recordIndex = recordIndex, };
        }
        if(step == WireStepStatus::Error)
            return ReplayFailure(GpuCommandIrReplayError::InvalidStream, cursor.validation(), cursor.validation().recordIndex);

        if(decodedContext.packet != packet){
            ++recordIndex;
            continue;
        }
        const b8 raster = IsRasterOpcode(wireOpcode);
        if(raster && !owners.hasOwnerSidecar)
            return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, GpuCommandIrReplayError::MissingOwnedSidecar, recordIndex);

        if(!hasPreviousTask || decodedContext.task != cachedRecordTask || decodedContext.queue != queue->id){
            const GpuCommandIrReplayError::Enum contextError = __hidden_gpu_command_ir_replay_preflight::ValidateRecordContext(
                decodedContext.task,
                decodedContext.packet,
                decodedContext.queue,
                graph,
                compiledGraph,
                packet,
                packetView,
                *queue,
                previousTaskOrder,
                hasPreviousTask
            );
            if(contextError != GpuCommandIrReplayError::None)
                return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, contextError, recordIndex);
            cachedRecordTask = decodedContext.task;
            cachedTaskView = graph.taskAt(decodedContext.task.index);
        }

        const GpuTaskGraphTaskView& task = cachedTaskView;
        GpuCommandIrReplayError::Enum operationError = GpuCommandIrReplayError::None;
        if(raster){
            if(!__hidden_gpu_command_ir_replay_preflight::QueueHasGraphicsCapability(*queue))
                operationError = GpuCommandIrReplayError::MissingGraphicsCapability;
            else if(graphRasterState.active && graphRasterState.task != decodedContext.task)
                operationError = GpuCommandIrReplayError::InvalidRasterState;
            else{
                switch(wireOpcode){
                case GpuCommandIrWireOpcode::SetGraphicsState:
                    operationError = ValidateSetGraphicsStateWireGraphOperation(recordPtr, decodedContext.task, cursor.blobBytes(), graph, task, graphRasterState);
                    break;
                case GpuCommandIrWireOpcode::BindGraphicsHeap:{
                    const auto wire = ReadWirePod<GpuCommandIrBindGraphicsHeapRecord>(recordPtr);
                    if(!graphRasterState.active || GpuGraphPipelineId{ .generation = decodedContext.task.generation, .index = wire.pipelineIndex } != graphRasterState.pipeline)
                        operationError = GpuCommandIrReplayError::InvalidRasterState;
                    else
                        graphRasterState.heapBound = true;
                    break;
                }
                case GpuCommandIrWireOpcode::SetPushConstants:{
                    const auto wire = ReadWirePod<GpuCommandIrSetPushConstantsRecord>(recordPtr);
                    if(!graphRasterState.active || wire.blobSizeBytes > s_MaxPushConstantSize)
                        operationError = GpuCommandIrReplayError::InvalidRasterState;
                    else
                        graphRasterState.pushConstantsSet = true;
                    break;
                }
                case GpuCommandIrWireOpcode::Draw:{
                    const auto wire = ReadWirePod<GpuCommandIrDrawRecord>(recordPtr);
                    if(!graphRasterState.active || !graphRasterState.heapBound || !graphRasterState.pushConstantsSet || wire.vertexCount == 0u || wire.instanceCount == 0u)
                        operationError = GpuCommandIrReplayError::InvalidRasterDraw;
                    break;
                }
                case GpuCommandIrWireOpcode::DrawIndexed:{
                    const auto wire = ReadWirePod<GpuCommandIrDrawRecord>(recordPtr);
                    if(!graphRasterState.active || !graphRasterState.heapBound || !graphRasterState.pushConstantsSet || wire.vertexCount == 0u || wire.instanceCount == 0u || !graphRasterState.indexResource.valid() || wire.startVertexLocation > static_cast<u32>(Limit<i32>::s_Max))
                        operationError = GpuCommandIrReplayError::InvalidRasterDraw;
                    break;
                }
                case GpuCommandIrWireOpcode::EndRenderPass:
                    if(!graphRasterState.active)
                        operationError = GpuCommandIrReplayError::InvalidRasterState;
                    else{
                        graphRasterState.active = false;
                        graphRasterState.task = {};
                        graphRasterState.heapBound = false;
                        graphRasterState.pushConstantsSet = false;
                    }
                    break;
                default:
                    operationError = GpuCommandIrReplayError::InvalidRasterState;
                    break;
                }
            }
        }
        else{
            if(!__hidden_gpu_command_ir_replay_preflight::QueueHasTransferCapability(*queue))
                operationError = GpuCommandIrReplayError::MissingTransferCapability;
            else{
                operationError = ValidateRasterBuiltinBoundary(decodedContext.task, graphRasterState);
                if(operationError == GpuCommandIrReplayError::None)
                    operationError = GpuCommandIrBuiltinPreflight::ValidateWire(wireOpcode, recordPtr, decodedContext.task.generation, cursor.blobBytes(), graph, task, *queue);
            }
        }
        if(operationError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir_replay_preflight::SemanticFailure(cursor, operationError, recordIndex);

        if(commandList && commandListError == GpuCommandIrReplayError::None && firstBackendError == GpuCommandIrReplayError::None){
            FOUNDATION_ASSERT(backendCommandQueue && backendRasterState.has_value() && builtinResourceCache.has_value());
            GpuCommandIrReplayError::Enum operandError = GpuCommandIrReplayError::None;
            if(raster){
                if(wireOpcode == GpuCommandIrWireOpcode::Draw){
                    if(!backendRasterState->active || !backendRasterState->heapBound || !backendRasterState->pushConstantsSet)
                        operandError = GpuCommandIrReplayError::InvalidRasterState;
                    else if(backendRasterState->cachedVertexValidations.empty())
                        operandError = backendRasterState->cachedInputLayoutError;
                    else
                        operandError = ValidateRasterWireBackendOperand(wireOpcode, recordPtr, cursor.blobBytes(), decodedContext.task, owners, graph, *backendCommandQueue, recordIndex, *commandList, *backendRasterState);
                }
                else
                    operandError = ValidateRasterWireBackendOperand(wireOpcode, recordPtr, cursor.blobBytes(), decodedContext.task, owners, graph, *backendCommandQueue, recordIndex, *commandList, *backendRasterState);
            }
            else{
                operandError = ValidateBuiltinWireBackendOperand(wireOpcode, recordPtr, decodedContext.task.generation, graph, *backendCommandQueue, *commandList, *builtinResourceCache);
            }
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

