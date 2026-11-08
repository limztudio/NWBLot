// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_system.h"
#include "avboit_compute_emulation_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitTaskGraphStageState::reset()noexcept{
    m_clearFirstTask = {};
    m_clearTask = {};
    m_transparentCsgIntervalClearFirstTask = {};
    m_transparentCsgIntervalClearTask = {};
    m_preTask = {};
    m_csgReceiverSpanTask = {};
    m_csgIntervalCombineTask = {};
    m_occupancyStreamTask = {};
    m_occupancyComputeEmulationTask = {};
    m_occupancyReusedGeometryProducer = {};
    for(Core::GpuTaskId& task : m_occupancySharedComputeEmulationTasks)
        task = {};
    m_occupancySharedComputeEmulationTaskCount = 0u;
    m_occupancyTask = {};
    m_depthWarpTask = {};
    m_extinctionStreamTask = {};
    m_extinctionComputeEmulationTask = {};
    m_extinctionReusedGeometryProducer = {};
    for(Core::GpuTaskId& task : m_extinctionSharedComputeEmulationTasks)
        task = {};
    m_extinctionSharedComputeEmulationTaskCount = 0u;
    m_extinctionTask = {};
    m_integrationTask = {};
    m_accumulationStreamTask = {};
    m_accumulationComputeEmulationTask = {};
    m_accumulationReusedGeometryProducer = {};
    for(Core::GpuTaskId& task : m_accumulationSharedComputeEmulationTasks)
        task = {};
    m_accumulationSharedComputeEmulationTaskCount = 0u;
    m_accumulationTask = {};
    m_accumulationFinalizeTask = {};
}

