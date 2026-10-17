// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/command.h>

#include <global/allocation_size.h>
#include <global/exception.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::PayloadLifetimeScope::~PayloadLifetimeScope()noexcept(false){
    if(!m_graph.retireTaskPayloads())
        TerminateInvariant();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::retireTaskPayloads(){
    {
        NothrowScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeDeclarationAccessCount != 0u
            || m_activeDeclarationReadCount != 0u
            || m_activeDiscardNotificationCount != 0u
            || m_activePacketRecordingClaimCount.load(MemoryOrder::acquire) != 0u
            || m_activeRecordingPreparationSerial != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
            || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
        )
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(task.lifecycleState == TaskLifecycleState::Recording || task.lifecycleState == TaskLifecycleState::Submitting || task.lifecycleState == TaskLifecycleState::Accepting)
                return false;
        }
        m_teardownInProgress = true;
    }

    ScopeExit reopenDeclarations([this]()noexcept{
        NothrowScopedLock lock(m_lifecycleMutex);
        m_teardownInProgress = false;
    }
    );
    return UncaughtExceptionCount() != 0 ? destroyTaskPayloadsWithoutCallbacks() : destroyTaskPayloads();
}


bool GpuTaskGraph::destroyTaskPayloads(){
    DiscardNotificationScope notification(*this);
    TaskPayloadDestroyScope payloadDestroy(*this);
    u64 notificationGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(!m_teardownInProgress || m_activeDeclarationAccessCount != 0u || m_activeDiscardNotificationCount != 0u || m_submissionBindingState == SubmissionBindingState::Active)
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(task.lifecycleState == TaskLifecycleState::Submitting || task.lifecycleState == TaskLifecycleState::Accepting || task.lifecycleState == TaskLifecycleState::Recording){
                return false;
            }
        }
        bool hasUnacceptedTask = false;
        for(const GpuTaskNode& task : m_tasks){
            if(task.lifecycleState == TaskLifecycleState::Declared || task.lifecycleState == TaskLifecycleState::Recorded)
                hasUnacceptedTask = true;
        }
        if(hasUnacceptedTask){
            notificationGeneration = AllocateGeneration();
            notification.activateWithinLock();
        }
        for(GpuTaskNode& task : m_tasks){
            if(task.lifecycleState == TaskLifecycleState::Declared || task.lifecycleState == TaskLifecycleState::Recorded){
                task.lifecycleState = TaskLifecycleState::Discarded;
                task.recordingClaimGeneration = 0u;
                task.submissionClaimGeneration = 0u;
                task.discardNotificationGeneration = notificationGeneration;
                task.recordThunkInProgress = false;
                task.recordThunkCompleted = false;
            }
        }
        payloadDestroy.activateWithinLock();
    }

    for(GpuTaskNode& task : m_tasks){
        if(notificationGeneration != 0u && task.discardNotificationGeneration == notificationGeneration && task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }
    return true;
}

bool GpuTaskGraph::destroyTaskPayloadsWithoutCallbacks()noexcept{
    {
        NothrowScopedLock lock(m_lifecycleMutex);
        if(!m_teardownInProgress || m_activeDeclarationAccessCount != 0u || m_activeDiscardNotificationCount != 0u || m_submissionBindingState == SubmissionBindingState::Active)
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(task.lifecycleState == TaskLifecycleState::Submitting || task.lifecycleState == TaskLifecycleState::Accepting || task.lifecycleState == TaskLifecycleState::Recording)
                return false;
        }
        for(GpuTaskNode& task : m_tasks){
            if(task.lifecycleState == TaskLifecycleState::Declared || task.lifecycleState == TaskLifecycleState::Recorded)
                task.lifecycleState = TaskLifecycleState::Discarded;
            task.recordingClaimGeneration = 0u;
            task.submissionClaimGeneration = 0u;
            task.discardNotificationGeneration = 0u;
            task.recordThunkInProgress = false;
            task.recordThunkCompleted = false;
        }
    }

    destroyTaskPayloadObjects();
    return true;
}

void GpuTaskGraph::destroyTaskPayloadObjects()noexcept{
    for(GpuTaskNode& task : m_tasks){
        if(task.payload && task.destroyPayload)
            task.destroyPayload(m_arena, task.payload);
        task.payload = nullptr;
        task.recordPayload = nullptr;
        task.acceptPayload = nullptr;
        task.discardPayload = nullptr;
        task.destroyPayload = nullptr;
    }
}

void GpuTaskGraph::destroyTaskStateSnapshots()noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    for(CommandListResourceStateHandoff* const states : m_externalStateSnapshots)
        DestroyArenaObjectNoexcept(m_arena, states);
    m_externalStateSnapshots.clear();
}

void GpuTaskGraph::destroyResourceStateSnapshots()noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    for(CommandListResourceStateHandoff* const states : m_initialOwnerHandoffStateSnapshots)
        DestroyArenaObjectNoexcept(m_arena, states);
    m_initialOwnerHandoffStateSnapshots.clear();
    m_initialOwnerHandoffSources.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

