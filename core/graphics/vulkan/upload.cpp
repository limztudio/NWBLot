// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UploadManagerChunk::UploadManagerChunk(CpuTaskScheduler& pool, BufferHandle buf, UploadManager* chunkManager, TrackedCommandBuffer& chunkOwner, u64 chunkNativeRecordingID, GpuPhysicalQueueId queue, u64 sz)noexcept
    : RefCounter<GraphicsResource>(pool)
    , buffer(Move(buf))
    , manager(chunkManager)
    , owner(&chunkOwner)
    , previousListChunk(nullptr)
    , nextListChunk(nullptr)
    , nativeRecordingID(chunkNativeRecordingID)
    , size(sz)
    , allocated(0)
    , version(0)
    , physicalQueue(queue)
{}

UploadManagerChunk::~UploadManagerChunk()noexcept = default;

Expected<BufferSuballocation> UploadManager::trySuballocateFromChunk(BufferChunk& chunk, const u64 size, const u32 alignment)noexcept{
    const auto alignedOffset = AlignUpChecked<u64>(chunk.allocated, static_cast<u64>(alignment));
    if(!alignedOffset)
        return MakeUnexpected(Failure{});
    if(*alignedOffset > chunk.size || size > chunk.size - *alignedOffset)
        return MakeUnexpected(Failure{});

    Buffer& buffer = *chunk.buffer;
    void* const cpuAddress = buffer.m_mappedMemory ? static_cast<u8*>(buffer.m_mappedMemory) + *alignedOffset : nullptr;

    chunk.allocated = *alignedOffset + size;
    return BufferSuballocation{ MakeNotNull(&buffer), *alignedOffset, cpuAddress };
}

UploadManager::UploadManager(Device& parent, u64 defaultChunkSize, u64 memoryLimit, const ChunkKind::Enum chunkKind)
    : m_device(parent)
    , m_defaultChunkSize(defaultChunkSize)
    , m_memoryLimit(memoryLimit)
    , m_chunkKind(chunkKind)
    , m_queueChunkLedgers(m_device.m_context.objectArena)
{}

UploadManager::~UploadManager()noexcept{
    clear();
}

void UploadManager::clear()noexcept{
    const usize chunkSlot = static_cast<usize>(m_chunkKind);
    for(QueueChunkLedger& entry : m_queueChunkLedgers){
        for(BufferChunk* chunk = entry.firstActiveChunk; chunk; chunk = chunk->nextListChunk){
            if(chunk->owner && chunk->owner->m_activeUploadChunks[chunkSlot] == chunk)
                chunk->owner->m_activeUploadChunks[chunkSlot] = nullptr;
        }
        entry.firstActiveChunk = nullptr;
        entry.firstRetiredChunk = nullptr;
        entry.chunks.clear();
    }
    m_queueChunkLedgers.clear();
    m_retiredChunkBytes = 0u;
}

