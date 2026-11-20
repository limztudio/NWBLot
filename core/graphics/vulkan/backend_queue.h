// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"

#include <core/task/cpu/scheduler.h>

#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_resource_provenance.h"
#include "native_queue_state.h"
#include "submitted_command_buffer_owner_lookup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Queue final : NoCopy{
    friend class CommandList;
    friend class Device;
    friend class DeviceCrashDiagnostics;
    friend class TrackedCommandBuffer;
    friend class TimerQueryClaimJournal;


private:
    class SubmissionValidator;
    class DescriptorSubmission;
    class CommandArenaStatistics;


private:
    using InFlightCommandBufferQueue = Deque<TrackedCommandBufferPtr, Alloc::GlobalArena>;
    using CommandBufferPoolList = Vector<TrackedCommandBufferPtr, Alloc::GlobalArena>;


private:
    struct DescriptorHeapUseCommitTicket{
        NotNull<GpuDescriptorHeap*> heap;
        NotNull<TrackedCommandBuffer*> commandBuffer;
        QueueSubmissionToken submissionToken;
        usize heapUseIndex = Limit<usize>::s_Max;
    };


public:
    using SubmissionWait = QueueSubmissionWait;


public:
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
        b8 graphSubmissionAuthorized = false;
    };
    struct SubmissionReceipt{
        u64 submittedID = 0u;
        usize timelineWaitCount = 0u;
        b8 accepted = false;
    };
    struct SubmissionFailure{
        u64 lastSubmittedID = 0u;
        VkResult nativeResult = VK_SUCCESS;
    };
    struct BatchSubmitEntry{
        CommandList* const* ppCmd = nullptr;
        usize numCmd = 0u;
        const SubmissionCommandListIdentity* expectedCommandLists = nullptr;
        const SubmissionWait* localWaits = nullptr;
        usize localWaitCount = 0u;
        const SubmissionSignal* localSignals = nullptr;
        usize localSignalCount = 0u;
        u64 maxSameQueueWaitValue = 0u;
        b8 forceNativeSubmission = false;
    };
    struct BatchSubmissionResult{
        enum Status : u8{
            Accepted,
            ValidationFailed,
            NativeSubmitFailed,
        };
        Status status = Status::ValidationFailed;
        u64 lastSubmittedID = 0u;
        VkResult nativeResult = VK_SUCCESS;
    };
    struct BatchPreparedEntry{
        usize waitOffset = 0u;
        usize waitCount = 0u;
        usize cmdOffset = 0u;
        usize cmdCount = 0u;
        usize signalOffset = 0u;
        usize signalCount = 0u;
        usize timelineWaitCount = 0u;
        u64 submissionID = 0u;
    };


private:
    struct WorkerCommandArena{
        u64 recordingWorkerDomain = 0u;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        u32 recordingWorkerIndex = 0u;
        Futex mutex;
        Atomic<WorkerCommandArena*> next = nullptr;
        Atomic<u64> currentCommandBufferCount = 0u;
        Atomic<u64> highWaterCommandBufferCount = 0u;
        Atomic<u64> reusableCommandBufferCount = 0u;
        Atomic<u64> leasedCommandBufferCount = 0u;
        Atomic<u64> pendingCommandBufferCount = 0u;
        Atomic<u64> growthEventCount = 0u;
        Atomic<u64> resetEventCount = 0u;
        CommandBufferPoolList commandBuffersPool;


    public:
        WorkerCommandArena(Alloc::GlobalArena& arena, const u64 workerDomain, const u32 workerIndex)
            : recordingWorkerDomain(workerDomain)
            , recordingWorkerIndex(workerIndex)
            , commandBuffersPool(arena)
        {}
    };


private:


public:
    Queue(const VulkanContext& context, Device& device, const GpuPhysicalQueueInfo& info, NativeQueueState& nativeQueue);
    ~Queue()noexcept;


public:
    [[nodiscard]] Expected<TrackedCommandBufferPtr, VkResult> getOrCreateCommandBuffer(u64 recordingWorkerDomain = 0u, u32 recordingWorkerIndex = 0u);
    [[nodiscard]] GpuCommandArenaStatistics commandArenaStatistics()const noexcept;
    [[nodiscard]] GpuCommandArenaWorkerStatistics commandArenaWorkerStatistics(u64 recordingWorkerDomain, u32 recordingWorkerIndex)const noexcept;
    [[nodiscard]] Expected<SubmissionReceipt, SubmissionFailure> submit(
        CommandList* const* ppCmd,
        usize numCmd,
        const SubmissionCommandListIdentity* expectedCommandLists = nullptr,
        const SubmissionWait* localWaits = nullptr,
        usize localWaitCount = 0u,
        const SubmissionSignal* localSignals = nullptr,
        usize localSignalCount = 0u,
        b8 forceNativeSubmission = false,
        u64 maxSameQueueWaitValue = 0u
    );
    [[nodiscard]] BatchSubmissionResult submitBatch(Span<const BatchSubmitEntry> entries, Span<SubmissionReceipt> outReceipts);
    [[nodiscard]] VkResult updateLastFinishedID();
    void waitForIdle();


