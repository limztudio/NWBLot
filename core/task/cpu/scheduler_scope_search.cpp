// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 CpuTaskScheduler::findScopeReadyLocked(const usize queue, ScopeWait& wait, u32& previous)noexcept{
    previous = TaskHandle::s_InvalidIndex;
    if(m_nodes[m_ready[queue].head].scope == &wait.m_scope)
        return m_ready[queue].head;
    if(wait.m_publicationGeneration != m_scopePublicationGeneration){
        for(TaskHandle& anchor : wait.m_unrelatedAnchors)
            anchor = {};
        wait.m_publicationGeneration = m_scopePublicationGeneration;
    }
    TaskHandle& anchor = wait.m_unrelatedAnchors[queue];
    if(const TaskNode* const node = resolveLocked(anchor)){
        if(node->state == TaskState::Ready && queueIndex(node->options) == queue)
            previous = anchor.index;
    }
    // Publication can add paths to the joined scope. Retirement only removes paths, so a
    // still-queued unrelated anchor stays safe until publication, even across other joins.
    u32 index = previous == TaskHandle::s_InvalidIndex ? m_ready[queue].head : m_nodes[previous].next;
    while(index != TaskHandle::s_InvalidIndex && !contributesToScopeLocked(index, wait)){
        previous = index;
        index = m_nodes[index].next;
    }
    anchor = previous == TaskHandle::s_InvalidIndex ? TaskHandle{} : TaskHandle{ m_domainIdentity, previous, m_nodes[previous].generation };
    return index;
}

void CpuTaskScheduler::invalidateScopeSearchLocked(const bool publication)noexcept{
    if(publication && ++m_scopePublicationGeneration == 0u)
        TerminateInvariant();
    m_scopeSearchGeneration += s_ScopeSearchGenerationStep;
    if(m_scopeSearchGeneration != 0u)
        return;
    for(u64& visit : m_scopeContributionVisits)
        visit = 0u;
    m_scopeSearchGeneration = s_ScopeSearchGenerationStep;
}

bool CpuTaskScheduler::contributesToScopeLocked(const u32 index, const ScopeWait& wait)noexcept{
    if(m_nodes[index].scope == &wait.m_scope)
        return true;
    if(m_scopeSearchWaitIdentity != wait.m_identity){
        m_scopeSearchWaitIdentity = wait.m_identity;
        invalidateScopeSearchLocked(false);
    }
    // Downstream completion still waits for this live prerequisite, so retirement cannot invalidate a proven path.
    const u64 positiveStamp = m_scopeSearchGeneration + 1u;
    if(m_scopeContributionVisits[index] == positiveStamp)
        return true;
    if(m_scopeContributionVisits[index] == m_scopeSearchGeneration)
        return false;
    beginLockedSearch();
    m_searchStack.push_back(index);
    m_searchVisits[index] = m_searchGeneration;
    for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
        const u32 current = m_searchStack[cursor];
        const TaskNode& node = m_nodes[current];
        if(node.scope == &wait.m_scope || m_scopeContributionVisits[current] == positiveStamp){
            m_scopeContributionVisits[current] = positiveStamp;
            const auto positive = [this, positiveStamp](const TaskHandle handle){
                return resolveLocked(handle) && m_scopeContributionVisits[handle.index] == positiveStamp;
            };
            // Only the processed prefix can contain discovery ancestors; unprocessed sibling frontiers stay untouched.
            for(usize previous = cursor; previous != 0u;){
                const u32 ancestorIndex = m_searchStack[--previous];
                const TaskNode& ancestor = m_nodes[ancestorIndex];
                if(ancestor.scope == &wait.m_scope || positive(ancestor.parent)){
                    m_scopeContributionVisits[ancestorIndex] = positiveStamp;
                    continue;
                }
                for(const TaskHandle dependent : ancestor.dependents){
                    if(positive(dependent)){
                        m_scopeContributionVisits[ancestorIndex] = positiveStamp;
                        break;
                    }
                }
            }
            NWB_ASSERT(m_scopeContributionVisits[index] == positiveStamp);
            return true;
        }
        const auto visit = [this](const TaskHandle handle){
            if(
                resolveLocked(handle) && m_searchVisits[handle.index] != m_searchGeneration
                && m_scopeContributionVisits[handle.index] != m_scopeSearchGeneration
            ){
                m_searchVisits[handle.index] = m_searchGeneration;
                m_searchStack.push_back(handle.index);
            }
        };
        visit(node.parent);
        for(const TaskHandle dependent : node.dependents)
            visit(dependent);
    }
    // Exhausted searches prove negatives; successful searches mark only paths proved to reach this scope.
    for(const u32 visited : m_searchStack)
        m_scopeContributionVisits[visited] = m_scopeSearchGeneration;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