Expected<BufferSuballocation> UploadManager::suballocateBuffer(const u64 size, TrackedCommandBuffer* const owner, const u64 nativeRecordingID, const GpuPhysicalQueueId queue, const u32 alignment){
    if(!owner || nativeRecordingID == 0u)
        return MakeUnexpected(Failure{});
    const usize chunkSlot = static_cast<usize>(m_chunkKind);
    FOUNDATION_ASSERT(chunkSlot < owner->m_activeUploadChunks.size());

    BufferChunk* const cachedChunk = owner->m_activeUploadChunks[chunkSlot];
    if(cachedChunk && cachedChunk->manager == this && cachedChunk->owner == owner && cachedChunk->nativeRecordingID == nativeRecordingID && cachedChunk->physicalQueue == queue){
        const auto fastResult = trySuballocateFromChunk(*cachedChunk, size, alignment);
        if(fastResult)
            return fastResult;
    }

    if(!m_device.matchesPhysicalQueueIdentity(queue))
        return MakeUnexpected(Failure{});

    UniqueLock<Futex> lock(m_mutex);
    QueueChunkLedger* activeLedger = &findOrCreateQueueLedgerLocked(queue);

    for(BufferChunk* chunk = activeLedger->firstActiveChunk; chunk; chunk = chunk->nextListChunk){
        if(chunk != cachedChunk && chunk->owner == owner && chunk->nativeRecordingID == nativeRecordingID){
            const auto result = trySuballocateFromChunk(*chunk, size, alignment);
            if(result){
                owner->m_activeUploadChunks[chunkSlot] = chunk;
                return result;
            }
        }
    }

    const auto findRetiredMatch = [](QueueChunkLedger& ledger, const u64 requiredSize, const u64 completedVersion, b8& outHasSizedPending)noexcept -> BufferChunk*{
        outHasSizedPending = false;
        for(BufferChunk* chunk = ledger.firstRetiredChunk; chunk; chunk = chunk->nextListChunk){
            FOUNDATION_ASSERT(!chunk->owner && chunk->nativeRecordingID == 0u && chunk->physicalQueue == ledger.queue);
            if(chunk->size < requiredSize)
                continue;
            if(chunk->version <= completedVersion)
                return chunk;
            outHasSizedPending = true;
        }
        return nullptr;
    };

    b8 hasSizedPending = false;
    BufferChunk* reusableChunk = findRetiredMatch(*activeLedger, size, activeLedger->lastKnownCompletedVersion, hasSizedPending);
    if(!reusableChunk && hasSizedPending){
        lock.unlock();
        const Expected<u64> completedVersion = m_device.queueGetCompletedInstance(queue);
        lock.lock();
        activeLedger = &findOrCreateQueueLedgerLocked(queue);
        if(completedVersion){
            activeLedger->lastKnownCompletedVersion = Max(activeLedger->lastKnownCompletedVersion, *completedVersion);
            reusableChunk = findRetiredMatch(*activeLedger, size, activeLedger->lastKnownCompletedVersion, hasSizedPending);
        }
    }

    if(reusableChunk){
        unlinkRetiredChunkLocked(*activeLedger, *reusableChunk);
        m_retiredChunkBytes = SubtractSaturating(m_retiredChunkBytes, reusableChunk->size);
        reusableChunk->owner = owner;
        reusableChunk->nativeRecordingID = nativeRecordingID;
        reusableChunk->allocated = 0u;
        reusableChunk->version = activeLedger->lastKnownCompletedVersion;
        linkActiveChunkLocked(*activeLedger, *reusableChunk);
        owner->m_activeUploadChunks[chunkSlot] = reusableChunk;
        return trySuballocateFromChunk(*reusableChunk, size, alignment);
    }

    const u64 versionForNewChunk = activeLedger->lastKnownCompletedVersion;
    lock.unlock();

    auto chunkSize = Max<u64>(size, m_defaultChunkSize);

    BufferDesc bufferDesc;
    bufferDesc.byteSize = chunkSize;
    bufferDesc.isVolatile = false;
    switch(m_chunkKind){
    case ChunkKind::Upload:
        bufferDesc.cpuAccess = CpuAccessMode::Write;
        bufferDesc.debugName = "UploadBuffer";
        break;
    case ChunkKind::Scratch:
        bufferDesc.cpuAccess = CpuAccessMode::None;
        bufferDesc.debugName = "ScratchBuffer";
        bufferDesc.structStride = 1u;
        bufferDesc.canHaveUAVs = true;
        break;
    case ChunkKind::AccelStructBuildInput:
        bufferDesc.cpuAccess = CpuAccessMode::Write;
        bufferDesc.debugName = "AccelStructBuildInputBuffer";
        bufferDesc.isAccelStructBuildInput = true;
        break;
    }

    auto bufferHandleResult = m_device.createBuffer(bufferDesc);
    if(!bufferHandleResult)
        return MakeUnexpected(Failure{});
    BufferHandle bufferHandle = Move(*bufferHandleResult);

    BufferChunkPtr createdChunk = MakeRefCount<BufferChunk>(m_device.m_context.cpuScheduler, Move(bufferHandle), this, *owner, nativeRecordingID, queue, chunkSize);
    createdChunk->version = versionForNewChunk;
    const Expected<BufferSuballocation> result = trySuballocateFromChunk(*createdChunk, size, alignment);
    if(!result)
        return MakeUnexpected(Failure{});

    lock.lock();
    activeLedger = &findOrCreateQueueLedgerLocked(queue);
    createdChunk->chunkVectorIndex = activeLedger->chunks.size();
    activeLedger->chunks.push_back(Move(createdChunk));
    BufferChunk& installedChunk = *activeLedger->chunks.back();
    linkActiveChunkLocked(*activeLedger, installedChunk);
    owner->m_activeUploadChunks[chunkSlot] = &installedChunk;
    return result;
}

