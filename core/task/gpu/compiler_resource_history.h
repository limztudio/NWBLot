// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TrackedCompiledResourceState{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask state = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    GpuTaskId task;
    GpuPhysicalQueueId queue;
};

// This compilation-local index retains global vector indices, never element pointers. Only append() mutates the
// indexed history; collectors may borrow state pointers until the next append, as before.
class TrackedResourceStateHistory final : NoCopy{
private:
    struct ResourceLinks{
        usize first = Limit<usize>::s_Max;
        usize last = Limit<usize>::s_Max;
    };

    struct StateLinks{
        usize previous = Limit<usize>::s_Max;
        usize next = Limit<usize>::s_Max;
    };


public:
    TrackedResourceStateHistory(
        Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& states,
        usize resourceCount,
        u64 graphGeneration,
        Alloc::ScratchArena& scratchArena);


public:
    [[nodiscard]] bool append(const TrackedCompiledResourceState& state);
    [[nodiscard]] bool validFor(const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& states)const noexcept{
        return &states == &m_states && states.size() == m_links.size();
    }
    [[nodiscard]] bool validResource(const GpuGraphResourceId resource)const noexcept{
        return resource.valid() && resource.generation == m_graphGeneration && resource.index < m_resources.size();
    }
    [[nodiscard]] usize first(const GpuGraphResourceId resource)const noexcept{
        return validResource(resource) ? m_resources[resource.index].first : Limit<usize>::s_Max;
    }
    [[nodiscard]] usize last(const GpuGraphResourceId resource)const noexcept{
        return validResource(resource) ? m_resources[resource.index].last : Limit<usize>::s_Max;
    }
    [[nodiscard]] usize previous(const usize stateIndex)const noexcept{
        return stateIndex < m_links.size() ? m_links[stateIndex].previous : Limit<usize>::s_Max;
    }
    [[nodiscard]] usize next(const usize stateIndex)const noexcept{
        return stateIndex < m_links.size() ? m_links[stateIndex].next : Limit<usize>::s_Max;
    }


private:
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& m_states;
    Vector<ResourceLinks, Alloc::ScratchArena> m_resources;
    Vector<StateLinks, Alloc::ScratchArena> m_links;
    u64 m_graphGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

