// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 CpuTaskScheduler::findScopeReadyLocked(const usize queue, ScopeWait& wait, u32& previous)noexcept{
    m_scopeSearchActive = true;
    previous = TaskHandle::s_InvalidIndex;
    if(m_slabs.meta(m_ready[queue].head).scope == &wait.scope)
        return m_ready[queue].head;
    if(wait.publicationGeneration != m_scopePublicationGeneration){
        for(TaskHandle& anchor : wait.unrelatedAnchors)
            anchor = {};
        wait.publicationGeneration = m_scopePublicationGeneration;
    }
    TaskHandle& anchor = wait.unrelatedAnchors[queue];
    if(const TaskMeta* const meta = resolveLocked(anchor)){
        if(meta->state == TaskState::Ready && QueueIndex(meta->cost, meta->priority, meta->target) == queue)
            previous = anchor.index;
    }
    u32 index = previous == TaskHandle::s_InvalidIndex ? m_ready[queue].head : m_slabs.meta(previous).next;
    while(index != TaskHandle::s_InvalidIndex && !contributesToScopeLocked(index, wait)){
        previous = index;
        index = m_slabs.meta(index).next;
    }
    anchor = previous == TaskHandle::s_InvalidIndex ? TaskHandle{} : TaskHandle{ m_domainIdentity, previous, m_slabs.meta(previous).generation };
    return index;
}

void CpuTaskScheduler::invalidateScopeSearchLocked(const b8 publication)noexcept{
    if(publication && ++m_scopePublicationGeneration == 0u)
        TerminateInvariant();
    m_scopeSearchGeneration += s_ScopeSearchGenerationStep;
    if(m_scopeSearchGeneration != 0u)
        return;
    for(u64& visit : m_scopeContributionVisits)
        visit = 0u;
    m_scopeSearchGeneration = s_ScopeSearchGenerationStep;
}

b8 CpuTaskScheduler::contributesToScopeLocked(const u32 index, const ScopeWait& wait)noexcept{
    if(m_slabs.meta(index).scope == &wait.scope)
        return true;
    if(m_scopeSearchIdentity != wait.scopeIdentity){
        m_scopeSearchIdentity = wait.scopeIdentity;
        invalidateScopeSearchLocked(false);
    }
    const u64 positiveStamp = m_scopeSearchGeneration + 1u;
    if(m_scopeContributionVisits[index] == positiveStamp)
        return true;
    if(m_scopeContributionVisits[index] == m_scopeSearchGeneration)
        return false;
    const TaskMeta& origin = m_slabs.meta(index);
    if(!origin.hasDependents && !resolveLocked(origin.parent)){
        m_scopeContributionVisits[index] = m_scopeSearchGeneration;
        return false;
    }
    beginLockedSearch();
    m_searchStack.push_back(index);
    m_searchVisits[index] = m_searchGeneration;
    for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
        const u32 current = m_searchStack[cursor];
        const TaskMeta& meta = m_slabs.meta(current);
        if(meta.scope == &wait.scope || m_scopeContributionVisits[current] == positiveStamp){
            m_scopeContributionVisits[current] = positiveStamp;
            const auto positive = [this, positiveStamp](const TaskHandle handle)noexcept{
                return resolveLocked(handle) && m_scopeContributionVisits[handle.index] == positiveStamp;
            };
            for(usize previous = cursor; previous != 0u;){
                const u32 ancestorIndex = m_searchStack[--previous];
                const TaskMeta& ancestor = m_slabs.meta(ancestorIndex);
                if(ancestor.scope == &wait.scope || positive(ancestor.parent)){
                    m_scopeContributionVisits[ancestorIndex] = positiveStamp;
                    continue;
                }
                if(ancestor.hasDependents){
                    for(const TaskHandle dependent : m_slabs.payload(ancestorIndex).dependents){
                        if(positive(dependent)){
                            m_scopeContributionVisits[ancestorIndex] = positiveStamp;
                            break;
                        }
                    }
                }
            }
            FOUNDATION_ASSERT(m_scopeContributionVisits[index] == positiveStamp);
            return true;
        }
        const auto visit = [this](const TaskHandle handle)noexcept{
            if(resolveLocked(handle) && m_searchVisits[handle.index] != m_searchGeneration && m_scopeContributionVisits[handle.index] != m_scopeSearchGeneration){
                m_searchVisits[handle.index] = m_searchGeneration;
                m_searchStack.push_back(handle.index);
            }
        };
        visit(meta.parent);
        if(meta.hasDependents){
            for(const TaskHandle dependent : m_slabs.payload(current).dependents)
                visit(dependent);
        }
    }
    for(const u32 visited : m_searchStack)
        m_scopeContributionVisits[visited] = m_scopeSearchGeneration;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