void UploadManager::submitChunks(
    const GpuPhysicalQueueId queue,
    const u64 submittedVersion,
    const Span<const Queue::SubmissionCommandListIdentity> submittedCommandLists,
    const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
)noexcept{
    static_assert(noexcept(retireSubmittedChunksLocked(queue, submittedVersion, submittedCommandLists, submittedOwners)), "accepted upload-chunk retirement must remain non-throwing");
    if(!m_device.matchesPhysicalQueueIdentity(queue) || submittedCommandLists.empty())
        return;

    NothrowScopedLock lock(m_mutex);
    retireSubmittedChunksLocked(queue, submittedVersion, submittedCommandLists, submittedOwners);
}

void UploadManager::discardChunks(const GpuPhysicalQueueId queue, TrackedCommandBuffer* const owner, const u64 nativeRecordingID, const u64 reusableVersion)noexcept{
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !owner || nativeRecordingID == 0u)
        return;

    ScopedLock lock(m_mutex);
    retireOwnerChunksLocked(queue, reusableVersion, true, nativeRecordingID, *owner);
    trimRetiredChunksLocked(queue, reusableVersion);
}

void UploadManager::abandonChunks(const GpuPhysicalQueueId queue, TrackedCommandBuffer* const owner, const u64 nativeRecordingID)noexcept{
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !owner || nativeRecordingID == 0u)
        return;

    NothrowScopedLock lock(m_mutex);
    retireOwnerChunksLocked(queue, 0u, true, nativeRecordingID, *owner);
}

void UploadManager::collectCompletedChunks(){
    for(const GpuPhysicalQueueInfo& queueInfo : m_device.m_physicalQueueInfos){
        const Expected<u64> completedVersion = m_device.queueGetCompletedInstance(queueInfo.id);
        if(!completedVersion)
            continue;
        ScopedLock lock(m_mutex);
        if(QueueChunkLedger* const ledger = findQueueLedgerLocked(queueInfo.id))
            ledger->lastKnownCompletedVersion = Max(ledger->lastKnownCompletedVersion, *completedVersion);

        trimRetiredChunksLocked(queueInfo.id, *completedVersion);
    }
}

void UploadManager::trimRetiredChunksLocked(const GpuPhysicalQueueId queue, const u64 completedVersion)noexcept{
    if(m_memoryLimit == 0 || m_retiredChunkBytes <= m_memoryLimit)
        return;

    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    BufferChunk* chunk = ledger->firstRetiredChunk;
    while(m_retiredChunkBytes > m_memoryLimit && chunk){
        BufferChunk* const nextChunk = chunk->nextListChunk;
        FOUNDATION_ASSERT(!chunk->owner && chunk->physicalQueue == queue);
        if(chunk->version > completedVersion){
            chunk = nextChunk;
            continue;
        }

        unlinkRetiredChunkLocked(*ledger, *chunk);
        m_retiredChunkBytes = SubtractSaturating(m_retiredChunkBytes, chunk->size);
        const usize removeIndex = chunk->chunkVectorIndex;
        const usize lastIndex = ledger->chunks.size() - 1u;
        FOUNDATION_ASSERT(removeIndex <= lastIndex && ledger->chunks[removeIndex].get() == chunk);
        if(removeIndex != lastIndex){
            ledger->chunks[removeIndex] = Move(ledger->chunks[lastIndex]);
            ledger->chunks[removeIndex]->chunkVectorIndex = removeIndex;
        }
        ledger->chunks.pop_back();
        chunk = nextChunk;
    }
}

UploadManager::QueueChunkLedger* UploadManager::findQueueLedgerLocked(const GpuPhysicalQueueId queue)noexcept{
    if(!queue.valid())
        return nullptr;
    for(QueueChunkLedger& entry : m_queueChunkLedgers){
        if(entry.queue == queue)
            return &entry;
    }
    return nullptr;
}

