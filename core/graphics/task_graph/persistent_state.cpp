// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "persistent_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_persistent_resource_state_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename HandleT>
[[nodiscard]] bool AppendUniqueHandle(
    GraphicsVector<HandleT>& outHandles,
    const HandleT& handle
){
    if(!handle)
        return true;

    for(const HandleT& existing : outHandles){
        if(existing.get() == handle.get())
            return true;
    }
    outHandles.push_back(handle);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuPersistentResourceStateCache::reset()noexcept{
    m_states.reset();
    m_textures.clear();
    m_buffers.clear();
}

bool GpuPersistentResourceStateCache::buildFilteredCandidate(
    CommandListResourceStateHandoff& outStates,
    GraphicsVector<TextureHandle>& outTextures,
    GraphicsVector<BufferHandle>& outBuffers,
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
)const{
    if(
        !source.valid()
        || (textureCount != 0u && !textures)
        || (bufferCount != 0u && !buffers)
    )
        return false;

    outTextures.clear();
    outBuffers.clear();
    GraphicsVector<Texture*> texturePointers(m_arena);
    GraphicsVector<Buffer*> bufferPointers(m_arena);
    texturePointers.reserve(textureCount);
    bufferPointers.reserve(bufferCount);

    for(usize textureIndex = 0u; textureIndex < textureCount; ++textureIndex){
        const TextureHandle& texture = textures[textureIndex];
        if(!__hidden_gpu_persistent_resource_state_cache::AppendUniqueHandle(outTextures, texture))
            return false;
        if(!texture)
            continue;

        bool alreadyAdded = false;
        for(Texture* const existing : texturePointers){
            if(existing == texture.get()){
                alreadyAdded = true;
                break;
            }
        }
        if(!alreadyAdded)
            texturePointers.push_back(texture.get());
    }

    for(usize bufferIndex = 0u; bufferIndex < bufferCount; ++bufferIndex){
        const BufferHandle& buffer = buffers[bufferIndex];
        if(!__hidden_gpu_persistent_resource_state_cache::AppendUniqueHandle(outBuffers, buffer))
            return false;
        if(!buffer)
            continue;

        bool alreadyAdded = false;
        for(Buffer* const existing : bufferPointers){
            if(existing == buffer.get()){
                alreadyAdded = true;
                break;
            }
        }
        if(!alreadyAdded)
            bufferPointers.push_back(buffer.get());
    }

    return outStates.buildResourceSubset(
        source,
        texturePointers.data(),
        texturePointers.size(),
        bufferPointers.data(),
        bufferPointers.size()
    );
}

bool GpuPersistentResourceStateCache::commitCandidate(
    const CommandListResourceStateHandoff& states,
    GraphicsVector<TextureHandle>& textures,
    GraphicsVector<BufferHandle>& buffers
){
    if(!states.valid() || !m_states.copyFrom(states))
        return false;

    m_textures.clear();
    for(TextureHandle& texture : textures)
        m_textures.push_back(Move(texture));
    m_buffers.clear();
    for(BufferHandle& buffer : buffers)
        m_buffers.push_back(Move(buffer));
    return true;
}

bool GpuPersistentResourceStateCache::replaceResourceSubset(
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
){
    CommandListResourceStateHandoff candidateStates(m_arena);
    GraphicsVector<TextureHandle> candidateTextures(m_arena);
    GraphicsVector<BufferHandle> candidateBuffers(m_arena);
    if(!buildFilteredCandidate(
        candidateStates,
        candidateTextures,
        candidateBuffers,
        source,
        textures,
        textureCount,
        buffers,
        bufferCount
    )){
        reset();
        return false;
    }

    if(!commitCandidate(candidateStates, candidateTextures, candidateBuffers)){
        reset();
        return false;
    }
    return true;
}

bool GpuPersistentResourceStateCache::mergeResourceSubset(
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
){
    CommandListResourceStateHandoff sourceStates(m_arena);
    GraphicsVector<TextureHandle> candidateTextures(m_arena);
    GraphicsVector<BufferHandle> candidateBuffers(m_arena);
    if(!buildFilteredCandidate(
        sourceStates,
        candidateTextures,
        candidateBuffers,
        source,
        textures,
        textureCount,
        buffers,
        bufferCount
    ))
        return false;

    CommandListResourceStateHandoff mergedStates(m_arena);
    if(m_states.valid()){
        CommandListResourceStateHandoff retainedStates(m_arena);
        GraphicsVector<TextureHandle> retainedTextures(m_arena);
        GraphicsVector<BufferHandle> retainedBuffers(m_arena);
        if(!buildFilteredCandidate(
            retainedStates,
            retainedTextures,
            retainedBuffers,
            m_states,
            textures,
            textureCount,
            buffers,
            bufferCount
        ))
            return false;

        const CommandListResourceStateHandoff* const branches[] = { &sourceStates };
        if(!mergedStates.buildFanIn(retainedStates, branches, LengthOf(branches)))
            return false;
    }else if(!mergedStates.copyFrom(sourceStates))
        return false;

    return commitCandidate(mergedStates, candidateTextures, candidateBuffers);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
