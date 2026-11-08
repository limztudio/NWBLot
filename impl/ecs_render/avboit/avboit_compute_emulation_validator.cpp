// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_compute_emulation_validator.h"


NWB_IMPL_BEGIN


b8 AvboitComputeEmulationValidator::optionalTaskPrecedesInSharedPacket(const Core::GpuTaskId optionalTask, const Core::GpuTaskId consumerTask)const noexcept{
    return !optionalTask.valid() || !m_compiledGraph.tasksSharePacket(optionalTask, consumerTask) || m_compiledGraph.taskPrecedesInSamePacket(optionalTask, consumerTask);
}

b8 AvboitComputeEmulationValidator::taskIsCompiled(const Core::GpuTaskId task)const noexcept{
    return m_compiledGraph.findTask(task).valid();
}

b8 AvboitComputeEmulationValidator::taskBoundaryIsOrdered(const Core::GpuTaskId producerTask, const Core::GpuTaskId consumerTask)const noexcept{
    if(!producerTask.valid() || !consumerTask.valid())
        return false;
    if(m_compiledGraph.tasksSharePacket(producerTask, consumerTask))
        return m_compiledGraph.taskPrecedesInSamePacket(producerTask, consumerTask);
    return m_compiledGraph.taskPrecedesOrSharesPacket(producerTask, consumerTask);
}

b8 AvboitComputeEmulationValidator::reusedGeometryIsOrdered(const Core::GpuTaskId source, const Core::GpuTaskId consumer, const Core::GpuTaskId localProducer, const usize sharedPhaseCount)const noexcept{
    return !source.valid() || (!localProducer.valid() && sharedPhaseCount == 0u && source != consumer && taskIsCompiled(source) && taskBoundaryIsOrdered(source, consumer));
}

b8 AvboitComputeEmulationValidator::computeEmulationMerged(const Core::GpuTaskId computeEmulationTask, const Core::GpuTaskId rasterTask, const Core::GpuTaskId streamTask, const Core::GpuPhysicalQueueInfo* const computeEmulationQueue, const Core::GpuPhysicalQueueInfo* const rasterQueue)const noexcept{
    if(!computeEmulationTask.valid())
        return true;
    if(
        !streamTask.valid()
        || !computeEmulationQueue
        || !rasterQueue
        || computeEmulationQueue->queueClass != Core::CommandQueue::Graphics
        || rasterQueue->queueClass != Core::CommandQueue::Graphics
        || !m_compiledGraph.tasksSharePacket(computeEmulationTask, rasterTask)
        || !m_compiledGraph.taskPrecedesOrSharesPacket(streamTask, computeEmulationTask)
    )
        return false;
    const Array<Core::GpuTaskId, 2> producerRasterSequence = { computeEmulationTask, rasterTask, };
    return m_compiledGraph.tasksFormContiguousPacketSequence(producerRasterSequence) && optionalTaskPrecedesInSharedPacket(streamTask, computeEmulationTask);
}

b8 AvboitComputeEmulationValidator::sharedComputeEmulationChainValid(const Span<const Core::GpuTaskId> sharedTasks, const usize phaseCount, const Core::GpuTaskId regularComputeEmulationTask, const Core::GpuTaskId rasterTask, const Core::GpuTaskId streamTask, const Core::GpuPhysicalQueueInfo* const sharedQueue, const Core::GpuPhysicalQueueInfo* const rasterQueue)const noexcept{
    if(phaseCount == 0u){
        for(const Core::GpuTaskId& task : sharedTasks){
            if(task.valid())
                return false;
        }
        return true;
    }
    if(
        phaseCount > sharedTasks.size()
        || !IsSupportedSharedComputeEmulationPhaseCount(phaseCount)
        || regularComputeEmulationTask.valid()
        || !streamTask.valid()
        || !sharedQueue
        || !rasterQueue
        || sharedQueue->queueClass != Core::CommandQueue::Graphics
        || rasterQueue->queueClass != Core::CommandQueue::Graphics
        || rasterTask != sharedTasks[phaseCount - 1u]
        || !m_compiledGraph.taskPrecedesOrSharesPacket(streamTask, sharedTasks[0u])
    )
        return false;
    for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
        const Core::GpuTaskId task = sharedTasks[phaseIndex];
        if(!task.valid())
            return false;
        const Core::GpuPhysicalQueueInfo* const queue = m_compiledGraph.queueInfoForTask(task);
        if(!queue || queue->queueClass != Core::CommandQueue::Graphics || !m_compiledGraph.tasksSharePacket(sharedTasks[0u], task))
            return false;
    }
    for(usize phaseIndex = phaseCount; phaseIndex < sharedTasks.size(); ++phaseIndex){
        if(sharedTasks[phaseIndex].valid())
            return false;
    }
    return m_compiledGraph.tasksFormContiguousPacketSequence(sharedTasks.subspan(0u, phaseCount));
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
