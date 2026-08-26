// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SubmittedOwnersContext{
    const Queue::SubmissionCommandListIdentity* commandLists = nullptr;
    usize count = 0;
};

static constexpr usize s_SubmittedOwnerLookupThreshold = 8u;

using SubmittedOwnerLookup = HashMap<
    TrackedCommandBuffer*,
    u64,
    Hasher<TrackedCommandBuffer*>,
    EqualTo<TrackedCommandBuffer*>,
    Alloc::ScratchArena
>;

struct SubmittedOwnerLookupContext{
    const SubmittedOwnerLookup* owners = nullptr;
};

struct OwnerIdentityContext{
    TrackedCommandBuffer* owner = nullptr;
    u64 nativeRecordingID = 0u;
};

static bool IsSubmittedOwner(
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const void* const context
){
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
){
    const auto& submitted = *static_cast<const SubmittedOwnerLookupContext*>(context);
    if(!owner || nativeRecordingID == 0u || !submitted.owners)
        return false;

    const auto it = submitted.owners->find(owner);
    return it != submitted.owners->end() && it->second == nativeRecordingID;
}

static bool IsMatchingOwner(
    TrackedCommandBuffer* const owner,
    const u64 nativeRecordingID,
    const void* const context
){
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

UploadManager::BufferChunkList* UploadManager::findActiveChunksLocked(
    const GpuPhysicalQueueId queue,
    const bool create
){
    if(!queue.valid())
        return nullptr;
    for(ActiveQueueChunks& entry : m_activeChunks){
        if(entry.queue == queue)
            return &entry.chunks;
    }
    if(!create)
        return nullptr;
    m_activeChunks.emplace_back(m_device.m_context.objectArena, queue);
    return &m_activeChunks.back().chunks;
}

UploadManager::BufferChunkList::iterator UploadManager::recycleActiveChunkLocked(BufferChunkList& activeChunks, BufferChunkList::iterator it, const u64 version, const bool resetAllocated){
    BufferChunkPtr& chunk = *it;
    chunk->owner = nullptr;
    chunk->nativeRecordingID = 0u;
    if(resetAllocated)
        chunk->allocated = 0;
    chunk->version = version;

    if(m_chunkPoolBytes > UINT64_MAX - chunk->size)
        m_chunkPoolBytes = UINT64_MAX;
    else
        m_chunkPoolBytes += chunk->size;

    m_chunkPool.push_back(Move(chunk));
    return activeChunks.erase(it);
}

void UploadManager::recycleMatchingActiveChunks(
    const GpuPhysicalQueueId queue,
    const u64 version,
    const bool resetAllocated,
    const ChunkRecyclePredicate predicate,
    const void* predicateContext
){
    ScopedLock lock(m_mutex);
    BufferChunkList* const activeChunks = findActiveChunksLocked(queue, false);
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

    trimChunkPoolLocked();
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
    BufferChunkList* const activeChunks = findActiveChunksLocked(queue, true);
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
            if(m_chunkPoolBytes >= pooledChunk->size)
                m_chunkPoolBytes -= pooledChunk->size;
            else
                m_chunkPoolBytes = 0;
            activeChunks->push_back(Move(pooledChunk));
            BufferChunkPtr& currentChunk = activeChunks->back();
            m_chunkPool.erase(it);
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
    bufferDesc.cpuAccess = CpuAccessMode::Write;
    bufferDesc.isVolatile = false;
    bufferDesc.debugName = m_isScratchBuffer ? "ScratchBuffer" : "UploadBuffer";

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
    const usize submittedCommandListCount
){
    if(!m_device.matchesPhysicalQueueIdentity(queue) || !submittedCommandLists || submittedCommandListCount == 0u)
        return;

    if(submittedCommandListCount > VulkanDetail::s_SubmittedOwnerLookupThreshold){
        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_SubmitChunksArena);
        VulkanDetail::SubmittedOwnerLookup submittedOwnerLookup(
            0,
            Hasher<TrackedCommandBuffer*>(),
            EqualTo<TrackedCommandBuffer*>(),
            scratchArena
        );
        submittedOwnerLookup.reserve(submittedCommandListCount);
        for(usize i = 0u; i < submittedCommandListCount; ++i){
            const Queue::SubmissionCommandListIdentity& commandList = submittedCommandLists[i];
            if(commandList.owner && commandList.nativeRecordingID != 0u)
                submittedOwnerLookup[commandList.owner.get()] = commandList.nativeRecordingID;
        }

        const VulkanDetail::SubmittedOwnerLookupContext submittedLookupContext{ &submittedOwnerLookup };
        recycleMatchingActiveChunks(
            queue,
            submittedVersion,
            false,
            VulkanDetail::IsSubmittedOwnerInLookup,
            &submittedLookupContext
        );
        return;
    }

    const VulkanDetail::SubmittedOwnersContext submittedContext{
        submittedCommandLists,
        submittedCommandListCount,
    };
    recycleMatchingActiveChunks(queue, submittedVersion, false, VulkanDetail::IsSubmittedOwner, &submittedContext);
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
    recycleMatchingActiveChunks(queue, reusableVersion, true, VulkanDetail::IsMatchingOwner, &ownerIdentity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

