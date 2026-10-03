// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::ResetCompletionScope::ResetCompletionScope(GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{}

GpuTaskGraph::ResetCompletionScope::~ResetCompletionScope(){
    complete();
}

void GpuTaskGraph::ResetCompletionScope::activateWithinLock()noexcept{
    const bool activationValid = !m_active && m_graph.m_teardownInProgress;
    NWB_FATAL_ASSERT_MSG(activationValid, "GPU task graph reset completion requires fresh teardown ownership");
    if(!activationValid)
        TerminateInvariant();
    m_active = true;
}

void GpuTaskGraph::ResetCompletionScope::complete()noexcept{
    if(!m_active)
        return;
    m_graph.completeResetWithoutCallbacks();
    m_active = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::tryReset(){
    ResetCompletionScope resetCompletion(*this);
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeDeclarationAccessCount != 0u
            || m_activeDiscardNotificationCount != 0u
            || m_activePacketRecordingClaimCount.load(MemoryOrder::acquire) != 0u
            || m_activeRecordingPreparationSerial != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
            || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
        )
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Recording
                || task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
            )
                return false;
        }
        m_teardownInProgress = true;
        resetCompletion.activateWithinLock();
    }

    if(!destroyTaskPayloads())
        return false;
    resetCompletion.complete();
    return true;
}


void GpuTaskGraph::reset(){
    if(tryReset())
        return;

    NWB_FATAL_ASSERT_MSG(false, "GpuTaskGraph::reset requires every bound or in-flight task to resolve first");
    TerminateInvariant();
}

void GpuTaskGraph::completeResetWithoutCallbacks()noexcept{
    const bool taskPayloadsDestroyed = destroyTaskPayloadsWithoutCallbacks();
    NWB_FATAL_ASSERT_MSG(taskPayloadsDestroyed, "GPU task graph reset cleanup requires terminal task payloads");
    if(!taskPayloadsDestroyed)
        TerminateInvariant();

    destroyTaskStateSnapshots();
    destroyResourceStateSnapshots();
    {
        NothrowScopedLock lock(m_lifecycleMutex);
        if(
            m_activeCompiledGraph
            && m_activeRecordingPlanGeneration != 0u
            && m_submissionBindingState == SubmissionBindingState::None
        ){
            const bool recordingAttemptResolved = m_activeCompiledGraph->resolveRecordingAttempt(
                *this,
                m_activeRecordingPlanGeneration,
                m_activeRecordingAttemptGeneration
            );
            NWB_FATAL_ASSERT_MSG(
                recordingAttemptResolved,
                "GPU task graph reset cleanup must release its exact recording-plan lease"
            );
            if(!recordingAttemptResolved)
                TerminateInvariant();
        }
        m_tasks.clear();
        m_dependencies.clear();
        m_normalExecutionPrelude = {};
        m_externalDependencies.clear();
        m_externalStateSources.clear();
        m_resourceUses.clear();
        m_resourceVersionUses.clear();
        if(m_resourceIdentityIndex)
            m_resourceIdentityIndex->clear();
        if(m_resourcePointerIndex)
            m_resourcePointerIndex->clear();
        m_resources.clear();
        m_resourceVersions.clear();
        m_initialOwnerHandoffSources.clear();
        m_queueFamilyIndices.clear();
        if(m_resourceSetIdentityIndex)
            m_resourceSetIdentityIndex->clear();
        m_resourceSets.clear();
        m_resourceSetMembers.clear();
        if(m_pipelineIdentityIndex)
            m_pipelineIdentityIndex->clear();
        if(m_pipelinePointerIndex)
            m_pipelinePointerIndex->clear();
        m_pipelines.clear();
        if(m_externalCompletionIdentityIndex)
            m_externalCompletionIdentityIndex->clear();
        m_externalCompletions.clear();
        m_externalCompletionDeviceGeneration = 0u;
        m_uploadBlobs.clear();
        m_markerText.clear();
        m_presentEndpoint = {};
        m_generation = allocateGeneration();
        m_declarationRevision = allocateGeneration();
        m_activeRecordingAttemptGeneration = allocateGeneration();
        m_activeRecordingPlanGeneration = 0u;
        m_activeRecordingPreparationSerial = 0u;
        NWB_FATAL_ASSERT_MSG(
            m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed) == 0u,
            "GPU task graph reset publication requires every packet recording claim to drain"
        );
        if(m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed) != 0u)
            TerminateInvariant();
        m_activeCompiledGraph = nullptr;
        m_activeSubmissionBinding = {};
        m_submissionBindingState = SubmissionBindingState::None;
        m_activeDiscardNotificationCount = 0u;
        m_hasPresentEndpoint = false;
        m_teardownInProgress = false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

