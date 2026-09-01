// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SubmittedOwnersContext{
    const Queue::SubmissionCommandListIdentity* commandLists = nullptr;
    usize count = 0;
};

struct OwnerIdentityContext{
    TrackedCommandBuffer* owner = nullptr;
    u64 nativeRecordingID = 0u;
};

static bool IsSubmittedOwner(
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const void* const context
)noexcept{
    const auto& submitted = *static_cast<const SubmittedOwnersContext*>(context);
    for(usize i = 0; i < submitted.count; ++i){
        const Queue::SubmissionCommandListIdentity& commandList = submitted.commandLists[i];
        if(commandList.owner.get() == owner && commandList.nativeRecordingID == nativeRecordingID)
            return true;
    }
    return false;
}

static bool IsSubmittedOwnerInLookup(
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const void* const context
)noexcept{
    if(!owner)
        return false;

    const auto& submitted = *static_cast<const SubmittedCommandBufferOwnerLookup*>(context);
    return submitted.contains(*owner, nativeRecordingID);
}

static bool IsMatchingOwner(
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const void* const context
)noexcept{
    const auto& expected = *static_cast<const OwnerIdentityContext*>(context);
    return owner == expected.owner && nativeRecordingID == expected.nativeRecordingID;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UploadManager::UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer)
    : m_device(pParent)
    , m_defaultChunkSize(defaultChunkSize)
    , m_memoryLimit(memoryLimit)
    , m_isScratchBuffer(isScratchBuffer)
    , m_chunkPool(m_device.m_context.objectArena)
    , m_activeChunks(m_device.m_context.objectArena)
{}
UploadManager::~UploadManager(){
    clear();
}

void UploadManager::clear(){
    m_chunkPool.clear();
    for(ActiveQueueChunks& entry : m_activeChunks)
        entry.chunks.clear();
    m_activeChunks.clear();
    m_chunkPoolBytes = 0u;
}

void UploadManager::collectCompletedChunks(){
    ScopedLock lock(m_mutex);

    trimChunkPoolLocked();
}

void UploadManager::trimChunkPoolLocked(){
    if(m_memoryLimit == 0)
        return;

    auto it = m_chunkPool.begin();
    while(m_chunkPoolBytes > m_memoryLimit && it != m_chunkPool.end()){
        BufferChunkPtr& chunk = *it;
        if(!chunk){
            it = m_chunkPool.erase(it);
            continue;
        }

        const u64 completedVersion = m_device.queueGetCompletedInstance(chunk->physicalQueue);
        if(!chunk->physicalQueue.valid() || chunk->version > completedVersion){
            ++it;
            continue;
        }

        if(m_chunkPoolBytes >= chunk->size)
            m_chunkPoolBytes -= chunk->size;
        else
            m_chunkPoolBytes = 0;
        it = m_chunkPool.erase(it);
    }
}

UploadManager::BufferChunkList* UploadManager::findActiveChunksLocked(const GpuPhysicalQueueId queue)noexcept{
    if(!queue.valid())
        return nullptr;
    for(ActiveQueueChunks& entry : m_activeChunks){
        if(entry.queue == queue)
            return &entry.chunks;
    }
    return nullptr;
}

UploadManager::BufferChunkList* UploadManager::findOrCreateActiveChunksLocked(const GpuPhysicalQueueId queue){
    BufferChunkList* const chunks = findActiveChunksLocked(queue);
    if(chunks || !queue.valid())
        return chunks;

    m_activeChunks.emplace_back(m_device.m_context.objectArena, queue);
    return &m_activeChunks.back().chunks;
}

UploadManager::BufferChunkList::iterator UploadManager::recycleActiveChunkLocked(
    BufferChunkList& activeChunks,
    const BufferChunkList::iterator it,
    const u64 version,
    const bool resetAllocated
)noexcept{
    auto next = it;
    ++next;
    BufferChunkPtr& chunk = *it;
    chunk->owner = nullptr;
    chunk->nativeRecordingID = 0u;
    if(resetAllocated)
        chunk->allocated = 0u;
    chunk->version = version;
    const u64 chunkSize = chunk->size;

    m_chunkPool.splice(m_chunkPool.end(), activeChunks, it);
    if(m_chunkPoolBytes > UINT64_MAX - chunkSize)
        m_chunkPoolBytes = UINT64_MAX;
    else
        m_chunkPoolBytes += chunkSize;
    return next;
}

