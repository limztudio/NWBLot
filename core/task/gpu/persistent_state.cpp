// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "persistent_state.h"

#include <core/graphics/rhi/resource_state_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_persistent_resource_state_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildSelection(
    CommandListResourceSelection& selection,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount
){
    for(usize index = 0u; index < textureCount; ++index){
        if(!selection.addTexture(textures[index].get(), index))
            return false;
    }
    for(usize index = 0u; index < bufferCount; ++index){
        if(!selection.addBuffer(buffers[index].get(), index))
            return false;
    }
    return true;
}

void RetainSelectedHandles(
    const CommandListResourceSelection& selection,
    const TextureHandle* const textures,
    const BufferHandle* const buffers,
    GraphicsVector<TextureHandle>& outTextures,
    GraphicsVector<BufferHandle>& outBuffers
){
    outTextures.reserve(selection.textureCount());
    outBuffers.reserve(selection.bufferCount());
    for(usize index = 0u; index < selection.size(); ++index){
        const CommandListResourceSelection::Entry& entry = selection.entries()[index];
        if(entry.texture)
            outTextures.push_back(textures[entry.inputIndex]);
        else
            outBuffers.push_back(buffers[entry.inputIndex]);
    }
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
    m_consumed = false;
}

bool GpuPersistentResourceStateCache::replaceResourceSubset(
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount,
    Alloc::ScratchArena& scratchArena
){
    Candidate candidate(*this);
    if(!buildFilteredResourceSubset(candidate, source, textures, textureCount, buffers, bufferCount, scratchArena)){
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
    const usize bufferCount,
    Alloc::ScratchArena& scratchArena
){
    Candidate candidate(*this);
    if(!buildMergedResourceSubset(candidate, source, textures, textureCount, buffers, bufferCount, scratchArena))
        return false;
    return commit(candidate);
}

bool GpuPersistentResourceStateCache::buildMergedResourceSubset(
    Candidate& outCandidate,
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount,
    Alloc::ScratchArena& scratchArena
)const{
    if(&outCandidate.m_owner != this)
        return false;

    Candidate sourceCandidate(*this);
    Optional<CommandListResourceStateHandoff> retainedStates;
    {
        if(!source.valid() || (textureCount != 0u && !textures) || (bufferCount != 0u && !buffers))
            return false;
        CommandListResourceSelection selection(scratchArena);
        using namespace __hidden_gpu_persistent_resource_state_cache;
        if(!BuildSelection(selection, textures, textureCount, buffers, bufferCount))
            return false;
        RetainSelectedHandles(selection, textures, buffers, sourceCandidate.m_textures, sourceCandidate.m_buffers);
        if(!sourceCandidate.m_states.buildResourceSubset(source, selection))
            return false;

        // Source can be the previous contents of outCandidate. Capture it completely before resetting that owner.
        outCandidate.reset();
        if(m_states.valid()){
            retainedStates.emplace(m_arena);
            if(!retainedStates->buildResourceSubset(m_states, selection))
                return false;
        }
    }
    // The selected typed handles stay in sourceCandidate through fan-in. Release the temporary selection before
    // fan-in reuses the same scratch arena, so repeated operations reclaim every selection allocation in LIFO order.
    if(retainedStates){
        const CommandListResourceStateHandoff* const branches[] = { &sourceCandidate.m_states };
        if(!outCandidate.m_states.buildFanIn(*retainedStates, branches, LengthOf(branches), scratchArena))
            return false;
    }
    else if(!outCandidate.m_states.copyFrom(sourceCandidate.m_states))
        return false;

    outCandidate.m_textures.swap(sourceCandidate.m_textures);
    outCandidate.m_buffers.swap(sourceCandidate.m_buffers);
    return true;
}

bool GpuPersistentResourceStateCache::buildFilteredResourceSubset(
    Candidate& outCandidate,
    const CommandListResourceStateHandoff& source,
    const TextureHandle* const textures,
    const usize textureCount,
    const BufferHandle* const buffers,
    const usize bufferCount,
    Alloc::ScratchArena& scratchArena
)const{
    if(&outCandidate.m_owner != this)
        return false;
    outCandidate.reset();
    if(!source.valid() || (textureCount != 0u && !textures) || (bufferCount != 0u && !buffers))
        return false;

    CommandListResourceSelection selection(scratchArena);
    using namespace __hidden_gpu_persistent_resource_state_cache;
    if(!BuildSelection(selection, textures, textureCount, buffers, bufferCount))
        return false;
    RetainSelectedHandles(selection, textures, buffers, outCandidate.m_textures, outCandidate.m_buffers);
    return outCandidate.m_states.buildResourceSubset(source, selection);
}

bool GpuPersistentResourceStateCache::commit(Candidate& candidate)noexcept{
    if(
        &candidate.m_owner != this
        || !candidate.valid()
        || m_textures.get_allocator() != candidate.m_textures.get_allocator()
        || m_buffers.get_allocator() != candidate.m_buffers.get_allocator()
    )
        return false;

    if(!m_states.exchangeSnapshot(candidate.m_states))
        return false;

    m_textures.swap(candidate.m_textures);
    m_buffers.swap(candidate.m_buffers);
    candidate.m_consumed = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

