// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::DeclarationMutationScope::DeclarationMutationScope(GpuTaskGraph& graph)
    : m_graph(graph)
    , m_declarationLock(graph.m_declarationMutex)
{
    ScopedLock lock(m_graph.m_lifecycleMutex);
    if(m_graph.m_teardownInProgress || m_graph.m_activeRecordingPlanGeneration != 0u || m_graph.m_activeDeclarationReadCount != 0u || m_graph.m_activeDeclarationAccessCount == Limit<u32>::s_Max)
        return;

    ++m_graph.m_activeDeclarationAccessCount;
    m_valid = true;
}

GpuTaskGraph::DeclarationMutationScope::~DeclarationMutationScope()noexcept{
    if(!m_valid)
        return;

    NothrowScopedLock lock(m_graph.m_lifecycleMutex);
    if(m_graph.m_activeDeclarationAccessCount == 0u)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration mutation admission must retain its exact lifecycle claim");
    --m_graph.m_activeDeclarationAccessCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraphDeclarationReadView GpuTaskGraphDeclarationReadView::TryAcquire(const GpuTaskGraph& graph)noexcept{
    return GpuTaskGraphDeclarationReadView(graph, TryAcquireTag{});
}


GpuTaskGraphDeclarationReadView::GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph){
    ScopedLock declarationTurnstile(graph.m_declarationMutex);

    acquire(graph);
}


GpuTaskGraphDeclarationReadView::~GpuTaskGraphDeclarationReadView()noexcept{
    if(!m_graph)
        return;

    NothrowScopedLock lock(m_graph->m_lifecycleMutex);
    if(m_graph->m_activeDeclarationAccessCount == 0u || m_graph->m_activeDeclarationReadCount == 0u)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration read admission must retain its exact lifecycle claim");
    --m_graph->m_activeDeclarationAccessCount;
    --m_graph->m_activeDeclarationReadCount;
}

GpuTaskGraphDeclarationReadView::GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph, TryAcquireTag)noexcept{
    acquire(graph);
}

void GpuTaskGraphDeclarationReadView::acquire(const GpuTaskGraph& graph)noexcept{
    NothrowScopedLock lock(graph.m_lifecycleMutex);
    if(graph.m_teardownInProgress || graph.m_activeDeclarationAccessCount == Limit<u32>::s_Max || graph.m_activeDeclarationReadCount == Limit<u32>::s_Max || graph.m_activeDeclarationAccessCount != graph.m_activeDeclarationReadCount)
        return;

    ++graph.m_activeDeclarationAccessCount;
    ++graph.m_activeDeclarationReadCount;
    m_graph = &graph;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

