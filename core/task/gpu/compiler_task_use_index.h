// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskGraphTaskView;


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The current immutable task's complete declaration order is indexed, including uses with no requested state.
class TaskResourceUseIndex final : NoCopy{
public:
    TaskResourceUseIndex(usize resourceCount, u64 graphGeneration, usize useCapacity, Alloc::ScratchArena& scratchArena);


public:
    [[nodiscard]] bool build(const GpuTaskGraphTaskView& task);
    [[nodiscard]] bool validFor(const GpuTaskGraphTaskView& task)const noexcept;
    [[nodiscard]] bool validResource(const GpuGraphResourceId resource)const noexcept{
        return resource.valid() && resource.generation == m_graphGeneration && resource.index < m_firstUses.size();
    }
    [[nodiscard]] usize first(const GpuGraphResourceId resource)const noexcept{
        return m_valid && validResource(resource) ? m_firstUses[resource.index] : Limit<usize>::s_Max;
    }
    [[nodiscard]] usize next(const usize useIndex)const noexcept{
        return m_valid && useIndex < m_nextUses.size() ? m_nextUses[useIndex] : Limit<usize>::s_Max;
    }


private:
    Vector<usize, Alloc::ScratchArena> m_firstUses;
    Vector<usize, Alloc::ScratchArena> m_nextUses;
    Vector<usize, Alloc::ScratchArena> m_touchedResources;
    GpuTaskId m_task;
    const GpuTaskResourceUse* m_uses = nullptr;
    u64 m_graphGeneration = 0u;
    usize m_useCapacity = 0u;
    bool m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

