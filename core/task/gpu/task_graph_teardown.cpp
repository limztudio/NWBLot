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


b8 GpuTaskGraph::retireTaskPayloads(){
    {
        NothrowScopedLock lock(m_lifecycleMutex);
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
        m_hasTaskLifecycleActivity = true;
        for(const GpuTaskNode& task : m_tasks){
            const TaskLifecycleState::Enum state = task.lifecycleState();
            if(state == TaskLifecycleState::Recording || state == TaskLifecycleState::Submitting || state == TaskLifecycleState::Accepting){
                m_declarationAdmissionState.fetch_and(~s_AdmissionTeardownBit, MemoryOrder::acq_rel);
                return false;
            }
        }
    }

    ScopeExit reopenDeclarations([this]()noexcept{
        NothrowScopedLock lock(m_lifecycleMutex);
        m_declarationAdmissionState.fetch_and(~s_AdmissionTeardownBit, MemoryOrder::acq_rel);
    });
    return UncaughtExceptionCount() != 0 ? destroyTaskPayloadsWithoutCallbacks() : destroyTaskPayloads();
}


b8 GpuTaskGraph::destroyTaskPayloads(){
    DiscardNotificationScope notification(*this);
    TaskPayloadDestroyScope payloadDestroy(*this);
    u64 notificationGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        const u64 admissionState = m_declarationAdmissionState.load(MemoryOrder::acquire);
        if((admissionState & s_AdmissionTeardownBit) == 0u || (admissionState & (s_AdmissionWriterMask | s_AdmissionReaderMask)) != 0u || m_activeDiscardNotificationCount != 0u || m_submissionBindingState == SubmissionBindingState::Active)
            return false;
        for(const GpuTaskNode& task : m_tasks){
            const TaskLifecycleState::Enum state = task.lifecycleState();
            if(state == TaskLifecycleState::Submitting || state == TaskLifecycleState::Accepting || state == TaskLifecycleState::Recording){
                return false;
            }
        }
        b8 hasUnacceptedTask = false;
        for(const GpuTaskNode& task : m_tasks){
            const TaskLifecycleState::Enum state = task.lifecycleState();
            if(state == TaskLifecycleState::Declared || state == TaskLifecycleState::Recorded)
                hasUnacceptedTask = true;
        }
        if(hasUnacceptedTask){
            notificationGeneration = AllocateGeneration();
            notification.activateWithinLock();
        }
        for(GpuTaskNode& task : m_tasks){
            const TaskLifecycleState::Enum state = task.lifecycleState();
            if(state == TaskLifecycleState::Declared || state == TaskLifecycleState::Recorded){
                task.recordingClaimGeneration = 0u;
                task.submissionClaimGeneration = 0u;
                task.discardNotificationGeneration = notificationGeneration;
                task.setLifecycle(TaskLifecycleState::Discarded);
            }
        }
        payloadDestroy.activateWithinLock();
    }

    for(GpuTaskNode& task : m_tasks){
        if(m_hasDiscardCallbacks && notificationGeneration != 0u && task.discardNotificationGeneration == notificationGeneration && task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }
    return true;
}

b8 GpuTaskGraph::destroyTaskPayloadsWithoutCallbacks()noexcept{
    {
        NothrowScopedLock lock(m_lifecycleMutex);
        const u64 admissionState = m_declarationAdmissionState.load(MemoryOrder::acquire);
        if((admissionState & s_AdmissionTeardownBit) == 0u || (admissionState & (s_AdmissionWriterMask | s_AdmissionReaderMask)) != 0u || m_activeDiscardNotificationCount != 0u || m_submissionBindingState == SubmissionBindingState::Active)
            return false;
        for(const GpuTaskNode& task : m_tasks){
            const TaskLifecycleState::Enum state = task.lifecycleState();
            if(state == TaskLifecycleState::Submitting || state == TaskLifecycleState::Accepting || state == TaskLifecycleState::Recording)
                return false;
        }
        for(GpuTaskNode& task : m_tasks){
            const TaskLifecycleState::Enum state = task.lifecycleState();
            if(state == TaskLifecycleState::Declared || state == TaskLifecycleState::Recorded)
                task.setLifecycle(TaskLifecycleState::Discarded);
            task.recordingClaimGeneration = 0u;
            task.submissionClaimGeneration = 0u;
            task.discardNotificationGeneration = 0u;
            task.setLifecycle(task.lifecycleState(MemoryOrder::relaxed));
        }
    }

    destroyTaskPayloadObjects();
    return true;
}

void GpuTaskGraph::destroyTaskPayloadObjects()noexcept{
    if(!m_hasTaskPayloads)
        return;

    const b8 runDestroyers = m_hasNonTrivialTaskPayloads;
    for(GpuTaskNode& task : m_tasks){
        if(runDestroyers && task.payload && task.destroyPayload)
            task.destroyPayload(m_arena, task.payload);
        task.payload = nullptr;
        task.recordPayload = nullptr;
        task.acceptPayload = nullptr;
        task.discardPayload = nullptr;
        task.destroyPayload = nullptr;
    }
    m_hasNonTrivialTaskPayloads = false;
    rewindTaskPayloadPool();
    m_hasTaskPayloads = false;
}

void GpuTaskGraph::recycleOrDestroyStateSnapshot(const NotNull<CommandListResourceStateHandoff*> states)noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    if(m_reusableStateSnapshots.size() < m_reusableStateSnapshots.capacity()){
        states->reset();
        m_reusableStateSnapshots.push_back(states);
        return;
    }
    DestroyArenaObjectNoexcept(m_arena, states.get());
}

void GpuTaskGraph::destroyReusableStateSnapshots()noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    for(const auto states : m_reusableStateSnapshots)
        DestroyArenaObjectNoexcept(m_arena, states.get());
    m_reusableStateSnapshots.clear();
}

void GpuTaskGraph::destroyTaskStateSnapshots()noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    for(const auto states : m_externalStateSnapshots)
        recycleOrDestroyStateSnapshot(states);
    m_externalStateSnapshots.clear();
}

void GpuTaskGraph::destroyResourceStateSnapshots()noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    for(const auto states : m_initialOwnerHandoffStateSnapshots)
        recycleOrDestroyStateSnapshot(states);
    m_initialOwnerHandoffStateSnapshots.clear();
    m_initialOwnerHandoffSources.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

