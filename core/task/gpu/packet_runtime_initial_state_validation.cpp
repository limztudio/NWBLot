// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime_initial_state_validation.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_initial_state_handoff_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_LinearStateCountLimit = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename States, typename ResourceGetter, typename RecordLess>
static void BuildRecordIndex(
    const States& states,
    Vector<usize, Alloc::ScratchArena>& index,
    const ResourceGetter resourceGetter,
    const RecordLess recordLess){
    index.clear();
    index.reserve(states.size());
    for(usize stateIndex = 0u; stateIndex < states.size(); ++stateIndex){
        if(resourceGetter(states[stateIndex]))
            index.push_back(stateIndex);
    }
    Sort(index.begin(), index.end(), [&](const usize lhs, const usize rhs){ return recordLess(states[lhs], states[rhs]); });
}

template<typename States, typename ResourceGetter>
[[nodiscard]] static bool DeduplicatePermanentIndex(
    const States& states,
    Vector<usize, Alloc::ScratchArena>& index,
    const ResourceGetter resourceGetter)noexcept{
    usize uniqueCount = 0u;
    for(const usize stateIndex : index){
        const auto& state = states[stateIndex];
        if(state.state == ResourceStates::Unknown)
            return false;
        if(uniqueCount != 0u){
            const auto& previous = states[index[uniqueCount - 1u]];
            if(resourceGetter(previous) == resourceGetter(state)){
                if(previous.state != state.state)
                    return false;
                continue;
            }
        }
        index[uniqueCount] = stateIndex;
        ++uniqueCount;
    }
    index.resize(uniqueCount);
    return true;
}

