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

void GpuPersistentResourceStateCache::Candidate::reset()noexcept{
    m_states.reset();
    m_textures.clear();
    m_buffers.clear();
}

bool GpuPersistentResourceStateCache::buildFilteredCandidate(
    Candidate& outCandidate,
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
)const{
    outCandidate.reset();
    if(
        !source.valid()
        || (textureCount != 0u && !textures)
        || (bufferCount != 0u && !buffers)
    )
        return false;

    GraphicsVector<Texture*> texturePointers(m_arena);
    GraphicsVector<Buffer*> bufferPointers(m_arena);
    texturePointers.reserve(textureCount);
    bufferPointers.reserve(bufferCount);

    for(usize textureIndex = 0u; textureIndex < textureCount; ++textureIndex){
        const TextureHandle& texture = textures[textureIndex];
        if(!__hidden_gpu_persistent_resource_state_cache::AppendUniqueHandle(outCandidate.m_textures, texture))
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
        if(!__hidden_gpu_persistent_resource_state_cache::AppendUniqueHandle(outCandidate.m_buffers, buffer))
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

    return outCandidate.m_states.buildResourceSubset(
        source,
        texturePointers.data(),
        texturePointers.size(),
        bufferPointers.data(),
        bufferPointers.size()
    );
}

bool GpuPersistentResourceStateCache::replaceResourceSubset(
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
){
    Candidate candidate(m_arena);
    if(!buildFilteredCandidate(
        candidate,
        source,
        textures,
        textureCount,
        buffers,
        bufferCount
    )){
        reset();
        return false;
    }

    if(!commit(candidate)){
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
    Candidate candidate(m_arena);
    if(!buildMergedResourceSubset(candidate, source, textures, textureCount, buffers, bufferCount))
        return false;

    return commit(candidate);
}

bool GpuPersistentResourceStateCache::buildFilteredResourceSubset(
    Candidate& outCandidate,
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
)const{
    return buildFilteredCandidate(outCandidate, source, textures, textureCount, buffers, bufferCount);
}

bool GpuPersistentResourceStateCache::buildMergedResourceSubset(
    Candidate& outCandidate,
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
)const{
    Candidate sourceCandidate(m_arena);
    if(!buildFilteredCandidate(
        sourceCandidate,
        source,
        textures,
        textureCount,
        buffers,
        bufferCount
    ))
        return false;

    outCandidate.reset();
    if(m_states.valid()){
        Candidate retainedCandidate(m_arena);
        if(!buildFilteredCandidate(
            retainedCandidate,
            m_states,
            textures,
            textureCount,
            buffers,
            bufferCount
        ))
            return false;

        const CommandListResourceStateHandoff* const branches[] = { &sourceCandidate.m_states };
        if(!outCandidate.m_states.buildFanIn(retainedCandidate.m_states, branches, LengthOf(branches)))
            return false;
    }else if(!outCandidate.m_states.copyFrom(sourceCandidate.m_states))
        return false;

    for(TextureHandle& texture : sourceCandidate.m_textures)
        outCandidate.m_textures.push_back(Move(texture));
    for(BufferHandle& buffer : sourceCandidate.m_buffers)
        outCandidate.m_buffers.push_back(Move(buffer));
    return true;
}

bool GpuPersistentResourceStateCache::commit(Candidate& candidate){
    if(!candidate.valid() || !m_states.copyFrom(candidate.m_states))
        return false;

    m_textures.clear();
    for(TextureHandle& texture : candidate.m_textures)
        m_textures.push_back(Move(texture));
    m_buffers.clear();
    for(BufferHandle& buffer : candidate.m_buffers)
        m_buffers.push_back(Move(buffer));
    candidate.reset();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

