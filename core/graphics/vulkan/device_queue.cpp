// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"
#include "submission_workspace.h"

#include <core/common/log.h>

#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_device_queue{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static VkSemaphore DecodeSubmissionNativeSemaphore(const Object& semaphore)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
    return static_cast<VkSemaphore>(semaphore.pointer());
#else
    return static_cast<VkSemaphore>(semaphore.integer);
#endif
}

class ScopedSubmissionHookResolution final : NoCopy{
public:
    explicit ScopedSubmissionHookResolution(const QueueSubmissionPreSubmitHook& hook)noexcept
        : m_hook(hook)
    {}
    ~ScopedSubmissionHookResolution()noexcept{
        resolve({});
    }


public:
    void arm()noexcept{ m_armed = m_hook.resolved != nullptr; }

    void resolve(const QueueSubmissionToken& token)noexcept{
        if(!m_armed)
            return;
        m_armed = false;
        if(!m_hook.resolved(m_hook.context, m_hook.identity, token))
            TerminateInvariant();
    }


private:
    QueueSubmissionPreSubmitHook m_hook;
    b8 m_armed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CommandListHandle> Device::createCommandList(const CommandListParameters& params){
    CommandListParameters resolvedParams = params;
    if(resolvedParams.recordingWorkerIndex == 0u)
        resolvedParams.recordingWorkerDomain = 0u;
    Queue* queue = nullptr;
    if(resolvedParams.physicalQueue.valid()){
        queue = getQueue(resolvedParams.physicalQueue);
        if(!queue){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create command list: requested physical queue is not available"));
            return MakeUnexpected(Failure{});
        }
        resolvedParams.queueType = queue->m_queueID;
    }
    else{
        queue = getQueue(resolvedParams.queueType);
        if(!queue){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create command list: requested queue is not available"));
            return MakeUnexpected(Failure{});
        }
        resolvedParams.physicalQueue = queue->m_physicalQueue;
    }

    auto* cmdList = NewArenaObject<CommandList>(m_context.objectArena, *this, resolvedParams);
    if(!cmdList)
        return MakeUnexpected(Failure{});
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

Expected<QueueSubmissionReceipt> Device::executeCommandLists(CommandList* const* commandLists, const usize numCommandLists, const CommandQueue::Enum executionQueue, const QueueSubmissionDesc& submitDesc){
    return executeCommandLists(commandLists, numCommandLists, getPrimaryPhysicalQueue(executionQueue), submitDesc);
}


Expected<QueueSubmissionReceipt> Device::executeCommandLists(CommandList* const* commandLists, const usize numCommandLists, const GpuPhysicalQueueId& executionQueue, const QueueSubmissionDesc& submitDesc){
    return executeCommandListsInternal(commandLists, numCommandLists, executionQueue, submitDesc, false);
}

b8 Device::validateSubmissionWaitToken(const QueueSubmissionToken& token)const noexcept{
    if(!token.valid() || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration))
        return false;

    FOUNDATION_ASSERT(token.physicalQueueIndex < m_physicalQueues.size());
    const auto producerQueue = m_physicalQueues[token.physicalQueueIndex];
    FOUNDATION_ASSERT(producerQueue->m_queueID == token.queue && token.matchesPhysicalQueue(producerQueue->m_physicalQueue.index, producerQueue->m_physicalQueue.deviceGeneration));

    NothrowScopedLock producerLock(producerQueue->m_mutex);
    return producerQueue->m_trackingSemaphore != VK_NULL_HANDLE && token.value <= producerQueue->m_lastSubmittedID;
}

b8 Device::waitForSubmissionToken(const QueueSubmissionToken& token){
    return waitForSubmissionTokenInternal(token, DeviceLossDiagnosticPolicy::Capture);
}

u32 Device::getQueueFamilyIndex(const CommandQueue::Enum queueType)const noexcept{
    return getQueueFamilyIndex(getPrimaryPhysicalQueue(queueType));
}

u32 Device::getQueueFamilyIndex(const GpuPhysicalQueueId& queue)const noexcept{
    const GpuPhysicalQueueInfo* const info = getPhysicalQueueInfo(queue);
    return info ? info->familyIndex : VK_QUEUE_FAMILY_IGNORED;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<QueueSubmissionReceipt> Device::executeGraphCommandLists(CommandList* const* commandLists, const usize numCommandLists, const GpuPhysicalQueueId& executionQueue, const QueueSubmissionDesc& submitDesc){
    return executeCommandListsInternal(commandLists, numCommandLists, executionQueue, submitDesc, true);
}

Expected<QueueSubmissionReceipt> Device::executeCommandListsInternal(
    CommandList* const* commandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc,
    const b8 graphSubmissionAuthorized,
    const DeviceLossDiagnosticPolicy::Enum deviceLossDiagnosticPolicy
){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return MakeUnexpected(Failure{});

    Queue* const queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return MakeUnexpected(Failure{});
    }

    if(numCommandLists > 0u && !commandLists){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list array is null"));
        return MakeUnexpected(Failure{});
    }
    if(numCommandLists > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list count exceeds Vulkan limit"));
        return MakeUnexpected(Failure{});
    }
    for(usize i = 0u; i < numCommandLists; ++i){
        CommandList* const commandList = commandLists[i];
        for(usize previous = 0u; previous < i; ++previous){
            if(commandLists[previous] == commandList){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} is duplicated"), i);
                return MakeUnexpected(Failure{});
            }
        }
        if(!commandList || &commandList->m_device != this){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} is null or foreign"), i);
            return MakeUnexpected(Failure{});
        }
        if(!commandList->matchesSubmissionLease(executionQueue, queue->m_queueID, graphSubmissionAuthorized)){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Command list {} lease provenance does not match execution queue"), i);
            return MakeUnexpected(Failure{});
        }
        if(!commandList->matchesNativeLeaseIdentity()){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} has no native command buffer"), i);
            return MakeUnexpected(Failure{});
        }
        if(commandList->m_commandRecordingFailed){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} has a sticky native recording failure"), i);
            return MakeUnexpected(Failure{});
        }
        if(commandList->m_isRecording){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} is still recording"), i);
            return MakeUnexpected(Failure{});
        }
    }
    for(usize i = 0u; i < numCommandLists; ++i){
        if(!commandLists[i]->validateTrackedResourcesReadyForSubmission())
            return MakeUnexpected(Failure{});
    }

    if(submitDesc.waitTokenCount > 0u && !submitDesc.waitTokens){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: submission wait token array is null"));
        return MakeUnexpected(Failure{});
    }

    UniqueLock<Futex> submissionWorkspaceLock(queue->m_submissionWorkspaceMutex);
    auto& localWaits = queue->m_executeLocalWaits;
    auto& expectedCommandLists = queue->m_executeExpectedCommandLists;
    localWaits.clear();
    expectedCommandLists.clear();
    if(submitDesc.waitTokenCount > 0u){
        localWaits.reserve(submitDesc.waitTokenCount);
        for(usize i = 0u; i < submitDesc.waitTokenCount; ++i){
            const QueueSubmissionToken& token = submitDesc.waitTokens[i];
            if(!validateSubmissionWaitToken(token)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: dependency token is invalid, unavailable, or unsignalled"));
                return MakeUnexpected(Failure{});
            }

            const auto producerQueue = m_physicalQueues[token.physicalQueueIndex];


            // Queue order already covers same-queue dependencies.
            if(token.matchesPhysicalQueue(executionQueue.index, executionQueue.deviceGeneration))
                continue;

            b8 merged = false;
            for(Queue::SubmissionWait& wait : localWaits){
                if(wait.semaphore != producerQueue->m_trackingSemaphore)
                    continue;

                wait.value = Max(wait.value, token.value);
                merged = true;
                break;
            }
            if(!merged)
                localWaits.push_back(Queue::SubmissionWait{ producerQueue->m_trackingSemaphore, token.value });
        }
    }

    SubmissionWorkspace submissionWorkspace(*this, *queue);
    submissionWorkspace.prepare(commandLists, numCommandLists, graphSubmissionAuthorized);

    // Hook runs after waits validate; its signal stays submission-local.
    Queue::SubmissionSignal hookSignal = {};
    const Queue::SubmissionSignal* localSignals = nullptr;
    usize localSignalCount = 0u;
    __hidden_vulkan_device_queue::ScopedSubmissionHookResolution hookResolution(submitDesc.preSubmitHook);
    if(submitDesc.preSubmitHook.valid()){
        const auto nativeSignal = submitDesc.preSubmitHook.invoke(submitDesc.preSubmitHook.context, submitDesc.preSubmitHook.identity, executionQueue);
        if(nativeSignal)
            hookResolution.arm();
        if(!nativeSignal || !nativeSignal->valid()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to prepare exact queue submission hook"));
            return MakeUnexpected(Failure{});
        }

        hookSignal.semaphore = __hidden_vulkan_device_queue::DecodeSubmissionNativeSemaphore(nativeSignal->semaphore);
        hookSignal.value = nativeSignal->value;
        if(hookSignal.semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exact queue submission hook returned an invalid native semaphore"));
            return MakeUnexpected(Failure{});
        }
        localSignals = &hookSignal;
        localSignalCount = 1u;
    }

    const auto submission = queue->submit(
        commandLists,
        numCommandLists,
        expectedCommandLists.empty() ? nullptr : expectedCommandLists.data(),
        localWaits.empty() ? nullptr : localWaits.data(),
        localWaits.size(),
        localSignals,
        localSignalCount,
        submitDesc.forceNativeSubmission
    );
    const b8 submissionAccepted = submission && submission->accepted;
    const u64 submittedID = submission ? submission->submittedID : submission.error().lastSubmittedID;
    const VkResult nativeSubmissionResult = submission ? VK_SUCCESS : submission.error().nativeResult;
    const QueueSubmissionToken submissionToken = submissionAccepted
        ? QueueSubmissionToken{ .value = submittedID, .physicalQueueIndex = executionQueue.index, .deviceGeneration = executionQueue.deviceGeneration, .queue = queue->m_queueID, }
        : QueueSubmissionToken{}
    ;

    submissionWorkspace.finalize(commandLists, numCommandLists, executionQueue, submittedID, submissionAccepted);
    hookResolution.resolve(submissionToken);

    submissionWorkspaceLock.unlock();
    if(nativeSubmissionResult == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss(VulkanArenaScope::s_QueueSubmitContext);

    if(!submissionAccepted)
        return MakeUnexpected(Failure{});

    return QueueSubmissionReceipt{ submissionToken, submission->timelineWaitCount };
}

