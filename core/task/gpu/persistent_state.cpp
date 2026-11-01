// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "persistent_state.h"

#include <core/graphics/rhi/resource_state_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_persistent_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 BuildSelection(CommandListResourceSelection& selection, const Span<const TextureHandle> textures, const Span<const BufferHandle> buffers)noexcept{
    for(usize index = 0u; index < textures.size(); ++index){
        if(!selection.addTexture(textures[index].get(), index))
            return false;
    }
    for(usize index = 0u; index < buffers.size(); ++index){
        if(!selection.addBuffer(buffers[index].get(), index))
            return false;
    }
    return true;
}

void RetainSelectedHandles(const CommandListResourceSelection& selection, const Span<const TextureHandle> textures, const Span<const BufferHandle> buffers, GraphicsVector<TextureHandle>& outTextures, GraphicsVector<BufferHandle>& outBuffers){
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


void GpuPersistentResourceStateCache::Candidate::reset()noexcept{
    m_states.reset();
    m_textures.clear();
    m_buffers.clear();
    m_consumed = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuPersistentResourceStateCache::reset()noexcept{
    m_states.reset();
    m_textures.clear();
    m_buffers.clear();
    m_spareTextures.clear();
    m_spareBuffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuPersistentResourceStateCache::replaceResourceSubset(
    const CommandListResourceStateHandoff& source,
    const Span<const TextureHandle> textures,
    const Span<const BufferHandle> buffers,
    Alloc::ScratchArena& scratchArena
){
    if(&source == &m_states){
        if(!source.valid() || source.deviceGeneration() == 0u || (!textures.empty() && !textures.data()) || (!buffers.empty() && !buffers.data())){
            reset();
            return false;
        }

        CommandListResourceSelection selection(scratchArena);
        using namespace __hidden_gpu_persistent_state;
        if(!BuildSelection(selection, textures, buffers)){
            reset();
            return false;
        }
        // A prior unwound call may have left partial handles here.
        m_spareTextures.clear();
        m_spareBuffers.clear();
        RetainSelectedHandles(selection, textures, buffers, m_spareTextures, m_spareBuffers);

        // Finish every allocation and retain new live handles before filtering the accepted snapshot.
        // Self-filtering only shrinks its existing state arrays; after its input checks, compaction and these same-arena swaps cannot fail.
        if(!m_states.buildResourceSubset(m_states, selection)){
            reset();
            return false;
        }
        m_textures.swap(m_spareTextures);
        m_buffers.swap(m_spareBuffers);
        // Release the displaced handles now but keep their capacity.
        m_spareTextures.clear();
        m_spareBuffers.clear();
        return true;
    }

    Candidate candidate(*this);
    if(!buildFilteredResourceSubset(source, textures, buffers, scratchArena, candidate)){
        reset();
        return false;
    }
    if(!commit(candidate)){
        reset();
        return false;
    }
    return true;
}

b8 GpuPersistentResourceStateCache::buildMergedResourceSubset(
    const CommandListResourceStateHandoff& source,
    const Span<const TextureHandle> textures,
    const Span<const BufferHandle> buffers,
    Alloc::ScratchArena& scratchArena,
    Candidate& outCandidate
)const{
    if(&outCandidate.m_owner != this)
        return false;

    Candidate sourceCandidate(*this);
    Optional<CommandListResourceStateHandoff> retainedStates;
    {
        if(!source.valid() || (!textures.empty() && !textures.data()) || (!buffers.empty() && !buffers.data()))
            return false;
        CommandListResourceSelection selection(scratchArena);
        using namespace __hidden_gpu_persistent_state;
        if(!BuildSelection(selection, textures, buffers))
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
    // The selected typed handles stay in sourceCandidate through fan-in.
    // Release the temporary selection before fan-in reuses the scratch arena, so repeats reclaim in LIFO order.
    if(retainedStates){
        const Array<const CommandListResourceStateHandoff*, 1> branches = {{ &sourceCandidate.m_states }};
        if(!outCandidate.m_states.buildFanIn(*retainedStates, branches.data(), branches.size(), scratchArena))
            return false;
    }
    else if(!outCandidate.m_states.copyFrom(sourceCandidate.m_states))
        return false;

    outCandidate.m_textures.swap(sourceCandidate.m_textures);
    outCandidate.m_buffers.swap(sourceCandidate.m_buffers);
    return true;
}

b8 GpuPersistentResourceStateCache::buildFilteredResourceSubset(
    const CommandListResourceStateHandoff& source,
    const Span<const TextureHandle> textures,
    const Span<const BufferHandle> buffers,
    Alloc::ScratchArena& scratchArena,
    Candidate& outCandidate
)const{
    if(&outCandidate.m_owner != this)
        return false;
    outCandidate.reset();
    if(!source.valid() || (!textures.empty() && !textures.data()) || (!buffers.empty() && !buffers.data()))
        return false;

    CommandListResourceSelection selection(scratchArena);
    using namespace __hidden_gpu_persistent_state;
    if(!BuildSelection(selection, textures, buffers))
        return false;
    RetainSelectedHandles(selection, textures, buffers, outCandidate.m_textures, outCandidate.m_buffers);
    return outCandidate.m_states.buildResourceSubset(source, selection);
}

b8 GpuPersistentResourceStateCache::commit(Candidate& candidate)noexcept{
    if(&candidate.m_owner != this || !candidate.valid() || m_textures.get_allocator() != candidate.m_textures.get_allocator() || m_buffers.get_allocator() != candidate.m_buffers.get_allocator())
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

