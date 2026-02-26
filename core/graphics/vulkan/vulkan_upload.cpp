// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UploadManager::UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer)
    : m_device(pParent)
    , m_defaultChunkSize(defaultChunkSize)
    , m_memoryLimit(memoryLimit)
    , m_isScratchBuffer(isScratchBuffer)
    , m_chunkPool(Alloc::CustomAllocator<RefCountPtr<BufferChunk>>(m_device.getContext().objectArena))
{}
UploadManager::~UploadManager(){
    m_chunkPool.clear();
    m_currentChunk.reset();
}

bool UploadManager::suballocateBuffer(u64 size, Buffer** pBuffer, u64* pOffset, void** pCpuVA, u64 currentVersion, u32 alignment){
    if(!pBuffer || !pOffset)
        return false;

    ScopedLock lock(m_mutex);

    if(alignment > 0)
        size = (size + alignment - 1) & ~(static_cast<u64>(alignment) - 1);

    if(m_currentChunk && (m_currentChunk->allocated + size <= m_currentChunk->size)){
        *pBuffer = static_cast<Buffer*>(m_currentChunk->buffer.get());
        *pOffset = m_currentChunk->allocated;
        if(pCpuVA)
            *pCpuVA = static_cast<u8*>(static_cast<Buffer*>(m_currentChunk->buffer.get())->m_mappedMemory) + m_currentChunk->allocated;

        m_currentChunk->allocated += size;
        return true;
    }

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

            *pBuffer = static_cast<Buffer*>(m_currentChunk->buffer.get());
            *pOffset = 0;
            if(pCpuVA)
                *pCpuVA = static_cast<Buffer*>(m_currentChunk->buffer.get())->m_mappedMemory;

            m_currentChunk->allocated = size;
            return true;
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

    m_currentChunk = MakeRefCount<BufferChunk>(m_device.getContext().threadPool, Move(bufferHandle), chunkSize);
    m_currentChunk->version = currentVersion;

    *pBuffer = static_cast<Buffer*>(m_currentChunk->buffer.get());
    *pOffset = 0;
    if(pCpuVA)
        *pCpuVA = static_cast<Buffer*>(m_currentChunk->buffer.get())->m_mappedMemory;

    m_currentChunk->allocated = size;
    return true;
}

void UploadManager::submitChunks(u64, u64 submittedVersion){
    ScopedLock lock(m_mutex);

    if(m_currentChunk){
        m_currentChunk->version = submittedVersion;
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

