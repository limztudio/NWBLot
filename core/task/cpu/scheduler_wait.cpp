// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CpuTaskScheduler::wait(const TaskHandle handle){
    if(!handle.valid())
        return;
    if(handle.domainIdentity != m_domainIdentity)
        throw RuntimeException("CPU task wait belongs to another scheduler");
    Optional<ProfileMeasure> profile;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profile.emplace(*this, CpuTaskProfileKind::HandleJoin, handle);
    const usize workerIndex = currentWorkerIndex();
    const CpuAffinity::Enum affinity = currentWorkerAffinity();
    b8 validated = false;
    for(;;){
        ClaimedExecution claimed;
        {
            UniqueLock lock(m_mutex);
            if(!validated){
                validateWaitLocked(handle, nullptr);
                validated = true;
            }
            if(!resolveLocked(handle))
                return;
            const b8 mainThread = isMainThread();
            const b8 executing = isExecuting();
            TaskHandle readyHandle = claimLocked(affinity, mainThread, executing);
            if(!readyHandle.valid()){
                ++m_joinWaiters;
                ++m_handleJoinWaiters;
                ScopeExit unregisterWaiter([this]()noexcept{
                    --m_handleJoinWaiters;
                    --m_joinWaiters;
                });

                m_changed.wait(lock, [this, handle, affinity, mainThread, executing]()noexcept{
                    return !resolveLocked(handle) || hasReadyLocked(affinity, mainThread, executing);
                });
                if(!resolveLocked(handle))
                    return;
                readyHandle = claimLocked(affinity, mainThread, executing);
            }
            if(readyHandle.valid())
                claimed = beginClaimedLocked(readyHandle, workerIndex, affinity, executing);
        }
        executeClaimed(claimed, workerIndex, affinity);
    }
}

void CpuTaskScheduler::wait(){
    if(isExecuting())
        throw RuntimeException("CPU task cannot wait for its entire scheduler");
    Optional<ProfileMeasure> profile;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profile.emplace(*this, CpuTaskProfileKind::SchedulerJoin);
    for(;;){
        ClaimedExecution claimed;
        {
            UniqueLock lock(m_mutex);
            if(m_outstanding == 0u)
                return;
            const b8 mainThread = isMainThread();
            TaskHandle readyHandle = claimLocked(CpuAffinity::Any, mainThread, false);
            if(!readyHandle.valid()){
                ++m_joinWaiters;
                ScopeExit unregisterWaiter([this]()noexcept{ --m_joinWaiters; });

                m_changed.wait(lock, [this, mainThread]()noexcept{
                    return m_outstanding == 0u || hasReadyLocked(CpuAffinity::Any, mainThread, false);
                });
                if(m_outstanding == 0u)
                    return;
                readyHandle = claimLocked(CpuAffinity::Any, mainThread, false);
            }
            if(readyHandle.valid())
                claimed = beginClaimedLocked(readyHandle, 0u, CpuAffinity::Any, false);
        }
        executeClaimed(claimed, 0u, CpuAffinity::Any);
    }
}

void CpuTaskScheduler::drain()noexcept{
    {
        ScopedLock lock(m_mutex);
        m_aborting = true;
        m_abortingAtomic.store(true, MemoryOrder::release);
    }
    notifyProgress();
    wait();
}

void CpuTaskScheduler::drainTask(const TaskHandle handle)noexcept{
    {
        ScopedLock lock(m_mutex);
        m_aborting = true;
        m_abortingAtomic.store(true, MemoryOrder::release);
    }
    notifyProgress();
    wait(handle);
}

void CpuTaskScheduler::validateWaitLocked(const TaskHandle handle, const CpuTaskScope* const scope){
    if(!isExecuting())
        return;
    beginLockedSearch();
    const auto visit = [this](const TaskHandle candidate)noexcept{
        if(resolveLocked(candidate) && m_searchVisits[candidate.index] != m_searchGeneration){
            m_searchVisits[candidate.index] = m_searchGeneration;
            m_searchStack.push_back(candidate.index);
        }
    };
    for(Execution* current = s_Execution; current; current = current->previous){
        if(&current->scheduler == this)
            visit(current->task);
    }
    for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
        const u32 index = m_searchStack[cursor];
        const TaskNode& node = m_nodes[index];
        if((scope && node.scope == scope) || (index == handle.index && node.generation == handle.generation))
            throw RuntimeException("CPU task cannot join work that requires its own completion");
        visit(node.parent);
        for(const TaskHandle dependent : node.dependents)
            visit(dependent);
    }
}

void CpuTaskScheduler::waitScope(CpuTaskScope& scope){
    Optional<ProfileMeasure> profile;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profile.emplace(*this, CpuTaskProfileKind::ScopeJoin, TaskHandle{}, scope.m_profileLabel);
    if(scope.m_pending.load(MemoryOrder::acquire) == 0u)
        return;
    ScopeWait wait{ scope };
    u64 lastValidatedPublicationGeneration = 0u;
    const usize workerIndex = currentWorkerIndex();
    const CpuAffinity::Enum affinity = currentWorkerAffinity();
    b8 initialized = false;
    while(scope.m_pending.load(MemoryOrder::acquire) != 0u){
        ClaimedExecution claimed;
        const b8 cooperative = isExecuting() || scope.m_allowCallerWork;
        {
            UniqueLock lock(m_mutex);
            if(!initialized){
                if(scope.m_searchIdentity == 0u){
                    scope.m_searchIdentity = ++m_nextScopeIdentity;
                    if(scope.m_searchIdentity == 0u)
                        TerminateInvariant();
                }
                wait.scopeIdentity = scope.m_searchIdentity;
                validateWaitLocked({}, &scope);
                lastValidatedPublicationGeneration = m_scopePublicationGeneration;
                initialized = true;
            }
            if(scope.m_pending.load(MemoryOrder::acquire) == 0u)
                return;
            const b8 mainThread = isMainThread();
            TaskHandle readyHandle = claimLocked(affinity, mainThread, cooperative, &wait);
            if(!readyHandle.valid()){
                ++m_joinWaiters;
                ScopeExit unregisterWaiter([this]()noexcept{ --m_joinWaiters; });

                m_changed.wait(lock, [this, &scope, &wait, &lastValidatedPublicationGeneration, affinity, mainThread, cooperative](){
                    if(m_scopePublicationGeneration != lastValidatedPublicationGeneration){
                        validateWaitLocked({}, &scope);
                        lastValidatedPublicationGeneration = m_scopePublicationGeneration;
                    }
                    return scope.m_pending.load(MemoryOrder::acquire) == 0u || hasReadyLocked(affinity, mainThread, cooperative, &wait);
                });
                if(scope.m_pending.load(MemoryOrder::acquire) == 0u)
                    return;
                readyHandle = claimLocked(affinity, mainThread, cooperative, &wait);
            }
            if(readyHandle.valid())
                claimed = beginClaimedLocked(readyHandle, workerIndex, affinity, cooperative);
        }
        executeClaimed(claimed, workerIndex, affinity);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

