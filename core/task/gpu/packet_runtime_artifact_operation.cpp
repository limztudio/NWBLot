// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"
#include "packet_runtime_internal.h"
#include "task_graph.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/gpu_timing.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuRecordedGraph::ArtifactOperation* GpuRecordedGraph::ArtifactOperation::s_activeOperation = nullptr;


bool GpuRecordedGraph::ArtifactOperation::activeFor(const GpuRecordedGraph& recordedGraph)noexcept{
    for(const ArtifactOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_recordedGraph == &recordedGraph)
            return true;
    }
    return false;
}

bool GpuRecordedGraph::ArtifactOperation::activeExclusiveFor(const GpuRecordedGraph& recordedGraph)noexcept{
    for(const ArtifactOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_recordedGraph == &recordedGraph)
            return operation->m_exclusive;
    }
    return false;
}


GpuRecordedGraph::ArtifactOperation::ArtifactOperation(
    const GpuRecordedGraph& recordedGraph,
    const ArtifactOperationMode mode
)noexcept{
    const bool exclusive = mode == ArtifactOperationMode::Exclusive;
    const bool waitRead = mode == ArtifactOperationMode::WaitRead;
    for(const ArtifactOperation* operation = s_activeOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_recordedGraph != &recordedGraph)
            continue;
        if(exclusive && !operation->m_exclusive)
            return;

        m_recordedGraph = &recordedGraph;
        m_previousOperation = s_activeOperation;
        m_exclusive = operation->m_exclusive;
        s_activeOperation = this;
        return;
    }

    // Mutating cross-artifact/transaction reentry is rejected even when the target happens to be idle.
    // This keeps blocking cleanup outside every unrelated scheduler gate and removes the symmetric ABBA shape entirely.
    if(s_activeOperation || GpuGraphSubmissionTransaction::SubmissionOperation::active())
        return;
    const bool acquireExclusive = exclusive;
    if(acquireExclusive){
        u32 expectedState = 0u;
        if(!recordedGraph.m_operationState.compare_exchange_strong(
            expectedState,
            GpuRecordedGraph::s_ArtifactOperationWriterBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            return;
    }
    else{
        u32 operationState = recordedGraph.m_operationState.load(MemoryOrder::acquire);
        for(;;){
            if((operationState & GpuRecordedGraph::s_ArtifactOperationWriterBit) != 0u){
                if(!waitRead)
                    return;
                recordedGraph.m_operationState.wait(operationState, MemoryOrder::acquire);
                operationState = recordedGraph.m_operationState.load(MemoryOrder::acquire);
                continue;
            }
            if(
                (operationState & GpuRecordedGraph::s_ArtifactOperationReaderMask)
                == GpuRecordedGraph::s_ArtifactOperationReaderMask
            ){
                GLOBAL_FATAL_ASSERT_MSG(false, "GpuRecordedGraph artifact reader ownership overflowed");
                TerminateInvariant();
            }

            if(recordedGraph.m_operationState.compare_exchange_weak(
                operationState,
                operationState + 1u,
                MemoryOrder::acq_rel,
                MemoryOrder::acquire
            ))
                break;
        }
    }
    m_exclusive = acquireExclusive;
    m_recordedGraph = &recordedGraph;
    m_previousOperation = s_activeOperation;
    m_ownsAdmission = true;
    s_activeOperation = this;
}
GpuRecordedGraph::ArtifactOperation::~ArtifactOperation()noexcept{
    if(!m_recordedGraph)
        return;

    if(s_activeOperation != this){
        GLOBAL_FATAL_ASSERT_MSG(false, "GpuRecordedGraph artifact operations must unwind in lexical order");
        TerminateInvariant();
    }
    s_activeOperation = m_previousOperation;
    if(!m_ownsAdmission)
        return;

    if(m_exclusive){
        if(m_recordedGraph->m_operationState.load(MemoryOrder::relaxed) != GpuRecordedGraph::s_ArtifactOperationWriterBit){
            GLOBAL_FATAL_ASSERT_MSG(false, "GpuRecordedGraph exclusive artifact operation must retain its writer claim");
            TerminateInvariant();
        }
        m_recordedGraph->m_operationState.store(0u, MemoryOrder::release);
        m_recordedGraph->m_operationState.notify_all();
    }
    else{
        u32 operationState = m_recordedGraph->m_operationState.load(MemoryOrder::acquire);
        for(;;){
            if((operationState & GpuRecordedGraph::s_ArtifactOperationReaderMask) == 0u){
                GLOBAL_FATAL_ASSERT_MSG(false, "GpuRecordedGraph shared artifact operation must retain its reader claim");
                TerminateInvariant();
            }
            if(m_recordedGraph->m_operationState.compare_exchange_weak(
                operationState,
                operationState - 1u,
                MemoryOrder::release,
                MemoryOrder::relaxed
            )){
                if((operationState & GpuRecordedGraph::s_ArtifactOperationReaderMask) == 1u)
                    m_recordedGraph->m_operationState.notify_all();
                break;
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

