// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UploadManager::UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer)
    : m_device(pParent)
    , m_defaultChunkSize(defaultChunkSize)
    , m_memoryLimit(memoryLimit)
    , m_queueChunkLedgers(m_device.m_context.objectArena)
    , m_isScratchBuffer(isScratchBuffer)
{}
UploadManager::~UploadManager(){
    clear();
}

void UploadManager::clear(){
    for(QueueChunkLedger& entry : m_queueChunkLedgers)
        entry.chunks.clear();
    m_queueChunkLedgers.clear();
    m_retiredChunkBytes = 0u;
}

void UploadManager::collectCompletedChunks(){
    for(const GpuPhysicalQueueInfo& queueInfo : m_device.m_physicalQueueInfos){
        const u64 completedVersion = m_device.queueGetCompletedInstance(queueInfo.id);
        ScopedLock lock(m_mutex);

        trimRetiredChunksLocked(queueInfo.id, completedVersion);
    }
}

void UploadManager::trimRetiredChunksLocked(const GpuPhysicalQueueId queue, const u64 completedVersion){
    if(m_memoryLimit == 0 || m_retiredChunkBytes <= m_memoryLimit)
        return;

    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    auto it = ledger->chunks.begin();
    while(m_retiredChunkBytes > m_memoryLimit && it != ledger->chunks.end()){
        BufferChunkPtr& chunk = *it;
        if(!chunk){
            it = ledger->chunks.erase(it);
            continue;
        }
        if(chunk->owner || chunk->physicalQueue != queue || chunk->version > completedVersion){
            ++it;
            continue;
        }

        if(m_retiredChunkBytes >= chunk->size)
            m_retiredChunkBytes -= chunk->size;
        else
            m_retiredChunkBytes = 0u;
        it = ledger->chunks.erase(it);
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

UploadManager::QueueChunkLedger* UploadManager::findOrCreateQueueLedgerLocked(const GpuPhysicalQueueId queue){
    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(ledger || !queue.valid())
        return ledger;

    m_queueChunkLedgers.emplace_back(m_device.m_context.objectArena, queue);
    return &m_queueChunkLedgers.back();
}

void UploadManager::linkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept{
    chunk.previousActiveChunk = nullptr;
    chunk.nextActiveChunk = ledger.firstActiveChunk;
    if(ledger.firstActiveChunk)
        ledger.firstActiveChunk->previousActiveChunk = &chunk;
    ledger.firstActiveChunk = &chunk;
}

void UploadManager::retireChunkLocked(
    QueueChunkLedger& ledger,
    BufferChunk& chunk,
    const u64 version,
    const bool resetAllocated
)noexcept{
    if(chunk.previousActiveChunk)
        chunk.previousActiveChunk->nextActiveChunk = chunk.nextActiveChunk;
    else
        ledger.firstActiveChunk = chunk.nextActiveChunk;
    if(chunk.nextActiveChunk)
        chunk.nextActiveChunk->previousActiveChunk = chunk.previousActiveChunk;

    chunk.owner = nullptr;
    chunk.previousActiveChunk = nullptr;
    chunk.nextActiveChunk = nullptr;
    chunk.nativeRecordingID = 0u;
    if(resetAllocated)
        chunk.allocated = 0u;
    chunk.version = version;
    if(m_retiredChunkBytes > UINT64_MAX - chunk.size)
        m_retiredChunkBytes = UINT64_MAX;
    else
        m_retiredChunkBytes += chunk.size;
}

void UploadManager::retireSubmittedChunksLocked(
    const GpuPhysicalQueueId queue,
    const u64 version,
    const Queue::SubmissionCommandListIdentity* const submittedCommandLists,
    const usize submittedCommandListCount,
    const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
)noexcept{
    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    BufferChunk* chunk = ledger->firstActiveChunk;
    while(chunk){
        BufferChunk* const nextChunk = chunk->nextActiveChunk;
        bool submitted = false;
        if(chunk->owner){
            if(submittedOwners.indexed())
                submitted = submittedOwners.contains(*chunk->owner, chunk->nativeRecordingID);
            else{
                for(usize i = 0u; i < submittedCommandListCount; ++i){
                    const Queue::SubmissionCommandListIdentity& commandList = submittedCommandLists[i];
                    if(commandList.owner == chunk->owner && commandList.nativeRecordingID == chunk->nativeRecordingID){
                        submitted = true;
                        break;
                    }
                }
            }
        }

        if(submitted)
            retireChunkLocked(*ledger, *chunk, version, false);
        chunk = nextChunk;
    }
}

void UploadManager::retireOwnerChunksLocked(
    const GpuPhysicalQueueId queue,
    const u64 version,
    const bool resetAllocated,
    TrackedCommandBuffer& owner,
    const u64 nativeRecordingID
)noexcept{
    QueueChunkLedger* const ledger = findQueueLedgerLocked(queue);
    if(!ledger)
        return;

    BufferChunk* chunk = ledger->firstActiveChunk;
    while(chunk){
        BufferChunk* const nextChunk = chunk->nextActiveChunk;
        if(chunk->owner == &owner && chunk->nativeRecordingID == nativeRecordingID)
            retireChunkLocked(*ledger, *chunk, version, resetAllocated);
        chunk = nextChunk;
    }
}

bool UploadManager::suballocateBuffer(
    const u64 size,
    Buffer** const pBuffer,
    u64* const pOffset,
    void** const pCpuVA,
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const GpuPhysicalQueueId queue,
    const u64 completedVersion,
    const u32 alignment
){
    if(!pBuffer || !pOffset || !owner || nativeRecordingID == 0u)
        return false;
    if(!m_device.matchesPhysicalQueueIdentity(queue))
        return false;

    ScopedLock lock(m_mutex);
    QueueChunkLedger* const ledger = findOrCreateQueueLedgerLocked(queue);
    if(!ledger)
        return false;

    const auto trySuballocateFromChunk = [&](BufferChunk& chunk) -> bool {
        u64 alignedOffset = 0;
        if(!AlignUpU64Checked(chunk.allocated, static_cast<u64>(alignment), alignedOffset))
            return false;
        if(alignedOffset > chunk.size || size > chunk.size - alignedOffset)
            return false;

        Buffer* buffer = chunk.buffer.get();
        *pBuffer = buffer;
        *pOffset = alignedOffset;
        if(pCpuVA)
            *pCpuVA = static_cast<u8*>(buffer->m_mappedMemory) + alignedOffset;

        chunk.allocated = alignedOffset + size;
        return true;
    };

    for(BufferChunk* chunk = ledger->firstActiveChunk; chunk; chunk = chunk->nextActiveChunk){
        if(
            chunk->owner == owner
            && chunk->nativeRecordingID == nativeRecordingID
            && trySuballocateFromChunk(*chunk)
        )
            return true;
    }

    for(BufferChunkPtr& retiredChunk : ledger->chunks){
        if(
            retiredChunk
            && !retiredChunk->owner
            && retiredChunk->nativeRecordingID == 0u
            && retiredChunk->physicalQueue == queue
            && retiredChunk->size >= size
            && retiredChunk->version <= completedVersion
        ){
            if(m_retiredChunkBytes >= retiredChunk->size)
                m_retiredChunkBytes -= retiredChunk->size;
            else
                m_retiredChunkBytes = 0u;
            retiredChunk->owner = owner;
            retiredChunk->nativeRecordingID = nativeRecordingID;
            retiredChunk->allocated = 0u;
            retiredChunk->version = completedVersion;
            linkActiveChunkLocked(*ledger, *retiredChunk);

            return trySuballocateFromChunk(*retiredChunk);
        }
    }

    auto chunkSize = Max<u64>(size, m_defaultChunkSize);

    BufferDesc bufferDesc;
    bufferDesc.byteSize = chunkSize;
    bufferDesc.cpuAccess = m_isScratchBuffer ? CpuAccessMode::None : CpuAccessMode::Write;
    bufferDesc.isVolatile = false;
    bufferDesc.debugName = m_isScratchBuffer ? "ScratchBuffer" : "UploadBuffer";
    if(m_isScratchBuffer){
        bufferDesc.structStride = 1u;
        bufferDesc.canHaveUAVs = true;
    }

    BufferHandle bufferHandle = m_device.createBuffer(bufferDesc);
    if(!bufferHandle)
        return false;

    ledger->chunks.push_back(MakeRefCount<BufferChunk>(
        m_device.m_context.cpuScheduler,
        Move(bufferHandle),
        owner,
        nativeRecordingID,
        queue,
        chunkSize
    ));
    BufferChunkPtr& currentChunk = ledger->chunks.back();
    currentChunk->version = completedVersion;
    linkActiveChunkLocked(*ledger, *currentChunk);

    return trySuballocateFromChunk(*currentChunk);
}

void UploadManager::submitChunks(
    const GpuPhysicalQueueId queue,
    const u64 submittedVersion,
    const Queue::SubmissionCommandListIdentity* const submittedCommandLists,
    const usize submittedCommandListCount,
    const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
)noexcept{
    static_assert(noexcept(retireSubmittedChunksLocked(
        queue,
        submittedVersion,
        submittedCommandLists,
        submittedCommandListCount,
        submittedOwners
    )), "accepted upload-chunk retirement must remain non-throwing");
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !submittedCommandLists || submittedCommandListCount == 0u)
        return;

    NothrowScopedLock lock(m_mutex);
    retireSubmittedChunksLocked(
        queue,
        submittedVersion,
        submittedCommandLists,
        submittedCommandListCount,
        submittedOwners
    );
}

void UploadManager::discardChunks(
    const GpuPhysicalQueueId queue,
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const u64 reusableVersion
){
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !owner || nativeRecordingID == 0u)
        return;

    ScopedLock lock(m_mutex);
    retireOwnerChunksLocked(queue, reusableVersion, true, *owner, nativeRecordingID);
    trimRetiredChunksLocked(queue, reusableVersion);
}

void UploadManager::abandonChunks(
    const GpuPhysicalQueueId queue,
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID
)noexcept{
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !owner || nativeRecordingID == 0u)
        return;

    NothrowScopedLock lock(m_mutex);
    retireOwnerChunksLocked(queue, 0u, true, *owner, nativeRecordingID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