UploadManager::QueueChunkLedger& UploadManager::findOrCreateQueueLedgerLocked(const GpuPhysicalQueueId queue){
    FOUNDATION_ASSERT(queue.valid());
    if(QueueChunkLedger* const ledger = findQueueLedgerLocked(queue))
        return *ledger;

    m_queueChunkLedgers.emplace_back(m_device.m_context.objectArena, queue);
    return m_queueChunkLedgers.back();
}

void UploadManager::linkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    chunk.previousListChunk = nullptr;
    chunk.nextListChunk = ledger.firstActiveChunk;
    if(ledger.firstActiveChunk)
        ledger.firstActiveChunk->previousListChunk = &chunk;
    ledger.firstActiveChunk = &chunk;
}

void UploadManager::unlinkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    if(chunk.previousListChunk)
        chunk.previousListChunk->nextListChunk = chunk.nextListChunk;
    else
        ledger.firstActiveChunk = chunk.nextListChunk;
    if(chunk.nextListChunk)
        chunk.nextListChunk->previousListChunk = chunk.previousListChunk;
    chunk.previousListChunk = nullptr;
    chunk.nextListChunk = nullptr;
}

void UploadManager::linkRetiredChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    chunk.previousListChunk = nullptr;
    chunk.nextListChunk = ledger.firstRetiredChunk;
    if(ledger.firstRetiredChunk)
        ledger.firstRetiredChunk->previousListChunk = &chunk;
    ledger.firstRetiredChunk = &chunk;
}

void UploadManager::unlinkRetiredChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    if(chunk.previousListChunk)
        chunk.previousListChunk->nextListChunk = chunk.nextListChunk;
    else
        ledger.firstRetiredChunk = chunk.nextListChunk;
    if(chunk.nextListChunk)
        chunk.nextListChunk->previousListChunk = chunk.previousListChunk;
    chunk.previousListChunk = nullptr;
    chunk.nextListChunk = nullptr;
}

void UploadManager::retireChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk, const u64 version, const b8 resetAllocated)noexcept{
    const usize chunkSlot = static_cast<usize>(m_chunkKind);
    if(chunk.owner && chunk.owner->m_activeUploadChunks[chunkSlot] == &chunk)
        chunk.owner->m_activeUploadChunks[chunkSlot] = nullptr;
    unlinkActiveChunkLocked(ledger, chunk);

    chunk.owner = nullptr;
    chunk.nativeRecordingID = 0u;
    if(resetAllocated)
        chunk.allocated = 0u;
    chunk.version = version;
    linkRetiredChunkLocked(ledger, chunk);
    m_retiredChunkBytes = AddSaturating<u64>(m_retiredChunkBytes, chunk.size);
}

void UploadManager::retireSubmittedChunksLocked(
    const GpuPhysicalQueueId queue,
    const u64 version,
    const Span<const Queue::SubmissionCommandListIdentity> submittedCommandLists,
    const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
)noexcept{
    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    BufferChunk* chunk = ledger->firstActiveChunk;
    while(chunk){
        BufferChunk* const nextChunk = chunk->nextListChunk;
        FOUNDATION_ASSERT(chunk->owner && chunk->nativeRecordingID != 0u);
        b8 submitted = false;
        if(submittedOwners.indexed())
            submitted = submittedOwners.contains(*chunk->owner, chunk->nativeRecordingID);
        else{
            for(const Queue::SubmissionCommandListIdentity& commandList : submittedCommandLists){
                if(commandList.owner == chunk->owner && commandList.nativeRecordingID == chunk->nativeRecordingID){
                    submitted = true;
                    break;
                }
            }
        }

        if(submitted)
            retireChunkLocked(*ledger, *chunk, version, false);
        chunk = nextChunk;
    }
}

void UploadManager::retireOwnerChunksLocked(const GpuPhysicalQueueId queue, const u64 version, const b8 resetAllocated, const u64 nativeRecordingID, TrackedCommandBuffer& owner)noexcept{
    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    BufferChunk* chunk = ledger->firstActiveChunk;
    while(chunk){
        BufferChunk* const nextChunk = chunk->nextListChunk;
        if(chunk->owner == &owner && chunk->nativeRecordingID == nativeRecordingID)
            retireChunkLocked(*ledger, *chunk, version, resetAllocated);
        chunk = nextChunk;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

