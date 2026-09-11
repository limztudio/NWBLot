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


thread_local GpuTaskScheduler::SubmissionAttemptExceptionFinalizer*
    GpuTaskScheduler::SubmissionAttemptExceptionFinalizer::s_activeFinalizer = nullptr
;


GpuTaskScheduler::SubmissionAttemptExceptionFinalizer*
GpuTaskScheduler::SubmissionAttemptExceptionFinalizer::activeFor(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphSubmissionTransaction& transaction
)noexcept{
    for(
        SubmissionAttemptExceptionFinalizer* finalizer = s_activeFinalizer;
        finalizer;
        finalizer = finalizer->m_previousFinalizer
    ){
        if(
            &finalizer->m_graph == &graph
            && &finalizer->m_compiledGraph == &compiledGraph
            && &finalizer->m_recordedGraph == &recordedGraph
            && &finalizer->m_transaction == &transaction
        )
            return finalizer->m_owner;
    }
    return nullptr;
}


GpuTaskScheduler::SubmissionAttemptExceptionFinalizer::SubmissionAttemptExceptionFinalizer(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction
)noexcept
    : m_graph(graph)
    , m_compiledGraph(compiledGraph)
    , m_recordedGraph(recordedGraph)
    , m_transaction(transaction)
    , m_uncaughtExceptionCount(UncaughtExceptionCount())
{
    if(!s_activeFinalizer){
        m_owner = this;
    }
    else{
        m_owner = activeFor(graph, compiledGraph, recordedGraph, transaction);
        if(!m_owner)
            return;
    }
    m_previousFinalizer = s_activeFinalizer;
    s_activeFinalizer = this;
    m_installed = true;
}
GpuTaskScheduler::SubmissionAttemptExceptionFinalizer::~SubmissionAttemptExceptionFinalizer()noexcept{
    if(!m_installed)
        return;
    if(s_activeFinalizer != this)
        TerminateInvariant();
    s_activeFinalizer = m_previousFinalizer;
    if(m_owner != this || !m_armed || UncaughtExceptionCount() <= m_uncaughtExceptionCount)
        return;

    if(m_transaction.submissionExceptionClosingResolved(
        m_compiledGraph,
        m_recordingAttemptGeneration,
        m_submissionBinding
    ))
        return;
    if(!m_graph.waitForSubmissionExceptionRecordingClaims(
        m_compiledGraph,
        m_recordingAttemptGeneration,
        m_submissionBinding
    )){
        if(m_transaction.submissionExceptionClosingResolved(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        ))
            return;
        TerminateInvariant();
    }
    if(m_transaction.submissionExceptionClosingResolved(
        m_compiledGraph,
        m_recordingAttemptGeneration,
        m_submissionBinding
    ))
        return;

    GpuRecordedGraph::ArtifactOperation artifactOperation(
        m_recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::WaitRead
    );
    if(!artifactOperation.valid())
        TerminateInvariant();
    if(m_transaction.submissionExceptionClosingResolved(
        m_compiledGraph,
        m_recordingAttemptGeneration,
        m_submissionBinding
    ))
        return;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(
        m_transaction,
        GpuGraphSubmissionTransaction::SubmissionOperationMode::ExceptionFinalizer,
        &artifactOperation
    );
    if(!submissionOperation.valid()){
        if(m_transaction.submissionExceptionClosingResolved(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        ))
            return;
        TerminateInvariant();
    }
    if(m_transaction.submissionExceptionClosingResolved(
        m_compiledGraph,
        m_recordingAttemptGeneration,
        m_submissionBinding
    ))
        return;
    GpuCompiledGraph::ReadView planAccess(m_compiledGraph);
    if(!planAccess.valid()){
        if(m_transaction.submissionExceptionClosingResolved(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        ))
            return;
        TerminateInvariant();
    }
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(m_graph);
    if(!planAccess.validFor(declarationAccess)){
        if(m_transaction.submissionExceptionClosingResolved(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        ))
            return;
        TerminateInvariant();
    }

    // This exact transaction writer prevents another admitted packet from resolving the binding. The graph remains
    // ExceptionClosing, so graph reset and declaration mutation both reject until callback-free finalization below.
    // Accepted-frontier rejection can bind and close the graph before publishing a recorded artifact; in that case
    // there are no exact-attempt artifact timing tickets to abandon, but the transaction still needs terminalization.
    const bool artifactMatchesAttempt = m_recordedGraph.validForWithinArtifactOperation(
        m_graph,
        declarationAccess,
        m_compiledGraph,
        planAccess,
        artifactOperation
    );
    if(artifactMatchesAttempt){
        for(usize packetIndex = 0u; packetIndex < planAccess.packetCount(); ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            if(m_transaction.packetToken(packet).valid())
                continue;
            GpuTimingSubmissionTicket* const timingTicket = m_recordedGraph.packetTimingTicket(
                packet,
                artifactOperation
            );
            if(timingTicket)
                timingTicket->abandonWithoutCallbacks();
        }
    }
    m_transaction.completeSubmissionExceptionClosingWithinSubmissionOperation(
        m_graph,
        m_compiledGraph,
        planAccess,
        m_recordingAttemptGeneration,
        m_submissionBinding
    );
}


void GpuTaskScheduler::SubmissionAttemptExceptionFinalizer::beginClosingWithinSubmissionOperation()noexcept{
    SubmissionAttemptExceptionFinalizer* const owner = m_owner;
    if(!owner || owner->m_armed)
        return;

    u64 recordingAttemptGeneration = 0u;
    GpuGraphSubmissionBinding submissionBinding;
    if(!owner->m_transaction.beginSubmissionExceptionClosingWithinSubmissionOperation(
        owner->m_graph,
        owner->m_compiledGraph,
        recordingAttemptGeneration,
        submissionBinding
    )){
        if(owner->m_transaction.hasUnresolvedSubmissionBinding(owner->m_compiledGraph))
            TerminateInvariant();
        return;
    }
    if(recordingAttemptGeneration == 0u || !submissionBinding.valid())
        TerminateInvariant();
    owner->m_recordingAttemptGeneration = recordingAttemptGeneration;
    owner->m_submissionBinding = submissionBinding;
    owner->m_armed = true;
}


GpuTaskScheduler::SubmissionAttemptExceptionScope::SubmissionAttemptExceptionScope(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuSubmissionPacketId* const outFailedPacket
)noexcept
    : m_finalizer(SubmissionAttemptExceptionFinalizer::activeFor(graph, compiledGraph, recordedGraph, transaction))
    , m_outFailedPacket(outFailedPacket)
    , m_uncaughtExceptionCount(UncaughtExceptionCount())
{}
GpuTaskScheduler::SubmissionAttemptExceptionScope::~SubmissionAttemptExceptionScope()noexcept{
    if(!m_active || UncaughtExceptionCount() <= m_uncaughtExceptionCount)
        return;
    if(m_outFailedPacket && m_failedPacket.valid() && !m_outFailedPacket->valid())
        *m_outFailedPacket = m_failedPacket;
    if(!m_finalizer)
        TerminateInvariant();
    m_finalizer->beginClosingWithinSubmissionOperation();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

