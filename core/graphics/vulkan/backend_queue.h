// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/task/cpu/scheduler.h>
#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_buffer_provenance.h"
#include "native_queue_state.h"
#include "native_texture_provenance.h"
#include "submitted_command_buffer_owner_lookup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Device;
class VulkanTestDispatchAccess;
class Queue;
class TrackedCommandBuffer;
class StateTracker;
class GpuDescriptorHeap;
class DescriptorBufferManager;

class Buffer;
class Texture;
class AccelStruct;
class OpacityMicromap;

struct VulkanContext;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Command queue wrapper with timeline semaphore tracking


class Queue final : NoCopy{
    friend class CommandList;
    friend class Device;
    friend class TrackedCommandBuffer;


private:
    using CommandBufferList = List<TrackedCommandBufferPtr, Alloc::GlobalArena>;


private:
    struct DescriptorHeapUseCommitTicket{
        GpuDescriptorHeap* heap = nullptr;
        TrackedCommandBuffer* commandBuffer = nullptr;
        usize heapUseIndex = Limit<usize>::s_Max;
    };


public:
    Queue(const VulkanContext& context, Device& device, const GpuPhysicalQueueInfo& info, NativeQueueState& nativeQueue);
    ~Queue()noexcept;


public:
    [[nodiscard]] TrackedCommandBufferPtr getOrCreateCommandBuffer(
        u64 recordingWorkerDomain = 0u,
        u32 recordingWorkerIndex = 0u
    );
    [[nodiscard]] GpuCommandArenaStatistics commandArenaStatistics()const noexcept;
    [[nodiscard]] GpuCommandArenaWorkerStatistics commandArenaWorkerStatistics(
        u64 recordingWorkerDomain,
        u32 recordingWorkerIndex
    )const noexcept;

    void addWaitSemaphore(VkSemaphore semaphore, u64 value);
    void addSignalSemaphore(VkSemaphore semaphore, u64 value);

    using SubmissionWait = QueueSubmissionWait;
    struct SubmissionSignal{
        VkSemaphore semaphore = VK_NULL_HANDLE;
        u64 value = 0u;
    };
    struct SubmissionCommandListIdentity{
        TrackedCommandBuffer* owner = nullptr;
        u64 recordingLeaseSerial = 0u;
        u64 nativeRecordingID = 0u;
        u64 recordingWorkerDomain = 0u;
        u64 graphRecordingOwnershipSerial = 0u;
        u32 recordingWorkerIndex = 0u;
        bool graphSubmissionAuthorized = false;
    };

    u64 submit(
        CommandList* const* ppCmd,
        usize numCmd,
        const SubmissionCommandListIdentity* expectedCommandLists = nullptr,
        const SubmissionWait* localWaits = nullptr,
        usize localWaitCount = 0u,
        bool* outSubmissionAccepted = nullptr,
        VkResult* outNativeResult = nullptr,
        const SubmissionSignal* localSignals = nullptr,
        usize localSignalCount = 0u,
        bool forceNativeSubmission = false
    );
    [[nodiscard]] VkResult updateLastFinishedID();

    void waitForIdle();


private:
    struct WorkerCommandArena{
        u64 recordingWorkerDomain = 0u;
        u32 recordingWorkerIndex = 0u;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        Futex mutex;
        Atomic<WorkerCommandArena*> next = nullptr;
        Atomic<u64> currentCommandBufferCount = 0u;
        Atomic<u64> highWaterCommandBufferCount = 0u;
        Atomic<u64> reusableCommandBufferCount = 0u;
        Atomic<u64> leasedCommandBufferCount = 0u;
        Atomic<u64> pendingCommandBufferCount = 0u;
        Atomic<u64> growthEventCount = 0u;
        Atomic<u64> resetEventCount = 0u;
        CommandBufferList commandBuffersPool;


        WorkerCommandArena(Alloc::GlobalArena& arena, const u64 workerDomain, const u32 workerIndex)
            : recordingWorkerDomain(workerDomain)
            , recordingWorkerIndex(workerIndex)
            , commandBuffersPool(arena)
        {}
    };

