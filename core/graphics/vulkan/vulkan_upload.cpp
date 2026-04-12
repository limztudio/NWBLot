// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AlignUploadOffsetChecked(const u64 value, const u32 alignment, u64& outAligned){
    if(alignment <= 1u){
        outAligned = value;
        return true;
    }

    const u64 alignmentValue = static_cast<u64>(alignment);
    const u64 remainder = value % alignmentValue;
    if(remainder == 0){
        outAligned = value;
        return true;
    }

    const u64 addend = alignmentValue - remainder;
    if(value > Limit<u64>::s_Max - addend)
        return false;

    outAligned = value + addend;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UploadManager::UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer)
    : m_device(pParent)
    , m_defaultChunkSize(defaultChunkSize)
    , m_memoryLimit(memoryLimit)
    , m_isScratchBuffer(isScratchBuffer)
    , m_chunkPool(Alloc::CustomAllocator<BufferChunkPtr>(m_device.m_context.objectArena))
{
    for(auto& chunks : m_activeChunks)
        chunks = BufferChunkList(Alloc::CustomAllocator<BufferChunkPtr>(m_device.m_context.objectArena));
}
UploadManager::~UploadManager(){
    m_chunkPool.clear();
    for(auto& chunks : m_activeChunks)
        chunks.clear();
}

void UploadManager::trimChunkPoolLocked(){
    if(m_memoryLimit == 0)
        return;

    while(m_chunkPoolBytes > m_memoryLimit && !m_chunkPool.empty()){
        auto& chunk = m_chunkPool.front();
        if(m_chunkPoolBytes >= chunk->size)
            m_chunkPoolBytes -= chunk->size;
        else
            m_chunkPoolBytes = 0;
        m_chunkPool.pop_front();
    }
}

bool UploadManager::suballocateBuffer(u64 size, Buffer** pBuffer, u64* pOffset, void** pCpuVA, TrackedCommandBuffer* owner, CommandQueue::Enum queueID, u64 completedVersion, u32 alignment){
    if(!pBuffer || !pOffset || !owner)
        return false;
    const u32 queueIndex = static_cast<u32>(queueID);
    if(queueIndex >= static_cast<u32>(CommandQueue::kCount))
        return false;

    ScopedLock lock(m_mutex);
    auto& activeChunks = m_activeChunks[queueIndex];

    const auto trySuballocateFromChunk = [&](BufferChunk& chunk) -> bool {
        u64 alignedOffset = 0;
        if(!__hidden_vulkan::AlignUploadOffsetChecked(chunk.allocated, alignment, alignedOffset))
            return false;
        if(alignedOffset > chunk.size || size > chunk.size - alignedOffset)
            return false;

        Buffer* buffer = static_cast<Buffer*>(chunk.buffer.get());
        *pBuffer = buffer;
        *pOffset = alignedOffset;
        if(pCpuVA)
            *pCpuVA = static_cast<u8*>(buffer->m_mappedMemory) + alignedOffset;

        chunk.allocated = alignedOffset + size;
        return true;
    };

    for(auto it = activeChunks.rbegin(); it != activeChunks.rend(); ++it){
        if((*it)->owner == owner && trySuballocateFromChunk(**it))
            return true;
    }

    for(auto it = m_chunkPool.begin(); it != m_chunkPool.end(); ++it){
        BufferChunkPtr& pooledChunk = *it;
        if(pooledChunk->queueID == queueID && pooledChunk->size >= size && pooledChunk->version <= completedVersion){
            if(m_chunkPoolBytes >= pooledChunk->size)
                m_chunkPoolBytes -= pooledChunk->size;
            else
                m_chunkPoolBytes = 0;
            activeChunks.push_back(Move(pooledChunk));
            BufferChunkPtr& currentChunk = activeChunks.back();
            m_chunkPool.erase(it);
            currentChunk->owner = owner;
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

    activeChunks.push_back(MakeRefCount<BufferChunk>(m_device.m_context.threadPool, Move(bufferHandle), owner, queueID, chunkSize));
    BufferChunkPtr& currentChunk = activeChunks.back();
    currentChunk->version = completedVersion;

    return trySuballocateFromChunk(*currentChunk);
}

void UploadManager::submitChunks(CommandQueue::Enum queueID, u64 submittedVersion, TrackedCommandBuffer* const* submittedOwners, usize submittedOwnerCount){
    const u32 queueIndex = static_cast<u32>(queueID);
    if(queueIndex >= static_cast<u32>(CommandQueue::kCount) || !submittedOwners || submittedOwnerCount == 0)
        return;

    ScopedLock lock(m_mutex);
    auto& activeChunks = m_activeChunks[queueIndex];

    const auto ownsSubmittedChunk = [&](TrackedCommandBuffer* owner) -> bool {
        for(usize i = 0; i < submittedOwnerCount; ++i){
            if(submittedOwners[i] == owner)
                return true;
        }
        return false;
    };

    auto it = activeChunks.begin();
    while(it != activeChunks.end()){
        BufferChunkPtr& chunk = *it;
        if(!chunk){
            it = activeChunks.erase(it);
            continue;
        }
        if(!ownsSubmittedChunk(chunk->owner)){
            ++it;
            continue;
        }

        chunk->owner = nullptr;
        chunk->version = submittedVersion;
        if(m_chunkPoolBytes > UINT64_MAX - chunk->size)
            m_chunkPoolBytes = UINT64_MAX;
        else
            m_chunkPoolBytes += chunk->size;
        m_chunkPool.push_back(Move(chunk));
        it = activeChunks.erase(it);
    }

    trimChunkPoolLocked();
}

void UploadManager::discardChunks(CommandQueue::Enum queueID, TrackedCommandBuffer* owner, u64 reusableVersion){
    const u32 queueIndex = static_cast<u32>(queueID);
    if(queueIndex >= static_cast<u32>(CommandQueue::kCount) || !owner)
        return;

    ScopedLock lock(m_mutex);
    auto& activeChunks = m_activeChunks[queueIndex];

    auto it = activeChunks.begin();
    while(it != activeChunks.end()){
        BufferChunkPtr& chunk = *it;
        if(!chunk){
            it = activeChunks.erase(it);
            continue;
        }
        if(chunk->owner != owner){
            ++it;
            continue;
        }

        chunk->owner = nullptr;
        chunk->allocated = 0;
        chunk->version = reusableVersion;
        if(m_chunkPoolBytes > UINT64_MAX - chunk->size)
            m_chunkPoolBytes = UINT64_MAX;
        else
            m_chunkPoolBytes += chunk->size;
        m_chunkPool.push_back(Move(chunk));
        it = activeChunks.erase(it);
    }

    trimChunkPoolLocked();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
