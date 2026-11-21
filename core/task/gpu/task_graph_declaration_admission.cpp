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


#if !defined(_ITERATOR_DEBUG_LEVEL) || (_ITERATOR_DEBUG_LEVEL == 0)
static_assert(sizeof(GpuTaskGraph) == 3968u && alignof(GpuTaskGraph) == 128u, "GpuTaskGraph layout contract");
#endif


void GpuTaskGraph::DeclarationMutationScope::acquireSlow(const u32 threadId, u64 state)noexcept{
    const u64 writerValue = GpuTaskGraph::s_AdmissionWriterBit | static_cast<u64>(threadId);
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
        if((state & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionPlanActiveBit)) != 0u)
            return;
        const b8 dirty = (state & GpuTaskGraph::s_AdmissionReaderStripesDirtyBit) != 0u;
        if(dirty && m_graph.hasAnyActiveDeclarationReader(MemoryOrder::relaxed))
            return;
        const u64 desired = (state & ~GpuTaskGraph::s_AdmissionWriterOwnerMask) | writerValue;
        if(m_graph.m_declarationAdmissionState.compare_exchange_weak(state, desired, MemoryOrder::seq_cst, MemoryOrder::acquire)){
            if(dirty){
                if(m_graph.hasAnyActiveDeclarationReader(MemoryOrder::seq_cst)){
                    const u64 previous = m_graph.m_declarationAdmissionState.fetch_and(~(GpuTaskGraph::s_AdmissionWriterBit | GpuTaskGraph::s_AdmissionWaiterBit | GpuTaskGraph::s_AdmissionWriterOwnerMask), MemoryOrder::acq_rel);
                    if((previous & GpuTaskGraph::s_AdmissionWaiterBit) != 0u)
                        m_graph.m_declarationAdmissionState.notify_all();
                    return;
                }
                m_graph.m_declarationAdmissionState.fetch_and(~GpuTaskGraph::s_AdmissionReaderStripesDirtyBit, MemoryOrder::release);
            }
            m_graph.m_writerRecursionDepth = 1u;
            m_ownerThreadId = threadId;
            m_valid = true;
            m_isRootWriter = true;
            return;
        }
    }
}

void GpuTaskGraph::DeclarationMutationScope::releaseSlow()noexcept{
    const u64 previous = m_graph.m_declarationAdmissionState.fetch_and(~(GpuTaskGraph::s_AdmissionWriterBit | GpuTaskGraph::s_AdmissionWaiterBit | GpuTaskGraph::s_AdmissionReaderStripesDirtyBit | GpuTaskGraph::s_AdmissionWriterOwnerMask), MemoryOrder::acq_rel);
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
    const u32 currentThreadId = GpuTaskGraph::CachedCurrentThreadId();
    u64 state = graph.m_declarationAdmissionState.load(MemoryOrder::acquire);
    if((state & GpuTaskGraph::s_AdmissionWriterBit) != 0u && (state & GpuTaskGraph::s_AdmissionWriterOwnerMask) == static_cast<u64>(currentThreadId))
        return;

    const u8 stripeIndex = GpuTaskGraph::LocalReaderStripeIndex();
    Atomic<u32>& stripeCount = graph.m_readerStripes[stripeIndex].count;
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
        if((state & GpuTaskGraph::s_AdmissionReaderStripesDirtyBit) == 0u){
            if(!graph.m_declarationAdmissionState.compare_exchange_weak(state, state | GpuTaskGraph::s_AdmissionReaderStripesDirtyBit, MemoryOrder::acq_rel, MemoryOrder::acquire))
                continue;
            state |= GpuTaskGraph::s_AdmissionReaderStripesDirtyBit;
        }
        const u32 prev = stripeCount.fetch_add(1u, MemoryOrder::seq_cst);
        if(prev == Limit<u32>::s_Max){
            stripeCount.fetch_sub(1u, MemoryOrder::release);
            return;
        }
        const u64 recheck = graph.m_declarationAdmissionState.load(MemoryOrder::seq_cst);
        if((recheck & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionWriterBit)) == 0u
            && (recheck & GpuTaskGraph::s_AdmissionReaderStripesDirtyBit) != 0u)
        {
            m_graph = &graph;
            m_taskCount = graph.m_publishedTaskCount;
            m_resourceCount = graph.m_publishedResourceCount;
            m_readerStripeIndex = stripeIndex;
            return;
        }
        stripeCount.fetch_sub(1u, MemoryOrder::release);
        state = recheck;
    }
}


GpuTaskGraphDeclarationReadView::~GpuTaskGraphDeclarationReadView()noexcept{
    if(!m_graph)
        return;

    const u32 previous = m_graph->m_readerStripes[m_readerStripeIndex].count.fetch_sub(1u, MemoryOrder::release);
    if(previous == 0u)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration read admission must retain its exact lifecycle claim");
}

GpuTaskGraphDeclarationReadView::GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph, TryAcquireTag)noexcept{
    acquire(graph);
}

void GpuTaskGraphDeclarationReadView::acquire(const GpuTaskGraph& graph)noexcept{
    const u8 stripeIndex = GpuTaskGraph::LocalReaderStripeIndex();
    Atomic<u32>& stripeCount = graph.m_readerStripes[stripeIndex].count;
    u64 state = graph.m_declarationAdmissionState.load(MemoryOrder::acquire);
    for(;;){
        if((state & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionWriterMask)) != 0u)
            return;
        if((state & GpuTaskGraph::s_AdmissionReaderStripesDirtyBit) == 0u){
            if(!graph.m_declarationAdmissionState.compare_exchange_weak(state, state | GpuTaskGraph::s_AdmissionReaderStripesDirtyBit, MemoryOrder::acq_rel, MemoryOrder::acquire))
                continue;
            state |= GpuTaskGraph::s_AdmissionReaderStripesDirtyBit;
        }
        const u32 prev = stripeCount.fetch_add(1u, MemoryOrder::seq_cst);
        if(prev == Limit<u32>::s_Max){
            stripeCount.fetch_sub(1u, MemoryOrder::release);
            return;
        }
        const u64 recheck = graph.m_declarationAdmissionState.load(MemoryOrder::seq_cst);
        if((recheck & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionWriterMask)) == 0u
            && (recheck & GpuTaskGraph::s_AdmissionReaderStripesDirtyBit) != 0u)
        {
            m_graph = &graph;
            m_taskCount = graph.m_publishedTaskCount;
            m_resourceCount = graph.m_publishedResourceCount;
            m_readerStripeIndex = stripeIndex;
            return;
        }
        stripeCount.fetch_sub(1u, MemoryOrder::release);
        if((recheck & (GpuTaskGraph::s_AdmissionTeardownBit | GpuTaskGraph::s_AdmissionWriterMask)) != 0u)
            return;
        state = recheck;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

