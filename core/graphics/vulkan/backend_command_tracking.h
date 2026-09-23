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


// Command buffer with resource tracking


struct RetainedTextureStateCommit{
    Texture* texture = nullptr;
    MipLevel mipLevel = 0;
    ArraySlice arraySlice = 0;
};

struct AccelStructGeometryBuildSignature{
    VkDeviceSize vertexStride = 0u;
    VkDeviceSize radiusStride = 0u;
    VkDeviceSize indexStride = 0u;
    VkGeometryTypeKHR geometryType = VK_GEOMETRY_TYPE_MAX_ENUM_KHR;
    VkGeometryFlagsKHR geometryFlags = 0u;
    u32 primitiveCount = 0u;
    VkFormat vertexFormat = VK_FORMAT_UNDEFINED;
    VkFormat radiusFormat = VK_FORMAT_UNDEFINED;
    VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
    u32 maxVertex = 0u;
    VkRayTracingLssIndexingModeNV lssIndexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_MAX_ENUM_NV;
    VkRayTracingLssPrimitiveEndCapsModeNV lssEndCapsMode = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_MAX_ENUM_NV;
    bool transformDataPresent = false;
};

struct AccelStructBuildSignatureRole{
    VkAccelerationStructureTypeKHR accelStructType = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;
    VkBuildAccelerationStructureFlagsKHR buildFlags = 0u;
    Vector<AccelStructGeometryBuildSignature, Alloc::GlobalArena> geometrySignatures;
    AccelStructBuildSignatureRole* nextRetiredRole = nullptr;


    explicit AccelStructBuildSignatureRole(Alloc::GlobalArena& arena)
        : geometrySignatures(arena)
    {}
};

struct PendingAccelStructBuildCommit{
    AccelStruct* accelStruct = nullptr;
    AccelStructBuildSignatureRole* preparedRole = nullptr;
    AccelStructBuildSignatureRole* displacedRole = nullptr;
};
static_assert(IsTriviallyCopyable_V<PendingAccelStructBuildCommit>, "accepted acceleration-structure publication must remain scalar-only");

struct PendingOpacityMicromapBuildCommit{
    OpacityMicromap* opacityMicromap = nullptr;
};

namespace TrackedCommandBufferArenaState{
    static constexpr u8 kTrackedCommandBufferArenaStateUntrackedBase = 0u;
    enum Enum : u8{
        Untracked = kTrackedCommandBufferArenaStateUntrackedBase,
        Leased,
        Reusable,
        Pending,
    };
};

struct QueueSubmissionWait{
    VkSemaphore semaphore = VK_NULL_HANDLE;
    u64 value = 0u;
};

class TrackedCommandBuffer final : public RefCounter<GraphicsResource>, NoCopy{
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class Queue;
    friend class StateTracker;
    friend class GpuDescriptorHeap;
    friend class VulkanTestDispatchAccess;


public:
    TrackedCommandBuffer(
        Queue& queue,
        const VulkanContext& context,
        u32 queueFamilyIndex,
        VkCommandPool commandPool,
        bool ownsCommandPool,
        Futex* sharedCommandPoolMutex
    );
    ~TrackedCommandBuffer();


private:
    struct TimerQueryRecordingClaim{
        TimerQuery* query = nullptr;
        GpuPhysicalQueueId queue;
        QueueSubmissionToken resetAuthorizationSubmission;
        QueueSubmissionToken prerequisiteSubmission;
        u64 queryIncarnation = 0u;
        u64 generation = 0u;
        u64 resetRecordingAuthorizationGeneration = 0u;
        u64 consumedResetAuthorizationGeneration = 0u;
        u64 recordingID = 0u;
        bool prerequisiteObservedComplete = false;
        bool recordsReset = false;
        bool recordsBegin = false;
        bool recordsEnd = false;
        bool consumesResetAuthorization = false;
    };

