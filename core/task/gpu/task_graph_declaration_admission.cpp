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
    static_assert(Atomic<u64>::is_always_lock_free, "GpuTaskGraph declaration admission atomic must remain lock-free");

    u64 state = m_graph.m_declarationAdmissionState.load(MemoryOrder::acquire);
    for(;;){
        if((state & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionPlanActiveBit | GpuTaskGraph::s_AdmissionReaderMask)) != 0u)
            return;
        if((state & GpuTaskGraph::s_AdmissionWriterMask) == GpuTaskGraph::s_AdmissionWriterMask)
            return;
        if(m_graph.m_declarationAdmissionState.compare_exchange_weak(state, state + GpuTaskGraph::s_AdmissionWriterUnit, MemoryOrder::acq_rel, MemoryOrder::acquire)){
            m_valid = true;
            return;
        }
    }
}

GpuTaskGraph::DeclarationMutationScope::~DeclarationMutationScope()noexcept{
    if(!m_valid)
        return;

    const u64 previous = m_graph.m_declarationAdmissionState.fetch_sub(GpuTaskGraph::s_AdmissionWriterUnit, MemoryOrder::acq_rel);
    if((previous & GpuTaskGraph::s_AdmissionWriterMask) == 0u)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration mutation admission must retain its exact lifecycle claim");
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

    const u64 previous = m_graph->m_declarationAdmissionState.fetch_sub(1ull, MemoryOrder::acq_rel);
    if((previous & GpuTaskGraph::s_AdmissionReaderMask) == 0u)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration read admission must retain its exact lifecycle claim");
}

GpuTaskGraphDeclarationReadView::GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph, TryAcquireTag)noexcept{
    acquire(graph);
}

void GpuTaskGraphDeclarationReadView::acquire(const GpuTaskGraph& graph)noexcept{
    u64 state = graph.m_declarationAdmissionState.load(MemoryOrder::acquire);
    for(;;){
        if((state & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionWriterMask)) != 0u)
            return;
        if((state & GpuTaskGraph::s_AdmissionReaderMask) == GpuTaskGraph::s_AdmissionReaderMask)
            return;
        if(graph.m_declarationAdmissionState.compare_exchange_weak(state, state + 1ull, MemoryOrder::acq_rel, MemoryOrder::acquire)){
            m_graph = &graph;
            return;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

