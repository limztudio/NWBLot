// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_list_state_handoff{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureStateKey = GraphicsBackend::TextureSubresourceStateKey;
using TextureStateIndexMap = HashMap<
    TextureStateKey,
    usize,
    GraphicsBackend::TextureSubresourceStateKeyHasher,
    GraphicsBackend::TextureSubresourceStateKeyEqualTo,
    Alloc::GlobalArena
>;
using BufferStateIndexMap = HashMap<Buffer*, usize, Hasher<Buffer*>, EqualTo<Buffer*>, Alloc::GlobalArena>;
using PermanentTextureStateIndexMap = HashMap<Texture*, usize, Hasher<Texture*>, EqualTo<Texture*>, Alloc::GlobalArena>;


[[nodiscard]] static bool OwnershipMatches(
    const GpuPhysicalQueueId ownerQueue,
    const GpuPhysicalQueueId releaseDestinationQueue,
    const GpuPhysicalQueueId expectedOwnerQueue,
    const GpuPhysicalQueueId expectedReleaseDestinationQueue
)noexcept{
    if(ownerQueue != expectedOwnerQueue)
        return false;
    return expectedOwnerQueue == expectedReleaseDestinationQueue
        ? !releaseDestinationQueue.valid()
        : releaseDestinationQueue == expectedReleaseDestinationQueue
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandListResourceStateHandoff::buildFanIn(
    const CommandListResourceStateHandoff& base,
    const CommandListResourceStateHandoff* const* branches,
    const usize branchCount
){
    if(this == &base)
        return false;

    if(
        !base.valid()
        || base.m_deviceGeneration == 0u
        || (branchCount != 0u && !branches)
    ){
        reset();
        return false;
    }

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        const CommandListResourceStateHandoff* branch = branches[branchIndex];
        if(branch == this)
            return false;

        if(
            !branch
            || !branch->valid()
            || branch->m_deviceGeneration != base.m_deviceGeneration
        ){
            reset();
            return false;
        }
    }

    reset();
    m_deviceGeneration = base.m_deviceGeneration;

    auto& arena = m_textureStates.get_allocator().arena();
    using namespace __hidden_command_list_state_handoff;
    const auto sameTextureState = [](const TextureState& lhs, const TextureState& rhs){
        return
            lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };
    const auto sameBufferState = [](const BufferState& lhs, const BufferState& rhs){
        return
            lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };
    const auto samePermanentTextureState = [](const PermanentTextureState& lhs, const PermanentTextureState& rhs){
        return
            lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };

    TextureStateIndexMap baseTextureIndices(
        0u,
        GraphicsBackend::TextureSubresourceStateKeyHasher(),
        GraphicsBackend::TextureSubresourceStateKeyEqualTo(),
        arena
    );
    TextureStateIndexMap resultTextureIndices(
        0u,
        GraphicsBackend::TextureSubresourceStateKeyHasher(),
        GraphicsBackend::TextureSubresourceStateKeyEqualTo(),
        arena
    );
    BufferStateIndexMap baseBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);
    BufferStateIndexMap resultBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);
    PermanentTextureStateIndexMap basePermanentTextureIndices(0u, Hasher<Texture*>(), EqualTo<Texture*>(), arena);
    PermanentTextureStateIndexMap resultPermanentTextureIndices(0u, Hasher<Texture*>(), EqualTo<Texture*>(), arena);
    BufferStateIndexMap basePermanentBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);
    BufferStateIndexMap resultPermanentBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);

    m_textureStates.reserve(base.m_textureStates.size());
    baseTextureIndices.reserve(base.m_textureStates.size());
    resultTextureIndices.reserve(base.m_textureStates.size());
    for(const TextureState& state : base.m_textureStates){
        const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        const usize index = m_textureStates.size();
        m_textureStates.push_back(state);
        baseTextureIndices.insert_or_assign(key, index);
        resultTextureIndices.insert_or_assign(key, index);
    }

    m_bufferStates.reserve(base.m_bufferStates.size());
    baseBufferIndices.reserve(base.m_bufferStates.size());
    resultBufferIndices.reserve(base.m_bufferStates.size());
    for(const BufferState& state : base.m_bufferStates){
        const usize index = m_bufferStates.size();
        m_bufferStates.push_back(state);
        baseBufferIndices.insert_or_assign(state.buffer, index);
        resultBufferIndices.insert_or_assign(state.buffer, index);
    }

    m_permanentTextureStates.reserve(base.m_permanentTextureStates.size());
    basePermanentTextureIndices.reserve(base.m_permanentTextureStates.size());
    resultPermanentTextureIndices.reserve(base.m_permanentTextureStates.size());
    for(const PermanentTextureState& state : base.m_permanentTextureStates){
        const usize index = m_permanentTextureStates.size();
        m_permanentTextureStates.push_back(state);
        basePermanentTextureIndices.insert_or_assign(state.texture, index);
        resultPermanentTextureIndices.insert_or_assign(state.texture, index);
    }

    m_permanentBufferStates.reserve(base.m_permanentBufferStates.size());
    basePermanentBufferIndices.reserve(base.m_permanentBufferStates.size());
    resultPermanentBufferIndices.reserve(base.m_permanentBufferStates.size());
    for(const BufferState& state : base.m_permanentBufferStates){
        const usize index = m_permanentBufferStates.size();
        m_permanentBufferStates.push_back(state);
        basePermanentBufferIndices.insert_or_assign(state.buffer, index);
        resultPermanentBufferIndices.insert_or_assign(state.buffer, index);
    }

    const auto mergeTextureState = [&](const TextureState& state){
        const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        const auto baseIt = baseTextureIndices.find(key);
        const TextureState* const baseState = baseIt != baseTextureIndices.end()
            ? &base.m_textureStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && sameTextureState(state, *baseState))
            return true;

        const auto resultIt = resultTextureIndices.find(key);
        if(resultIt == resultTextureIndices.end()){
            const usize index = m_textureStates.size();
            m_textureStates.push_back(state);
            resultTextureIndices.insert_or_assign(key, index);
            return true;
        }

        TextureState& resultState = m_textureStates[resultIt.value()];
        if((!baseState || !sameTextureState(resultState, *baseState)) && !sameTextureState(resultState, state))
            return false;

        resultState = state;
        return true;
    };
    const auto mergeBufferState = [&](const BufferState& state){
        const auto baseIt = baseBufferIndices.find(state.buffer);
        const BufferState* const baseState = baseIt != baseBufferIndices.end()
            ? &base.m_bufferStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && sameBufferState(state, *baseState))
            return true;

        const auto resultIt = resultBufferIndices.find(state.buffer);
        if(resultIt == resultBufferIndices.end()){
            const usize index = m_bufferStates.size();
            m_bufferStates.push_back(state);
            resultBufferIndices.insert_or_assign(state.buffer, index);
            return true;
        }

        BufferState& resultState = m_bufferStates[resultIt.value()];
        if((!baseState || !sameBufferState(resultState, *baseState)) && !sameBufferState(resultState, state))
            return false;

        resultState = state;
        return true;
    };
    const auto mergePermanentTextureState = [&](const PermanentTextureState& state){
        const auto baseIt = basePermanentTextureIndices.find(state.texture);
        const PermanentTextureState* const baseState = baseIt != basePermanentTextureIndices.end()
            ? &base.m_permanentTextureStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && samePermanentTextureState(state, *baseState))
            return true;

        const auto resultIt = resultPermanentTextureIndices.find(state.texture);
        if(resultIt == resultPermanentTextureIndices.end()){
            const usize index = m_permanentTextureStates.size();
            m_permanentTextureStates.push_back(state);
            resultPermanentTextureIndices.insert_or_assign(state.texture, index);
            return true;
        }

        PermanentTextureState& resultState = m_permanentTextureStates[resultIt.value()];
        if((!baseState || !samePermanentTextureState(resultState, *baseState)) && !samePermanentTextureState(resultState, state))
            return false;

        resultState = state;
        return true;
    };
    const auto mergePermanentBufferState = [&](const BufferState& state){
        const auto baseIt = basePermanentBufferIndices.find(state.buffer);
        const BufferState* const baseState = baseIt != basePermanentBufferIndices.end()
            ? &base.m_permanentBufferStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && sameBufferState(state, *baseState))
            return true;

        const auto resultIt = resultPermanentBufferIndices.find(state.buffer);
        if(resultIt == resultPermanentBufferIndices.end()){
            const usize index = m_permanentBufferStates.size();
            m_permanentBufferStates.push_back(state);
            resultPermanentBufferIndices.insert_or_assign(state.buffer, index);
            return true;
        }

        BufferState& resultState = m_permanentBufferStates[resultIt.value()];
        if((!baseState || !sameBufferState(resultState, *baseState)) && !sameBufferState(resultState, state))
            return false;

        resultState = state;
        return true;
    };

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        const CommandListResourceStateHandoff& branch = *branches[branchIndex];
        for(const TextureState& state : branch.m_textureStates){
            if(!mergeTextureState(state)){
                reset();
                return false;
            }
        }
        for(const BufferState& state : branch.m_bufferStates){
            if(!mergeBufferState(state)){
                reset();
                return false;
            }
        }
        for(const PermanentTextureState& state : branch.m_permanentTextureStates){
            if(!mergePermanentTextureState(state)){
                reset();
                return false;
            }
        }
        for(const BufferState& state : branch.m_permanentBufferStates){
            if(!mergePermanentBufferState(state)){
                reset();
                return false;
            }
        }
    }

    m_valid = true;
    return true;
}

