// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"
#include "backend_core_context.h"
#include "backend_command_tracking.h"
#include "backend_queue.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferSuballocation{
    NotNull<Buffer*> buffer;
    u64 offset = 0u;
    void* cpuAddress = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UploadManager final : NoCopy{
    friend class Device;


private:
    struct ChunkKind{
        enum Enum : u8{
            Upload,
            Scratch,
            // CPU-written, device-addressed acceleration-structure build inputs such as TLAS instances and BLAS transforms.
            AccelStructBuildInput,
        };
    };

    struct BufferChunk final : public RefCounter<GraphicsResource>{
        BufferHandle buffer;
        TrackedCommandBuffer* owner;
        BufferChunk* previousActiveChunk;
        BufferChunk* nextActiveChunk;
        u64 nativeRecordingID;
        u64 size;
        u64 allocated;
        u64 version;
        GpuPhysicalQueueId physicalQueue;


    public:
        BufferChunk(CpuTaskScheduler& pool, BufferHandle buf, TrackedCommandBuffer& chunkOwner, u64 chunkNativeRecordingID, GpuPhysicalQueueId queue, u64 sz)noexcept;
        ~BufferChunk()noexcept;
    };


private:
    using BufferChunkPtr = RefCountPtr<BufferChunk>;
    using BufferChunkList = List<BufferChunkPtr, Alloc::GlobalArena>;


private:
    // The owning list keeps every chunk address stable.
    // Accepted submissions traverse only this intrusive active chain, so accumulated retired storage cannot increase the post-native-accept commit cost.
    struct QueueChunkLedger{
        GpuPhysicalQueueId queue;
        BufferChunkList chunks;
        BufferChunk* firstActiveChunk = nullptr;


    public:
        QueueChunkLedger(GraphicsArena& arena, GpuPhysicalQueueId value)
            : queue(value)
            , chunks(arena)
        {}
    };


public:
    UploadManager(Device& parent, u64 defaultChunkSize, u64 memoryLimit, ChunkKind::Enum chunkKind);
    ~UploadManager()noexcept;


public:
    void clear()noexcept;
    [[nodiscard]] Expected<BufferSuballocation> suballocateBuffer(u64 size, TrackedCommandBuffer* owner, u64 nativeRecordingID, GpuPhysicalQueueId queue, u64 completedVersion, u32 alignment = s_DefaultUploadSuballocationAlignment);
    void submitChunks(GpuPhysicalQueueId queue, u64 submittedVersion, Span<const Queue::SubmissionCommandListIdentity> submittedCommandLists, const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners)noexcept;
    void discardChunks(GpuPhysicalQueueId queue, TrackedCommandBuffer* owner, u64 nativeRecordingID, u64 reusableVersion)noexcept;
    void abandonChunks(GpuPhysicalQueueId queue, TrackedCommandBuffer* owner, u64 nativeRecordingID)noexcept;


private:
    void collectCompletedChunks();
    void trimRetiredChunksLocked(GpuPhysicalQueueId queue, u64 completedVersion)noexcept;
    [[nodiscard]] QueueChunkLedger* findQueueLedgerLocked(GpuPhysicalQueueId queue)noexcept;
    [[nodiscard]] QueueChunkLedger& findOrCreateQueueLedgerLocked(GpuPhysicalQueueId queue);
    void linkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void retireChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk, u64 version, b8 resetAllocated)noexcept;
    void retireSubmittedChunksLocked(GpuPhysicalQueueId queue, u64 version, Span<const Queue::SubmissionCommandListIdentity> submittedCommandLists, const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners)noexcept;
    void retireOwnerChunksLocked(GpuPhysicalQueueId queue, u64 version, b8 resetAllocated, u64 nativeRecordingID, TrackedCommandBuffer& owner)noexcept;


private:
    Device& m_device;
    u64 m_defaultChunkSize;
    u64 m_memoryLimit;
    Futex m_mutex;
    ChunkKind::Enum m_chunkKind;
    u64 m_retiredChunkBytes = 0;

    GraphicsDeque<QueueChunkLedger> m_queueChunkLedgers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

