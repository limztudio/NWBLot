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
    Optional<ProfileMeasure> profile;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profile.emplace(*this, CpuTaskProfileKind::HandleJoin, handle);
    {
        ScopedLock lock(m_mutex);
        if(handle.domainIdentity != m_domainIdentity)
            throw RuntimeException("CPU task wait belongs to another scheduler");
        validateWaitLocked(handle, nullptr);
    }
    while(!isComplete(handle)){
        if(executeOne(isExecuting()))
            continue;
        UniqueLock lock(m_mutex);
        m_changed.wait(lock, [this, handle](){
            return !resolveLocked(handle) || hasReadyLocked(currentWorkerAffinity(), isMainThread(), isExecuting());
        });
    }
}

void CpuTaskScheduler::wait(){
    if(isExecuting())
        throw RuntimeException("CPU task cannot wait for its entire scheduler");
    Optional<ProfileMeasure> profile;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profile.emplace(*this, CpuTaskProfileKind::SchedulerJoin);
    for(;;){
        {
            ScopedLock lock(m_mutex);
            if(m_outstanding == 0u)
                return;
        }
        if(executeOne(false))
            continue;
        UniqueLock lock(m_mutex);
        m_changed.wait(lock, [this](){
            return m_outstanding == 0u || hasReadyLocked(CpuAffinity::Any, isMainThread(), false);
        });
    }
}

void CpuTaskScheduler::drain()noexcept{
    {
        ScopedLock lock(m_mutex);
        m_aborting = true;
    }
    notifyProgress();
    wait();
}

void CpuTaskScheduler::drainTask(const TaskHandle handle)noexcept{
    {
        ScopedLock lock(m_mutex);
        m_aborting = true;
    }
    notifyProgress();
    wait(handle);
}

void CpuTaskScheduler::validateWaitLocked(const TaskHandle handle, const CpuTaskScope* const scope){
    if(++m_searchGeneration == 0u){
        for(u64& visit : m_searchVisits)
            visit = 0u;
        ++m_searchGeneration;
    }
    m_searchStack.clear();
    const auto visit = [this](const TaskHandle candidate){
        if(resolveLocked(candidate) && m_searchVisits[candidate.index] != m_searchGeneration){
            m_searchVisits[candidate.index] = m_searchGeneration;
            m_searchStack.push_back(candidate.index);
        }
    };
    for(Execution* current = s_execution; current; current = current->previous){
        if(&current->scheduler == this)
            visit(current->task);
    }
    // Dependent tasks and structured parents cannot complete until the current execution finishes.
    for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
        const u32 index = m_searchStack[cursor];
        const TaskNode& node = m_nodes[index];
        if(
            (scope && node.scope == scope)
            || (index == handle.index && node.generation == handle.generation)
        )
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
    ScopeWait wait{ scope };
    {
        ScopedLock lock(m_mutex);
        wait.identity = ++m_nextScopeWaitIdentity;
        if(wait.identity == 0u)
            TerminateInvariant();
        validateWaitLocked({}, &scope);
    }
    while(scope.m_pending.load(MemoryOrder::acquire) != 0u){
        if(executeOne(isExecuting() || scope.m_allowCallerWork, &wait))
            continue;
        UniqueLock lock(m_mutex);
        m_changed.wait(lock, [this, &scope, &wait](){
            validateWaitLocked({}, &scope);
            return
                scope.m_pending.load(MemoryOrder::acquire) == 0u
                || hasReadyLocked(currentWorkerAffinity(), isMainThread(), isExecuting() || scope.m_allowCallerWork, &wait)
            ;
        });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

