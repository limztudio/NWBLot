// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>
#include <global/allocation_size.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::appendMarkerLabel(const AStringView text, u32& outOffset, u32& outSize){
    if(
        text.empty()
        || !text.data()
        || text.size() > Limit<u32>::s_Max
        || text.size() > Limit<u32>::s_Max - m_markerText.size()
    )
        return false;

    outOffset = static_cast<u32>(m_markerText.size());
    outSize = static_cast<u32>(text.size());
    const usize nextSize = m_markerText.size() + text.size();
    m_markerText.resize(nextSize);
    NWB_MEMCPY(m_markerText.data() + outOffset, outSize, text.data(), text.size());
    return true;
}

AStringView GpuTaskGraph::markerLabel(const u32 offset, const u32 size)const{
    NWB_ASSERT(offset <= m_markerText.size());
    NWB_ASSERT(size <= m_markerText.size() - offset);
    return AStringView(reinterpret_cast<const char*>(m_markerText.data() + offset), size);
}

bool GpuTaskGraph::destroyTaskPayloads(){
    DiscardNotificationScope notification(*this);
    TaskPayloadDestroyScope payloadDestroy(*this);
    u64 notificationGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            !m_teardownInProgress
            || m_activeDeclarationAccessCount != 0u
            || m_activeDiscardNotificationCount != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
        )
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
                || task.lifecycleState == TaskLifecycleState::Recording
            ){
                return false;
            }
        }
        bool hasUnacceptedTask = false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Declared
                || task.lifecycleState == TaskLifecycleState::Recorded
            )
                hasUnacceptedTask = true;
        }
        if(hasUnacceptedTask){
            notificationGeneration = allocateGeneration();
            notification.activateWithinLock();
        }
        for(GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Declared
                || task.lifecycleState == TaskLifecycleState::Recorded
            ){
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
        if(
            notificationGeneration != 0u
            && task.discardNotificationGeneration == notificationGeneration
            && task.payload
            && task.discardPayload
        )
            task.discardPayload(task.payload);
    }
    return true;
}

bool GpuTaskGraph::destroyTaskPayloadsWithoutCallbacks()noexcept{
    {
        NothrowScopedLock lock(m_lifecycleMutex);
        if(
            !m_teardownInProgress
            || m_activeDeclarationAccessCount != 0u
            || m_activeDiscardNotificationCount != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
        )
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
                || task.lifecycleState == TaskLifecycleState::Recording
            )
                return false;
        }
        for(GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Declared
                || task.lifecycleState == TaskLifecycleState::Recorded
            )
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
    for(GpuGraphResourceNode& resource : m_resources){
        if(resource.initialOwnerStateSource)
            DestroyArenaObjectNoexcept(m_arena, resource.initialOwnerStateSource);
        resource.initialOwnerStateSource = nullptr;
    }
    for(GpuTaskGraphInitialOwnerHandoffSourceView& source : m_initialOwnerHandoffSources){
        if(source.stateSource)
            DestroyArenaObjectNoexcept(m_arena, const_cast<CommandListResourceStateHandoff*>(source.stateSource));
        source.stateSource = nullptr;
    }
    m_initialOwnerHandoffSources.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

