// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_buffer_state_fan_in.h"

#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/containers.h>
#include <global/fixed_index_map.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


b8 GpuBufferStateFanIn::Merge(
    CommandListResourceStateHandoff& result,
    const CommandListResourceStateHandoff& base,
    const CommandListResourceStateHandoff* const* branches,
    const usize branchCount,
    const usize bufferStateCapacity,
    Alloc::ScratchArena& scratchArena
){
    using BufferState = CommandListResourceStateHandoff::BufferState;
    using BufferStateIndexMap = FixedIndexMap<Buffer*, Alloc::ScratchArena, Hasher<Buffer*>, EqualTo<Buffer*>>;
    const auto sameBufferState = [](const BufferState& lhs, const BufferState& rhs)noexcept{
        return lhs.state == rhs.state && lhs.queueSharing == rhs.queueSharing && lhs.ownerQueue == rhs.ownerQueue && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue;
    };
    struct BufferRangeEvent{
        NotNull<const BufferState*> state;
        usize bufferIndex = 0u;
        usize branchIndex = 0u;
        u64 offset = 0u;
        b8 beginning = false;
    };

    BufferStateIndexMap bufferIndices(scratchArena, bufferStateCapacity);
    if(!bufferIndices.valid()){
        result.reset();
        return false;
    }
    Vector<BufferRangeEvent, Alloc::ScratchArena> events(scratchArena);
    events.reserve(bufferStateCapacity + bufferStateCapacity);
    Vector<const BufferState*, Alloc::ScratchArena> activeStates(scratchArena);
    activeStates.resize(branchCount + 1u, nullptr);
    usize bufferCount = 0u;
    const auto addEvents = [&](const BufferState& state, const usize branchIndex)noexcept{
        if(!state.buffer || !state.range.hasExtent())
            return false;

        const usize* const existingIndex = bufferIndices.find(state.buffer);
        const usize bufferIndex = existingIndex ? *existingIndex : bufferCount;
        if(!existingIndex){
            if(!bufferIndices.insertOrAssign(state.buffer, bufferIndex))
                return false;
            ++bufferCount;
        }
        events.push_back({ MakeNotNull(&state), bufferIndex, branchIndex, state.range.byteOffset, true });
        events.push_back({ MakeNotNull(&state), bufferIndex, branchIndex, state.range.end(), false });
        return true;
    };
    for(const BufferState& state : base.m_bufferStates){
        if(!addEvents(state, 0u)){
            result.reset();
            return false;
        }
    }
    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        for(const BufferState& state : branches[branchIndex]->m_bufferStates){
            if(!addEvents(state, branchIndex + 1u)){
                result.reset();
                return false;
            }
        }
    }
    Sort(events.begin(), events.end(), [](const BufferRangeEvent& lhs, const BufferRangeEvent& rhs)noexcept{
        if(lhs.bufferIndex != rhs.bufferIndex)
            return lhs.bufferIndex < rhs.bufferIndex;
        if(lhs.offset != rhs.offset)
            return lhs.offset < rhs.offset;
        return lhs.beginning < rhs.beginning;
    });

    // Sweep the union of interval boundaries.
    // A branch retaining the base state contributes no change; independent changes must agree only where their byte ranges overlap.
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
                if(mergedState && mergedState != baseState && !sameBufferState(*mergedState, *state)){
                    result.reset();
                    return false;
                }
                mergedState = state;
            }
            if(mergedState){
                if(mergedState->releaseDestinationQueue.valid() && (mergedState->range.byteOffset != previousOffset || mergedState->range.end() != event.offset)){
                    result.reset();
                    return false;
                }
                if(
                    !mergedState->releaseDestinationQueue.valid()
                    && !result.m_bufferStates.empty()
                    && result.m_bufferStates.back().buffer == mergedState->buffer
                    && result.m_bufferStates.back().range.end() == previousOffset
                    && sameBufferState(result.m_bufferStates.back(), *mergedState)
                )
                    result.m_bufferStates.back().range.byteSize = event.offset - result.m_bufferStates.back().range.byteOffset;
                else{
                    BufferState state = *mergedState;
                    state.range = BufferRange(previousOffset, event.offset - previousOffset);
                    result.m_bufferStates.push_back(state);
                }
            }
            previousOffset = event.offset;
        }
        const BufferState*& activeState = activeStates[event.branchIndex];
        if(event.beginning){
            if(activeState){
                result.reset();
                return false;
            }
            activeState = event.state.get();
        }
        else
            activeState = nullptr;
    }
    return true;
}

b8 GpuBufferStateFanIn::MergePermanent(
    CommandListResourceStateHandoff& result,
    const CommandListResourceStateHandoff& base,
    const CommandListResourceStateHandoff* const* branches,
    const usize branchCount,
    const usize permanentBufferStateCapacity,
    Alloc::ScratchArena& scratchArena
){
    using BufferState = CommandListResourceStateHandoff::BufferState;
    using BufferStateIndexMap = FixedIndexMap<Buffer*, Alloc::ScratchArena, Hasher<Buffer*>, EqualTo<Buffer*>>;
    const auto sameBufferState = [](const BufferState& lhs, const BufferState& rhs)noexcept{
        return lhs.state == rhs.state && lhs.queueSharing == rhs.queueSharing && lhs.ownerQueue == rhs.ownerQueue && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue;
    };
    BufferStateIndexMap resultIndices(scratchArena, permanentBufferStateCapacity);
    if(!resultIndices.valid()){
        result.reset();
        return false;
    }
    for(const BufferState& state : base.m_permanentBufferStates){
        const usize index = result.m_permanentBufferStates.size();
        result.m_permanentBufferStates.push_back(state);
        if(!resultIndices.insertOrAssign(state.buffer, index)){
            result.reset();
            return false;
        }
    }

    const usize baseStateCount = base.m_permanentBufferStates.size();
    const auto mergeState = [&](const BufferState& state)noexcept{
        const usize* const resultIndex = resultIndices.find(state.buffer);
        if(!resultIndex){
            const usize index = result.m_permanentBufferStates.size();
            result.m_permanentBufferStates.push_back(state);
            return resultIndices.insertOrAssign(state.buffer, index);
        }

        const BufferState* const baseState = *resultIndex < baseStateCount ? &base.m_permanentBufferStates[*resultIndex] : nullptr;
        if(baseState && sameBufferState(state, *baseState))
            return true;

        BufferState& resultState = result.m_permanentBufferStates[*resultIndex];
        if((!baseState || !sameBufferState(resultState, *baseState)) && !sameBufferState(resultState, state))
            return false;

        resultState = state;
        return true;
    };

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        for(const BufferState& state : branches[branchIndex]->m_permanentBufferStates){
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