void UploadManager::recycleMatchingActiveChunksLocked(
    const GpuPhysicalQueueId queue,
    const u64 version,
    const bool resetAllocated,
    const ChunkRecyclePredicate predicate,
    const void* const predicateContext
)noexcept{
    BufferChunkList* const activeChunks = findActiveChunksLocked(queue);
    if(!activeChunks)
        return;

    auto it = activeChunks->begin();
    while(it != activeChunks->end()){
        BufferChunkPtr& chunk = *it;
        if(!chunk){
            it = activeChunks->erase(it);
            continue;
        }
        if(!predicate(chunk->owner, chunk->nativeRecordingID, predicateContext)){
            ++it;
            continue;
        }

        it = recycleActiveChunkLocked(*activeChunks, it, version, resetAllocated);
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
    BufferChunkList* const activeChunks = findOrCreateActiveChunksLocked(queue);
    if(!activeChunks)
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

    for(auto it = activeChunks->rbegin(); it != activeChunks->rend(); ++it){
        if(
            (*it)->owner == owner
            && (*it)->nativeRecordingID == nativeRecordingID
            && trySuballocateFromChunk(**it)
        )
            return true;
    }

    for(auto it = m_chunkPool.begin(); it != m_chunkPool.end(); ++it){
        BufferChunkPtr& pooledChunk = *it;
        if(pooledChunk->physicalQueue == queue && pooledChunk->size >= size && pooledChunk->version <= completedVersion){
            const u64 pooledChunkSize = pooledChunk->size;
            activeChunks->splice(activeChunks->end(), m_chunkPool, it);
            if(m_chunkPoolBytes >= pooledChunkSize)
                m_chunkPoolBytes -= pooledChunkSize;
            else
                m_chunkPoolBytes = 0;
            BufferChunkPtr& currentChunk = activeChunks->back();
            currentChunk->owner = owner;
            currentChunk->nativeRecordingID = nativeRecordingID;
            currentChunk->allocated = 0;
            currentChunk->version = completedVersion;

            return trySuballocateFromChunk(*currentChunk);
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

    activeChunks->push_back(MakeRefCount<BufferChunk>(
        m_device.m_context.threadPool,
        Move(bufferHandle),
        owner,
        nativeRecordingID,
        queue,
        chunkSize
    ));
    BufferChunkPtr& currentChunk = activeChunks->back();
    currentChunk->version = completedVersion;

    return trySuballocateFromChunk(*currentChunk);
}

void UploadManager::submitChunks(
    const GpuPhysicalQueueId queue,
    const u64 submittedVersion,
    const Queue::SubmissionCommandListIdentity* const submittedCommandLists,
    const usize submittedCommandListCount,
    const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
)noexcept{
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !submittedCommandLists || submittedCommandListCount == 0u)
        return;

    ScopedLock lock(m_mutex);
    if(submittedOwners.indexed()){
        recycleMatchingActiveChunksLocked(
            queue,
            submittedVersion,
            false,
            VulkanDetail::IsSubmittedOwnerInLookup,
            &submittedOwners
        );
        return;
    }

    const VulkanDetail::SubmittedOwnersContext submittedContext{
        submittedCommandLists,
        submittedCommandListCount,
    };
    recycleMatchingActiveChunksLocked(queue, submittedVersion, false, VulkanDetail::IsSubmittedOwner, &submittedContext);
}

void UploadManager::discardChunks(
    const GpuPhysicalQueueId queue,
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const u64 reusableVersion
){
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !owner || nativeRecordingID == 0u)
        return;

    const VulkanDetail::OwnerIdentityContext ownerIdentity{ owner, nativeRecordingID };
    ScopedLock lock(m_mutex);
    recycleMatchingActiveChunksLocked(queue, reusableVersion, true, VulkanDetail::IsMatchingOwner, &ownerIdentity);
    trimChunkPoolLocked();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