bool CommandListResourceStateHandoff::buildResourceSubset(
    const CommandListResourceStateHandoff& source,
    Texture* const* textures,
    const usize textureCount,
    Buffer* const* buffers,
    const usize bufferCount
){
    if(
        this == &source
        || !source.valid()
        || source.m_deviceGeneration == 0u
        || (textureCount != 0u && !textures)
        || (bufferCount != 0u && !buffers)
    ){
        reset();
        return false;
    }

    const auto containsTexture = [&](Texture* texture){
        if(!texture)
            return false;
        for(usize i = 0u; i < textureCount; ++i){
            if(textures[i] == texture)
                return true;
        }
        return false;
    };
    const auto containsBuffer = [&](Buffer* buffer){
        if(!buffer)
            return false;
        for(usize i = 0u; i < bufferCount; ++i){
            if(buffers[i] == buffer)
                return true;
        }
        return false;
    };

    reset();
    m_deviceGeneration = source.m_deviceGeneration;
    for(const TextureState& state : source.m_textureStates){
        if(containsTexture(state.texture))
            m_textureStates.push_back(state);
    }
    for(const BufferState& state : source.m_bufferStates){
        if(containsBuffer(state.buffer))
            m_bufferStates.push_back(state);
    }
    for(const PermanentTextureState& state : source.m_permanentTextureStates){
        if(containsTexture(state.texture))
            m_permanentTextureStates.push_back(state);
    }
    for(const BufferState& state : source.m_permanentBufferStates){
        if(containsBuffer(state.buffer))
            m_permanentBufferStates.push_back(state);
    }

    m_valid = true;
    return true;
}

