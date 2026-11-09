// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <global/atomic.h>
#include <global/exception.h>

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 GpuTaskGraph::AllocateGeneration()noexcept{
    static Atomic<u64> s_NextGeneration{ 1u };
    const u64 identity = TryNextAtomicIdentity(s_NextGeneration);
    if(identity == 0u)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GPU task graph generation identity space is exhausted");
    return identity;
}

GpuTaskGraph::~GpuTaskGraph()noexcept(false){
    ResetCompletionScope resetCompletion(*this);
    b8 teardownValid = true;
    {
        ScopedLock lock(m_lifecycleMutex);
        teardownValid = !m_teardownInProgress
            && m_activeDeclarationAccessCount == 0u
            && m_activeDeclarationReadCount == 0u
            && m_activeDiscardNotificationCount == 0u
            && m_activePacketRecordingClaimCount.load(MemoryOrder::acquire) == 0u
            && m_activeRecordingPreparationSerial == 0u
            && m_submissionBindingState != SubmissionBindingState::Active
            && m_submissionBindingState != SubmissionBindingState::ExceptionClosing
        ;
        for(const GpuTaskNode& task : m_tasks){
            if(task.lifecycleState == TaskLifecycleState::Recording || task.lifecycleState == TaskLifecycleState::Submitting || task.lifecycleState == TaskLifecycleState::Accepting)
                teardownValid = false;
        }
        if(teardownValid){
            m_teardownInProgress = true;
            resetCompletion.activateWithinLock();
        }
    }
    FOUNDATION_FATAL_ASSERT_MSG(teardownValid, "GpuTaskGraph destruction requires in-flight task work to resolve first");

    const b8 taskPayloadsDestroyed = UncaughtExceptionCount() != 0 ? destroyTaskPayloadsWithoutCallbacks() : destroyTaskPayloads();
    if(!taskPayloadsDestroyed)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph destruction requires in-flight task work to resolve first");
    resetCompletion.complete();
}


Expected<GpuTaskId> GpuTaskGraph::addTask(const GpuTaskDesc& desc, const GpuTaskCommandRequirements& commands){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});
    return appendTaskWithinMutation(desc, commands, nullptr, nullptr, nullptr, nullptr, 0u, mutation, nullptr);
}

b8 GpuTaskGraph::setNormalExecutionPrelude(const GpuTaskId& task){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid() || m_normalExecutionPrelude.valid() || !validTask(task) || task.index != 0u || m_tasks.size() != 1u)
        return false;

    const GpuTaskNode& prelude = m_tasks[0u];
    if(
        !prelude.commands.requiresPrimaryGraphicsQueue
        || (static_cast<u8>(prelude.commands.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
        || prelude.commands.externalQueue.valid()
        || !prelude.scheduling.forceSubmissionBoundary
        || prelude.scheduling.allowPacketMerge
        || prelude.scheduling.joinsAcceptedQueueFrontier
        || prelude.scheduling.isRecoverySubmission
        || prelude.dependencyCount != 0u
        || prelude.externalDependencyCount != 0u
        || prelude.externalStateSourceCount != 0u
        || prelude.resourceUseCount != 0u
        || prelude.declaredResourceSetUseCount != 0u
        || prelude.resourceVersionUseCount != 0u
    )
        return false;

    m_normalExecutionPrelude = task;
    m_declarationRevision = AllocateGeneration();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