    static void updateCommandBufferHighWater(Atomic<u64>& highWaterCount, u64 currentCount)noexcept;
    [[nodiscard]] u64 nextRecordingID()noexcept;
    void registerCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept;
    [[nodiscard]] bool validateCommandBufferSubmissionState(const TrackedCommandBuffer& commandBuffer)const;
    void transitionCommandBufferState(
        TrackedCommandBuffer& commandBuffer,
        TrackedCommandBufferArenaState::Enum nextState
    );
    void commitCommandBufferStateTransition(
        TrackedCommandBuffer& commandBuffer,
        TrackedCommandBufferArenaState::Enum nextState
    )noexcept;
    void unregisterCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept;
    // Requires m_mutex. Releases native command-buffer resource/staging references only after the queue timeline
    // has completed their submission, then preserves each worker-affine lease in its own reusable pool.
    void collectCompletedCommandBuffers();
    // Default/direct lease zero stays private per command buffer because external callers may record it from
    // unrelated threads. Explicit graph workers use one Vulkan pool shard per physical queue and worker identity.
    [[nodiscard]] TrackedCommandBufferPtr createCommandBuffer(
        VkCommandPool commandPool,
        Futex* sharedCommandPoolMutex,
        u64 recordingWorkerDomain,
        u32 recordingWorkerIndex
    );
    [[nodiscard]] WorkerCommandArena* findWorkerCommandArena(u64 recordingWorkerDomain, u32 recordingWorkerIndex)const noexcept;
    [[nodiscard]] WorkerCommandArena* getOrCreateWorkerCommandArena(u64 recordingWorkerDomain, u32 recordingWorkerIndex);
    [[nodiscard]] TrackedCommandBufferPtr getOrCreateDirectCommandBuffer();
    [[nodiscard]] TrackedCommandBufferPtr getOrCreateWorkerCommandBuffer(u64 recordingWorkerDomain, u32 recordingWorkerIndex);
    void destroyWorkerCommandArenas()noexcept;
    void clearPendingSemaphores()noexcept;
    [[nodiscard]] CommandBufferList::iterator recycleCommandBuffer(
        CommandBufferList& source,
        CommandBufferList::iterator commandBuffer
    );
    [[nodiscard]] bool coversTimerQueryPrerequisite(
        const QueueSubmissionToken& prerequisite,
        bool prerequisiteObservedComplete,
        const SubmissionWait* localWaits,
        usize localWaitCount
    )const noexcept;


private:
    VkSemaphore m_trackingSemaphore = VK_NULL_HANDLE;

    const VulkanContext& m_context;
    Device& m_device;

    NativeQueueState& m_nativeQueue;
    GpuPhysicalQueueId m_physicalQueue;
    u32 m_queueFamilyIndex;
    CommandQueue::Enum m_queueID;

    // Serializes reusable Device::executeCommandLists workspace through accepted post-submit publication.
    // Acquire before m_mutex when both are required.
    Futex m_submissionWorkspaceMutex;
    // Always acquire before m_nativeQueue.hostMutex when both are required.
    Futex m_mutex;
    Futex m_workerCommandArenasMutex;
    Vector<VkSemaphore, Alloc::GlobalArena> m_waitSemaphores;
    Vector<u64, Alloc::GlobalArena> m_waitSemaphoreValues;
    Vector<VkSemaphore, Alloc::GlobalArena> m_signalSemaphores;
    Vector<u64, Alloc::GlobalArena> m_signalSemaphoreValues;
    // Queue::submit holds m_mutex throughout, so this persistent high-water workspace can be reused without
    // allocation or deallocation after vkQueueSubmit2 accepts the submission.
    Vector<DescriptorHeapUseCommitTicket, Alloc::GlobalArena> m_submitDescriptorHeapUseCommitTickets;
    Vector<TrackedCommandBuffer*, Alloc::GlobalArena> m_submitValidatedTimerQueryCommandBuffers;
    Vector<VkSemaphoreSubmitInfo, Alloc::GlobalArena> m_submitWaitInfos;
    Vector<VkSemaphoreSubmitInfo, Alloc::GlobalArena> m_submitSignalInfos;
    Vector<VkCommandBufferSubmitInfo, Alloc::GlobalArena> m_submitCommandBufferInfos;
    CommandBufferList m_submitPreparedCommandBuffers;
    Vector<SubmissionWait, Alloc::GlobalArena> m_executeLocalWaits;
    Vector<SubmissionCommandListIdentity, Alloc::GlobalArena> m_executeExpectedCommandLists;
    VulkanDetail::SubmittedCommandBufferOwnerLookup m_executeSubmittedOwners;

    Atomic<u64> m_lastRecordingID = 0u;
    u64 m_lastSubmittedID = 0;
    u64 m_lastFinishedID = 0;

    CommandBufferList m_commandBuffersInFlight;
    CommandBufferList m_commandBuffersPool;
    Vector<WorkerCommandArena*, Alloc::GlobalArena> m_workerCommandArenas;
    // Published worker nodes are append-only while the queue is usable. Device teardown joins recording clients
    // before destroyWorkerCommandArenas() clears the head and reclaims the stable nodes.
    Atomic<WorkerCommandArena*> m_workerCommandArenaHead = nullptr;

    Atomic<u64> m_explicitWorkerArenaCount = 0u;
    Atomic<u64> m_directCommandBufferCount = 0u;
    Atomic<u64> m_directHighWaterCommandBufferCount = 0u;
    Atomic<u64> m_directReusableCommandBufferCount = 0u;
    Atomic<u64> m_directLeasedCommandBufferCount = 0u;
    Atomic<u64> m_pendingDirectCommandBufferCount = 0u;
    Atomic<u64> m_directCommandBufferGrowthEventCount = 0u;
    Atomic<u64> m_directCommandBufferResetEventCount = 0u;
    Atomic<u64> m_pendingWorkerEpochCount = 0u;
    Atomic<u64> m_currentCommandBufferCount = 0u;
    Atomic<u64> m_highWaterCommandBufferCount = 0u;
    Atomic<u64> m_reusableCommandBufferCount = 0u;
    Atomic<u64> m_leasedCommandBufferCount = 0u;
    Atomic<u64> m_pendingCommandBufferCount = 0u;
    Atomic<u64> m_commandBufferGrowthEventCount = 0u;
    Atomic<u64> m_commandBufferResetEventCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

