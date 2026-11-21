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
    , nextOwnerChunk(nullptr)
    , nativeRecordingID(chunkNativeRecordingID)
    , size(sz)
    , allocated(0)
    , ownerStartAllocated(0)
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
            chunk->nextOwnerChunk = nullptr;
            if(chunk->owner)
                chunk->owner->m_activeUploadChunks[chunkSlot] = nullptr;
        }
        entry.firstActiveChunk = nullptr;
        entry.firstRetiredChunk = nullptr;
        entry.lastRetiredChunk = nullptr;
        entry.chunks.clear();
    }
    m_queueChunkLedgers.clear();
    m_retiredChunkBytes = 0u;
    m_hasAnyChunks.store(false, MemoryOrder::release);
}

Expected<BufferSuballocation> UploadManager::suballocateBuffer(const u64 size, TrackedCommandBuffer* const owner, const u64 nativeRecordingID, const GpuPhysicalQueueId queue, const u32 alignment){
    if(!owner || nativeRecordingID == 0u)
        return MakeUnexpected(Failure{});
    const usize chunkSlot = static_cast<usize>(m_chunkKind);
    const u8 slotBit = static_cast<u8>(1u << chunkSlot);
    FOUNDATION_ASSERT(chunkSlot < owner->m_activeUploadChunks.size());

    BufferChunk* const cachedChunk = owner->m_activeUploadChunks[chunkSlot];
    if(cachedChunk && cachedChunk->manager == this && cachedChunk->owner == owner && cachedChunk->nativeRecordingID == nativeRecordingID && cachedChunk->physicalQueue == queue){
        const auto fastResult = trySuballocateFromChunk(*cachedChunk, size, alignment);
        if(fastResult){
            FOUNDATION_ASSERT((owner->m_usedUploadChunkMask & slotBit) != 0u);
            return fastResult;
        }
    }

    if(!m_device.matchesPhysicalQueueIdentity(queue))
        return MakeUnexpected(Failure{});

    UniqueLock<Futex> lock(m_mutex);
    QueueChunkLedger* activeLedger = &findOrCreateQueueLedgerLocked(queue);

    if((owner->m_usedUploadChunkMask & slotBit) != 0u){
        for(BufferChunk* prev = nullptr, *chunk = owner->m_activeUploadChunks[chunkSlot]; chunk; prev = chunk, chunk = chunk->nextOwnerChunk){
            if(chunk != cachedChunk && chunk->owner == owner && chunk->nativeRecordingID == nativeRecordingID && chunk->physicalQueue == queue){
                const auto result = trySuballocateFromChunk(*chunk, size, alignment);
                if(result){
                    if(prev){
                        prev->nextOwnerChunk = chunk->nextOwnerChunk;
                        chunk->nextOwnerChunk = owner->m_activeUploadChunks[chunkSlot];
                        owner->m_activeUploadChunks[chunkSlot] = chunk;
                    }
                    return result;
                }
            }
        }
    }

    if(Queue* const queueObj = m_device.getQueue(queue)){
        const u64 observedVersion = queueObj->lastObservedFinishedID();
        if(observedVersion > activeLedger->lastKnownCompletedVersion)
            activeLedger->lastKnownCompletedVersion = observedVersion;
    }

    const b8 allowTailContinuation = supportsTailContinuation();
    const auto findRetiredMatch = [size, alignment, allowTailContinuation](QueueChunkLedger& ledger, const u64 completedVersion, BufferChunk*& outTailCandidate, b8& outHasSizedPending)noexcept -> BufferChunk*{
        outTailCandidate = nullptr;
        outHasSizedPending = false;
        for(BufferChunk* chunk = ledger.firstRetiredChunk; chunk; chunk = chunk->nextListChunk){
            FOUNDATION_ASSERT(!chunk->owner && chunk->nativeRecordingID == 0u && chunk->physicalQueue == ledger.queue);
            if(chunk->version <= completedVersion){
                if(chunk->size >= size)
                    return chunk;
                continue;
            }
            if(chunk->size >= size)
                outHasSizedPending = true;
            if(allowTailContinuation && !outTailCandidate){
                const auto alignedOffset = AlignUpChecked<u64>(chunk->allocated, static_cast<u64>(alignment));
                if(alignedOffset && *alignedOffset <= chunk->size && size <= chunk->size - *alignedOffset)
                    outTailCandidate = chunk;
            }
        }
        return nullptr;
    };

    BufferChunk* tailCandidate = nullptr;
    b8 hasSizedPending = false;
    BufferChunk* reusableChunk = findRetiredMatch(*activeLedger, activeLedger->lastKnownCompletedVersion, tailCandidate, hasSizedPending);
    if(!reusableChunk && !tailCandidate && hasSizedPending){
        lock.unlock();
        const Expected<u64> completedVersion = m_device.queueGetCompletedInstance(queue);
        lock.lock();
        activeLedger = &findOrCreateQueueLedgerLocked(queue);
        if(completedVersion){
            activeLedger->lastKnownCompletedVersion = Max(activeLedger->lastKnownCompletedVersion, *completedVersion);
            reusableChunk = findRetiredMatch(*activeLedger, activeLedger->lastKnownCompletedVersion, tailCandidate, hasSizedPending);
        }
    }

    if(reusableChunk || tailCandidate){
        const b8 completedReuse = reusableChunk != nullptr;
        BufferChunk* const claimedChunk = completedReuse ? reusableChunk : tailCandidate;
        unlinkRetiredChunkLocked(*activeLedger, *claimedChunk);
        m_retiredChunkBytes = SubtractSaturating(m_retiredChunkBytes, claimedChunk->size);
        claimedChunk->owner = owner;
        claimedChunk->nativeRecordingID = nativeRecordingID;
        if(completedReuse){
            claimedChunk->allocated = 0u;
            claimedChunk->ownerStartAllocated = 0u;
            claimedChunk->version = 0u;
        }
        else
            claimedChunk->ownerStartAllocated = claimedChunk->allocated;
        linkActiveChunkLocked(*activeLedger, *claimedChunk);
        claimedChunk->nextOwnerChunk = owner->m_activeUploadChunks[chunkSlot];
        owner->m_activeUploadChunks[chunkSlot] = claimedChunk;
        owner->m_usedUploadChunkMask |= slotBit;
        return trySuballocateFromChunk(*claimedChunk, size, alignment);
    }

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
    createdChunk->version = 0u;
    createdChunk->ownerStartAllocated = 0u;
    const Expected<BufferSuballocation> result = trySuballocateFromChunk(*createdChunk, size, alignment);
    if(!result)
        return MakeUnexpected(Failure{});

    lock.lock();
    activeLedger = &findOrCreateQueueLedgerLocked(queue);
    createdChunk->chunkVectorIndex = activeLedger->chunks.size();
    activeLedger->chunks.push_back(Move(createdChunk));
    BufferChunk& installedChunk = *activeLedger->chunks.back();
    linkActiveChunkLocked(*activeLedger, installedChunk);
    installedChunk.nextOwnerChunk = owner->m_activeUploadChunks[chunkSlot];
    owner->m_activeUploadChunks[chunkSlot] = &installedChunk;
    owner->m_usedUploadChunkMask |= slotBit;
    m_hasAnyChunks.store(true, MemoryOrder::release);
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
    retireOwnerChunksLocked(queue, reusableVersion, nativeRecordingID, *owner);
    trimRetiredChunksLocked(queue, reusableVersion);
}

