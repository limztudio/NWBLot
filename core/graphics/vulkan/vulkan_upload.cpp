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
    , m_chunkPool(Alloc::CustomAllocator<RefCountPtr<BufferChunk>>(m_device.m_context.objectArena))
{}
UploadManager::~UploadManager(){
    m_chunkPool.clear();
    m_currentChunk.reset();
}

bool UploadManager::suballocateBuffer(u64 size, Buffer** pBuffer, u64* pOffset, void** pCpuVA, u64 currentVersion, u32 alignment){
    if(!pBuffer || !pOffset)
        return false;

    ScopedLock lock(m_mutex);

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

    if(m_currentChunk && trySuballocateFromChunk(*m_currentChunk))
        return true;

    for(auto it = m_chunkPool.begin(); it != m_chunkPool.end(); ++it){
        if((*it)->size >= size && (*it)->version < currentVersion){
            m_currentChunk = *it;
            if(m_chunkPoolBytes >= (*it)->size)
                m_chunkPoolBytes -= (*it)->size;
            else
                m_chunkPoolBytes = 0;
            m_chunkPool.erase(it);
            m_currentChunk->allocated = 0;
            m_currentChunk->version = currentVersion;

            return trySuballocateFromChunk(*m_currentChunk);
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

    m_currentChunk = MakeRefCount<BufferChunk>(m_device.m_context.threadPool, Move(bufferHandle), chunkSize);
    m_currentChunk->version = currentVersion;

    return trySuballocateFromChunk(*m_currentChunk);
}

void UploadManager::submitChunks(u64, u64 submittedVersion){
    ScopedLock lock(m_mutex);

    if(m_currentChunk){
        m_currentChunk->version = submittedVersion;
        if(m_chunkPoolBytes > UINT64_MAX - m_currentChunk->size)
            m_chunkPoolBytes = UINT64_MAX;
        else
            m_chunkPoolBytes += m_currentChunk->size;
        m_chunkPool.push_back(m_currentChunk);
        m_currentChunk.reset();

        if(m_memoryLimit > 0){
            while(m_chunkPoolBytes > m_memoryLimit && !m_chunkPool.empty()){
                auto& chunk = m_chunkPool.front();
                if(m_chunkPoolBytes >= chunk->size)
                    m_chunkPoolBytes -= chunk->size;
                else
                    m_chunkPoolBytes = 0;
                m_chunkPool.pop_front();
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