template<typename States, typename Resource, typename ResourceGetter>
static void FindPermanentState(
    const States& states,
    const Vector<usize, Alloc::ScratchArena>& index,
    Resource* const resource,
    const ResourceGetter resourceGetter,
    ResourceStates::Mask& outState)noexcept{
    usize begin = 0u;
    usize end = index.size();
    while(begin < end){
        const usize middle = begin + (end - begin) / 2u;
        if(LessThan<Resource*>{}(resourceGetter(states[index[middle]]), resource))
            begin = middle + 1u;
        else
            end = middle;
    }
    if(begin < index.size() && resourceGetter(states[index[begin]]) == resource)
        outState = states[index[begin]].state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuInitialStateHandoffValidation::ValidBufferRange(const BufferRange& range, const u64 bufferSize)noexcept{
    return
        range.hasExtent()
        && range.byteOffset < bufferSize
        && (range.byteSize == BufferRange::s_AllBytes || range.byteSize <= bufferSize - range.byteOffset)
    ;
}

bool GpuInitialStateHandoffValidation::validate(){
    using namespace __hidden_gpu_initial_state_handoff_validation;

    m_permanentTexturesIndexed = false;
    m_permanentBuffersIndexed = false;
    const auto textureResource = [](const auto& state){ return state.texture; };
    const auto bufferResource = [](const auto& state){ return state.buffer; };
    if(m_states.m_permanentTextureStates.size() > s_LinearStateCountLimit){
        if(!m_permanentTextureIndex)
            m_permanentTextureIndex.emplace(m_scratchArena);
        BuildRecordIndex(
            m_states.m_permanentTextureStates,
            *m_permanentTextureIndex,
            textureResource,
            [](const auto& lhs, const auto& rhs){ return LessThan<Texture*>{}(lhs.texture, rhs.texture); }
        );
        if(!DeduplicatePermanentIndex(m_states.m_permanentTextureStates, *m_permanentTextureIndex, textureResource))
            return false;
        m_permanentTexturesIndexed = true;
    }
    else{
        for(const auto& state : m_states.m_permanentTextureStates){
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(state.texture && !permanentTextureState(state.texture, permanentState))
                return false;
        }
    }
    if(m_states.m_permanentBufferStates.size() > s_LinearStateCountLimit){
        if(!m_permanentBufferIndex)
            m_permanentBufferIndex.emplace(m_scratchArena);
        BuildRecordIndex(
            m_states.m_permanentBufferStates,
            *m_permanentBufferIndex,
            bufferResource,
            [](const auto& lhs, const auto& rhs){ return LessThan<Buffer*>{}(lhs.buffer, rhs.buffer); }
        );
        if(!DeduplicatePermanentIndex(m_states.m_permanentBufferStates, *m_permanentBufferIndex, bufferResource))
            return false;
        m_permanentBuffersIndexed = true;
    }
    else{
        for(const auto& state : m_states.m_permanentBufferStates){
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(state.buffer && !permanentBufferState(state.buffer, permanentState))
                return false;
        }
    }

    if(m_states.m_textureStates.size() > s_LinearStateCountLimit){
        if(!m_textureIndex)
            m_textureIndex.emplace(m_scratchArena);
        BuildRecordIndex(
            m_states.m_textureStates,
            *m_textureIndex,
            textureResource,
            [](const auto& lhs, const auto& rhs){
                if(lhs.texture != rhs.texture)
                    return LessThan<Texture*>{}(lhs.texture, rhs.texture);
                if(lhs.mipLevel != rhs.mipLevel)
                    return lhs.mipLevel < rhs.mipLevel;
                return lhs.arraySlice < rhs.arraySlice;
            }
        );
        for(usize index = 1u; index < m_textureIndex->size(); ++index){
            const auto& previous = m_states.m_textureStates[(*m_textureIndex)[index - 1u]];
            const auto& state = m_states.m_textureStates[(*m_textureIndex)[index]];
            if(
                previous.texture == state.texture
                && previous.mipLevel == state.mipLevel
                && previous.arraySlice == state.arraySlice
                && previous.state != state.state
            )
                return false;
        }
    }
    for(usize stateIndex = 0u; stateIndex < m_states.m_textureStates.size(); ++stateIndex){
        const auto& state = m_states.m_textureStates[stateIndex];
        if(!state.texture)
            continue;
        if(m_states.m_textureStates.size() <= s_LinearStateCountLimit){
            for(usize otherIndex = 0u; otherIndex < stateIndex; ++otherIndex){
                const auto& other = m_states.m_textureStates[otherIndex];
                if(
                    other.texture == state.texture
                    && other.mipLevel == state.mipLevel
                    && other.arraySlice == state.arraySlice
                    && other.state != state.state
                )
                    return false;
            }
        }
        ResourceStates::Mask permanentState = ResourceStates::Unknown;
        if(!permanentTextureState(state.texture, permanentState))
            return false;
        if(permanentState != ResourceStates::Unknown && permanentState != state.state)
            return false;
    }
    for(const auto& state : m_states.m_bufferStates){
        if(state.buffer && !ValidBufferRange(state.range, state.buffer->getCreationDescription().byteSize))
            return false;
    }
    if(m_states.m_bufferStates.size() > s_LinearStateCountLimit){
        if(!m_bufferIndex)
            m_bufferIndex.emplace(m_scratchArena);
        BuildRecordIndex(
            m_states.m_bufferStates,
            *m_bufferIndex,
            bufferResource,
            [](const auto& lhs, const auto& rhs){
                if(lhs.buffer != rhs.buffer)
                    return LessThan<Buffer*>{}(lhs.buffer, rhs.buffer);
                return lhs.range.byteOffset < rhs.range.byteOffset;
            }
        );
        for(usize index = 1u; index < m_bufferIndex->size(); ++index){
            const auto& previous = m_states.m_bufferStates[(*m_bufferIndex)[index - 1u]];
            const auto& state = m_states.m_bufferStates[(*m_bufferIndex)[index]];
            if(previous.buffer == state.buffer && previous.range.overlaps(state.range))
                return false;
        }
    }
    for(usize stateIndex = 0u; stateIndex < m_states.m_bufferStates.size(); ++stateIndex){
        const auto& state = m_states.m_bufferStates[stateIndex];
        if(!state.buffer)
            continue;
        if(m_states.m_bufferStates.size() <= s_LinearStateCountLimit){
            for(usize otherIndex = 0u; otherIndex < stateIndex; ++otherIndex){
                const auto& other = m_states.m_bufferStates[otherIndex];
                if(other.buffer == state.buffer && other.range.overlaps(state.range))
                    return false;
            }
        }
        ResourceStates::Mask permanentState = ResourceStates::Unknown;
        if(!permanentBufferState(state.buffer, permanentState))
            return false;
        if(permanentState != ResourceStates::Unknown && permanentState != state.state)
            return false;
    }
    return true;
}

bool GpuInitialStateHandoffValidation::permanentTextureState(Texture* const texture, ResourceStates::Mask& outState)const noexcept{
    outState = ResourceStates::Unknown;
    if(m_permanentTexturesIndexed && texture){
        __hidden_gpu_initial_state_handoff_validation::FindPermanentState(
            m_states.m_permanentTextureStates, *m_permanentTextureIndex, texture,
            [](const auto& state){ return state.texture; }, outState
        );
        return true;
    }
    for(const auto& state : m_states.m_permanentTextureStates){
        if(state.texture != texture)
            continue;
        if(state.state == ResourceStates::Unknown)
            return false;
        if(outState != ResourceStates::Unknown && outState != state.state)
            return false;
        outState = state.state;
    }
    return true;
}

bool GpuInitialStateHandoffValidation::permanentBufferState(Buffer* const buffer, ResourceStates::Mask& outState)const noexcept{
    outState = ResourceStates::Unknown;
    if(m_permanentBuffersIndexed && buffer){
        __hidden_gpu_initial_state_handoff_validation::FindPermanentState(
            m_states.m_permanentBufferStates, *m_permanentBufferIndex, buffer,
            [](const auto& state){ return state.buffer; }, outState
        );
        return true;
    }
    for(const auto& state : m_states.m_permanentBufferStates){
        if(state.buffer != buffer)
            continue;
        if(state.state == ResourceStates::Unknown)
            return false;
        if(outState != ResourceStates::Unknown && outState != state.state)
            return false;
        outState = state.state;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

