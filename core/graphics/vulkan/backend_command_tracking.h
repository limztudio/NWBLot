// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"
#include "backend_core_context.h"
#include "command_buffer_resource_references.h"
#include "timer_query_claim_journal.h"
#include "accel_struct_build_journal.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_resource_provenance.h"
#include "native_queue_state.h"
#include "submitted_command_buffer_owner_lookup.h"

#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RetainedTextureStateCommit{
    NotNull<Texture*> texture;
    MipLevel mipLevel = 0;
    ArraySlice arraySlice = 0;
};

struct PendingOpacityMicromapBuildCommit{
    NotNull<OpacityMicromap*> opacityMicromap;
};

namespace TrackedCommandBufferArenaState{
    static constexpr u8 s_TrackedCommandBufferArenaStateUntrackedBase = VulkanDefaults::s_VulkanDefaultTrackedCommandBufferArenaStateUntrackedBase;
    enum Enum : u8{
        Untracked = s_TrackedCommandBufferArenaStateUntrackedBase,
        Leased,
        Reusable,
        Pending,
    };
};

struct QueueSubmissionWait{
    VkSemaphore semaphore = VK_NULL_HANDLE;
    u64 value = 0u;
};

class TrackedCommandBuffer final : public RefCounter<GraphicsResource>{
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class Queue;
    friend class StateTracker;
    friend class GpuDescriptorHeap;
    friend class TimerQueryClaimJournal;
    friend class AccelStructBuildJournal;


private:
    using TimerQueryRecordingClaim = TimerQueryClaimJournal::Claim;


public:
    TrackedCommandBuffer(Queue& queue, const VulkanContext& context, VkCommandPool commandPool, b8 ownsCommandPool, Futex* sharedCommandPoolMutex)noexcept
        : RefCounter<GraphicsResource>(context.cpuScheduler)
        , m_cmdPool(commandPool)
        , m_sharedCommandPoolMutex(sharedCommandPoolMutex)
        , m_resourceReferences(context.objectArena)
        , m_referencedStagingBuffers(context.objectArena)
        , m_referencedDescriptorHeaps(context.objectArena)
        , m_retainedTextureStateCommits(context.objectArena)
        , m_accelStructJournal(*this, context.objectArena)
        , m_pendingOpacityMicromapBuildCommits(context.objectArena)
        , m_timerQueryClaims(*this, context.objectArena)
        , m_ownsCmdPool(ownsCommandPool)
        , m_context(context)
        , m_queue(queue)
    {}
    ~TrackedCommandBuffer()noexcept;


private:
    [[nodiscard]] Expected<void, VkResult> initialize(u32 queueFamilyIndex);
    [[nodiscard]] TimerQueryRecordingClaim& findOrAppendTimerQueryRecordingClaim(TimerQuery& query);
    [[nodiscard]] TimerQueryRecordingClaim* findTimerQueryRecordingClaim(TimerQuery& query, u64 generation)noexcept;
    [[nodiscard]] b8 validateTimerQueryRecordingClaims(const Queue& submissionQueue, Span<const QueueSubmissionWait> localWaits, Span<const NotNull<TrackedCommandBuffer*>> precedingCommandBuffers)const noexcept;
    void commitTimerQueryRecordingClaims(const QueueSubmissionToken& submissionToken)noexcept;
    void discardTimerQueryRecordingClaims()noexcept;
    void commitRetainedBufferStateCommits()noexcept;
    void appendRetainedTextureStateCommit(Texture& texture, MipLevel mipLevel, ArraySlice arraySlice);
    void commitRetainedTextureStateCommits()noexcept;
    [[nodiscard]] b8 appendPendingAccelStructBuildCommit(
        AccelStruct& accelStruct,
        VkAccelerationStructureTypeKHR accelStructType,
        VkBuildAccelerationStructureFlagsKHR buildFlags,
        Span<const AccelStructGeometryBuildSignature> geometrySignatures,
        Span<const RayTracingInstanceDesc> instances = {}
    );
    [[nodiscard]] Expected<NotNull<const AccelStructBuildSignatureRole*>> getPendingAccelStructBuildSignature(const AccelStruct& accelStruct)const noexcept;
    [[nodiscard]] b8 validatePendingAccelStructBuildCommits()const noexcept;
    void commitPendingAccelStructBuildCommits()noexcept;
    void retainTopLevelAccelStruct(AccelStruct& accelStruct);
    [[nodiscard]] b8 hasRetainedTopLevelAccelStructs()const noexcept{ return m_accelStructJournal.hasRetainedTopLevelAccelStructs(); }
    void retainAcceptedTopLevelAccelStructReferences();
    void retainPendingTopLevelAccelStructReferences(const TrackedCommandBuffer& buildCommandBuffer);
    void abandonPendingAccelStructBuildCommits()noexcept;
    void appendPendingOpacityMicromapBuildCommit(OpacityMicromap& opacityMicromap);
    [[nodiscard]] b8 hasPendingOpacityMicromapBuild(const OpacityMicromap& opacityMicromap)const noexcept;
    void commitPendingOpacityMicromapBuildCommits()noexcept;
    void discardPendingRecordingCommits()noexcept;
    void clearTrackedReferences()noexcept;


private:
    VkCommandBuffer m_cmdBuf = VK_NULL_HANDLE;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    Futex* m_sharedCommandPoolMutex = nullptr;

    CommandBufferResourceReferences m_resourceReferences;
    Vector<BufferHandle, Alloc::GlobalArena> m_referencedStagingBuffers;
    Vector<NotNull<GpuDescriptorHeap*>, Alloc::GlobalArena> m_referencedDescriptorHeaps;
    Vector<RetainedTextureStateCommit, Alloc::GlobalArena> m_retainedTextureStateCommits;
    AccelStructBuildJournal m_accelStructJournal;
    Vector<PendingOpacityMicromapBuildCommit, Alloc::GlobalArena> m_pendingOpacityMicromapBuildCommits;
    TimerQueryClaimJournal m_timerQueryClaims;
    DescriptorBufferManager* m_descriptorBufferManager = nullptr;
    u64 m_descriptorBufferGeneration = 0u;

    u64 m_recordingID = 0;
    u64 m_submissionID = 0;
    // Explicit graph-worker leases own one queue-local Vulkan pool each.
    // Recycled buffers retain that identity until the queue timeline retires them; default/direct lease zero instead keeps its private pool.
    u64 m_recordingWorkerDomain = 0u;
    u32 m_recordingWorkerIndex = 0u;
    TrackedCommandBufferArenaState::Enum m_arenaState = TrackedCommandBufferArenaState::Untracked;
    b8 m_ownsCmdPool = false;

    const VulkanContext& m_context;
    Queue& m_queue;
};
typedef Handle<TrackedCommandBuffer> TrackedCommandBufferPtr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