private:
    void registerCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept;
    [[nodiscard]] b8 validateCommandBufferSubmissionState(const TrackedCommandBuffer& commandBuffer)const;
    void transitionCommandBufferState(TrackedCommandBuffer& commandBuffer, TrackedCommandBufferArenaState::Enum nextState);
    void commitCommandBufferStateTransition(TrackedCommandBuffer& commandBuffer, TrackedCommandBufferArenaState::Enum nextState)noexcept;
    void unregisterCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept;
    void collectCompletedCommandBuffers();
    [[nodiscard]] Expected<TrackedCommandBufferPtr, VkResult> createCommandBuffer(VkCommandPool commandPool, Futex* sharedCommandPoolMutex, u64 recordingWorkerDomain, u32 recordingWorkerIndex);
    [[nodiscard]] WorkerCommandArena* findWorkerCommandArena(u64 recordingWorkerDomain, u32 recordingWorkerIndex)const noexcept;
    [[nodiscard]] Expected<NotNull<WorkerCommandArena*>, VkResult> getOrCreateWorkerCommandArena(u64 recordingWorkerDomain, u32 recordingWorkerIndex);
    [[nodiscard]] Expected<TrackedCommandBufferPtr, VkResult> getOrCreateDirectCommandBuffer();
    [[nodiscard]] Expected<TrackedCommandBufferPtr, VkResult> getOrCreateWorkerCommandBuffer(u64 recordingWorkerDomain, u32 recordingWorkerIndex);
    void destroyWorkerCommandArenas()noexcept;
    void recycleCommandBuffer(TrackedCommandBufferPtr cmdBuf);
    [[nodiscard]] b8 coversTimerQueryPrerequisite(const QueueSubmissionToken& prerequisite, b8 prerequisiteObservedComplete, Span<const SubmissionWait> localWaits)const noexcept;


private:
    VkSemaphore m_trackingSemaphore = VK_NULL_HANDLE;
    const VulkanContext& m_context;
    Device& m_device;
    NativeQueueState& m_nativeQueue;
    GpuPhysicalQueueId m_physicalQueue;
    u32 m_queueFamilyIndex;
    CommandQueue::Enum m_queueID;
    Futex m_submissionWorkspaceMutex;
    Futex m_mutex;
    Futex m_workerCommandArenasMutex;
    Vector<SubmissionWait, Alloc::GlobalArena> m_acceptedTimelineWaits;
    Vector<SubmissionWait, Alloc::GlobalArena> m_submitBatchAcceptedTimelineWaits;
    Vector<DescriptorHeapUseCommitTicket, Alloc::GlobalArena> m_submitDescriptorHeapUseCommitTickets;
    Vector<NotNull<TrackedCommandBuffer*>, Alloc::GlobalArena> m_submitValidatedTimerQueryCommandBuffers;
    Vector<CommandList*, Alloc::GlobalArena> m_submitBatchUniqueCommandLists;
    Vector<BatchPreparedEntry, Alloc::GlobalArena> m_submitBatchEntries;
    Vector<VkSubmitInfo2, Alloc::GlobalArena> m_submitBatchInfos;
    Vector<VkSemaphoreSubmitInfo, Alloc::GlobalArena> m_submitWaitInfos;
    Vector<VkSemaphoreSubmitInfo, Alloc::GlobalArena> m_submitSignalInfos;
    Vector<VkCommandBufferSubmitInfo, Alloc::GlobalArena> m_submitCommandBufferInfos;
    Vector<SubmissionWait, Alloc::GlobalArena> m_executeLocalWaits;
    Vector<SubmissionCommandListIdentity, Alloc::GlobalArena> m_executeExpectedCommandLists;
    VulkanDetail::SubmittedCommandBufferOwnerLookup m_executeSubmittedOwners;
    Atomic<u64> m_lastRecordingID = 0u;
    u64 m_lastSubmittedID = 0;
    u64 m_lastFinishedID = 0;
    InFlightCommandBufferQueue m_commandBuffersInFlight;
    CommandBufferPoolList m_commandBuffersPool;
    Vector<NotNull<WorkerCommandArena*>, Alloc::GlobalArena> m_workerCommandArenas;
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