bool CommandListResourceStateHandoff::buildTextureSubset(
    const CommandListResourceStateHandoff& source,
    Texture* const texture
){
    Texture* const textures[] = { texture };
    return buildResourceSubset(source, textures, 1u, nullptr, 0u);
}

bool CommandListResourceStateHandoff::buildTextureRangeSubset(
    const CommandListResourceStateHandoff& source,
    Texture* const texture,
    const TextureSubresourceSet subresources
){
    if(
        this == &source
        || !source.valid()
        || source.m_deviceGeneration == 0u
        || !texture
    ){
        reset();
        return false;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(
        texture->getCreationDescription(),
        TextureSubresourceMipResolve::Range
    );
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const auto contains = [&](const MipLevel mipLevel, const ArraySlice arraySlice){
        return mipLevel >= resolvedSubresources.baseMipLevel
            && mipLevel < mipEnd
            && arraySlice >= resolvedSubresources.baseArraySlice
            && arraySlice < arrayEnd
        ;
    };

    reset();
    m_deviceGeneration = source.m_deviceGeneration;
    for(const TextureState& state : source.m_textureStates){
        if(state.texture == texture && contains(state.mipLevel, state.arraySlice))
            m_textureStates.push_back(state);
    }
    for(const PermanentTextureState& state : source.m_permanentTextureStates){
        if(state.texture == texture)
            m_permanentTextureStates.push_back(state);
    }
    m_valid = true;
    return true;
}

bool CommandListResourceStateHandoff::coversTextureRangeWithOwnership(
    Texture* const texture,
    const TextureSubresourceSet subresources,
    const GpuPhysicalQueueId expectedOwnerQueue,
    const GpuPhysicalQueueId expectedReleaseDestinationQueue
)const{
    if(
        !m_valid
        || m_deviceGeneration == 0u
        || !texture
        || !expectedOwnerQueue.valid()
    )
        return false;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(
        texture->getCreationDescription(),
        TextureSubresourceMipResolve::Range
    );
    if(resolvedSubresources.numMipLevels == 0u || resolvedSubresources.numArraySlices == 0u)
        return false;

    const PermanentTextureState* permanentState = nullptr;
    for(const PermanentTextureState& state : m_permanentTextureStates){
        if(state.texture != texture)
            continue;
        if(
            permanentState
            || !__hidden_command_list_state_handoff::OwnershipMatches(
                state.ownerQueue,
                state.releaseDestinationQueue,
                expectedOwnerQueue,
                expectedReleaseDestinationQueue
            )
        )
            return false;
        permanentState = &state;
    }
    if(permanentState)
        return true;

    for(MipLevel mipOffset = 0u; mipOffset < resolvedSubresources.numMipLevels; ++mipOffset){
        const MipLevel mipLevel = resolvedSubresources.baseMipLevel + mipOffset;
        for(ArraySlice arrayOffset = 0u; arrayOffset < resolvedSubresources.numArraySlices; ++arrayOffset){
            const ArraySlice arraySlice = resolvedSubresources.baseArraySlice + arrayOffset;
            const TextureState* matchingState = nullptr;
            for(const TextureState& state : m_textureStates){
                if(
                    state.texture != texture
                    || state.mipLevel != mipLevel
                    || state.arraySlice != arraySlice
                )
                    continue;
                if(
                    matchingState
                    || !__hidden_command_list_state_handoff::OwnershipMatches(
                        state.ownerQueue,
                        state.releaseDestinationQueue,
                        expectedOwnerQueue,
                        expectedReleaseDestinationQueue
                    )
                )
                    return false;
                matchingState = &state;
            }
            if(!matchingState)
                return false;
        }
    }
    return true;
}

bool CommandListResourceStateHandoff::coversBufferWithOwnership(
    Buffer* const buffer,
    const GpuPhysicalQueueId expectedOwnerQueue,
    const GpuPhysicalQueueId expectedReleaseDestinationQueue
)const{
    if(
        !m_valid
        || m_deviceGeneration == 0u
        || !buffer
        || !expectedOwnerQueue.valid()
    )
        return false;

    const BufferState* permanentState = nullptr;
    for(const BufferState& state : m_permanentBufferStates){
        if(state.buffer != buffer)
            continue;
        if(
            permanentState
            || !__hidden_command_list_state_handoff::OwnershipMatches(
                state.ownerQueue,
                state.releaseDestinationQueue,
                expectedOwnerQueue,
                expectedReleaseDestinationQueue
            )
        )
            return false;
        permanentState = &state;
    }
    if(permanentState)
        return true;

    const BufferState* matchingState = nullptr;
    for(const BufferState& state : m_bufferStates){
        if(state.buffer != buffer)
            continue;
        if(
            matchingState
            || !__hidden_command_list_state_handoff::OwnershipMatches(
                state.ownerQueue,
                state.releaseDestinationQueue,
                expectedOwnerQueue,
                expectedReleaseDestinationQueue
            )
        )
            return false;
        matchingState = &state;
    }
    return matchingState != nullptr;
}

bool CommandListResourceStateHandoff::copyFrom(const CommandListResourceStateHandoff& source){
    if(this == &source)
        return source.valid() && source.m_deviceGeneration != 0u;

    reset();
    if(!source.valid() || source.m_deviceGeneration == 0u)
        return false;

    m_textureStates.reserve(source.m_textureStates.size());
    for(const TextureState& state : source.m_textureStates)
        m_textureStates.push_back(state);
    m_bufferStates.reserve(source.m_bufferStates.size());
    for(const BufferState& state : source.m_bufferStates)
        m_bufferStates.push_back(state);
    m_permanentTextureStates.reserve(source.m_permanentTextureStates.size());
    for(const PermanentTextureState& state : source.m_permanentTextureStates)
        m_permanentTextureStates.push_back(state);
    m_permanentBufferStates.reserve(source.m_permanentBufferStates.size());
    for(const BufferState& state : source.m_permanentBufferStates)
        m_permanentBufferStates.push_back(state);
    m_deviceGeneration = source.m_deviceGeneration;
    m_valid = true;
    return true;
}

bool CommandListResourceStateHandoff::empty()const noexcept{
    return m_textureStates.empty()
        && m_bufferStates.empty()
        && m_permanentTextureStates.empty()
        && m_permanentBufferStates.empty()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