    [[nodiscard]] TimerQueryRecordingClaim& findOrAppendTimerQueryRecordingClaim(TimerQuery& query);
    [[nodiscard]] TimerQueryRecordingClaim* findTimerQueryRecordingClaim(
        TimerQuery& query,
        u64 generation
    )noexcept;
    [[nodiscard]] bool recordsTimerQueryBegin(const TimerQuery& query, u64 generation)const noexcept;
    [[nodiscard]] bool validateTimerQueryRecordingClaims(
        const Queue& submissionQueue,
        const QueueSubmissionWait* localWaits,
        usize localWaitCount,
        TrackedCommandBuffer* const* precedingCommandBuffers,
        usize precedingCommandBufferCount
    )const noexcept;
    void commitTimerQueryRecordingClaims(const QueueSubmissionToken& submissionToken)noexcept;
    void discardTimerQueryRecordingClaims()noexcept;
    void commitRetainedBufferStateCommits()noexcept;
    void appendRetainedTextureStateCommit(Texture& texture, MipLevel mipLevel, ArraySlice arraySlice);
    void commitRetainedTextureStateCommits()noexcept;
    void discardRetainedTextureStateCommits()noexcept;
    [[nodiscard]] bool appendPendingAccelStructBuildCommit(
        AccelStruct& accelStruct,
        VkAccelerationStructureTypeKHR accelStructType,
        VkBuildAccelerationStructureFlagsKHR buildFlags,
        const AccelStructGeometryBuildSignature* geometrySignatures,
        usize geometrySignatureCount
    );
    [[nodiscard]] bool getPendingAccelStructBuildSignature(
        const AccelStruct& accelStruct,
        VkAccelerationStructureTypeKHR& outAccelStructType,
        VkBuildAccelerationStructureFlagsKHR& outBuildFlags,
        const AccelStructGeometryBuildSignature*& outGeometrySignatures,
        usize& outGeometrySignatureCount
    )const;
    [[nodiscard]] bool validatePendingAccelStructBuildCommits()const noexcept;
    void commitPendingAccelStructBuildCommits()noexcept;
    void releasePendingAccelStructBuildCommits();
    void abandonPendingAccelStructBuildCommits()noexcept;
    void appendPendingOpacityMicromapBuildCommit(OpacityMicromap& opacityMicromap);
    [[nodiscard]] bool hasPendingOpacityMicromapBuild(const OpacityMicromap& opacityMicromap)const;
    void commitPendingOpacityMicromapBuildCommits()noexcept;
    void discardPendingOpacityMicromapBuildCommits()noexcept;
    void clearTrackedReferences()noexcept;


private:
    VkCommandBuffer m_cmdBuf = VK_NULL_HANDLE;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    Futex* m_sharedCommandPoolMutex = nullptr;

    CommandBufferResourceReferences m_resourceReferences;
    Vector<BufferHandle, Alloc::GlobalArena> m_referencedStagingBuffers;
    Vector<GpuDescriptorHeap*, Alloc::GlobalArena> m_referencedDescriptorHeaps;
    Vector<RetainedTextureStateCommit, Alloc::GlobalArena> m_retainedTextureStateCommits;
    Vector<PendingAccelStructBuildCommit, Alloc::GlobalArena> m_pendingAccelStructBuildCommits;
    Vector<PendingOpacityMicromapBuildCommit, Alloc::GlobalArena> m_pendingOpacityMicromapBuildCommits;
    Vector<TimerQueryRecordingClaim, Alloc::GlobalArena> m_timerQueryRecordingClaims;
    DescriptorBufferManager* m_descriptorBufferManager = nullptr;
    u64 m_descriptorBufferGeneration = 0u;

    u64 m_recordingID = 0;
    u64 m_submissionID = 0;
    // Explicit graph-worker leases own one queue-local Vulkan pool each. Recycled buffers retain that identity
    // until the queue timeline retires them; default/direct lease zero instead keeps its private pool.
    u64 m_recordingWorkerDomain = 0u;
    u32 m_recordingWorkerIndex = 0u;

    const VulkanContext& m_context;
    Queue& m_queue;
    TrackedCommandBufferArenaState::Enum m_arenaState = TrackedCommandBufferArenaState::Untracked;
    bool m_ownsCmdPool = false;
};
typedef Handle<TrackedCommandBuffer> TrackedCommandBufferPtr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

