// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_declaration_admission{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_WriterSpinIterations = 64u;

static void WaitForWriterRelease(Atomic<u64>& admissionState, u64& inOutState, const u64 writerBit, const u64 waiterBit)noexcept{
    for(u32 spin = 0u; spin < s_WriterSpinIterations; ++spin){
        MachinePause(1);
        inOutState = admissionState.load(MemoryOrder::acquire);
        if((inOutState & writerBit) == 0u)
            return;
    }
    if((inOutState & waiterBit) == 0u){
        const u64 desired = inOutState | waiterBit;
        if(!admissionState.compare_exchange_weak(inOutState, desired, MemoryOrder::acq_rel, MemoryOrder::acquire))
            return;
        inOutState = desired;
    }
    admissionState.wait(inOutState, MemoryOrder::relaxed);
    inOutState = admissionState.load(MemoryOrder::acquire);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::DeclarationMutationScope::DeclarationMutationScope(GpuTaskGraph& graph)
    : m_graph(graph)
{
    static_assert(Atomic<u64>::is_always_lock_free, "GpuTaskGraph declaration admission atomic must remain lock-free");

    const u32 threadId = CurrentThreadId();
    if(m_graph.m_writerOwnerThread.load(MemoryOrder::relaxed) == threadId){
        ++m_graph.m_writerRecursionDepth;
        m_valid = true;
        return;
    }

    u64 expected = 0u;
    if(m_graph.m_declarationAdmissionState.compare_exchange_strong(expected, GpuTaskGraph::s_AdmissionWriterBit, MemoryOrder::acq_rel, MemoryOrder::acquire)){
        m_graph.m_writerOwnerThread.store(threadId, MemoryOrder::relaxed);
        m_graph.m_writerRecursionDepth = 1u;
        m_valid = true;
        m_isRootWriter = true;
        return;
    }

    u64 state = expected;
    for(;;){
        if((state & GpuTaskGraph::s_AdmissionWriterBit) != 0u){
            __hidden_gpu_task_graph_declaration_admission::WaitForWriterRelease(
                m_graph.m_declarationAdmissionState,
                state,
                GpuTaskGraph::s_AdmissionWriterBit,
                GpuTaskGraph::s_AdmissionWaiterBit
            );
            continue;
        }
        if((state & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionPlanActiveBit | GpuTaskGraph::s_AdmissionReaderMask)) != 0u)
            return;
        if(m_graph.m_declarationAdmissionState.compare_exchange_weak(state, state | GpuTaskGraph::s_AdmissionWriterBit, MemoryOrder::acq_rel, MemoryOrder::acquire)){
            m_graph.m_writerOwnerThread.store(threadId, MemoryOrder::relaxed);
            m_graph.m_writerRecursionDepth = 1u;
            m_valid = true;
            m_isRootWriter = true;
            return;
        }
    }
}

GpuTaskGraph::DeclarationMutationScope::~DeclarationMutationScope()noexcept{
    if(!m_valid)
        return;

    if(!m_isRootWriter){
        if(m_graph.m_writerRecursionDepth == 0u)
            FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration mutation recursion depth must stay balanced");
        --m_graph.m_writerRecursionDepth;
        return;
    }

    m_graph.m_writerRecursionDepth = 0u;
    m_graph.m_writerOwnerThread.store(0u, MemoryOrder::relaxed);
    const u64 previous = m_graph.m_declarationAdmissionState.fetch_and(~(GpuTaskGraph::s_AdmissionWriterBit | GpuTaskGraph::s_AdmissionWaiterBit), MemoryOrder::acq_rel);
    if((previous & GpuTaskGraph::s_AdmissionWriterBit) == 0u)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration mutation admission must retain its exact lifecycle claim");
    if((previous & GpuTaskGraph::s_AdmissionWaiterBit) != 0u)
        m_graph.m_declarationAdmissionState.notify_all();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraphDeclarationReadView GpuTaskGraphDeclarationReadView::TryAcquire(const GpuTaskGraph& graph)noexcept{
    return GpuTaskGraphDeclarationReadView(graph, TryAcquireTag{});
}


GpuTaskGraphDeclarationReadView::GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph){
    if(graph.m_writerOwnerThread.load(MemoryOrder::relaxed) == CurrentThreadId())
        return;

    u64 state = graph.m_declarationAdmissionState.load(MemoryOrder::acquire);
    for(;;){
        if((state & GpuTaskGraph::s_AdmissionTeardownBit) != 0u)
            return;
        if((state & GpuTaskGraph::s_AdmissionWriterBit) != 0u){
            __hidden_gpu_task_graph_declaration_admission::WaitForWriterRelease(
                graph.m_declarationAdmissionState,
                state,
                GpuTaskGraph::s_AdmissionWriterBit,
                GpuTaskGraph::s_AdmissionWaiterBit
            );
            continue;
        }
        if((state & GpuTaskGraph::s_AdmissionReaderMask) == GpuTaskGraph::s_AdmissionReaderMask)
            return;
        if(graph.m_declarationAdmissionState.compare_exchange_weak(state, state + 1ull, MemoryOrder::acq_rel, MemoryOrder::acquire)){
            m_graph = &graph;
            return;
        }
    }
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

