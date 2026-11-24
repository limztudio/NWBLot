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


struct UploadManagerChunk final : public RefCounter<GraphicsResource>{
    BufferHandle buffer;
    UploadManager* manager = nullptr;
    TrackedCommandBuffer* owner = nullptr;
    UploadManagerChunk* previousListChunk = nullptr;
    UploadManagerChunk* nextListChunk = nullptr;
    UploadManagerChunk* nextOwnerChunk = nullptr;
    usize chunkVectorIndex = 0u;
    u64 nativeRecordingID = 0u;
    u64 size = 0u;
    u64 allocated = 0u;
    u64 ownerStartAllocated = 0u;
    u64 version = 0u;
    GpuPhysicalQueueId physicalQueue;


public:
    UploadManagerChunk(CpuTaskScheduler& pool, BufferHandle buf, UploadManager* chunkManager, TrackedCommandBuffer& chunkOwner, u64 chunkNativeRecordingID, GpuPhysicalQueueId queue, u64 sz)noexcept;
    ~UploadManagerChunk()noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UploadManager final : NoCopy{
    friend class CommandList;
    friend class Device;


private:
    struct ChunkKind{
        enum Enum : u8{
            Upload,
            Scratch,
            AccelStructBuildInput,
        };
    };

    using BufferChunk = UploadManagerChunk;


private:
    using BufferChunkPtr = RefCountPtr<BufferChunk>;
    using BufferChunkList = Vector<BufferChunkPtr, Alloc::GlobalArena>;


private:
    struct QueueChunkLedger{
        GpuPhysicalQueueId queue;
        BufferChunkList chunks;
        BufferChunk* firstActiveChunk = nullptr;
        BufferChunk* firstRetiredChunk = nullptr;
        BufferChunk* lastRetiredChunk = nullptr;
        u64 lastKnownCompletedVersion = 0u;


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
    [[nodiscard]] Expected<BufferSuballocation> suballocateBuffer(u64 size, TrackedCommandBuffer* owner, u64 nativeRecordingID, GpuPhysicalQueueId queue, u32 alignment = s_DefaultUploadSuballocationAlignment);
    void submitChunks(GpuPhysicalQueueId queue, u64 submittedVersion, Span<const Queue::SubmissionCommandListIdentity> submittedCommandLists)noexcept;
    void discardChunks(GpuPhysicalQueueId queue, TrackedCommandBuffer* owner, u64 nativeRecordingID, u64 reusableVersion)noexcept;
    void abandonChunks(GpuPhysicalQueueId queue, TrackedCommandBuffer* owner, u64 nativeRecordingID)noexcept;


private:
    [[nodiscard]] b8 supportsTailContinuation()const noexcept{
        return m_chunkKind == ChunkKind::Upload || m_chunkKind == ChunkKind::AccelStructBuildInput;
    }
    [[nodiscard]] static Expected<BufferSuballocation> TrySuballocateFromChunk(BufferChunk& chunk, u64 size, u32 alignment)noexcept;
    [[nodiscard]] Expected<BufferSuballocation> suballocateBufferSlow(u64 size, TrackedCommandBuffer* owner, u64 nativeRecordingID, GpuPhysicalQueueId queue, u32 alignment, usize chunkSlot, u8 slotBit, BufferChunk* cachedChunk);
    void collectCompletedChunks();
    void trimRetiredChunksLocked(GpuPhysicalQueueId queue, u64 completedVersion)noexcept;
    [[nodiscard]] QueueChunkLedger* findQueueLedgerLocked(GpuPhysicalQueueId queue)noexcept;
    [[nodiscard]] QueueChunkLedger& findOrCreateQueueLedgerLocked(GpuPhysicalQueueId queue);
    void linkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void unlinkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void linkRetiredChunkHeadLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void linkRetiredChunkTailLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void unlinkRetiredChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void retireChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk, u64 version, b8 linkAtHead)noexcept;
    void retireSubmittedChunksLocked(GpuPhysicalQueueId queue, u64 version, Span<const Queue::SubmissionCommandListIdentity> submittedCommandLists)noexcept;
    void retireOwnerChunksLocked(GpuPhysicalQueueId queue, u64 reusableVersion, u64 nativeRecordingID, TrackedCommandBuffer& owner)noexcept;


private:
    Device& m_device;
    u64 m_defaultChunkSize;
    u64 m_memoryLimit;
    Futex m_mutex;
    ChunkKind::Enum m_chunkKind;
    u64 m_retiredChunkBytes = 0;
    Atomic<b8> m_hasAnyChunks{ false };

    GraphicsDeque<QueueChunkLedger> m_queueChunkLedgers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