b8 Device::waitForSubmissionTokenInternal(const QueueSubmissionToken& token, const DeviceLossDiagnosticPolicy::Enum deviceLossDiagnosticPolicy){
    if(!token.valid() || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration))
        return false;

    FOUNDATION_ASSERT(token.physicalQueueIndex < m_physicalQueues.size());
    const auto producerQueue = m_physicalQueues[token.physicalQueueIndex];
    FOUNDATION_ASSERT(producerQueue->m_queueID == token.queue);

    VkResult result = VK_SUCCESS;
    {
        ScopedLock producerLock(producerQueue->m_mutex);
        if(producerQueue->m_trackingSemaphore == VK_NULL_HANDLE || token.value > producerQueue->m_lastSubmittedID)
            return false;
        if(token.value <= producerQueue->m_lastFinishedID)
            return true;

        auto waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        waitInfo.semaphoreCount = 1u;
        waitInfo.pSemaphores = &producerQueue->m_trackingSemaphore;
        waitInfo.pValues = &token.value;
        result = m_context.deviceDispatch.vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
        if(result == VK_SUCCESS){
            producerQueue->m_lastFinishedID = Max(producerQueue->m_lastFinishedID, token.value);
            producerQueue->collectCompletedCommandBuffers();
        }
        else if(result == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
    }

    if(result == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss("submission token wait");
    if(result != VK_SUCCESS && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to wait for submission token: {}"), ResultToString(result));
    return result == VK_SUCCESS;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

