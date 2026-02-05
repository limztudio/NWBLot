// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferChunk : public IResource{
    RefCountPtr<Buffer> buffer;
    u64 size;
    u64 allocated;
    u64 version;
    
    BufferChunk(RefCountPtr<Buffer> buf, u64 sz)
        : buffer(buf)
        , size(sz)
        , allocated(0)
        , version(0)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UploadManager::UploadManager(Device* pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer)
    : m_Device(pParent)
    , m_DefaultChunkSize(defaultChunkSize)
    , m_MemoryLimit(memoryLimit)
    , m_IsScratchBuffer(isScratchBuffer)
{}

UploadManager::~UploadManager(){
    m_ChunkPool.clear();
    m_CurrentChunk.Reset();
}

bool UploadManager::suballocateBuffer(u64 size, Buffer** pBuffer, u64* pOffset, void** pCpuVA,
                                     u64 currentVersion, u32 alignment){
    // Align size
    if(alignment > 0)
        size = (size + alignment - 1) & ~(u64(alignment) - 1);
    
    // Check if current chunk has space
    if(m_CurrentChunk && (m_CurrentChunk->allocated + size <= m_CurrentChunk->size)){
        *pBuffer = m_CurrentChunk->buffer.Get();
        *pOffset = m_CurrentChunk->allocated;
        if(pCpuVA)
            *pCpuVA = static_cast<u8*>(m_CurrentChunk->buffer->mappedMemory) + m_CurrentChunk->allocated;
        
        m_CurrentChunk->allocated += size;
        return true;
    }
    
    // Try to find a chunk from the pool
    for(auto it = m_ChunkPool.begin(); it != m_ChunkPool.end(); ++it){
        if((*it)->size >= size && (*it)->version < currentVersion){
            m_CurrentChunk = *it;
            m_ChunkPool.erase(it);
            m_CurrentChunk->allocated = 0;
            m_CurrentChunk->version = currentVersion;
            
            *pBuffer = m_CurrentChunk->buffer.Get();
            *pOffset = 0;
            if(pCpuVA)
                *pCpuVA = m_CurrentChunk->buffer->mappedMemory;
            
            m_CurrentChunk->allocated = size;
            return true;
        }
    }
    
    // Create new chunk
    u64 chunkSize = max(size, m_DefaultChunkSize);
    
    BufferDesc bufferDesc;
    bufferDesc.byteSize = chunkSize;
    bufferDesc.cpuAccess = CpuAccessMode::Write;
    bufferDesc.isVolatile = false;
    bufferDesc.debugName = m_IsScratchBuffer ? "ScratchBuffer" : "UploadBuffer";
    
    RefCountPtr<Buffer> buffer = static_cast<Buffer*>(m_Device->createBuffer(bufferDesc).Get());
    if(!buffer)
        return false;
    
    m_CurrentChunk = MakeRefCountPtr<BufferChunk, BlankDeleter<BufferChunk>>(buffer, chunkSize);
    m_CurrentChunk->version = currentVersion;
    
    *pBuffer = buffer.Get();
    *pOffset = 0;
    if(pCpuVA)
        *pCpuVA = buffer->mappedMemory;
    
    m_CurrentChunk->allocated = size;
    return true;
}

void UploadManager::submitChunks(u64 currentVersion, u64 submittedVersion){
    if(m_CurrentChunk){
        m_CurrentChunk->version = submittedVersion;
        m_ChunkPool.push_back(m_CurrentChunk);
        m_CurrentChunk.Reset();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
