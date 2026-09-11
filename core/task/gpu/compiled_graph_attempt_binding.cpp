// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiled_graph.h"

#include "task_graph.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuCompiledGraph::beginRecordingAttempt(
    const GpuTaskGraph& graph,
    const GpuTaskGraphDeclarationReadView& declarations,
    const GpuSubmissionPacketId packet,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const u64 previousRecordingAttemptGeneration,
    const ReadView& planAccess
)const noexcept{
    if(!packet.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(
        !planAccess.validFor(*this)
        || !declarations.validFor(graph)
        || !planAccess.validFor(declarations)
        || m_planGeneration != expectedPlanGeneration
        || !validPacket(packet)
    )
        return false;

    const bool exactAttempt = m_attemptGraph == &graph
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptRecordingGeneration == recordingAttemptGeneration
    ;
    if(
        m_attemptBindingState == AttemptBindingState::Recording
        || m_attemptBindingState == AttemptBindingState::Submitting
    ){
        if(exactAttempt)
            return true;
        if(
            m_attemptBindingState != AttemptBindingState::Recording
            || previousRecordingAttemptGeneration == 0u
            || m_attemptGraph != &graph
            || m_attemptPlanGeneration != expectedPlanGeneration
            || m_attemptRecordingGeneration != previousRecordingAttemptGeneration
        )
            return false;
    }
    else if(
        m_attemptBindingState != AttemptBindingState::None
        && m_attemptBindingState != AttemptBindingState::Resolved
    )
        return false;

    m_attemptGraph = &graph;
    m_attemptPlanGeneration = expectedPlanGeneration;
    m_attemptRecordingGeneration = recordingAttemptGeneration;
    m_attemptTransactionIdentity = 0u;
    m_attemptTransactionResetGeneration = 0u;
    m_attemptBindingState = AttemptBindingState::Recording;
    return true;
}


bool GpuCompiledGraph::matchesRecordingAttempt(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    return (
            m_attemptBindingState == AttemptBindingState::Recording
            || m_attemptBindingState == AttemptBindingState::Submitting
        )
        && m_attemptGraph == &graph
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptRecordingGeneration == recordingAttemptGeneration
        && m_planGeneration == expectedPlanGeneration
    ;
}


bool GpuCompiledGraph::resolveRecordingAttempt(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(
        m_attemptGraph != &graph
        || m_attemptPlanGeneration != expectedPlanGeneration
        || m_attemptRecordingGeneration != recordingAttemptGeneration
    )
        return false;
    if(m_attemptBindingState == AttemptBindingState::Resolved)
        return true;
    if(m_attemptBindingState != AttemptBindingState::Recording)
        return false;
    m_attemptBindingState = AttemptBindingState::Resolved;
    return true;
}


bool GpuCompiledGraph::bindSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(m_attemptBindingState == AttemptBindingState::Submitting){
        return m_attemptGraph == &graph
            && m_attemptPlanGeneration == expectedPlanGeneration
            && m_attemptRecordingGeneration == recordingAttemptGeneration
            && m_attemptTransactionIdentity == submissionBinding.m_transactionIdentity
            && m_attemptTransactionResetGeneration == submissionBinding.m_resetGeneration
            && m_planGeneration == expectedPlanGeneration
        ;
    }
    if(
        m_attemptBindingState != AttemptBindingState::Recording
        || m_attemptGraph != &graph
        || m_attemptPlanGeneration != expectedPlanGeneration
        || m_attemptRecordingGeneration != recordingAttemptGeneration
    )
        return false;
    if(m_planGeneration != expectedPlanGeneration)
        return false;
    m_attemptTransactionIdentity = submissionBinding.m_transactionIdentity;
    m_attemptTransactionResetGeneration = submissionBinding.m_resetGeneration;
    m_attemptBindingState = AttemptBindingState::Submitting;
    return true;
}


bool GpuCompiledGraph::matchesSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    return m_attemptGraph == &graph
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptBindingState != AttemptBindingState::None
        && m_attemptBindingState != AttemptBindingState::Recording
        && m_attemptRecordingGeneration == recordingAttemptGeneration
        && m_attemptTransactionIdentity == submissionBinding.m_transactionIdentity
        && m_attemptTransactionResetGeneration == submissionBinding.m_resetGeneration
        && m_planGeneration == expectedPlanGeneration
    ;
}


bool GpuCompiledGraph::resolveSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(
        m_attemptGraph != &graph
        || m_attemptPlanGeneration != expectedPlanGeneration
        || m_attemptRecordingGeneration != recordingAttemptGeneration
        || m_attemptTransactionIdentity != submissionBinding.m_transactionIdentity
        || m_attemptTransactionResetGeneration != submissionBinding.m_resetGeneration
    )
        return false;
    if(m_attemptBindingState == AttemptBindingState::Resolved)
        return true;
    if(m_attemptBindingState != AttemptBindingState::Submitting)
        return false;
    m_attemptBindingState = AttemptBindingState::Resolved;
    return true;
}


bool GpuCompiledGraph::matchesActiveAttemptIdentity(
    const GpuTaskGraph* const graphIdentity,
    const u64 compiledObjectIdentity,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const ReadView& planAccess
)const noexcept{
    if(
        !graphIdentity
        || compiledObjectIdentity == 0u
        || expectedPlanGeneration == 0u
        || recordingAttemptGeneration == 0u
    )
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    return planAccess.validFor(*this)
        && m_objectIdentity == compiledObjectIdentity
        && (
            m_attemptBindingState == AttemptBindingState::Recording
            || m_attemptBindingState == AttemptBindingState::Submitting
        )
        && m_attemptGraph == graphIdentity
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptRecordingGeneration == recordingAttemptGeneration
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

