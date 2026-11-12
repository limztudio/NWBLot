// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_texture_state_fan_in.h"
#include "resource_state_handoff_key.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/backend_selection/resource_validation.h>
#include <core/alloc/scratch.h>

#include <global/fixed_index_map.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


b8 GpuTextureStateFanIn::MergeTransient(CommandListResourceStateHandoff& result, const CommandListResourceStateHandoff& base, const CommandListResourceStateHandoff* const* branches, const usize branchCount, const usize textureStateCapacity, Alloc::ScratchArena& scratchArena){
    using TextureState = CommandListResourceStateHandoff::TextureState;
    using TextureStateKey = TextureSubresourceStateKey;
    using TextureStateIndexMap = FixedIndexMap<TextureStateKey, Alloc::ScratchArena, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo>;
    const auto sameTextureState = [](const TextureState& lhs, const TextureState& rhs)noexcept{
        return lhs.state == rhs.state && lhs.queueSharing == rhs.queueSharing && lhs.ownerQueue == rhs.ownerQueue && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue;
    };
    TextureStateIndexMap resultIndices(scratchArena, textureStateCapacity, TextureSubresourceStateKeyHasher(), TextureSubresourceStateKeyEqualTo());
    if(!resultIndices.valid()){
        result.reset();
        return false;
    }
    for(const TextureState& state : base.m_textureStates){
        const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        const usize index = result.m_textureStates.size();
        result.m_textureStates.push_back(state);
        if(!resultIndices.insertOrAssign(key, index)){
            result.reset();
            return false;
        }
    }

    const usize baseStateCount = base.m_textureStates.size();
    const auto mergeState = [&](const TextureState& state)noexcept{
        const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        const usize* const resultIndex = resultIndices.find(key);
        if(!resultIndex){
            const usize index = result.m_textureStates.size();
            result.m_textureStates.push_back(state);
            return resultIndices.insertOrAssign(key, index);
        }

        const TextureState* const baseState = *resultIndex < baseStateCount ? &base.m_textureStates[*resultIndex] : nullptr;
        TextureState& resultState = result.m_textureStates[*resultIndex];
        if(
            GraphicsBackend::AreTextureReadStatesCompatible(resultState.state, state.state)
            && resultState.queueSharing == state.queueSharing
            && resultState.ownerQueue == state.ownerQueue
            && resultState.releaseDestinationQueue == state.releaseDestinationQueue
        ){
            // Retain both read scopes for the next transition without changing native layout or ownership.
            resultState.state |= state.state;
            return true;
        }
        if(baseState && sameTextureState(state, *baseState))
            return true;

        if((!baseState || !sameTextureState(resultState, *baseState)) && !sameTextureState(resultState, state))
            return false;

        resultState = state;
        return true;
    };

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        for(const TextureState& state : branches[branchIndex]->m_textureStates){
            if(!mergeState(state)){
                result.reset();
                return false;
            }
        }
    }
    return true;
}

b8 GpuTextureStateFanIn::MergePermanent(CommandListResourceStateHandoff& result, const CommandListResourceStateHandoff& base, const CommandListResourceStateHandoff* const* branches, const usize branchCount, const usize permanentTextureStateCapacity, Alloc::ScratchArena& scratchArena){
    using PermanentTextureState = CommandListResourceStateHandoff::PermanentTextureState;
    using PermanentTextureStateIndexMap = FixedIndexMap<Texture*, Alloc::ScratchArena, Hasher<Texture*>, EqualTo<Texture*>>;
    const auto samePermanentTextureState = [](const PermanentTextureState& lhs, const PermanentTextureState& rhs)noexcept{
        return lhs.state == rhs.state && lhs.queueSharing == rhs.queueSharing && lhs.ownerQueue == rhs.ownerQueue && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue;
    };
    PermanentTextureStateIndexMap resultIndices(scratchArena, permanentTextureStateCapacity);
    if(!resultIndices.valid()){
        result.reset();
        return false;
    }
    for(const PermanentTextureState& state : base.m_permanentTextureStates){
        const usize index = result.m_permanentTextureStates.size();
        result.m_permanentTextureStates.push_back(state);
        if(!resultIndices.insertOrAssign(state.texture, index)){
            result.reset();
            return false;
        }
    }

    const usize baseStateCount = base.m_permanentTextureStates.size();
    const auto mergeState = [&](const PermanentTextureState& state)noexcept{
        const usize* const resultIndex = resultIndices.find(state.texture);
        if(!resultIndex){
            const usize index = result.m_permanentTextureStates.size();
            result.m_permanentTextureStates.push_back(state);
            return resultIndices.insertOrAssign(state.texture, index);
        }

        const PermanentTextureState* const baseState = *resultIndex < baseStateCount ? &base.m_permanentTextureStates[*resultIndex] : nullptr;
        if(baseState && samePermanentTextureState(state, *baseState))
            return true;

        PermanentTextureState& resultState = result.m_permanentTextureStates[*resultIndex];
        if((!baseState || !samePermanentTextureState(resultState, *baseState)) && !samePermanentTextureState(resultState, state))
            return false;

        resultState = state;
        return true;
    };

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        for(const PermanentTextureState& state : branches[branchIndex]->m_permanentTextureStates){
            if(!mergeState(state)){
                result.reset();
                return false;
            }
        }
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

