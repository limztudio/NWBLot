// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/alloc/scratch.h>
#include <core/graphics/rhi/resource_state_selection.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_list_state_handoff{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureStateKey = GraphicsBackend::TextureSubresourceStateKey;


// ScratchArena reclaims one most-recent allocation per alignment bucket. Keep each temporary index in one fixed
// allocation so destruction rewinds it exactly; node/bucket hash containers can leave non-LIFO storage behind and
// grow the arena on every repeated fan-in query.
template<typename Key, typename Hash, typename Equal>
class ScratchStateIndexMap final : NoCopy{
private:
    struct Slot{
        Key key{};
        usize value = 0u;
        bool occupied = false;
    };


public:
    ScratchStateIndexMap(
        Alloc::ScratchArena& arena,
        const usize maximumEntryCount,
        const Hash& hash = Hash(),
        const Equal& equal = Equal()
    )
        : m_slots(arena)
        , m_hash(hash)
        , m_equal(equal)
    {
        if(maximumEntryCount == 0u)
            return;

        if(AddOverflows<usize>(maximumEntryCount, maximumEntryCount)){
            m_valid = false;
            return;
        }
        const usize doubledEntryCount = maximumEntryCount + maximumEntryCount;
        if(AddOverflows<usize>(doubledEntryCount, 1u)){
            m_valid = false;
            return;
        }
        m_slots.resize(doubledEntryCount + 1u);
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_valid; }

    [[nodiscard]] const usize* find(const Key& key)const noexcept{
        if(m_slots.empty())
            return nullptr;

        usize slotIndex = m_hash(key) % m_slots.size();
        for(usize probedSlotCount = 0u; probedSlotCount < m_slots.size(); ++probedSlotCount){
            const Slot& slot = m_slots[slotIndex];
            if(!slot.occupied)
                return nullptr;
            if(m_equal(slot.key, key))
                return &slot.value;
            if(++slotIndex == m_slots.size())
                slotIndex = 0u;
        }
        return nullptr;
    }

    [[nodiscard]] bool insertOrAssign(const Key& key, const usize value)noexcept{
        if(m_slots.empty())
            return false;

        usize slotIndex = m_hash(key) % m_slots.size();
        for(usize probedSlotCount = 0u; probedSlotCount < m_slots.size(); ++probedSlotCount){
            Slot& slot = m_slots[slotIndex];
            if(!slot.occupied){
                slot.key = key;
                slot.value = value;
                slot.occupied = true;
                return true;
            }
            if(m_equal(slot.key, key)){
                slot.value = value;
                return true;
            }
            if(++slotIndex == m_slots.size())
                slotIndex = 0u;
        }
        return false;
    }


private:
    Vector<Slot, Alloc::ScratchArena> m_slots;
    Hash m_hash;
    Equal m_equal;
    bool m_valid = true;
};


using TextureStateIndexMap = ScratchStateIndexMap<
    TextureStateKey,
    GraphicsBackend::TextureSubresourceStateKeyHasher,
    GraphicsBackend::TextureSubresourceStateKeyEqualTo
>;
using BufferStateIndexMap = ScratchStateIndexMap<Buffer*, Hasher<Buffer*>, EqualTo<Buffer*>>;
using PermanentTextureStateIndexMap = ScratchStateIndexMap<Texture*, Hasher<Texture*>, EqualTo<Texture*>>;


