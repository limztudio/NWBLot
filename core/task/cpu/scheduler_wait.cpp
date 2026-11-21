// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CpuTaskScheduler::waitJoinEpoch(const u32 expectedEpoch)noexcept{
    if(!m_profileEnabled.load(MemoryOrder::relaxed)){
        for(u32 spin = 0u; spin < s_JoinSpinIterations; ++spin){
            if(m_joinEpoch.load(MemoryOrder::relaxed) != expectedEpoch){
                if(m_joinEpoch.load(MemoryOrder::acquire) != expectedEpoch)
                    return;
            }
            MachinePause(1);
        }
    }
    while(m_joinEpoch.load(MemoryOrder::acquire) == expectedEpoch)
        m_joinEpoch.wait(expectedEpoch, MemoryOrder::acquire);
}

void CpuTaskScheduler::wait(const TaskHandle handle){
    if(!handle.valid())
        return;
    if(handle.domainIdentity != m_domainIdentity)
        throw RuntimeException("CPU task wait belongs to another scheduler");
    Optional<ProfileMeasure> profile;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profile.emplace(*this, CpuTaskProfileKind::HandleJoin, handle);
    const Execution* const activeExec = s_Execution;
    const b8 executing = activeExec && &activeExec->scheduler == this;
    const usize workerIndex = executing ? activeExec->workerIndex : 0u;
    const CpuAffinity::Enum affinity = executing ? activeExec->affinity : CpuAffinity::Any;
    const b8 mainThread = isMainThread();
    const ClaimPipeline pipeline{
        .preferredScope = nullptr,
        .stopOnCompleteHandle = &handle,
        .stopOnCompleteScope = nullptr,
        .stopWhenNoOutstanding = false,
        .mainThread = mainThread,
        .executing = executing,
        .cooperative = executing,
        .allowPark = false,
    };
    b8 validated = false;
    ClaimedExecution claimed;
    for(;;){
        if(!claimed.hasWork()){
            UniqueLock lock(m_mutex);
            if(!validated){
                validateWaitLocked(handle, nullptr);
                validated = true;
            }
            TaskMeta* awaited = resolveLocked(handle);
            if(!awaited)
                return;
            if(!claimNextLocked(affinity, mainThread, executing, executing, nullptr, workerIndex, claimed)){
                do{
                    const u32 epoch = m_joinEpoch.load(MemoryOrder::relaxed);
                    awaited->hasHandleWaiter = true;
                    ++m_joinWaiters;
                    ScopeExit unregisterWaiter([this]()noexcept{ --m_joinWaiters; });
                    lock.unlock();
                    waitJoinEpoch(epoch);
                    lock.lock();
                    awaited = resolveLocked(handle);
                    if(!awaited)
                        return;
                }while(!claimNextLocked(affinity, mainThread, executing, executing, nullptr, workerIndex, claimed));
            }
        }
        ClaimedExecution nextClaim;
        executeClaimed(claimed, workerIndex, affinity, &nextClaim, &pipeline);
        claimed = Move(nextClaim);
    }
}

