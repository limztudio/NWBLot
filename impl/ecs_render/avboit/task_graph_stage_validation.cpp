// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererAvboitTaskGraphValidation RendererAvboitSystem::validateTaskGraphStage(
    const Core::GpuCompiledGraph::ReadView& compiledGraph,
    const bool clearTargets,
    const bool hasTransparentRenderers
)const{
    const RendererAvboitTaskGraphStageState& taskGraphStage = m_taskGraphStage;
    const auto optionalTaskPrecedesInSharedPacket = [&compiledGraph](
        const Core::GpuTaskId optionalTask,
        const Core::GpuTaskId consumerTask
    ){
        return !optionalTask.valid()
            || !compiledGraph.tasksSharePacket(optionalTask, consumerTask)
            || compiledGraph.taskPrecedesInSamePacket(optionalTask, consumerTask)
        ;
    };
    const auto taskIsCompiled = [&compiledGraph](const Core::GpuTaskId task){
        return compiledGraph.findTask(task).valid();
    };
    const auto taskBoundaryIsOrdered = [&compiledGraph](
        const Core::GpuTaskId producerTask,
        const Core::GpuTaskId consumerTask
    ){
        if(!producerTask.valid() || !consumerTask.valid())
            return false;
        if(compiledGraph.tasksSharePacket(producerTask, consumerTask))
            return compiledGraph.taskPrecedesInSamePacket(producerTask, consumerTask);
        return compiledGraph.taskPrecedesOrSharesPacket(producerTask, consumerTask);
    };
    const bool avboitPrePacketContainsClear = !clearTargets || (
        taskGraphStage.m_clearFirstTask.valid()
        && taskGraphStage.m_clearTask.valid()
        && compiledGraph.tasksSharePacket(
            taskGraphStage.m_preTask,
            taskGraphStage.m_clearFirstTask
        )
        && compiledGraph.tasksSharePacket(
            taskGraphStage.m_preTask,
            taskGraphStage.m_clearTask
        )
    );
    // Keep CSG clears with AVBOIT Pre for its timing and handoff.
    const bool avboitPrePacketContainsTransparentCsgClear =
        (!taskGraphStage.m_transparentCsgIntervalClearFirstTask.valid()
            && !taskGraphStage.m_transparentCsgIntervalClearTask.valid())
        || (
            taskGraphStage.m_transparentCsgIntervalClearFirstTask.valid()
            && taskGraphStage.m_transparentCsgIntervalClearTask.valid()
            && compiledGraph.tasksSharePacket(
                taskGraphStage.m_preTask,
                taskGraphStage.m_transparentCsgIntervalClearFirstTask
            )
            && compiledGraph.tasksSharePacket(
                taskGraphStage.m_preTask,
                taskGraphStage.m_transparentCsgIntervalClearTask
            )
        )
    ;
    const bool avboitPrePacketContainsOccupancy = compiledGraph.tasksSharePacket(
        taskGraphStage.m_preTask,
        taskGraphStage.m_occupancyTask
    ) && compiledGraph.taskPrecedesInSamePacket(
        taskGraphStage.m_preTask,
        taskGraphStage.m_occupancyTask
    )
    ;
    // Span/Combine share Pre's timing; reject a split before recording.
    const bool avboitPrePacketContainsCsgReceiverSpan =
        !taskGraphStage.m_csgReceiverSpanTask.valid()
        || compiledGraph.tasksSharePacket(
            taskGraphStage.m_preTask,
            taskGraphStage.m_csgReceiverSpanTask
        )
    ;
    const bool avboitPrePacketContainsCsgIntervalCombine =
        !taskGraphStage.m_csgIntervalCombineTask.valid()
        || compiledGraph.tasksSharePacket(
            taskGraphStage.m_preTask,
            taskGraphStage.m_csgIntervalCombineTask
        )
    ;
    const Core::GpuPhysicalQueueInfo* const avboitPreQueue =
        compiledGraph.queueInfoForTask(taskGraphStage.m_preTask);
    const Core::GpuPhysicalQueueInfo* const avboitOccupancyQueue =
        compiledGraph.queueInfoForTask(taskGraphStage.m_occupancyTask);
    const Core::GpuPhysicalQueueInfo* const avboitOccupancyComputeEmulationQueue =
        taskGraphStage.m_occupancyComputeEmulationTask.valid()
            ? compiledGraph.queueInfoForTask(taskGraphStage.m_occupancyComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitOccupancySharedComputeEmulationQueue =
        taskGraphStage.m_occupancySharedComputeEmulationTaskCount != 0u
            && taskGraphStage.m_occupancySharedComputeEmulationTasks[0u].valid()
            ? compiledGraph.queueInfoForTask(
                taskGraphStage.m_occupancySharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    const bool hasAllTransparentTasks =
        taskGraphStage.m_depthWarpTask.valid()
        && taskGraphStage.m_extinctionTask.valid()
        && taskGraphStage.m_integrationTask.valid()
        && taskGraphStage.m_accumulationTask.valid()
        && taskGraphStage.m_accumulationFinalizeTask.valid()
    ;
    const bool hasAnyTransparentTask =
        taskGraphStage.m_depthWarpTask.valid()
        || taskGraphStage.m_extinctionTask.valid()
        || taskGraphStage.m_integrationTask.valid()
        || taskGraphStage.m_accumulationTask.valid()
        || taskGraphStage.m_accumulationFinalizeTask.valid()
    ;
    const bool transparentTaskShapeValid = hasTransparentRenderers
        ? hasAllTransparentTasks
        : !hasAnyTransparentTask
    ;
    const bool avboitExtinctionPacketContainsStreams = !taskGraphStage.m_extinctionStreamTask.valid()
        || compiledGraph.tasksSharePacket(
            taskGraphStage.m_extinctionTask,
            taskGraphStage.m_extinctionStreamTask
        )
    ;
    const bool avboitAccumulationPacketContainsStreams = !taskGraphStage.m_accumulationStreamTask.valid()
        || compiledGraph.tasksSharePacket(
            taskGraphStage.m_accumulationTask,
            taskGraphStage.m_accumulationStreamTask
        )
    ;
    // The finalizer is part of accumulation's packet; a split would bypass it.
    const bool avboitAccumulationPacketContainsFinalizer = !hasTransparentRenderers
        || compiledGraph.tasksSharePacket(
            taskGraphStage.m_accumulationTask,
            taskGraphStage.m_accumulationFinalizeTask
        )
    ;
    const Core::GpuPhysicalQueueInfo* const avboitDepthWarpQueue =
        compiledGraph.queueInfoForTask(taskGraphStage.m_depthWarpTask);
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionQueue =
        compiledGraph.queueInfoForTask(taskGraphStage.m_extinctionTask);
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionComputeEmulationQueue =
        taskGraphStage.m_extinctionComputeEmulationTask.valid()
            ? compiledGraph.queueInfoForTask(taskGraphStage.m_extinctionComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionSharedComputeEmulationQueue =
        taskGraphStage.m_extinctionSharedComputeEmulationTaskCount != 0u
            && taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u].valid()
            ? compiledGraph.queueInfoForTask(
                taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitIntegrationQueue =
        compiledGraph.queueInfoForTask(taskGraphStage.m_integrationTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationQueue =
        compiledGraph.queueInfoForTask(taskGraphStage.m_accumulationTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationFinalizeQueue =
        compiledGraph.queueInfoForTask(taskGraphStage.m_accumulationFinalizeTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationComputeEmulationQueue =
        taskGraphStage.m_accumulationComputeEmulationTask.valid()
            ? compiledGraph.queueInfoForTask(taskGraphStage.m_accumulationComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationSharedComputeEmulationQueue =
        taskGraphStage.m_accumulationSharedComputeEmulationTaskCount != 0u
            && taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u].valid()
            ? compiledGraph.queueInfoForTask(
                taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    const bool depthWarpRunsOnGraphics = avboitDepthWarpQueue
        && avboitDepthWarpQueue->queueClass == Core::CommandQueue::Graphics
    ;
    const bool depthWarpRunsOnCompute = avboitDepthWarpQueue
        && avboitDepthWarpQueue->queueClass == Core::CommandQueue::Compute
    ;
    const bool integrationRunsOnGraphics = avboitIntegrationQueue
        && avboitIntegrationQueue->queueClass == Core::CommandQueue::Graphics
    ;
    const bool integrationRunsOnCompute = avboitIntegrationQueue
        && avboitIntegrationQueue->queueClass == Core::CommandQueue::Compute
    ;
    // Keep natural stage order; collapsed stages merge with adjacent Graphics.
    const bool avboitNaturalStagePlacementValid = !hasTransparentRenderers || (
        avboitDepthWarpQueue
        && avboitExtinctionQueue
        && avboitIntegrationQueue
        && avboitAccumulationQueue
        && avboitAccumulationFinalizeQueue
        && (depthWarpRunsOnGraphics || depthWarpRunsOnCompute)
        && avboitExtinctionQueue->queueClass == Core::CommandQueue::Graphics
        && (integrationRunsOnGraphics || integrationRunsOnCompute)
        && avboitAccumulationQueue->queueClass == Core::CommandQueue::Graphics
        && avboitAccumulationFinalizeQueue->queueClass == Core::CommandQueue::Graphics
        && taskBoundaryIsOrdered(taskGraphStage.m_occupancyTask, taskGraphStage.m_depthWarpTask)
        && taskBoundaryIsOrdered(taskGraphStage.m_depthWarpTask, taskGraphStage.m_extinctionTask)
        && taskBoundaryIsOrdered(taskGraphStage.m_extinctionTask, taskGraphStage.m_integrationTask)
        && taskBoundaryIsOrdered(taskGraphStage.m_integrationTask, taskGraphStage.m_accumulationTask)
        && taskBoundaryIsOrdered(taskGraphStage.m_accumulationTask, taskGraphStage.m_accumulationFinalizeTask)
        && (!depthWarpRunsOnGraphics || (
            compiledGraph.tasksSharePacket(taskGraphStage.m_occupancyTask, taskGraphStage.m_depthWarpTask)
            && compiledGraph.tasksSharePacket(taskGraphStage.m_depthWarpTask, taskGraphStage.m_extinctionTask)
        ))
        && (!integrationRunsOnGraphics || (
            compiledGraph.tasksSharePacket(taskGraphStage.m_extinctionTask, taskGraphStage.m_integrationTask)
            && compiledGraph.tasksSharePacket(taskGraphStage.m_integrationTask, taskGraphStage.m_accumulationTask)
        ))
        && compiledGraph.tasksSharePacket(
            taskGraphStage.m_accumulationTask,
            taskGraphStage.m_accumulationFinalizeTask
        )
    );
    // Require Pre-packet order so the graph owns the output handoff.
    const bool avboitOccupancyComputeEmulationMerged = [&](){
        if(!taskGraphStage.m_occupancyComputeEmulationTask.valid())
            return true;
        if(
            !taskGraphStage.m_occupancyStreamTask.valid()
            || !avboitOccupancyComputeEmulationQueue
            || !avboitOccupancyQueue
            || !avboitPreQueue
            || avboitOccupancyComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitOccupancyQueue->queueClass != Core::CommandQueue::Graphics
            || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
            || !compiledGraph.tasksSharePacket(
                taskGraphStage.m_preTask,
                taskGraphStage.m_occupancyComputeEmulationTask
            )
            || !compiledGraph.tasksSharePacket(
                taskGraphStage.m_occupancyComputeEmulationTask,
                taskGraphStage.m_occupancyTask
            )
            || !compiledGraph.taskPrecedesOrSharesPacket(
                taskGraphStage.m_occupancyStreamTask,
                taskGraphStage.m_occupancyComputeEmulationTask
            )
            || (taskGraphStage.m_clearTask.valid()
                && !compiledGraph.taskPrecedesOrSharesPacket(
                    taskGraphStage.m_clearTask,
                    taskGraphStage.m_occupancyComputeEmulationTask
                ))
        )
            return false;
        const Core::GpuTaskId producerRasterSequence[] = {
            taskGraphStage.m_occupancyComputeEmulationTask,
            taskGraphStage.m_occupancyTask,
        };
        return compiledGraph.tasksFormContiguousPacketSequence(
            producerRasterSequence,
            LengthOf(producerRasterSequence)
        ) && optionalTaskPrecedesInSharedPacket(
            taskGraphStage.m_occupancyStreamTask,
            taskGraphStage.m_occupancyComputeEmulationTask
        ) && optionalTaskPrecedesInSharedPacket(
            taskGraphStage.m_clearTask,
            taskGraphStage.m_occupancyComputeEmulationTask
        );
    }();
    // Shared outputs need contiguous D/R callbacks in Pre's packet.
    const bool avboitOccupancySharedComputeEmulationMerged = [&](){
        const usize phaseCount = taskGraphStage.m_occupancySharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : taskGraphStage.m_occupancySharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(
            !ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount)
            || taskGraphStage.m_occupancyComputeEmulationTask.valid()
            || !taskGraphStage.m_occupancyStreamTask.valid()
            || !taskGraphStage.m_clearTask.valid()
            || !taskIsCompiled(taskGraphStage.m_preTask)
            || !avboitOccupancySharedComputeEmulationQueue
            || !avboitOccupancyQueue
            || !avboitPreQueue
            || avboitOccupancySharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitOccupancyQueue->queueClass != Core::CommandQueue::Graphics
            || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
            || taskGraphStage.m_occupancyTask
                != taskGraphStage.m_occupancySharedComputeEmulationTasks[phaseCount - 1u]
            || !compiledGraph.tasksSharePacket(
                taskGraphStage.m_preTask,
                taskGraphStage.m_occupancySharedComputeEmulationTasks[0u]
            )
            || !compiledGraph.tasksSharePacket(
                taskGraphStage.m_preTask,
                taskGraphStage.m_occupancyStreamTask
            )
            || !compiledGraph.tasksSharePacket(
                taskGraphStage.m_preTask,
                taskGraphStage.m_clearTask
            )
            || !compiledGraph.taskPrecedesOrSharesPacket(
                taskGraphStage.m_occupancyStreamTask,
                taskGraphStage.m_occupancySharedComputeEmulationTasks[0u]
            )
            || !compiledGraph.taskPrecedesOrSharesPacket(
                taskGraphStage.m_clearTask,
                taskGraphStage.m_occupancySharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = taskGraphStage.m_occupancySharedComputeEmulationTasks[phaseIndex];
            if(!task.valid())
                return false;
            const Core::GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfoForTask(task);
            if(
                !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !compiledGraph.tasksSharePacket(
                    taskGraphStage.m_occupancySharedComputeEmulationTasks[0u],
                    task
                )
            )
                return false;
        }
        for(usize phaseIndex = phaseCount;
            phaseIndex < LengthOf(taskGraphStage.m_occupancySharedComputeEmulationTasks);
            ++phaseIndex
        ){
            if(taskGraphStage.m_occupancySharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        return compiledGraph.tasksFormContiguousPacketSequence(
            taskGraphStage.m_occupancySharedComputeEmulationTasks,
            phaseCount
        ) && compiledGraph.taskPrecedesInSamePacket(
            taskGraphStage.m_preTask,
            taskGraphStage.m_occupancyStreamTask
        ) && compiledGraph.taskPrecedesInSamePacket(
            taskGraphStage.m_occupancyStreamTask,
            taskGraphStage.m_clearTask
        ) && compiledGraph.taskPrecedesInSamePacket(
            taskGraphStage.m_clearTask,
            taskGraphStage.m_occupancySharedComputeEmulationTasks[0u]
        );
    }();
    const bool avboitOccupancyAllComputeEmulationMerged =
        avboitOccupancyComputeEmulationMerged
        && avboitOccupancySharedComputeEmulationMerged
    ;
    // Require exact packet order so the graph owns the output handoff.
    const bool avboitExtinctionComputeEmulationMerged = [&](){
        if(!taskGraphStage.m_extinctionComputeEmulationTask.valid())
            return true;
        if(
            !taskGraphStage.m_extinctionStreamTask.valid()
            || !avboitExtinctionComputeEmulationQueue
            || !avboitExtinctionQueue
            || avboitExtinctionComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
            || !compiledGraph.tasksSharePacket(
                taskGraphStage.m_extinctionComputeEmulationTask,
                taskGraphStage.m_extinctionTask
            )
            || !compiledGraph.taskPrecedesOrSharesPacket(
                taskGraphStage.m_extinctionStreamTask,
                taskGraphStage.m_extinctionComputeEmulationTask
            )
        )
            return false;
        const Core::GpuTaskId producerRasterSequence[] = {
            taskGraphStage.m_extinctionComputeEmulationTask,
            taskGraphStage.m_extinctionTask,
        };
        return compiledGraph.tasksFormContiguousPacketSequence(
            producerRasterSequence,
            LengthOf(producerRasterSequence)
        ) && optionalTaskPrecedesInSharedPacket(
            taskGraphStage.m_extinctionStreamTask,
            taskGraphStage.m_extinctionComputeEmulationTask
        );
    }();
    // Shared outputs need a packet-local D/R chain.
    const bool avboitExtinctionSharedComputeEmulationMerged = [&](){
        const usize phaseCount = taskGraphStage.m_extinctionSharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : taskGraphStage.m_extinctionSharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(
            !ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount)
            || taskGraphStage.m_extinctionComputeEmulationTask.valid()
            || !taskGraphStage.m_extinctionStreamTask.valid()
            || !taskIsCompiled(taskGraphStage.m_extinctionTask)
            || !avboitExtinctionSharedComputeEmulationQueue
            || !avboitExtinctionQueue
            || avboitExtinctionSharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
            || taskGraphStage.m_extinctionTask
                != taskGraphStage.m_extinctionSharedComputeEmulationTasks[phaseCount - 1u]
            || !compiledGraph.taskPrecedesOrSharesPacket(
                taskGraphStage.m_extinctionStreamTask,
                taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = taskGraphStage.m_extinctionSharedComputeEmulationTasks[phaseIndex];
            if(!task.valid())
                return false;
            const Core::GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfoForTask(task);
            if(
                !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !compiledGraph.tasksSharePacket(
                    taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u],
                    task
                )
            )
                return false;
        }
        for(usize phaseIndex = phaseCount;
            phaseIndex < LengthOf(taskGraphStage.m_extinctionSharedComputeEmulationTasks);
            ++phaseIndex
        ){
            if(taskGraphStage.m_extinctionSharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        return compiledGraph.tasksFormContiguousPacketSequence(
            taskGraphStage.m_extinctionSharedComputeEmulationTasks,
            phaseCount
        ) && optionalTaskPrecedesInSharedPacket(
            taskGraphStage.m_extinctionStreamTask,
            taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u]
        );
    }();
    const bool avboitExtinctionAllComputeEmulationMerged =
        avboitExtinctionComputeEmulationMerged
        && avboitExtinctionSharedComputeEmulationMerged
    ;
    // Require exact packet order so the graph owns the output handoff.
    const bool avboitAccumulationComputeEmulationMerged = [&](){
        if(!taskGraphStage.m_accumulationComputeEmulationTask.valid())
            return true;
        if(
            !taskGraphStage.m_accumulationStreamTask.valid()
            || !avboitAccumulationComputeEmulationQueue
            || !avboitAccumulationQueue
            || avboitAccumulationComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
            || !compiledGraph.tasksSharePacket(
                taskGraphStage.m_accumulationComputeEmulationTask,
                taskGraphStage.m_accumulationTask
            )
            || !compiledGraph.taskPrecedesOrSharesPacket(
                taskGraphStage.m_accumulationStreamTask,
                taskGraphStage.m_accumulationComputeEmulationTask
            )
        )
            return false;
        const Core::GpuTaskId producerRasterSequence[] = {
            taskGraphStage.m_accumulationComputeEmulationTask,
            taskGraphStage.m_accumulationTask,
        };
        return compiledGraph.tasksFormContiguousPacketSequence(
            producerRasterSequence,
            LengthOf(producerRasterSequence)
        ) && optionalTaskPrecedesInSharedPacket(
            taskGraphStage.m_accumulationStreamTask,
            taskGraphStage.m_accumulationComputeEmulationTask
        );
    }();
    // Shared outputs need packet-local D/R callbacks.
    const bool avboitAccumulationSharedComputeEmulationMerged = [&](){
        const usize phaseCount = taskGraphStage.m_accumulationSharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : taskGraphStage.m_accumulationSharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(
            !ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount)
            || taskGraphStage.m_accumulationComputeEmulationTask.valid()
            || !taskGraphStage.m_accumulationStreamTask.valid()
            || !taskIsCompiled(taskGraphStage.m_accumulationTask)
            || !taskGraphStage.m_accumulationFinalizeTask.valid()
            || !avboitAccumulationSharedComputeEmulationQueue
            || !avboitAccumulationQueue
            || avboitAccumulationSharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
            || taskGraphStage.m_accumulationTask
                != taskGraphStage.m_accumulationSharedComputeEmulationTasks[phaseCount - 1u]
            || !compiledGraph.taskPrecedesOrSharesPacket(
                taskGraphStage.m_accumulationStreamTask,
                taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = taskGraphStage.m_accumulationSharedComputeEmulationTasks[phaseIndex];
            if(!task.valid())
                return false;
            const Core::GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfoForTask(task);
            if(
                !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !compiledGraph.tasksSharePacket(
                    taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u],
                    task
                )
            )
                return false;
        }
        for(usize phaseIndex = phaseCount;
            phaseIndex < LengthOf(taskGraphStage.m_accumulationSharedComputeEmulationTasks);
            ++phaseIndex
        ){
            if(taskGraphStage.m_accumulationSharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        const Core::GpuTaskId finalizerBoundary[] = {
            taskGraphStage.m_accumulationSharedComputeEmulationTasks[phaseCount - 1u],
            taskGraphStage.m_accumulationFinalizeTask,
        };
        return compiledGraph.tasksFormContiguousPacketSequence(
            taskGraphStage.m_accumulationSharedComputeEmulationTasks,
            phaseCount
        ) && compiledGraph.tasksFormContiguousPacketSequence(
            finalizerBoundary,
            LengthOf(finalizerBoundary)
        ) && optionalTaskPrecedesInSharedPacket(
            taskGraphStage.m_accumulationStreamTask,
            taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u]
        );
    }();
    const bool avboitAccumulationAllComputeEmulationMerged =
        avboitAccumulationComputeEmulationMerged
        && avboitAccumulationSharedComputeEmulationMerged
    ;
    const RendererTaskGraphTransparencyStage stage = taskGraphStage.transparencyStage();
    return {
        .m_stage = stage,
        .m_valid =
            compiledGraph.valid()
            && stage.valid()
            && taskGraphStage.m_preTask.valid()
            && taskGraphStage.m_occupancyTask.valid()
            && transparentTaskShapeValid
            && stage.hasTransparentTasks == hasTransparentRenderers
            && taskIsCompiled(stage.firstTask)
            && taskIsCompiled(stage.completionTask)
            && avboitPrePacketContainsClear
            && avboitPrePacketContainsTransparentCsgClear
            && avboitPrePacketContainsCsgReceiverSpan
            && avboitPrePacketContainsCsgIntervalCombine
            && avboitPrePacketContainsOccupancy
            && avboitOccupancyAllComputeEmulationMerged
            && avboitExtinctionPacketContainsStreams
            && avboitExtinctionAllComputeEmulationMerged
            && avboitAccumulationAllComputeEmulationMerged
            && avboitAccumulationPacketContainsStreams
            && avboitAccumulationPacketContainsFinalizer
            && avboitNaturalStagePlacementValid
            && (!hasTransparentRenderers || (
                taskIsCompiled(taskGraphStage.m_depthWarpTask)
                && taskIsCompiled(taskGraphStage.m_extinctionTask)
                && taskIsCompiled(taskGraphStage.m_integrationTask)
                && taskIsCompiled(taskGraphStage.m_accumulationTask)
                && taskIsCompiled(taskGraphStage.m_accumulationFinalizeTask)
            ))
            && avboitPreQueue
            && avboitPreQueue->queueClass == Core::CommandQueue::Graphics
            && avboitOccupancyQueue
            && avboitOccupancyQueue->queueClass == Core::CommandQueue::Graphics
            && compiledGraph.taskPrecedesOrSharesPacket(stage.firstTask, stage.completionTask),
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

