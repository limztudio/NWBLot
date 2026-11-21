// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/exception.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::ResetCompletionScope::ResetCompletionScope(GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{}

GpuTaskGraph::ResetCompletionScope::~ResetCompletionScope()noexcept{
    complete();
}

void GpuTaskGraph::ResetCompletionScope::activateWithinLock()noexcept{
    const b8 activationValid = !m_active && m_graph.isTeardownInProgress(MemoryOrder::relaxed);
    FOUNDATION_FATAL_ASSERT_MSG(activationValid, "GPU task graph reset completion requires fresh teardown ownership");
    m_active = true;
}

void GpuTaskGraph::ResetCompletionScope::complete()noexcept{
    if(!m_active)
        return;
    m_graph.completeResetWithoutCallbacks(m_payloadsAlreadyDestroyed);
    m_active = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskGraph::tryReset(){
    ResetCompletionScope resetCompletion(*this);
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_activeDiscardNotificationCount != 0u
            || m_activePacketRecordingClaimCount.load(MemoryOrder::acquire) != 0u
            || m_activeRecordingPreparationSerial != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
            || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
        )
            return false;
        if(!tryBeginTeardownWithinLock())
            return false;
        if(m_hasTaskLifecycleActivity){
            for(const GpuTaskNode& task : m_tasks){
                const TaskLifecycleState::Enum state = task.lifecycleState();
                if(state == TaskLifecycleState::Recording || state == TaskLifecycleState::Submitting || state == TaskLifecycleState::Accepting){
                    m_declarationAdmissionState.fetch_and(~s_AdmissionTeardownBit, MemoryOrder::acq_rel);
                    return false;
                }
            }
        }
        if(!m_hasDiscardCallbacks && !m_hasNonTrivialTaskPayloads){
            if(m_hasTaskPayloads){
                rewindTaskPayloadPool();
                m_hasTaskPayloads = false;
            }
            completeResetWithinLock();
            return true;
        }
        resetCompletion.activateWithinLock();
    }

    if(!m_hasDiscardCallbacks){
        destroyTaskPayloadObjects();
        resetCompletion.markPayloadsDestroyed();
        resetCompletion.complete();
        return true;
    }

    const b8 taskPayloadsDestroyed = UncaughtExceptionCount() != 0 ? destroyTaskPayloadsWithoutCallbacks() : destroyTaskPayloads();
    if(!taskPayloadsDestroyed)
        return false;
    resetCompletion.markPayloadsDestroyed();
    resetCompletion.complete();
    return true;
}


void GpuTaskGraph::reset(){
    if(tryReset())
        return;

    FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph::reset requires every bound or in-flight task to resolve first");
}

void GpuTaskGraph::completeResetWithinLock()noexcept{
    if(!m_externalStateSnapshots.empty())
        destroyTaskStateSnapshots();
    if(!m_initialOwnerHandoffStateSnapshots.empty() || !m_initialOwnerHandoffSources.empty())
        destroyResourceStateSnapshots();

    const u64 activePlanGeneration = m_activeRecordingPlanGeneration.load(MemoryOrder::relaxed);
    const u64 activeAttemptGeneration = m_activeRecordingAttemptGeneration.load(MemoryOrder::relaxed);
    if(m_activeCompiledGraph && activePlanGeneration != 0u && m_submissionBindingState == SubmissionBindingState::None){
        const b8 recordingAttemptResolved = m_activeCompiledGraph->resolveRecordingAttempt(*this, activePlanGeneration, activeAttemptGeneration);
        FOUNDATION_FATAL_ASSERT_MSG(recordingAttemptResolved, "GPU task graph reset cleanup must release its exact recording-plan lease");
    }
    m_tasks.clear();
    m_dependencies.clear();
    m_normalExecutionPrelude = {};
    m_externalDependencies.clear();
    m_externalStateSources.clear();
    m_resourceUses.clear();
    m_resourceVersionUses.clear();
    m_resourceImportIndexes.clearResources();
    m_resources.clear();
    m_resourceVersions.clear();
    m_initialOwnerHandoffSources.clear();
    m_queueFamilyIndices.clear();
    m_resourceImportIndexes.clearResourceSets();
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
    m_uploadBlobStorage.clear();
    m_overalignedUploadBlobs.clear();
    m_uploadBlobStorageByteSize = 0u;
    m_markerText.clear();
    m_presentEndpoint = {};
    m_generation = AllocateGeneration();
    bumpDeclarationRevision();
    m_activeRecordingAttemptGeneration.store(AllocateGeneration(), MemoryOrder::release);
    m_activeRecordingPlanGeneration.store(0u, MemoryOrder::release);
    m_activeRecordingPreparationSerial = 0u;
    FOUNDATION_FATAL_ASSERT_MSG(m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed) == 0u, "GPU task graph reset publication requires every packet recording claim to drain");
    m_activeCompiledGraph = nullptr;
    m_activeSubmissionBinding = {};
    m_submissionBindingState = SubmissionBindingState::None;
    m_activeDiscardNotificationCount = 0u;
    m_hasPresentEndpoint = false;
    m_hasTaskPayloads = false;
    m_hasNonTrivialTaskPayloads = false;
    m_hasDiscardCallbacks = false;
    m_hasAcceptedQueueFrontierOrRecoveryTask = false;
    m_hasTaskLifecycleActivity = false;
    m_declarationAdmissionState.fetch_and(~(s_AdmissionTeardownBit | s_AdmissionPlanActiveBit), MemoryOrder::acq_rel);
}

void GpuTaskGraph::completeResetWithoutCallbacks(const b8 payloadsAlreadyDestroyed)noexcept{
    if(!payloadsAlreadyDestroyed){
        const b8 taskPayloadsDestroyed = destroyTaskPayloadsWithoutCallbacks();
        FOUNDATION_FATAL_ASSERT_MSG(taskPayloadsDestroyed, "GPU task graph reset cleanup requires terminal task payloads");
    }

    NothrowScopedLock lock(m_lifecycleMutex);
    completeResetWithinLock();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