void CpuTaskScheduler::wait(){
    if(isExecuting())
        throw RuntimeException("CPU task cannot wait for its entire scheduler");
    Optional<ProfileMeasure> profile;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profile.emplace(*this, CpuTaskProfileKind::SchedulerJoin);
    else if(m_outstandingAtomic.load(MemoryOrder::acquire) == 0u)
        return;

    const b8 mainThread = isMainThread();
    const ClaimPipeline pipeline{
        .preferredScope = nullptr,
        .stopOnCompleteHandle = nullptr,
        .stopOnCompleteScope = nullptr,
        .stopWhenNoOutstanding = true,
        .mainThread = mainThread,
        .executing = false,
        .cooperative = false,
        .allowPark = false,
    };
    ClaimedExecution claimed;
    for(;;){
        if(!claimed.hasWork()){
            UniqueLock lock(m_mutex);
            if(m_outstanding == 0u)
                return;
            if(!claimNextLocked(CpuAffinity::Any, mainThread, false, false, nullptr, 0u, claimed)){
                do{
                    const u32 epoch = m_joinEpoch.load(MemoryOrder::relaxed);
                    ++m_joinWaiters;
                    ScopeExit unregisterWaiter([this]()noexcept{ --m_joinWaiters; });
                    lock.unlock();
                    waitJoinEpoch(epoch);
                    lock.lock();
                    if(m_outstanding == 0u)
                        return;
                }while(!claimNextLocked(CpuAffinity::Any, mainThread, false, false, nullptr, 0u, claimed));
            }
        }
        ClaimedExecution nextClaim;
        executeClaimed(claimed, 0u, CpuAffinity::Any, &nextClaim, &pipeline);
        claimed = Move(nextClaim);
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
        const TaskMeta& meta = m_slabs.meta(index);
        if((scope && meta.scope == scope) || (index == handle.index && meta.generation == handle.generation))
            throw RuntimeException("CPU task cannot join work that requires its own completion");
        visit(meta.parent);
        if(meta.hasDependents){
            for(const TaskHandle dependent : m_slabs.payload(index).dependents)
                visit(dependent);
        }
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
    const Execution* const activeExec = s_Execution;
    const b8 executing = activeExec && &activeExec->scheduler == this;
    const usize workerIndex = executing ? activeExec->workerIndex : 0u;
    const CpuAffinity::Enum affinity = executing ? activeExec->affinity : CpuAffinity::Any;
    const b8 mainThread = isMainThread();
    b8 initialized = false;
    ClaimedExecution claimed;
    while(claimed.hasWork() || scope.m_pending.load(MemoryOrder::acquire) != 0u){
        const b8 cooperative = executing || scope.m_allowCallerWork;
        if(!claimed.hasWork()){
            UniqueLock lock(m_mutex);
            if(!initialized){
                if(scope.m_searchIdentity == 0u){
                    scope.m_searchIdentity = ++m_nextScopeIdentity;
                    if(scope.m_searchIdentity == 0u)
                        TerminateInvariant();
                }
                wait.scopeIdentity = scope.m_searchIdentity;
                m_scopeSearchActive = true;
                validateWaitLocked({}, &scope);
                lastValidatedPublicationGeneration = m_scopePublicationGeneration;
                initialized = true;
            }
            if(scope.m_pending.load(MemoryOrder::acquire) == 0u)
                return;
            if(!claimNextLocked(affinity, mainThread, executing, cooperative, &wait, workerIndex, claimed)){
                do{
                    if(m_scopePublicationGeneration != lastValidatedPublicationGeneration){
                        validateWaitLocked({}, &scope);
                        lastValidatedPublicationGeneration = m_scopePublicationGeneration;
                    }
                    if(scope.m_pending.load(MemoryOrder::acquire) == 0u)
                        return;
                    const u32 epoch = m_joinEpoch.load(MemoryOrder::relaxed);
                    ++m_joinWaiters;
                    ScopeExit unregisterWaiter([this]()noexcept{ --m_joinWaiters; });
                    lock.unlock();
                    waitJoinEpoch(epoch);
                    lock.lock();
                    if(m_scopePublicationGeneration != lastValidatedPublicationGeneration){
                        validateWaitLocked({}, &scope);
                        lastValidatedPublicationGeneration = m_scopePublicationGeneration;
                    }
                    if(scope.m_pending.load(MemoryOrder::acquire) == 0u)
                        return;
                }while(!claimNextLocked(affinity, mainThread, executing, cooperative, &wait, workerIndex, claimed));
            }
        }
        const ClaimPipeline pipeline{
            .preferredScope = &wait,
            .stopOnCompleteHandle = nullptr,
            .stopOnCompleteScope = &scope,
            .stopWhenNoOutstanding = false,
            .mainThread = mainThread,
            .executing = executing,
            .cooperative = cooperative,
            .allowPark = false,
        };
        ClaimedExecution nextClaim;
        executeClaimed(claimed, workerIndex, affinity, &nextClaim, &pipeline);
        claimed = Move(nextClaim);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

