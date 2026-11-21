// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <global/range_partition.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, CpuTaskAdmissionError::Enum> CpuTaskScheduler::parallelRange(const usize begin, const usize end, const usize grainSize, const CpuTaskOptions options, const NotNull<const void*> context, const NotNull<RangeFunction> invoke, CpuTaskScope* const callerScope){
    const usize count = end - begin;
    const usize grain = Max(grainSize, static_cast<usize>(1u));
    const usize chunkGroups = DivideUp(count, grain);
    const Execution* const activeExec = s_Execution;
    const b8 inScheduler = activeExec && &activeExec->scheduler == this;
    const b8 mainThread = isMainThread();
    const b8 runFirstInline = (mainThread || inScheduler) && (options.target != CpuTaskTarget::MainThread || mainThread);
    const usize workerIndex = inScheduler ? activeExec->workerIndex : 0u;
    const CpuAffinity::Enum affinity = inScheduler ? activeExec->affinity : CpuAffinity::Any;

    b8 callerScopePending = false;
    ScopeExit releaseCallerScope([&]()noexcept{
        if(!callerScope || !callerScopePending)
            return;
        WorkerWakePlan wakePlan;
        b8 wakeJoiners = false;
        {
            ScopedLock lock(m_mutex);
            const b8 completed = callerScope->m_pending.fetch_sub(1u, MemoryOrder::acq_rel) == 1u;
            takeWorkerWakePlanLocked(wakePlan);
            wakeJoiners = publishJoinWakeLocked(completed);
        }
        notifyProgress(wakePlan, wakeJoiners);
    });

    if(!m_profileEnabled.load(MemoryOrder::relaxed) && options.target != CpuTaskTarget::MainThread && m_workerCount != 0u && runFirstInline && chunkGroups >= 2u){
        const usize workerCapacity = static_cast<usize>(1u) + static_cast<usize>(m_workerCount);
        const usize chunkCount = Min(chunkGroups, static_cast<usize>(m_workerCount) * 2u);
        const usize chunkSize = count / chunkCount;
        const usize remainder = count % chunkCount;
        const usize taskCount = Min(chunkCount, workerCapacity);

        alignas(64) CpuTaskScope chunks(*this, callerScope ? callerScope->m_profileLabel : CpuTaskProfileLabel{});
        chunks.m_allowCallerWork = true;
        chunks.m_parentScope = callerScope;

        ParallelRangeGroup group(context, invoke, begin, chunkCount, chunkSize, remainder);
        group.scope = &chunks;
        group.options = options;

        TaskHandle anchorHandle;
        WorkerWakePlan wakePlan;
        b8 wakeJoiners = false;
        {
            ScopedLock lock(m_mutex);
            if(m_aborting)
                return MakeUnexpected(CpuTaskAdmissionError::Aborting);
            if(options.cost > CpuTaskCost::Light || options.priority > CpuTaskPriority::Background || options.target > CpuTaskTarget::MainThread)
                return MakeUnexpected(CpuTaskAdmissionError::InvalidOptions);
            if(callerScope && isScopeCanceledLocked(callerScope))
                return {};

            const auto reserved = reserveTaskLocked();
            if(!reserved)
                return MakeUnexpected(reserved.error());
            anchorHandle = reserved->handle;
            TaskMeta& meta = reserved->meta;
            TaskPayload& payload = reserved->payload;
            ScopeExit releaseAnchor([&]()noexcept{
                meta.state = TaskState::Free;
                meta.next = m_freeNode;
                m_freeNode = anchorHandle.index;
            });
            if(meta.latestCanceledGeneration != 0u)
                ContainerDetail::ReserveGrowingCapacity(payload.olderCanceledGenerations, AddSize(payload.olderCanceledGenerations.size(), 1u));

            const TaskHandle parentHandle = inScheduler ? activeExec->task : TaskHandle{};
            TaskMeta* const parent = resolveLocked(parentHandle);
            const usize children = parent ? AddSize(parent->children, static_cast<usize>(1u)) : 0u;
            const usize outstanding = AddSize(m_outstanding, static_cast<usize>(1u));
            if(parent){
                parent->children = children;
                s_Execution->spawnedChildren = true;
            }

            meta.scope = &chunks;
            meta.parent = parent ? parentHandle : TaskHandle{};
            meta.dependencies = 0u;
            meta.children = 0u;
            meta.cost = options.cost;
            meta.priority = options.priority;
            meta.target = CpuTaskTarget::Worker;
            meta.canceled = parent && parent->canceled;
            meta.hasHandleWaiter = false;
            meta.hasDependents = false;
            meta.next = TaskHandle::s_InvalidIndex;
            meta.state = TaskState::Running;
            payload.profileLabel = options.profileLabel;
            releaseAnchor.release();

            if(callerScope){
                callerScope->m_pending.fetch_add(1u, MemoryOrder::release);
                callerScopePending = true;
            }
            if(m_scopeSearchActive)
                invalidateScopeSearchLocked(true);
            chunks.m_pending.store(1u, MemoryOrder::release);
            if(m_outstanding == 0u)
                m_outstandingAtomic.store(1u, MemoryOrder::release);
            m_outstanding = outstanding;
            m_statistics.peakOutstandingTasks = Max(m_statistics.peakOutstandingTasks, m_outstanding);
            ++m_dispatchCount;

            if(workerIndex != 0u){
                b8 dummy = false;
                addActiveDepthLocked(workerIndex, affinity, dummy);
            }
            ++m_statistics.cooperativeTasks;
            switch(affinity){
            case CpuAffinity::Performance: ++m_statistics.performanceTasks; break;
            case CpuAffinity::Efficiency: ++m_statistics.efficiencyTasks; break;
            default: ++m_statistics.unclassifiedTasks; break;
            }

            group.anchorHandle = anchorHandle;
            group.remainingHelperClaims = static_cast<u32>(taskCount - 1u);
            group.next = m_activeRangeHead;
            m_activeRangeHead = &group;
            group.linked = true;

            takeRangeWakePlanLocked(group, 2u, wakePlan);
            wakeJoiners = publishJoinWakeLocked(true);
        }
        notifyProgress(wakePlan, wakeJoiners);

        b8 spawnedChildren = false;
        b8 rangeSucceeded = false;
        ScopeExit cleanupInlineRange([&]()noexcept{
            WorkerWakePlan errWakePlan;
            b8 errWakeJoiners = false;
            {
                ScopedLock lock(m_mutex);
                unlinkRangeGroupLocked(group);
                if(!rangeSucceeded){
                    m_aborting = true;
                    m_abortingAtomic.store(true, MemoryOrder::release);
                    if(TaskMeta* const anchorMeta = resolveLocked(anchorHandle))
                        anchorMeta->canceled = true;
                    takeWorkerWakePlanLocked(errWakePlan);
                    errWakeJoiners = publishJoinWakeLocked(true);
                }
                if(workerIndex != 0u)
                    removeActiveDepthLocked(workerIndex, affinity);
            }
            if(!rangeSucceeded)
                notifyProgress(errWakePlan, errWakeJoiners);

            waitRangeHelpers(group);

            TaskHandle parentHandle;
            TaskMeta* parentToRetire = nullptr;
            WorkerWakePlan finishWakePlan;
            b8 finishWakeJoiners = false;
            b8 waitForDescendants = false;
            {
                ScopedLock lock(m_mutex);
                TaskMeta& anchorMeta = m_slabs.meta(anchorHandle.index);
                if(!rangeSucceeded)
                    anchorMeta.canceled = true;
                if(anchorMeta.children == 0u){
                    anchorMeta.state = TaskState::Retiring;
                    b8 madeReady = false;
                    b8 scopeCompleted = false;
                    b8 handleWaited = false;
                    retireNodeLocked(anchorHandle, anchorMeta, parentHandle, parentToRetire, madeReady, scopeCompleted, handleWaited);
                    takeWorkerWakePlanLocked(finishWakePlan);
                    finishWakeJoiners = publishJoinWakeLocked(madeReady || scopeCompleted || m_outstanding == 0u || handleWaited || !rangeSucceeded);
                }
                else{
                    anchorMeta.state = TaskState::Children;
                    waitForDescendants = true;
                    if(!rangeSucceeded){
                        takeWorkerWakePlanLocked(finishWakePlan);
                        finishWakeJoiners = publishJoinWakeLocked(true);
                    }
                }
            }
            notifyProgress(finishWakePlan, finishWakeJoiners);
            if(waitForDescendants)
                chunks.wait();
            else if(parentToRetire)
                retire(parentHandle, *parentToRetire);
        });

        {
            Execution execution(*this, anchorHandle, workerIndex, affinity, &spawnedChildren);
            for(;;){
                if(isRangeCanceled(&chunks))
                    break;
                const usize chunk = group.nextChunk.fetch_add(1u, MemoryOrder::relaxed);
                if(chunk >= chunkCount)
                    break;
                const BalancedPartitionRange range = ComputeBalancedPartitionRange(begin, chunk, chunkSize, remainder);
                invoke.get()(context, range.first, range.last);
            }
        }
        rangeSucceeded = true;
        return {};
    }

    const usize chunkCount = Min(chunkGroups, Max(static_cast<usize>(m_workerCount), static_cast<usize>(1u)) * s_ChunksPerWorker);
    const usize chunkSize = count / chunkCount;
    const usize remainder = count % chunkCount;
    const usize workerCapacity = (options.target == CpuTaskTarget::MainThread || m_workerCount == 0u)
        ? static_cast<usize>(1u)
        : (runFirstInline ? static_cast<usize>(1u) + static_cast<usize>(m_workerCount) : static_cast<usize>(m_workerCount))
    ;
    const usize taskCount = Min(chunkCount, workerCapacity);
    alignas(64) Atomic<usize> nextChunk{ 0u };

    alignas(64) CpuTaskScope chunks(*this, callerScope ? callerScope->m_profileLabel : CpuTaskProfileLabel{});
    chunks.m_allowCallerWork = true;
    chunks.m_parentScope = callerScope;

    ClaimedExecution inlineClaimed;
    WorkerWakePlan wakePlan;
    b8 wakeJoiners = false;
    {
        ScopedLock lock(m_mutex);
        if(m_aborting)
            return MakeUnexpected(CpuTaskAdmissionError::Aborting);
        if(options.cost > CpuTaskCost::Light || options.priority > CpuTaskPriority::Background || options.target > CpuTaskTarget::MainThread)
            return MakeUnexpected(CpuTaskAdmissionError::InvalidOptions);
        if(callerScope && isScopeCanceledLocked(callerScope))
            return {};
        u32 reservedHead = TaskHandle::s_InvalidIndex;
        u32 reservedTail = TaskHandle::s_InvalidIndex;
        ScopeExit releaseReservations([&]()noexcept{
            while(reservedHead != TaskHandle::s_InvalidIndex){
                TaskMeta& meta = m_slabs.meta(reservedHead);
                const u32 next = meta.next;
                meta.state = TaskState::Free;
                meta.next = m_freeNode;
                m_freeNode = reservedHead;
                reservedHead = next;
            }
        });
        for(usize task = 0u; task < taskCount; ++task){
            const auto reserved = reserveTaskLocked();
            if(!reserved)
                return MakeUnexpected(reserved.error());
            const TaskHandle handle = reserved->handle;
            if(reservedTail != TaskHandle::s_InvalidIndex)
                m_slabs.meta(reservedTail).next = handle.index;
            else
                reservedHead = handle.index;
            reservedTail = handle.index;
            TaskMeta& meta = reserved->meta;
            TaskPayload& payload = reserved->payload;
            if(meta.latestCanceledGeneration != 0u)
                ContainerDetail::ReserveGrowingCapacity(payload.olderCanceledGenerations, AddSize(payload.olderCanceledGenerations.size(), 1u));
        }
        const usize outstanding = AddSize(m_outstanding, taskCount);
        const TaskHandle parentHandle = inScheduler ? activeExec->task : TaskHandle{};
        TaskMeta* const parent = resolveLocked(parentHandle);
        const usize children = parent ? AddSize(parent->children, taskCount) : 0u;
        if(parent){
            parent->children = children;
            s_Execution->spawnedChildren = true;
        }
        u32 inlineIndex = TaskHandle::s_InvalidIndex;
        for(u32 index = reservedHead; index != TaskHandle::s_InvalidIndex;){
            TaskMeta& meta = m_slabs.meta(index);
            TaskPayload& payload = m_slabs.payload(index);
            const u32 next = meta.next;
            payload.function = [this, &nextChunk, &chunks, begin, chunkCount, chunkSize, remainder, context, invoke](){
                for(;;){
                    if(isRangeCanceled(&chunks))
                        break;
                    const usize chunk = nextChunk.fetch_add(1u, MemoryOrder::relaxed);
                    if(chunk >= chunkCount)
                        break;
                    const BalancedPartitionRange range = ComputeBalancedPartitionRange(begin, chunk, chunkSize, remainder);
                    invoke.get()(context, range.first, range.last);
                }
            };
            payload.profileLabel = options.profileLabel;
            meta.scope = &chunks;
            meta.parent = parent ? parentHandle : TaskHandle{};
            meta.dependencies = 0u;
            meta.children = 0u;
            meta.cost = options.cost;
            meta.priority = options.priority;
            meta.target = options.target;
            meta.canceled = parent && parent->canceled;
            meta.hasHandleWaiter = false;
            meta.hasDependents = false;
            meta.next = TaskHandle::s_InvalidIndex;
            if(runFirstInline && index == reservedHead){
                meta.state = TaskState::Running;
                if(m_profileEnabled.load(MemoryOrder::relaxed))
                    profileReadyLocked(index);
                inlineIndex = index;
                ++m_dispatchCount;
            }
            else
                enqueueLocked(index, meta);
            index = next;
        }
        releaseReservations.release();
        if(callerScope){
            callerScope->m_pending.fetch_add(1u, MemoryOrder::release);
            callerScopePending = true;
        }
        if(m_scopeSearchActive)
            invalidateScopeSearchLocked(true);
        chunks.m_pending.fetch_add(taskCount, MemoryOrder::release);
        if(m_outstanding == 0u)
            m_outstandingAtomic.store(1u, MemoryOrder::release);
        m_outstanding = outstanding;
        m_statistics.peakOutstandingTasks = Max(m_statistics.peakOutstandingTasks, m_outstanding);
        if(inlineIndex != TaskHandle::s_InvalidIndex)
            beginClaimedLocked(inlineIndex, m_slabs.meta(inlineIndex), workerIndex, affinity, true, false, inlineClaimed);
        takeWorkerWakePlanLocked(wakePlan);
        wakeJoiners = publishJoinWakeLocked(true);
    }
    notifyProgress(wakePlan, wakeJoiners);
    if(inlineClaimed.hasWork())
        executeClaimed(inlineClaimed, workerIndex, affinity);
    chunks.wait();
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

