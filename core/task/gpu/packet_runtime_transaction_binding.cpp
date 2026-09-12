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


bool GpuGraphSubmissionTransaction::bindRecordingAttemptWithinSubmissionOperation(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuTaskGraph::RecordingAttemptScope* const preparationAttempt
)noexcept{
    if(!SubmissionOperation::activeFor(*this) || recordingAttemptGeneration == 0u)
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;

    NothrowScopedLock lock(m_mutex);
    const GpuGraphSubmissionBinding submissionBinding(m_transactionIdentity, m_resetGeneration);
    if(
        !validForLocked(planAccess)
        || !submissionBinding.valid()
        || (
            m_recordingAttemptGeneration != 0u
            && m_recordingAttemptGeneration != recordingAttemptGeneration
        )
        || (m_activeSubmissionBinding.valid() && m_activeSubmissionBinding != submissionBinding)
        || !graph.bindSubmissionTransaction(
            compiledGraph,
            recordingAttemptGeneration,
            submissionBinding,
            preparationAttempt
        )
    )
        return false;
    if(!m_activeSubmissionBinding.valid())
        m_submissionBindingResolved = false;
    m_recordingAttemptGeneration = recordingAttemptGeneration;
    m_activeSubmissionBinding = submissionBinding;
    m_submissionStatistics.recordingAttemptGeneration = recordingAttemptGeneration;
    return true;
}

bool GpuGraphSubmissionTransaction::matchesRecordingAttemptBinding(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    NothrowScopedLock lock(m_mutex);
    return recordingAttemptGeneration != 0u
        && validForLocked(planAccess)
        && m_recordingAttemptGeneration == recordingAttemptGeneration
        && m_activeSubmissionBinding == submissionBinding
        && graph.matchesSubmissionTransaction(
            compiledGraph,
            recordingAttemptGeneration,
            submissionBinding
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

