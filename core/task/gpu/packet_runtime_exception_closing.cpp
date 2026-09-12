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


bool GpuGraphSubmissionTransaction::beginSubmissionExceptionClosingWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    u64& outRecordingAttemptGeneration,
    GpuGraphSubmissionBinding& outSubmissionBinding
)noexcept{
    outRecordingAttemptGeneration = 0u;
    outSubmissionBinding = {};
    if(!SubmissionOperation::activeFor(*this))
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;

    NothrowScopedLock lock(m_mutex);
    if(
        !validForLocked(planAccess)
        || m_recordingAttemptGeneration == 0u
        || !m_activeSubmissionBinding.valid()
        || m_submissionBindingResolved
    )
        return false;
    if(m_submissionExceptionClosing.test(MemoryOrder::acquire)){
        if(
            m_exceptionClosingRecordingAttemptGeneration != m_recordingAttemptGeneration
            || m_exceptionClosingBinding != m_activeSubmissionBinding
        )
            TerminateInvariant();
        outRecordingAttemptGeneration = m_exceptionClosingRecordingAttemptGeneration;
        outSubmissionBinding = m_exceptionClosingBinding;
        return true;
    }

    m_exceptionClosingRecordingAttemptGeneration = m_recordingAttemptGeneration;
    m_exceptionClosingBinding = m_activeSubmissionBinding;
    if(m_submissionExceptionClosing.test_and_set(MemoryOrder::release))
        TerminateInvariant();
    if(!graph.beginSubmissionExceptionClosing(
        compiledGraph,
        m_exceptionClosingRecordingAttemptGeneration,
        m_exceptionClosingBinding
    )){
        m_submissionExceptionClosing.clear(MemoryOrder::release);
        m_submissionExceptionClosing.notify_all();
        m_exceptionClosingRecordingAttemptGeneration = 0u;
        m_exceptionClosingBinding = {};
        return false;
    }
    outRecordingAttemptGeneration = m_exceptionClosingRecordingAttemptGeneration;
    outSubmissionBinding = m_exceptionClosingBinding;
    return true;
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
    m_submissionExceptionClosing.notify_all();
    return true;
}

void GpuGraphSubmissionTransaction::completeSubmissionExceptionClosingWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)noexcept{
    if(!SubmissionOperation::activeExclusiveFor(*this))
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
            m_submissionExceptionClosing.notify_all();
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
    m_submissionExceptionClosing.notify_all();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

