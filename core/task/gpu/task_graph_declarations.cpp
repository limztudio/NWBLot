// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <global/atomic.h>
#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 GpuTaskGraph::allocateGeneration()noexcept{
    static Atomic<u64> s_NextGeneration{ 1u };

    u64 nextGeneration = s_NextGeneration.load(MemoryOrder::relaxed);
    while(true){
        if(nextGeneration == 0u || nextGeneration == Limit<u64>::s_Max){
            NWB_FATAL_ASSERT_MSG(false, "GPU task graph generation identity space is exhausted");
            TerminateInvariant();
        }
        if(s_NextGeneration.compare_exchange_weak(
            nextGeneration,
            nextGeneration + 1u,
            MemoryOrder::relaxed,
            MemoryOrder::relaxed
        ))
            return nextGeneration;
    }
}

GpuTaskGraph::GpuTaskGraph(GraphicsArena& arena)
    : m_arena(arena)
    , m_declarationScratch(Name("core/graphics/task_graph/declaration_scratch"))
    , m_tasks(arena)
    , m_dependencies(arena)
    , m_externalDependencies(arena)
    , m_externalStateSources(arena)
    , m_externalStateSnapshots(arena)
    , m_resourceUses(arena)
    , m_resourceVersionUses(arena)
    , m_resources(arena)
    , m_resourceVersions(arena)
    , m_initialOwnerHandoffSources(arena)
    , m_queueFamilyIndices(arena)
    , m_resourceSets(arena)
    , m_resourceSetMembers(arena)
    , m_pipelines(arena)
    , m_externalCompletions(arena)
    , m_uploadBlobs(arena)
    , m_markerText(arena)
    , m_generation(allocateGeneration())
    , m_declarationRevision(allocateGeneration())
    , m_activeRecordingAttemptGeneration(allocateGeneration())
{}

GpuTaskGraph::~GpuTaskGraph()noexcept(false){
    ResetCompletionScope resetCompletion(*this);
    bool teardownValid = true;
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
            if(
                task.lifecycleState == TaskLifecycleState::Recording
                || task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
            )
                teardownValid = false;
        }
        if(teardownValid){
            m_teardownInProgress = true;
            resetCompletion.activateWithinLock();
        }
    }
    NWB_FATAL_ASSERT_MSG(teardownValid, "GpuTaskGraph destruction requires in-flight task work to resolve first");
    if(!teardownValid)
        TerminateInvariant();

    const bool taskPayloadsDestroyed = UncaughtExceptionCount() != 0
        ? destroyTaskPayloadsWithoutCallbacks()
        : destroyTaskPayloads()
    ;
    if(!taskPayloadsDestroyed){
        NWB_FATAL_ASSERT_MSG(false, "GpuTaskGraph destruction requires in-flight task work to resolve first");
        TerminateInvariant();
    }
    resetCompletion.complete();
}


GpuTaskId GpuTaskGraph::addTask(const GpuTaskDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};
    return appendTaskWithinMutation(desc, nullptr, nullptr, nullptr, nullptr, nullptr, 0u, mutation);
}

GpuGraphResourceVersionId GpuTaskGraph::declareResourceVersion(const GpuGraphResourceVersionDesc& desc){
    return appendResourceVersion(desc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