template<typename StateVector, typename Predicate>
void RetainSelectedStates(StateVector& states, const Predicate& selected){
    usize retainedStateCount = 0u;
    for(usize stateIndex = 0u; stateIndex < states.size(); ++stateIndex){
        if(!selected(states[stateIndex]))
            continue;
        if(retainedStateCount != stateIndex)
            states[retainedStateCount] = states[stateIndex];
        ++retainedStateCount;
    }
    states.resize(retainedStateCount);
}


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
    const usize branchCount,
    Alloc::ScratchArena& scratchArena
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

    usize textureStateCapacity = base.m_textureStates.size();
    usize bufferStateCapacity = base.m_bufferStates.size();
    usize permanentTextureStateCapacity = base.m_permanentTextureStates.size();
    usize permanentBufferStateCapacity = base.m_permanentBufferStates.size();
    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        const CommandListResourceStateHandoff& branch = *branches[branchIndex];
        if(
            AddOverflows<usize>(textureStateCapacity, branch.m_textureStates.size())
            || AddOverflows<usize>(bufferStateCapacity, branch.m_bufferStates.size())
            || AddOverflows<usize>(permanentTextureStateCapacity, branch.m_permanentTextureStates.size())
            || AddOverflows<usize>(permanentBufferStateCapacity, branch.m_permanentBufferStates.size())
        ){
            reset();
            return false;
        }
        textureStateCapacity += branch.m_textureStates.size();
        bufferStateCapacity += branch.m_bufferStates.size();
        permanentTextureStateCapacity += branch.m_permanentTextureStates.size();
        permanentBufferStateCapacity += branch.m_permanentBufferStates.size();
    }

    reset();
    m_deviceGeneration = base.m_deviceGeneration;

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

    m_textureStates.reserve(textureStateCapacity);
    m_bufferStates.reserve(bufferStateCapacity);
    m_permanentTextureStates.reserve(permanentTextureStateCapacity);
    m_permanentBufferStates.reserve(permanentBufferStateCapacity);

    {
        TextureStateIndexMap resultIndices(
            scratchArena,
            textureStateCapacity,
            GraphicsBackend::TextureSubresourceStateKeyHasher(),
            GraphicsBackend::TextureSubresourceStateKeyEqualTo()
        );
        if(!resultIndices.valid()){
            reset();
            return false;
        }
        for(const TextureState& state : base.m_textureStates){
            const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
            const usize index = m_textureStates.size();
            m_textureStates.push_back(state);
            if(!resultIndices.insertOrAssign(key, index)){
                reset();
                return false;
            }
        }

        const usize baseStateCount = base.m_textureStates.size();
        const auto mergeState = [&](const TextureState& state){
            const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
            const usize* const resultIndex = resultIndices.find(key);
            if(!resultIndex){
                const usize index = m_textureStates.size();
                m_textureStates.push_back(state);
                return resultIndices.insertOrAssign(key, index);
            }

            const TextureState* const baseState = *resultIndex < baseStateCount
                ? &base.m_textureStates[*resultIndex]
                : nullptr
            ;
            if(baseState && sameTextureState(state, *baseState))
                return true;

            TextureState& resultState = m_textureStates[*resultIndex];
            if((!baseState || !sameTextureState(resultState, *baseState)) && !sameTextureState(resultState, state))
                return false;

            resultState = state;
            return true;
        };

        for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
            for(const TextureState& state : branches[branchIndex]->m_textureStates){
                if(!mergeState(state)){
                    reset();
                    return false;
                }
            }
        }
    }

    {
        BufferStateIndexMap resultIndices(scratchArena, bufferStateCapacity);
        if(!resultIndices.valid()){
            reset();
            return false;
        }
        for(const BufferState& state : base.m_bufferStates){
            const usize index = m_bufferStates.size();
            m_bufferStates.push_back(state);
            if(!resultIndices.insertOrAssign(state.buffer, index)){
                reset();
                return false;
            }
        }

        const usize baseStateCount = base.m_bufferStates.size();
        const auto mergeState = [&](const BufferState& state){
            const usize* const resultIndex = resultIndices.find(state.buffer);
            if(!resultIndex){
                const usize index = m_bufferStates.size();
                m_bufferStates.push_back(state);
                return resultIndices.insertOrAssign(state.buffer, index);
            }

            const BufferState* const baseState = *resultIndex < baseStateCount
                ? &base.m_bufferStates[*resultIndex]
                : nullptr
            ;
            if(baseState && sameBufferState(state, *baseState))
                return true;

            BufferState& resultState = m_bufferStates[*resultIndex];
            if((!baseState || !sameBufferState(resultState, *baseState)) && !sameBufferState(resultState, state))
                return false;

            resultState = state;
            return true;
        };

        for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
            for(const BufferState& state : branches[branchIndex]->m_bufferStates){
                if(!mergeState(state)){
                    reset();
                    return false;
                }
            }
        }
    }

    {
        PermanentTextureStateIndexMap resultIndices(scratchArena, permanentTextureStateCapacity);
        if(!resultIndices.valid()){
            reset();
            return false;
        }
        for(const PermanentTextureState& state : base.m_permanentTextureStates){
            const usize index = m_permanentTextureStates.size();
            m_permanentTextureStates.push_back(state);
            if(!resultIndices.insertOrAssign(state.texture, index)){
                reset();
                return false;
            }
        }

        const usize baseStateCount = base.m_permanentTextureStates.size();
        const auto mergeState = [&](const PermanentTextureState& state){
            const usize* const resultIndex = resultIndices.find(state.texture);
            if(!resultIndex){
                const usize index = m_permanentTextureStates.size();
                m_permanentTextureStates.push_back(state);
                return resultIndices.insertOrAssign(state.texture, index);
            }

            const PermanentTextureState* const baseState = *resultIndex < baseStateCount
                ? &base.m_permanentTextureStates[*resultIndex]
                : nullptr
            ;
            if(baseState && samePermanentTextureState(state, *baseState))
                return true;

            PermanentTextureState& resultState = m_permanentTextureStates[*resultIndex];
            if((!baseState || !samePermanentTextureState(resultState, *baseState)) && !samePermanentTextureState(resultState, state))
                return false;

            resultState = state;
            return true;
        };

        for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
            for(const PermanentTextureState& state : branches[branchIndex]->m_permanentTextureStates){
                if(!mergeState(state)){
                    reset();
                    return false;
                }
            }
        }
    }

    {
        BufferStateIndexMap resultIndices(scratchArena, permanentBufferStateCapacity);
        if(!resultIndices.valid()){
            reset();
            return false;
        }
        for(const BufferState& state : base.m_permanentBufferStates){
            const usize index = m_permanentBufferStates.size();
            m_permanentBufferStates.push_back(state);
            if(!resultIndices.insertOrAssign(state.buffer, index)){
                reset();
                return false;
            }
        }

        const usize baseStateCount = base.m_permanentBufferStates.size();
        const auto mergeState = [&](const BufferState& state){
            const usize* const resultIndex = resultIndices.find(state.buffer);
            if(!resultIndex){
                const usize index = m_permanentBufferStates.size();
                m_permanentBufferStates.push_back(state);
                return resultIndices.insertOrAssign(state.buffer, index);
            }

            const BufferState* const baseState = *resultIndex < baseStateCount
                ? &base.m_permanentBufferStates[*resultIndex]
                : nullptr
            ;
            if(baseState && sameBufferState(state, *baseState))
                return true;

            BufferState& resultState = m_permanentBufferStates[*resultIndex];
            if((!baseState || !sameBufferState(resultState, *baseState)) && !sameBufferState(resultState, state))
                return false;

            resultState = state;
            return true;
        };

        for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
            for(const BufferState& state : branches[branchIndex]->m_permanentBufferStates){
                if(!mergeState(state)){
                    reset();
                    return false;
                }
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
    const usize bufferCount,
    Alloc::ScratchArena& scratchArena
){
    if(
        !source.valid()
        || source.m_deviceGeneration == 0u
        || (textureCount != 0u && !textures)
        || (bufferCount != 0u && !buffers)
    )
        return false;

    CommandListResourceSelection selection(scratchArena);
    for(usize index = 0u; index < textureCount; ++index){
        if(!selection.addTexture(textures[index], index))
            return false;
    }
    for(usize index = 0u; index < bufferCount; ++index){
        if(!selection.addBuffer(buffers[index], index))
            return false;
    }
    return buildResourceSubset(source, selection);
}

bool CommandListResourceStateHandoff::buildResourceSubset(
    const CommandListResourceStateHandoff& source,
    const CommandListResourceSelection& selection
){
    if(!source.valid() || source.m_deviceGeneration == 0u || !selection.valid())
        return false;

    usize textureStateCount = 0u;
    for(const TextureState& state : source.m_textureStates){
        if(selection.containsTexture(state.texture))
            ++textureStateCount;
    }
    usize bufferStateCount = 0u;
    for(const BufferState& state : source.m_bufferStates){
        if(selection.containsBuffer(state.buffer))
            ++bufferStateCount;
    }
    usize permanentTextureStateCount = 0u;
    for(const PermanentTextureState& state : source.m_permanentTextureStates){
        if(selection.containsTexture(state.texture))
            ++permanentTextureStateCount;
    }
    usize permanentBufferStateCount = 0u;
    for(const BufferState& state : source.m_permanentBufferStates){
        if(selection.containsBuffer(state.buffer))
            ++permanentBufferStateCount;
    }

    // Reserve every destination vector before changing logical state. An allocation exception therefore leaves the
    // published snapshot intact, while the commit phase only copies trivially copyable states into warmed storage.
    m_textureStates.reserve(textureStateCount);
    m_bufferStates.reserve(bufferStateCount);
    m_permanentTextureStates.reserve(permanentTextureStateCount);
    m_permanentBufferStates.reserve(permanentBufferStateCount);
    const u16 sourceDeviceGeneration = source.m_deviceGeneration;
    if(this == &source){
        using namespace __hidden_command_list_state_handoff;
        RetainSelectedStates(m_textureStates, [&](const TextureState& state){
            return selection.containsTexture(state.texture);
        });
        RetainSelectedStates(m_bufferStates, [&](const BufferState& state){
            return selection.containsBuffer(state.buffer);
        });
        RetainSelectedStates(m_permanentTextureStates, [&](const PermanentTextureState& state){
            return selection.containsTexture(state.texture);
        });
        RetainSelectedStates(m_permanentBufferStates, [&](const BufferState& state){
            return selection.containsBuffer(state.buffer);
        });
    }
    else{
        m_textureStates.clear();
        m_bufferStates.clear();
        m_permanentTextureStates.clear();
        m_permanentBufferStates.clear();
        for(const TextureState& state : source.m_textureStates){
            if(selection.containsTexture(state.texture))
                m_textureStates.push_back(state);
        }
        for(const BufferState& state : source.m_bufferStates){
            if(selection.containsBuffer(state.buffer))
                m_bufferStates.push_back(state);
        }
        for(const PermanentTextureState& state : source.m_permanentTextureStates){
            if(selection.containsTexture(state.texture))
                m_permanentTextureStates.push_back(state);
        }
        for(const BufferState& state : source.m_permanentBufferStates){
            if(selection.containsBuffer(state.buffer))
                m_permanentBufferStates.push_back(state);
        }
    }
    m_deviceGeneration = sourceDeviceGeneration;
    m_valid = true;
    return true;
}

bool CommandListResourceStateHandoff::buildTextureSubset(
    const CommandListResourceStateHandoff& source,
    Texture* const texture,
    Alloc::ScratchArena& scratchArena
){
    Texture* const textures[] = { texture };
    return buildResourceSubset(source, textures, 1u, nullptr, 0u, scratchArena);
}

bool CommandListResourceStateHandoff::buildTextureRangeSubset(
    const CommandListResourceStateHandoff& source,
    Texture* const texture,
    const TextureSubresourceSet subresources
){
    if(
        !source.valid()
        || source.m_deviceGeneration == 0u
        || !texture
    )
        return false;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(
        texture->getCreationDescription(),
        TextureSubresourceMipResolve::Range
    );
    if(resolvedSubresources.numMipLevels == 0u || resolvedSubresources.numArraySlices == 0u)
        return false;

    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const auto contains = [&](const MipLevel mipLevel, const ArraySlice arraySlice){
        return mipLevel >= resolvedSubresources.baseMipLevel
            && mipLevel < mipEnd
            && arraySlice >= resolvedSubresources.baseArraySlice
            && arraySlice < arrayEnd
        ;
    };

    usize textureStateCount = 0u;
    for(const TextureState& state : source.m_textureStates){
        if(state.texture == texture && contains(state.mipLevel, state.arraySlice))
            ++textureStateCount;
    }
    usize permanentTextureStateCount = 0u;
    for(const PermanentTextureState& state : source.m_permanentTextureStates){
        if(state.texture == texture)
            ++permanentTextureStateCount;
    }

    m_textureStates.reserve(textureStateCount);
    m_permanentTextureStates.reserve(permanentTextureStateCount);
    const u16 sourceDeviceGeneration = source.m_deviceGeneration;
    if(this == &source){
        using namespace __hidden_command_list_state_handoff;
        RetainSelectedStates(m_textureStates, [&](const TextureState& state){
            return state.texture == texture && contains(state.mipLevel, state.arraySlice);
        });
        RetainSelectedStates(m_permanentTextureStates, [&](const PermanentTextureState& state){
            return state.texture == texture;
        });
        m_bufferStates.clear();
        m_permanentBufferStates.clear();
    }
    else{
        m_textureStates.clear();
        m_bufferStates.clear();
        m_permanentTextureStates.clear();
        m_permanentBufferStates.clear();
        for(const TextureState& state : source.m_textureStates){
            if(state.texture == texture && contains(state.mipLevel, state.arraySlice))
                m_textureStates.push_back(state);
        }
        for(const PermanentTextureState& state : source.m_permanentTextureStates){
            if(state.texture == texture)
                m_permanentTextureStates.push_back(state);
        }
    }
    m_deviceGeneration = sourceDeviceGeneration;
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

    if(!source.valid() || source.m_deviceGeneration == 0u)
        return false;

    m_textureStates.reserve(source.m_textureStates.size());
    m_bufferStates.reserve(source.m_bufferStates.size());
    m_permanentTextureStates.reserve(source.m_permanentTextureStates.size());
    m_permanentBufferStates.reserve(source.m_permanentBufferStates.size());
    AssignTriviallyCopyableVector(m_textureStates, source.m_textureStates);
    AssignTriviallyCopyableVector(m_bufferStates, source.m_bufferStates);
    AssignTriviallyCopyableVector(m_permanentTextureStates, source.m_permanentTextureStates);
    AssignTriviallyCopyableVector(m_permanentBufferStates, source.m_permanentBufferStates);
    m_deviceGeneration = source.m_deviceGeneration;
    m_valid = true;
    return true;
}

bool CommandListResourceStateHandoff::equivalentTo(const CommandListResourceStateHandoff& snapshot)const noexcept{
    if(
        m_valid != snapshot.m_valid
        || m_deviceGeneration != snapshot.m_deviceGeneration
        || m_textureStates.size() != snapshot.m_textureStates.size()
        || m_bufferStates.size() != snapshot.m_bufferStates.size()
        || m_permanentTextureStates.size() != snapshot.m_permanentTextureStates.size()
        || m_permanentBufferStates.size() != snapshot.m_permanentBufferStates.size()
    )
        return false;

    const auto sameTextureState = [](const TextureState& lhs, const TextureState& rhs)noexcept{
        return lhs.texture == rhs.texture
            && lhs.mipLevel == rhs.mipLevel
            && lhs.arraySlice == rhs.arraySlice
            && lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };
    for(usize stateIndex = 0u; stateIndex < m_textureStates.size(); ++stateIndex){
        if(!sameTextureState(m_textureStates[stateIndex], snapshot.m_textureStates[stateIndex]))
            return false;
    }

    const auto sameBufferState = [](const BufferState& lhs, const BufferState& rhs)noexcept{
        return lhs.buffer == rhs.buffer
            && lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };
    for(usize stateIndex = 0u; stateIndex < m_bufferStates.size(); ++stateIndex){
        if(!sameBufferState(m_bufferStates[stateIndex], snapshot.m_bufferStates[stateIndex]))
            return false;
    }

    const auto samePermanentTextureState = [](const PermanentTextureState& lhs, const PermanentTextureState& rhs)noexcept{
        return lhs.texture == rhs.texture
            && lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };
    for(usize stateIndex = 0u; stateIndex < m_permanentTextureStates.size(); ++stateIndex){
        if(!samePermanentTextureState(m_permanentTextureStates[stateIndex], snapshot.m_permanentTextureStates[stateIndex]))
            return false;
    }
    for(usize stateIndex = 0u; stateIndex < m_permanentBufferStates.size(); ++stateIndex){
        if(!sameBufferState(m_permanentBufferStates[stateIndex], snapshot.m_permanentBufferStates[stateIndex]))
            return false;
    }
    return true;
}

bool CommandListResourceStateHandoff::exchangeSnapshot(CommandListResourceStateHandoff& snapshot)noexcept{
    if(
        this == &snapshot
        || m_textureStates.get_allocator() != snapshot.m_textureStates.get_allocator()
        || m_bufferStates.get_allocator() != snapshot.m_bufferStates.get_allocator()
        || m_permanentTextureStates.get_allocator() != snapshot.m_permanentTextureStates.get_allocator()
        || m_permanentBufferStates.get_allocator() != snapshot.m_permanentBufferStates.get_allocator()
    )
        return false;

    m_textureStates.swap(snapshot.m_textureStates);
    m_bufferStates.swap(snapshot.m_bufferStates);
    m_permanentTextureStates.swap(snapshot.m_permanentTextureStates);
    m_permanentBufferStates.swap(snapshot.m_permanentBufferStates);
    Swap(m_deviceGeneration, snapshot.m_deviceGeneration);
    Swap(m_valid, snapshot.m_valid);
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

