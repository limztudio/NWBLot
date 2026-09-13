// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_task_use_index.h"
#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TaskResourceUseIndex::TaskResourceUseIndex(
    const usize resourceCount,
    const u64 graphGeneration,
    const usize useCapacity,
    Alloc::ScratchArena& scratchArena)
    : m_firstUses(resourceCount, Limit<usize>::s_Max, scratchArena)
    , m_nextUses(scratchArena)
    , m_touchedResources(scratchArena)
    , m_graphGeneration(graphGeneration)
    , m_useCapacity(useCapacity){
    m_nextUses.reserve(useCapacity);
    m_touchedResources.reserve(resourceCount < useCapacity ? resourceCount : useCapacity);
}

bool TaskResourceUseIndex::build(const GpuTaskGraphTaskView& task){
    m_valid = false;
    if(
        !task.id.valid()
        || task.id.generation != m_graphGeneration
        || task.resourceUseCount > m_useCapacity
        || (task.resourceUseCount != 0u && !task.resourceUses)
    )
        return false;

    // Reset only resources used by the preceding task; large graphs with small tasks do not clear a graph-wide table.
    for(const usize resourceIndex : m_touchedResources)
        m_firstUses[resourceIndex] = Limit<usize>::s_Max;
    m_touchedResources.clear();
    m_nextUses.resize(task.resourceUseCount);
    for(usize remaining = task.resourceUseCount; remaining > 0u; --remaining){
        const usize useIndex = remaining - 1u;
        const GpuGraphResourceId resource = task.resourceUses[useIndex].resource;
        if(!validResource(resource))
            return false;
        usize& firstUse = m_firstUses[resource.index];
        if(firstUse == Limit<usize>::s_Max)
            m_touchedResources.push_back(resource.index);
        m_nextUses[useIndex] = firstUse;
        firstUse = useIndex;
    }
    m_task = task.id;
    m_uses = task.resourceUses;
    m_valid = true;
    return true;
}

bool TaskResourceUseIndex::validFor(const GpuTaskGraphTaskView& task)const noexcept{
    if(!m_valid || task.id != m_task)
        return false;
    return task.resourceUses == m_uses && task.resourceUseCount == m_nextUses.size();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