void UploadManager::abandonChunks(const GpuPhysicalQueueId queue, TrackedCommandBuffer* const owner, const u64 nativeRecordingID)noexcept{
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !owner || nativeRecordingID == 0u)
        return;

    NothrowScopedLock lock(m_mutex);
    retireOwnerChunksLocked(queue, 0u, nativeRecordingID, *owner);
}

void UploadManager::collectCompletedChunks(){
    if(!m_hasAnyChunks.load(MemoryOrder::acquire))
        return;
    {
        ScopedLock lock(m_mutex);
        if(m_memoryLimit == 0u || m_retiredChunkBytes <= m_memoryLimit)
            return;
    }
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
    if(ledger->chunks.empty()){
        b8 anyChunks = false;
        for(const QueueChunkLedger& entry : m_queueChunkLedgers){
            if(!entry.chunks.empty()){
                anyChunks = true;
                break;
            }
        }
        if(!anyChunks)
            m_hasAnyChunks.store(false, MemoryOrder::release);
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

void UploadManager::linkRetiredChunkHeadLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    chunk.previousListChunk = nullptr;
    chunk.nextListChunk = ledger.firstRetiredChunk;
    if(ledger.firstRetiredChunk)
        ledger.firstRetiredChunk->previousListChunk = &chunk;
    else
        ledger.lastRetiredChunk = &chunk;
    ledger.firstRetiredChunk = &chunk;
}

void UploadManager::linkRetiredChunkTailLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    chunk.previousListChunk = ledger.lastRetiredChunk;
    chunk.nextListChunk = nullptr;
    if(ledger.lastRetiredChunk)
        ledger.lastRetiredChunk->nextListChunk = &chunk;
    else
        ledger.firstRetiredChunk = &chunk;
    ledger.lastRetiredChunk = &chunk;
}