RendererTaskGraphTransparencyStage RendererAvboitTaskGraphStageState::transparencyStage()const noexcept{
    return RendererTaskGraphTransparencyStage(
        m_preTask,
        m_accumulationFinalizeTask.valid() ? m_accumulationFinalizeTask : m_occupancyTask,
        m_depthWarpTask.valid()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererAvboitTaskGraphValidation RendererAvboitSystem::validateTaskGraphStage(const Core::GpuCompiledGraph::ReadView& compiledGraph, const b8 clearTargets, const b8 hasTransparentRenderers)const noexcept{
    const RendererAvboitTaskGraphStageState& taskGraphStage = m_taskGraphStage;
    const AvboitComputeEmulationValidator computeValidator(compiledGraph);
    const b8 reusedGeometryValid = computeValidator.reusedGeometryIsOrdered(
        taskGraphStage.m_occupancyReusedGeometryProducer, taskGraphStage.m_occupancyTask,
        taskGraphStage.m_occupancyComputeEmulationTask, taskGraphStage.m_occupancySharedComputeEmulationTaskCount
    ) && computeValidator.reusedGeometryIsOrdered(taskGraphStage.m_extinctionReusedGeometryProducer, taskGraphStage.m_extinctionTask, taskGraphStage.m_extinctionComputeEmulationTask, taskGraphStage.m_extinctionSharedComputeEmulationTaskCount) && computeValidator.reusedGeometryIsOrdered(
        taskGraphStage.m_accumulationReusedGeometryProducer, taskGraphStage.m_accumulationTask,
        taskGraphStage.m_accumulationComputeEmulationTask, taskGraphStage.m_accumulationSharedComputeEmulationTaskCount
    );
    const b8 avboitPrePacketContainsClear = !clearTargets || (
        taskGraphStage.m_clearFirstTask.valid()
        && taskGraphStage.m_clearTask.valid()
        && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_clearFirstTask)
        && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_clearTask)
    );
    // Keep CSG clears with AVBOIT Pre for its timing and handoff.
    const b8 avboitPrePacketContainsTransparentCsgClear =
        (!taskGraphStage.m_transparentCsgIntervalClearFirstTask.valid() && !taskGraphStage.m_transparentCsgIntervalClearTask.valid())
        || (
            taskGraphStage.m_transparentCsgIntervalClearFirstTask.valid()
            && taskGraphStage.m_transparentCsgIntervalClearTask.valid()
            && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_transparentCsgIntervalClearFirstTask)
            && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_transparentCsgIntervalClearTask)
        )
    ;
    const b8 avboitPrePacketContainsOccupancy = compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_occupancyTask) && compiledGraph.taskPrecedesInSamePacket(taskGraphStage.m_preTask, taskGraphStage.m_occupancyTask);
    // Span/Combine share Pre's timing; reject a split before recording.
    const b8 avboitPrePacketContainsCsgReceiverSpan = !taskGraphStage.m_csgReceiverSpanTask.valid() || compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_csgReceiverSpanTask);
    const b8 avboitPrePacketContainsCsgIntervalCombine = !taskGraphStage.m_csgIntervalCombineTask.valid() || compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_csgIntervalCombineTask);
    const Core::GpuPhysicalQueueInfo* const avboitPreQueue = compiledGraph.queueInfoForTask(taskGraphStage.m_preTask);
    const Core::GpuPhysicalQueueInfo* const avboitOccupancyQueue = compiledGraph.queueInfoForTask(taskGraphStage.m_occupancyTask);
    const Core::GpuPhysicalQueueInfo* const avboitOccupancyComputeEmulationQueue = taskGraphStage.m_occupancyComputeEmulationTask.valid() ? compiledGraph.queueInfoForTask(taskGraphStage.m_occupancyComputeEmulationTask) : nullptr;
    const Core::GpuPhysicalQueueInfo* const avboitOccupancySharedComputeEmulationQueue =
        taskGraphStage.m_occupancySharedComputeEmulationTaskCount != 0u
            && taskGraphStage.m_occupancySharedComputeEmulationTasks[0u].valid()
            ? compiledGraph.queueInfoForTask(taskGraphStage.m_occupancySharedComputeEmulationTasks[0u])
            : nullptr
    ;
    const b8 hasAllTransparentTasks =
        taskGraphStage.m_depthWarpTask.valid()
        && taskGraphStage.m_extinctionTask.valid()
        && taskGraphStage.m_integrationTask.valid()
        && taskGraphStage.m_accumulationTask.valid()
        && taskGraphStage.m_accumulationFinalizeTask.valid()
    ;
    const b8 hasAnyTransparentTask =
        taskGraphStage.m_depthWarpTask.valid()
        || taskGraphStage.m_extinctionTask.valid()
        || taskGraphStage.m_integrationTask.valid()
        || taskGraphStage.m_accumulationTask.valid()
        || taskGraphStage.m_accumulationFinalizeTask.valid()
    ;
    const b8 transparentTaskShapeValid = hasTransparentRenderers ? hasAllTransparentTasks : !hasAnyTransparentTask;
    const b8 avboitExtinctionPacketContainsStreams = !taskGraphStage.m_extinctionStreamTask.valid() || compiledGraph.tasksSharePacket(taskGraphStage.m_extinctionTask, taskGraphStage.m_extinctionStreamTask);
    const b8 avboitAccumulationPacketContainsStreams = !taskGraphStage.m_accumulationStreamTask.valid() || compiledGraph.tasksSharePacket(taskGraphStage.m_accumulationTask, taskGraphStage.m_accumulationStreamTask);
    // The finalizer is part of accumulation's packet; a split would bypass it.
    const b8 avboitAccumulationPacketContainsFinalizer = !hasTransparentRenderers || compiledGraph.tasksSharePacket(taskGraphStage.m_accumulationTask, taskGraphStage.m_accumulationFinalizeTask);
    const Core::GpuPhysicalQueueInfo* const avboitDepthWarpQueue = compiledGraph.queueInfoForTask(taskGraphStage.m_depthWarpTask);
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionQueue = compiledGraph.queueInfoForTask(taskGraphStage.m_extinctionTask);
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionComputeEmulationQueue = taskGraphStage.m_extinctionComputeEmulationTask.valid() ? compiledGraph.queueInfoForTask(taskGraphStage.m_extinctionComputeEmulationTask) : nullptr;
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionSharedComputeEmulationQueue =
        taskGraphStage.m_extinctionSharedComputeEmulationTaskCount != 0u
            && taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u].valid()
            ? compiledGraph.queueInfoForTask(taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u])
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitIntegrationQueue = compiledGraph.queueInfoForTask(taskGraphStage.m_integrationTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationQueue = compiledGraph.queueInfoForTask(taskGraphStage.m_accumulationTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationFinalizeQueue = compiledGraph.queueInfoForTask(taskGraphStage.m_accumulationFinalizeTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationComputeEmulationQueue = taskGraphStage.m_accumulationComputeEmulationTask.valid() ? compiledGraph.queueInfoForTask(taskGraphStage.m_accumulationComputeEmulationTask) : nullptr;
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationSharedComputeEmulationQueue =
        taskGraphStage.m_accumulationSharedComputeEmulationTaskCount != 0u
            && taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u].valid()
            ? compiledGraph.queueInfoForTask(taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u])
            : nullptr
    ;
    const b8 depthWarpRunsOnGraphics = avboitDepthWarpQueue && avboitDepthWarpQueue->queueClass == Core::CommandQueue::Graphics;
    const b8 depthWarpRunsOnCompute = avboitDepthWarpQueue && avboitDepthWarpQueue->queueClass == Core::CommandQueue::Compute;
    const b8 integrationRunsOnGraphics = avboitIntegrationQueue && avboitIntegrationQueue->queueClass == Core::CommandQueue::Graphics;
    const b8 integrationRunsOnCompute = avboitIntegrationQueue && avboitIntegrationQueue->queueClass == Core::CommandQueue::Compute;
    // Keep natural stage order; collapsed stages merge with adjacent Graphics.
    const b8 avboitNaturalStagePlacementValid = !hasTransparentRenderers || (
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
        && computeValidator.taskBoundaryIsOrdered(taskGraphStage.m_occupancyTask, taskGraphStage.m_depthWarpTask)
        && computeValidator.taskBoundaryIsOrdered(taskGraphStage.m_depthWarpTask, taskGraphStage.m_extinctionTask)
        && computeValidator.taskBoundaryIsOrdered(taskGraphStage.m_extinctionTask, taskGraphStage.m_integrationTask)
        && computeValidator.taskBoundaryIsOrdered(taskGraphStage.m_integrationTask, taskGraphStage.m_accumulationTask)
        && computeValidator.taskBoundaryIsOrdered(taskGraphStage.m_accumulationTask, taskGraphStage.m_accumulationFinalizeTask)
        && (!depthWarpRunsOnGraphics || (compiledGraph.tasksSharePacket(taskGraphStage.m_occupancyTask, taskGraphStage.m_depthWarpTask) && compiledGraph.tasksSharePacket(taskGraphStage.m_depthWarpTask, taskGraphStage.m_extinctionTask)))
        && (!integrationRunsOnGraphics || (compiledGraph.tasksSharePacket(taskGraphStage.m_extinctionTask, taskGraphStage.m_integrationTask) && compiledGraph.tasksSharePacket(taskGraphStage.m_integrationTask, taskGraphStage.m_accumulationTask)))
        && compiledGraph.tasksSharePacket(taskGraphStage.m_accumulationTask, taskGraphStage.m_accumulationFinalizeTask)
    );
    // Require exact packet order so the graph owns the output handoff.
    const b8 avboitOccupancyComputeEmulationMerged =
        computeValidator.computeEmulationMerged(taskGraphStage.m_occupancyComputeEmulationTask, taskGraphStage.m_occupancyTask, taskGraphStage.m_occupancyStreamTask, avboitOccupancyComputeEmulationQueue, avboitOccupancyQueue)
        && (
            !taskGraphStage.m_occupancyComputeEmulationTask.valid()
            || (
                avboitPreQueue
                && avboitPreQueue->queueClass == Core::CommandQueue::Graphics
                && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_occupancyComputeEmulationTask)
                && (!taskGraphStage.m_clearTask.valid() || compiledGraph.taskPrecedesOrSharesPacket(taskGraphStage.m_clearTask, taskGraphStage.m_occupancyComputeEmulationTask))
                && computeValidator.optionalTaskPrecedesInSharedPacket(taskGraphStage.m_clearTask, taskGraphStage.m_occupancyComputeEmulationTask)
            )
        )
    ;
    // Shared outputs need contiguous D/R callbacks in Pre's packet.
    const b8 avboitOccupancySharedComputeEmulationMerged = [&]()noexcept{
        const usize phaseCount = taskGraphStage.m_occupancySharedComputeEmulationTaskCount;
        if(!computeValidator.sharedComputeEmulationChainValid(
            taskGraphStage.m_occupancySharedComputeEmulationTasks,
            phaseCount,
            taskGraphStage.m_occupancyComputeEmulationTask,
            taskGraphStage.m_occupancyTask,
            taskGraphStage.m_occupancyStreamTask,
            avboitOccupancySharedComputeEmulationQueue,
            avboitOccupancyQueue
        ))
            return false;
        if(phaseCount == 0u)
            return true;
        return
            taskGraphStage.m_clearTask.valid()
            && computeValidator.taskIsCompiled(taskGraphStage.m_preTask)
            && avboitPreQueue
            && avboitPreQueue->queueClass == Core::CommandQueue::Graphics
            && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_occupancySharedComputeEmulationTasks[0u])
            && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_occupancyStreamTask)
            && compiledGraph.tasksSharePacket(taskGraphStage.m_preTask, taskGraphStage.m_clearTask)
            && compiledGraph.taskPrecedesOrSharesPacket(taskGraphStage.m_clearTask, taskGraphStage.m_occupancySharedComputeEmulationTasks[0u])
            && compiledGraph.taskPrecedesInSamePacket(taskGraphStage.m_preTask, taskGraphStage.m_occupancyStreamTask)
            && compiledGraph.taskPrecedesInSamePacket(taskGraphStage.m_occupancyStreamTask, taskGraphStage.m_clearTask)
            && compiledGraph.taskPrecedesInSamePacket(taskGraphStage.m_clearTask, taskGraphStage.m_occupancySharedComputeEmulationTasks[0u])
        ;
    }();
    const b8 avboitOccupancyAllComputeEmulationMerged = avboitOccupancyComputeEmulationMerged && avboitOccupancySharedComputeEmulationMerged;
    const b8 avboitExtinctionComputeEmulationMerged = computeValidator.computeEmulationMerged(
        taskGraphStage.m_extinctionComputeEmulationTask,
        taskGraphStage.m_extinctionTask,
        taskGraphStage.m_extinctionStreamTask,
        avboitExtinctionComputeEmulationQueue,
        avboitExtinctionQueue
    );
    // Shared outputs need a packet-local D/R chain.
    const b8 avboitExtinctionSharedComputeEmulationMerged = [&]()noexcept{
        const usize phaseCount = taskGraphStage.m_extinctionSharedComputeEmulationTaskCount;
        if(!computeValidator.sharedComputeEmulationChainValid(
            taskGraphStage.m_extinctionSharedComputeEmulationTasks,
            phaseCount,
            taskGraphStage.m_extinctionComputeEmulationTask,
            taskGraphStage.m_extinctionTask,
            taskGraphStage.m_extinctionStreamTask,
            avboitExtinctionSharedComputeEmulationQueue,
            avboitExtinctionQueue
        ))
            return false;
        return phaseCount == 0u || (computeValidator.taskIsCompiled(taskGraphStage.m_extinctionTask) && computeValidator.optionalTaskPrecedesInSharedPacket(taskGraphStage.m_extinctionStreamTask, taskGraphStage.m_extinctionSharedComputeEmulationTasks[0u]));
    }();
    const b8 avboitExtinctionAllComputeEmulationMerged = avboitExtinctionComputeEmulationMerged && avboitExtinctionSharedComputeEmulationMerged;
    const b8 avboitAccumulationComputeEmulationMerged = computeValidator.computeEmulationMerged(
        taskGraphStage.m_accumulationComputeEmulationTask,
        taskGraphStage.m_accumulationTask,
        taskGraphStage.m_accumulationStreamTask,
        avboitAccumulationComputeEmulationQueue,
        avboitAccumulationQueue
    );
    // Shared outputs need packet-local D/R callbacks.
    const b8 avboitAccumulationSharedComputeEmulationMerged = [&]()noexcept{
        const usize phaseCount = taskGraphStage.m_accumulationSharedComputeEmulationTaskCount;
        if(!computeValidator.sharedComputeEmulationChainValid(
            taskGraphStage.m_accumulationSharedComputeEmulationTasks,
            phaseCount,
            taskGraphStage.m_accumulationComputeEmulationTask,
            taskGraphStage.m_accumulationTask,
            taskGraphStage.m_accumulationStreamTask,
            avboitAccumulationSharedComputeEmulationQueue,
            avboitAccumulationQueue
        ))
            return false;
        if(phaseCount == 0u)
            return true;
        if(!computeValidator.taskIsCompiled(taskGraphStage.m_accumulationTask) || !taskGraphStage.m_accumulationFinalizeTask.valid())
            return false;
        const Array<Core::GpuTaskId, 2> finalizerBoundary = { taskGraphStage.m_accumulationSharedComputeEmulationTasks[phaseCount - 1u], taskGraphStage.m_accumulationFinalizeTask, };
        return compiledGraph.tasksFormContiguousPacketSequence(finalizerBoundary) && computeValidator.optionalTaskPrecedesInSharedPacket(taskGraphStage.m_accumulationStreamTask, taskGraphStage.m_accumulationSharedComputeEmulationTasks[0u]);
    }();
    const b8 avboitAccumulationAllComputeEmulationMerged = avboitAccumulationComputeEmulationMerged && avboitAccumulationSharedComputeEmulationMerged;
    const RendererTaskGraphTransparencyStage stage = taskGraphStage.transparencyStage();
    return {
        .m_stage = stage,
        .m_valid =
            compiledGraph.valid()
            && stage.valid()
            && taskGraphStage.m_preTask.valid()
            && taskGraphStage.m_occupancyTask.valid()
            && transparentTaskShapeValid
            && reusedGeometryValid
            && stage.hasTransparentTasks == hasTransparentRenderers
            && computeValidator.taskIsCompiled(stage.firstTask)
            && computeValidator.taskIsCompiled(stage.completionTask)
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
                computeValidator.taskIsCompiled(taskGraphStage.m_depthWarpTask)
                && computeValidator.taskIsCompiled(taskGraphStage.m_extinctionTask)
                && computeValidator.taskIsCompiled(taskGraphStage.m_integrationTask)
                && computeValidator.taskIsCompiled(taskGraphStage.m_accumulationTask)
                && computeValidator.taskIsCompiled(taskGraphStage.m_accumulationFinalizeTask)
            ))
            && avboitPreQueue
            && avboitPreQueue->queueClass == Core::CommandQueue::Graphics
            && avboitOccupancyQueue
            && avboitOccupancyQueue->queueClass == Core::CommandQueue::Graphics
            && compiledGraph.taskPrecedesOrSharesPacket(stage.firstTask, stage.completionTask),
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererAvboitSystem::appendTaskGraphTimingTickets(
    const RendererAvboitTaskGraphValidation& validation,
    RendererAvboitTaskGraphTimingTickets& timingTickets,
    const Span<Core::GpuTaskGraphTaskTimingTicket> bindings,
    usize& bindingCount
)const{
    if(!validation.valid() || bindingCount > bindings.size())
        return false;

    // Depth warp, extinction, integration, and accumulation tickets.
    usize requiredBindingCount = 1u;
    if(validation.stage().hasTransparentTasks){
        requiredBindingCount += s_AvboitTransparentTimingTicketCount;
        if(m_taskGraphStage.m_extinctionComputeEmulationTask.valid())
            ++requiredBindingCount;
        if(m_taskGraphStage.m_accumulationComputeEmulationTask.valid())
            ++requiredBindingCount;
    }
    if(requiredBindingCount > s_AvboitTaskGraphTimingTicketCapacity || requiredBindingCount > bindings.size() - bindingCount)
        return false;

    const auto appendTimingTicket = [bindings, &bindingCount](const Core::GpuTaskId task, Core::GpuTimingSubmissionTicket& timingTicket)noexcept{
        bindings[bindingCount++] = Core::GpuTaskGraphTaskTimingTicket{ .task = task, .timingTicket = &timingTicket, };
    };

    appendTimingTicket(m_taskGraphStage.m_preTask, timingTickets.m_pre);
    if(validation.stage().hasTransparentTasks){
        appendTimingTicket(m_taskGraphStage.m_depthWarpTask, timingTickets.m_depthWarp);
        if(m_taskGraphStage.m_extinctionComputeEmulationTask.valid()){
            // Shares the one Extinction ticket.
            appendTimingTicket(m_taskGraphStage.m_extinctionComputeEmulationTask, timingTickets.m_extinction);
        }
        appendTimingTicket(m_taskGraphStage.m_extinctionTask, timingTickets.m_extinction);
        appendTimingTicket(m_taskGraphStage.m_integrationTask, timingTickets.m_integration);
        if(m_taskGraphStage.m_accumulationComputeEmulationTask.valid()){
            // Shares the one Accumulation ticket.
            appendTimingTicket(m_taskGraphStage.m_accumulationComputeEmulationTask, timingTickets.m_accumulation);
        }
        appendTimingTicket(m_taskGraphStage.m_accumulationTask, timingTickets.m_accumulation);
    }
    else{
        timingTickets.m_depthWarp.discard();
        timingTickets.m_extinction.discard();
        timingTickets.m_integration.discard();
        timingTickets.m_accumulation.discard();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

