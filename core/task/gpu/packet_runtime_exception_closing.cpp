// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


Expected<GpuGraphSubmissionTransaction::SubmissionExceptionClosing> GpuGraphSubmissionTransaction::beginSubmissionExceptionClosingWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph
)noexcept{
    if(!SubmissionOperation::ActiveFor(*this))
        return MakeUnexpected(Failure{});
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return MakeUnexpected(Failure{});

    NothrowScopedLock lock(m_mutex);
    if(
        !validForLocked(planAccess)
        || m_recordingAttemptGeneration == 0u
        || !m_activeSubmissionBinding.valid()
        || m_submissionBindingResolved
    )
        return MakeUnexpected(Failure{});
    if(m_submissionExceptionClosing.test(MemoryOrder::acquire)){
        if(
            m_exceptionClosingRecordingAttemptGeneration != m_recordingAttemptGeneration
            || m_exceptionClosingBinding != m_activeSubmissionBinding
        )
            TerminateInvariant();
        return SubmissionExceptionClosing{ m_exceptionClosingRecordingAttemptGeneration, m_exceptionClosingBinding };
    }

    m_exceptionClosingRecordingAttemptGeneration = m_recordingAttemptGeneration;
    m_exceptionClosingBinding = m_activeSubmissionBinding;
    if(m_submissionExceptionClosing.testAndSet(MemoryOrder::release))
        TerminateInvariant();
    if(!graph.beginSubmissionExceptionClosing(
        compiledGraph,
        m_exceptionClosingRecordingAttemptGeneration,
        m_exceptionClosingBinding
    )){
        m_submissionExceptionClosing.clear(MemoryOrder::release);
        m_submissionExceptionClosing.notifyAll();
        m_exceptionClosingRecordingAttemptGeneration = 0u;
        m_exceptionClosingBinding = {};
        return MakeUnexpected(Failure{});
    }
    return SubmissionExceptionClosing{ m_exceptionClosingRecordingAttemptGeneration, m_exceptionClosingBinding };
}

bool GpuGraphSubmissionTransaction::submissionExceptionClosingResolved(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)noexcept{
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    NothrowScopedLock lock(m_mutex);
    if(
        !m_submissionExceptionClosing.test(MemoryOrder::acquire)
        || m_exceptionClosingRecordingAttemptGeneration != recordingAttemptGeneration
        || m_exceptionClosingBinding != submissionBinding
    )
        return true;
    if(
        m_recordingAttemptGeneration != recordingAttemptGeneration
        || m_activeSubmissionBinding != submissionBinding
    )
        TerminateInvariant();
    if(!m_submissionBindingResolved){
        if(!validForLocked(planAccess))
            TerminateInvariant();
        return false;
    }

    m_exceptionClosingRecordingAttemptGeneration = 0u;
    m_exceptionClosingBinding = {};
    m_submissionExceptionClosing.clear(MemoryOrder::release);
    m_submissionExceptionClosing.notifyAll();
    return true;
}

void GpuGraphSubmissionTransaction::completeSubmissionExceptionClosingWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)noexcept{
    if(!SubmissionOperation::ActiveExclusiveFor(*this))
        TerminateInvariant();
    {
        NothrowScopedLock lock(m_mutex);
        const bool closureValid = m_submissionExceptionClosing.test(MemoryOrder::acquire)
            && planAccess.validFor(compiledGraph)
            && validForLocked(planAccess)
            && m_recordingAttemptGeneration == recordingAttemptGeneration
            && m_activeSubmissionBinding == submissionBinding
            && m_exceptionClosingRecordingAttemptGeneration == recordingAttemptGeneration
            && m_exceptionClosingBinding == submissionBinding
        ;
        if(!closureValid)
            TerminateInvariant();
        if(m_submissionBindingResolved){
            m_exceptionClosingRecordingAttemptGeneration = 0u;
            m_exceptionClosingBinding = {};
            m_submissionExceptionClosing.clear(MemoryOrder::release);
            m_submissionExceptionClosing.notifyAll();
            return;
        }
    }

    abandonUnacceptedPacketsAfterExceptionWithinSubmissionOperation(graph, compiledGraph, planAccess);
    NothrowScopedLock lock(m_mutex);
    if(!m_submissionBindingResolved)
        TerminateInvariant();
    m_exceptionClosingRecordingAttemptGeneration = 0u;
    m_exceptionClosingBinding = {};
    m_submissionExceptionClosing.clear(MemoryOrder::release);
    m_submissionExceptionClosing.notifyAll();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

