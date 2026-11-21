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
    const auto clearIfNonEmpty = [](auto& container)noexcept{
        if(!container.empty())
            container.clear();
    };
    clearIfNonEmpty(m_tasks);
    m_publishedTaskCount = 0u;
    clearIfNonEmpty(m_dependencies);
    m_normalExecutionPrelude = {};
    clearIfNonEmpty(m_externalDependencies);
    clearIfNonEmpty(m_externalStateSources);
    clearIfNonEmpty(m_resourceUses);
    clearIfNonEmpty(m_resourceVersionUses);
    m_resourceImportIndexes.clearResources();
    clearIfNonEmpty(m_resources);
    m_publishedResourceCount = 0u;
    clearIfNonEmpty(m_resourceVersions);
    clearIfNonEmpty(m_initialOwnerHandoffSources);
    clearIfNonEmpty(m_queueFamilyIndices);
    m_resourceImportIndexes.clearResourceSets();
    clearIfNonEmpty(m_resourceSets);
    clearIfNonEmpty(m_resourceSetMembers);
    if(m_pipelineIdentityIndex && !m_pipelineIdentityIndex->empty())
        m_pipelineIdentityIndex->clear();
    if(m_pipelinePointerIndex && !m_pipelinePointerIndex->empty())
        m_pipelinePointerIndex->clear();
    clearIfNonEmpty(m_pipelines);
    if(m_externalCompletionIdentityIndex && !m_externalCompletionIdentityIndex->empty())
        m_externalCompletionIdentityIndex->clear();
    clearIfNonEmpty(m_externalCompletions);
    m_externalCompletionDeviceGeneration = 0u;
    clearIfNonEmpty(m_uploadBlobs);
    clearIfNonEmpty(m_uploadBlobStorage);
    clearIfNonEmpty(m_overalignedUploadBlobs);
    m_uploadBlobStorageByteSize = 0u;
    clearIfNonEmpty(m_markerText);
    m_presentEndpoint = {};
    const u64 nextGenerationBlock = AllocateGenerationBlock(2u);
    m_generation = nextGenerationBlock;
    bumpDeclarationRevision();
    m_activeRecordingAttemptGeneration.store(nextGenerationBlock + 1u, MemoryOrder::release);
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
    m_declarationAdmissionState.fetch_and(~(s_AdmissionTeardownBit | s_AdmissionPlanActiveBit | s_AdmissionReaderStripesDirtyBit), MemoryOrder::acq_rel);
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

