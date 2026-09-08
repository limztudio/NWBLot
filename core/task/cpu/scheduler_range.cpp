// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <global/exception.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CpuTaskScheduler::parallelRange(
    const usize begin,
    const usize end,
    const usize grainSize,
    const CpuTaskOptions options,
    const void* const context,
    const RangeFunction invoke){
    CpuTaskScope chunks(*this);
    chunks.m_allowCallerWork = true;
    const usize count = end - begin;
    const usize grain = Max(grainSize, static_cast<usize>(1u));
    const usize chunkCount = Min(DivideUp(count, grain), Max(static_cast<usize>(m_workerCount), static_cast<usize>(1u)) * s_ChunksPerWorker);
    const usize chunkSize = count / chunkCount;
    const usize remainder = count % chunkCount;
    u32 reservedHead = TaskHandle::s_InvalidIndex;
    u32 reservedTail = TaskHandle::s_InvalidIndex;
    ScopeExit releaseReservations([&]()noexcept{
        while(reservedHead != TaskHandle::s_InvalidIndex){
            TaskHandle handle;
            {
                ScopedLock lock(m_mutex);
                const TaskNode& node = m_nodes[reservedHead];
                handle = { m_domainIdentity, reservedHead, node.generation };
                reservedHead = node.next;
            }
            releaseReservation(handle);
        }
    });

    TaskHandle inlineTask;
    u32 wakeMask;
    {
        ScopedLock lock(m_mutex);
        if(
            m_aborting || options.cost > CpuTaskCost::Light || options.priority > CpuTaskPriority::Background
            || options.target > CpuTaskTarget::MainThread
        )
            throw RuntimeException("CPU task scheduler rejected a parallel range");
        for(usize chunk = 0u; chunk < chunkCount; ++chunk){
            const TaskHandle handle = reserveTaskLocked();
            if(!handle.valid())
                throw RuntimeException("CPU task scheduler rejected a parallel range");
            if(reservedTail != TaskHandle::s_InvalidIndex)
                m_nodes[reservedTail].next = handle.index;
            else
                reservedHead = handle.index;
            reservedTail = handle.index;
            TaskNode& node = m_nodes[handle.index];
            if(node.latestCanceledGeneration != 0u)
                ContainerDetail::ReserveGrowingCapacity(node.olderCanceledGenerations, AddSize(node.olderCanceledGenerations.size(), 1u));
            const usize first = begin + chunk * chunkSize + Min(chunk, remainder);
            const usize last = first + chunkSize + (chunk < remainder ? 1u : 0u);
            // These internal captures are trivial pointers and bounds. No user callable is copied or invoked under the lock.
            node.function = [context, invoke, first, last](){ invoke(context, first, last); };
        }
        const usize outstanding = AddSize(m_outstanding, chunkCount);
        const TaskHandle parentHandle = s_execution && &s_execution->scheduler == this ? s_execution->task : TaskHandle{};
        TaskNode* const parent = resolveLocked(parentHandle);
        const usize children = parent ? AddSize(parent->children, chunkCount) : 0u;
        const bool runInline =
            chunkCount == 1u && (isMainThread() || isExecuting())
            && (options.target != CpuTaskTarget::MainThread || isMainThread())
        ;
        if(parent)
            parent->children = children;
        for(u32 index = reservedHead; index != TaskHandle::s_InvalidIndex;){
            TaskNode& node = m_nodes[index];
            const u32 next = node.next;
            node.scope = &chunks;
            node.options = options;
            node.parent = parent ? parentHandle : TaskHandle{};
            node.dependencies = 0u;
            node.children = 0u;
            node.canceled = parent && parent->canceled;
            node.next = TaskHandle::s_InvalidIndex;
            if(runInline){
                node.state = TaskState::Running;
                inlineTask = { m_domainIdentity, index, node.generation };
                ++m_dispatchCount;
            }
            else
                enqueueLocked(index);
            index = next;
        }
        invalidateScopeSearchLocked();
        chunks.m_pending.fetch_add(chunkCount, MemoryOrder::release);
        m_outstanding = outstanding;
        m_statistics.peakOutstandingTasks = Max(m_statistics.peakOutstandingTasks, m_outstanding);
        wakeMask = workerWakeMaskLocked();
    }
    releaseReservations.release();
    notifyProgress(wakeMask);
    if(inlineTask.valid())
        execute(inlineTask, currentWorkerIndex(), currentWorkerAffinity(), true);
    chunks.wait();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

