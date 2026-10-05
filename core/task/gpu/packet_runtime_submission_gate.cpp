// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime_internal.h"
#include "packet_runtime.h"
#include "scheduler.h"
#include "task_graph.h"

#include <core/common/log.h>
#include <core/graphics/gpu_timing.h>
#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRuntimeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextAcceptanceRevision{ 1u };

u64 AllocateAcceptanceRevision()noexcept{
    u64 nextRevision = s_NextAcceptanceRevision.load(MemoryOrder::relaxed);
    for(;;){
        if(nextRevision == 0u || nextRevision == Limit<u64>::s_Max){
            GLB_FATAL_ASSERT_MSG(false, "GPU task graph acceptance revision identity exhausted");
            TerminateInvariant();
        }
        if(s_NextAcceptanceRevision.compare_exchange_weak(
            nextRevision,
            nextRevision + 1u,
            MemoryOrder::relaxed,
            MemoryOrder::relaxed
        ))
            return nextRevision;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuGraphSubmissionTransaction::SubmissionWriterReservation::~SubmissionWriterReservation()noexcept{
    reset();
}


bool GpuGraphSubmissionTransaction::SubmissionWriterReservation::acquire(const GpuGraphSubmissionTransaction& transaction, const bool tryOnly)noexcept{
    if(m_transaction)
        TerminateInvariant();
    if(tryOnly){
        u32 expectedWriterCount = 0u;
        if(!transaction.m_submissionGateWriterCount.compare_exchange_strong(
            expectedWriterCount,
            1u,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            return false;
    }
    else{
        u32 writerCount = transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire);
        for(;;){
            if(writerCount == Limit<u32>::s_Max){
                GLB_FATAL_ASSERT_MSG(false, "GPU graph submission writer ownership overflowed");
                TerminateInvariant();
            }
            if(transaction.m_submissionGateWriterCount.compare_exchange_weak(
                writerCount,
                writerCount + 1u,
                MemoryOrder::acq_rel,
                MemoryOrder::acquire
            ))
                break;
        }
    }
    m_transaction = &transaction;
    return true;
}

void GpuGraphSubmissionTransaction::SubmissionWriterReservation::reset()noexcept{
    if(!m_transaction)
        return;
    const u32 previousWriterCount = m_transaction->m_submissionGateWriterCount.fetch_sub(1u, MemoryOrder::release);
    if(previousWriterCount == 0u){
        GLB_FATAL_ASSERT_MSG(false, "GPU graph submission writer ownership underflowed");
        TerminateInvariant();
    }
    if(previousWriterCount == 1u)
        m_transaction->m_submissionGateWriterCount.notify_all();
    m_transaction = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_submission_gate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_WriterBit = 1u << 31u;
inline constexpr u32 s_ReaderMask = s_WriterBit - 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuGraphSubmissionTransaction::SubmissionOperation* GpuGraphSubmissionTransaction::SubmissionOperation::s_ActiveOperation = nullptr;


GpuGraphSubmissionTransaction::SubmissionOperation::SubmissionOperation(
    const GpuGraphSubmissionTransaction& transaction,
    const SubmissionOperationMode mode,
    const GpuRecordedGraph::ArtifactOperation* const borrowedArtifact
)noexcept{
    constexpr u32 s_SubmissionGateWriterBit = __hidden_submission_gate::s_WriterBit;
    constexpr u32 s_SubmissionGateReaderMask = __hidden_submission_gate::s_ReaderMask;
    for(const SubmissionOperation* operation = s_ActiveOperation; operation; operation = operation->m_previousOperation){
        if(operation->m_transaction == &transaction)
            return;
    }
    // Cross-transaction reentry is a prompt rejection regardless of target availability.
    // An exception finalizer can therefore run after its local gates unwind without retaining an unrelated transaction gate on this thread.
    if(s_ActiveOperation)
        return;
    if(
        GpuRecordedGraph::ArtifactOperation::Active()
        && (
            !borrowedArtifact
            || !GpuRecordedGraph::ArtifactOperation::ActiveScopeIs(*borrowedArtifact)
        )
    )
        return;
    if(borrowedArtifact && !GpuRecordedGraph::ArtifactOperation::ActiveScopeIs(*borrowedArtifact))
        return;
    if(transaction.m_compositeOperationActive.test(MemoryOrder::acquire))
        return;

    const bool composite = mode == SubmissionOperationMode::CompositeBarrier;
    const bool tryExclusive = mode == SubmissionOperationMode::TryExclusiveBarrier;
    const bool exceptionFinalizer = mode == SubmissionOperationMode::ExceptionFinalizer;
    const bool exceptionClosing = transaction.m_submissionExceptionClosing.test(MemoryOrder::acquire);
    if(exceptionClosing != exceptionFinalizer)
        return;
    const bool exclusive = mode != SubmissionOperationMode::OrdinaryPacket;
    const bool tryWriter = tryExclusive;
    if(exclusive){
        if(!m_writerReservation.acquire(transaction, tryWriter))
            return;

        u32 expectedState = 0u;
        while(!transaction.m_submissionGateState.compare_exchange_strong(
            expectedState,
            s_SubmissionGateWriterBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        )){
            if(tryWriter){
                m_writerReservation.reset();
                return;
            }
            transaction.m_submissionGateState.wait(expectedState, MemoryOrder::acquire);
            expectedState = 0u;
        }
    }
    else{
        for(;;){
            const u32 writerCount = transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire);
            if(writerCount != 0u){
                transaction.m_submissionGateWriterCount.wait(writerCount, MemoryOrder::acquire);
                continue;
            }
            u32 operationState = transaction.m_submissionGateState.load(MemoryOrder::acquire);
            if((operationState & s_SubmissionGateWriterBit) != 0u){
                transaction.m_submissionGateState.wait(operationState, MemoryOrder::acquire);
                continue;
            }
            if((operationState & s_SubmissionGateReaderMask) == s_SubmissionGateReaderMask){
                GLB_FATAL_ASSERT_MSG(false, "GPU graph submission reader ownership overflowed");
                TerminateInvariant();
            }
            if(!transaction.m_submissionGateState.compare_exchange_weak(
                operationState,
                operationState + 1u,
                MemoryOrder::acq_rel,
                MemoryOrder::acquire
            ))
                continue;
            if(transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire) == 0u)
                break;
            const u32 previousState = transaction.m_submissionGateState.fetch_sub(1u, MemoryOrder::release);
            if((previousState & s_SubmissionGateReaderMask) == 1u)
                transaction.m_submissionGateState.notify_all();
        }
    }
    const bool closingAfterAdmission = transaction.m_submissionExceptionClosing.test(MemoryOrder::acquire);
    if(closingAfterAdmission != exceptionFinalizer){
        if(exclusive){
            transaction.m_submissionGateState.store(0u, MemoryOrder::release);
            transaction.m_submissionGateState.notify_all();
            m_writerReservation.reset();
        }
        else{
            const u32 previousState = transaction.m_submissionGateState.fetch_sub(1u, MemoryOrder::release);
            if((previousState & s_SubmissionGateReaderMask) == 0u)
                TerminateInvariant();
            if((previousState & s_SubmissionGateReaderMask) == 1u)
                transaction.m_submissionGateState.notify_all();
        }
        return;
    }
    if(composite && transaction.m_compositeOperationActive.testAndSet(MemoryOrder::acq_rel)){
        if(exclusive){
            transaction.m_submissionGateState.store(0u, MemoryOrder::release);
            transaction.m_submissionGateState.notify_all();
            m_writerReservation.reset();
        }
        return;
    }
    m_transaction = &transaction;
    m_previousOperation = s_ActiveOperation;
    m_exclusive = exclusive;
    m_composite = composite;
    s_ActiveOperation = this;
}

GpuGraphSubmissionTransaction::SubmissionOperation::~SubmissionOperation()noexcept{
    if(!m_transaction)
        return;

    if(s_ActiveOperation != this){
        GLB_FATAL_ASSERT_MSG(false, "GPU graph submission operations must unwind in lexical order");
        TerminateInvariant();
    }
    s_ActiveOperation = m_previousOperation;
    if(m_composite)
        m_transaction->m_compositeOperationActive.clear(MemoryOrder::release);
    if(m_exclusive){
        if(m_transaction->m_submissionGateState.load(MemoryOrder::acquire) != __hidden_submission_gate::s_WriterBit){
            GLB_FATAL_ASSERT_MSG(false, "GPU graph submission writer operation lost its exact gate claim");
            TerminateInvariant();
        }
        m_transaction->m_submissionGateState.store(0u, MemoryOrder::release);
        m_transaction->m_submissionGateState.notify_all();
        m_writerReservation.reset();
    }
    else{
        const u32 previousState = m_transaction->m_submissionGateState.fetch_sub(1u, MemoryOrder::release);
        if((previousState & __hidden_submission_gate::s_ReaderMask) == 0u){
            GLB_FATAL_ASSERT_MSG(false, "GPU graph submission reader ownership underflowed");
            TerminateInvariant();
        }
        if((previousState & __hidden_submission_gate::s_ReaderMask) == 1u)
            m_transaction->m_submissionGateState.notify_all();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