void UploadManager::unlinkRetiredChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    if(chunk.previousListChunk)
        chunk.previousListChunk->nextListChunk = chunk.nextListChunk;
    else
        ledger.firstRetiredChunk = chunk.nextListChunk;
    if(chunk.nextListChunk)
        chunk.nextListChunk->previousListChunk = chunk.previousListChunk;
    else
        ledger.lastRetiredChunk = chunk.previousListChunk;
    chunk.previousListChunk = nullptr;
    chunk.nextListChunk = nullptr;
}

void UploadManager::retireChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk, const u64 version, const b8 linkAtHead)noexcept{
    unlinkActiveChunkLocked(ledger, chunk);

    chunk.owner = nullptr;
    chunk.nativeRecordingID = 0u;
    chunk.version = version;
    if(linkAtHead)
        linkRetiredChunkHeadLocked(ledger, chunk);
    else
        linkRetiredChunkTailLocked(ledger, chunk);
    m_retiredChunkBytes = AddSaturating<u64>(m_retiredChunkBytes, chunk.size);
}

void UploadManager::retireSubmittedChunksLocked(
    const GpuPhysicalQueueId queue,
    const u64 version,
    const Span<const Queue::SubmissionCommandListIdentity> submittedCommandLists,
    const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
)noexcept{
    (void)submittedOwners;
    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    const usize chunkSlot = static_cast<usize>(m_chunkKind);
    const u8 slotBit = static_cast<u8>(1u << chunkSlot);
    for(const Queue::SubmissionCommandListIdentity& commandList : submittedCommandLists){
        TrackedCommandBuffer* const owner = commandList.owner;
        if(!owner || (owner->m_usedUploadChunkMask & slotBit) == 0u)
            continue;

        BufferChunk** link = &owner->m_activeUploadChunks[chunkSlot];
        while(BufferChunk* const chunk = *link){
            if(chunk->owner == owner && chunk->nativeRecordingID == commandList.nativeRecordingID && chunk->physicalQueue == queue){
                *link = chunk->nextOwnerChunk;
                chunk->nextOwnerChunk = nullptr;
                chunk->ownerStartAllocated = chunk->allocated;
                retireChunkLocked(*ledger, *chunk, Max(chunk->version, version), false);
            }
            else
                link = &chunk->nextOwnerChunk;
        }
        if(!owner->m_activeUploadChunks[chunkSlot])
            owner->m_usedUploadChunkMask = static_cast<u8>(owner->m_usedUploadChunkMask & ~slotBit);
    }
}

void UploadManager::retireOwnerChunksLocked(const GpuPhysicalQueueId queue, const u64 reusableVersion, const u64 nativeRecordingID, TrackedCommandBuffer& owner)noexcept{
    const usize chunkSlot = static_cast<usize>(m_chunkKind);
    const u8 slotBit = static_cast<u8>(1u << chunkSlot);
    if((owner.m_usedUploadChunkMask & slotBit) == 0u)
        return;

    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    BufferChunk** link = &owner.m_activeUploadChunks[chunkSlot];
    while(BufferChunk* const chunk = *link){
        if(chunk->owner == &owner && chunk->nativeRecordingID == nativeRecordingID && chunk->physicalQueue == queue){
            *link = chunk->nextOwnerChunk;
            chunk->nextOwnerChunk = nullptr;
            chunk->allocated = chunk->ownerStartAllocated;
            if(chunk->version <= reusableVersion){
                chunk->allocated = 0u;
                chunk->ownerStartAllocated = 0u;
                retireChunkLocked(*ledger, *chunk, 0u, true);
            }
            else
                retireChunkLocked(*ledger, *chunk, chunk->version, false);
        }
        else
            link = &chunk->nextOwnerChunk;
    }
    if(!owner.m_activeUploadChunks[chunkSlot])
        owner.m_usedUploadChunkMask = static_cast<u8>(owner.m_usedUploadChunkMask & ~slotBit);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

