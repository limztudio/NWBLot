// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/alloc/scratch.h>
#include <global/algorithm.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_list_state_handoff_merge{


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

    if(AddOverflows<usize>(bufferStateCapacity, bufferStateCapacity) || AddOverflows<usize>(branchCount, 1u)){
        reset();
        return false;
    }

    reset();
    m_deviceGeneration = base.m_deviceGeneration;

    using namespace __hidden_command_list_state_handoff_merge;
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
    m_bufferStates.reserve(bufferStateCapacity + bufferStateCapacity);
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
        struct BufferRangeEvent{
            const BufferState* state = nullptr;
            usize bufferIndex = 0u;
            usize branchIndex = 0u;
            u64 offset = 0u;
            bool beginning = false;
        };

        BufferStateIndexMap bufferIndices(scratchArena, bufferStateCapacity);
        if(!bufferIndices.valid()){
            reset();
            return false;
        }
        Vector<BufferRangeEvent, Alloc::ScratchArena> events(scratchArena);
        events.reserve(bufferStateCapacity + bufferStateCapacity);
        Vector<const BufferState*, Alloc::ScratchArena> activeStates(scratchArena);
        activeStates.resize(branchCount + 1u, nullptr);
        usize bufferCount = 0u;
        const auto addEvents = [&](const BufferState& state, const usize branchIndex){
            if(!state.buffer || !state.range.hasExtent())
                return false;

            const usize* const existingIndex = bufferIndices.find(state.buffer);
            const usize bufferIndex = existingIndex ? *existingIndex : bufferCount;
            if(!existingIndex){
                if(!bufferIndices.insertOrAssign(state.buffer, bufferIndex))
                    return false;
                ++bufferCount;
            }
            events.push_back({ &state, bufferIndex, branchIndex, state.range.byteOffset, true });
            events.push_back({ &state, bufferIndex, branchIndex, state.range.end(), false });
            return true;
        };
        for(const BufferState& state : base.m_bufferStates){
            if(!addEvents(state, 0u)){
                reset();
                return false;
            }
        }
        for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
            for(const BufferState& state : branches[branchIndex]->m_bufferStates){
                if(!addEvents(state, branchIndex + 1u)){
                    reset();
                    return false;
                }
            }
        }
        Sort(events.begin(), events.end(), [](const BufferRangeEvent& lhs, const BufferRangeEvent& rhs){
            if(lhs.bufferIndex != rhs.bufferIndex)
                return lhs.bufferIndex < rhs.bufferIndex;
            if(lhs.offset != rhs.offset)
                return lhs.offset < rhs.offset;
            return lhs.beginning < rhs.beginning;
        });

        // Sweep the union of interval boundaries. A branch retaining the base state contributes no change;
        // independent changes must agree only where their byte ranges overlap.
        usize previousBufferIndex = 0u;
        u64 previousOffset = 0u;
        for(const BufferRangeEvent& event : events){
            if(event.bufferIndex != previousBufferIndex){
                previousBufferIndex = event.bufferIndex;
                previousOffset = event.offset;
            }
            if(previousOffset < event.offset){
                const BufferState* const baseState = activeStates[0u];
                const BufferState* mergedState = baseState;
                for(usize branchIndex = 1u; branchIndex < activeStates.size(); ++branchIndex){
                    const BufferState* const state = activeStates[branchIndex];
                    if(!state || (baseState && sameBufferState(*state, *baseState)))
                        continue;
                    if(
                        mergedState
                        && mergedState != baseState
                        && !sameBufferState(*mergedState, *state)
                    ){
                        reset();
                        return false;
                    }
                    mergedState = state;
                }
                if(mergedState){
                    if(
                        mergedState->releaseDestinationQueue.valid()
                        && (mergedState->range.byteOffset != previousOffset || mergedState->range.end() != event.offset)
                    ){
                        reset();
                        return false;
                    }
                    if(
                        !mergedState->releaseDestinationQueue.valid()
                        && !m_bufferStates.empty()
                        && m_bufferStates.back().buffer == mergedState->buffer
                        && m_bufferStates.back().range.end() == previousOffset
                        && sameBufferState(m_bufferStates.back(), *mergedState)
                    )
                        m_bufferStates.back().range.byteSize = event.offset - m_bufferStates.back().range.byteOffset;
                    else{
                        BufferState state = *mergedState;
                        state.range = BufferRange(previousOffset, event.offset - previousOffset);
                        m_bufferStates.push_back(state);
                    }
                }
                previousOffset = event.offset;
            }
            const BufferState*& activeState = activeStates[event.branchIndex];
            if(event.beginning){
                if(activeState){
                    reset();
                    return false;
                }
                activeState = event.state;
            }
            else
                activeState = nullptr;
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

