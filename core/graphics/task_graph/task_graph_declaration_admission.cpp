// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::DeclarationMutationScope::DeclarationMutationScope(GpuTaskGraph& graph)
    : m_graph(graph)
    , m_declarationLock(graph.m_declarationMutex)
{
    // Mutation owns the recursive declaration gate before briefly taking lifecycle state. Teardown and recording
    // take only the lifecycle gate and reject a nonzero claim count, so neither path waits back on this gate.
    ScopedLock lock(m_graph.m_lifecycleMutex);
    if(
        m_graph.m_teardownInProgress
        || m_graph.m_activeRecordingPlanGeneration != 0u
        || m_graph.m_activeDeclarationReadCount != 0u
        || m_graph.m_activeDeclarationAccessCount == Limit<u32>::s_Max
    )
        return;

    ++m_graph.m_activeDeclarationAccessCount;
    m_valid = true;
}

GpuTaskGraph::DeclarationMutationScope::~DeclarationMutationScope(){
    if(!m_valid)
        return;

    NothrowScopedLock lock(m_graph.m_lifecycleMutex);
    if(m_graph.m_activeDeclarationAccessCount == 0u){
        NWB_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration mutation admission must retain its exact lifecycle claim");
        TerminateInvariant();
    }
    --m_graph.m_activeDeclarationAccessCount;
}


GpuTaskGraphDeclarationReadView GpuTaskGraphDeclarationReadView::tryAcquire(const GpuTaskGraph& graph)noexcept{
    return GpuTaskGraphDeclarationReadView(graph, TryAcquireTag{});
}


GpuTaskGraphDeclarationReadView::GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph){
    UniqueLock<RecursiveMutex> declarationTurnstile(graph.m_declarationMutex);

    acquire(graph);
}

GpuTaskGraphDeclarationReadView::~GpuTaskGraphDeclarationReadView()noexcept{
    if(!m_graph)
        return;

    NothrowScopedLock lock(m_graph->m_lifecycleMutex);
    if(m_graph->m_activeDeclarationAccessCount == 0u || m_graph->m_activeDeclarationReadCount == 0u){
        NWB_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration read admission must retain its exact lifecycle claim");
        TerminateInvariant();
    }
    --m_graph->m_activeDeclarationAccessCount;
    --m_graph->m_activeDeclarationReadCount;
}


GpuTaskGraphDeclarationReadView::GpuTaskGraphDeclarationReadView(
    const GpuTaskGraph& graph,
    TryAcquireTag
)noexcept{
    acquire(graph);
}

void GpuTaskGraphDeclarationReadView::acquire(const GpuTaskGraph& graph)noexcept{
    // Try admission needs only the lifecycle state. If a mutation owns the declaration turnstile but has not
    // published its lifecycle claim yet, this reader may win safely: the mutation then observes the nonzero read count
    // and rejects before changing declarations. An active mutation has accessCount != readCount and rejects here.
    // Reset takes lifecycle state only and rejects this access claim, so neither path waits back on the turnstile.
    NothrowScopedLock lock(graph.m_lifecycleMutex);
    if(
        graph.m_teardownInProgress
        || graph.m_activeDeclarationAccessCount == Limit<u32>::s_Max
        || graph.m_activeDeclarationReadCount == Limit<u32>::s_Max
        || graph.m_activeDeclarationAccessCount != graph.m_activeDeclarationReadCount
    )
        return;

    ++graph.m_activeDeclarationAccessCount;
    ++graph.m_activeDeclarationReadCount;
    m_graph = &graph;

    // The counters retain the shared read claim after the admission turnstile is released.
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

