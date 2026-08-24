// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 GpuTaskGraph::allocateGeneration()noexcept{
    static Atomic<u64> s_NextGeneration{ 1u };

    u64 generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    if(generation == 0u)
        generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    return generation;
}


GpuTaskGraph::GpuTaskGraph(GraphicsArena& arena)
    : m_arena(arena)
    , m_tasks(arena)
    , m_dependencies(arena)
    , m_externalDependencies(arena)
    , m_externalStateSources(arena)
    , m_externalStateSnapshots(arena)
    , m_resourceUses(arena)
    , m_resources(arena)
    , m_initialOwnerHandoffSources(arena)
    , m_resourceSets(arena)
    , m_resourceSetMembers(arena)
    , m_pipelines(arena)
    , m_externalCompletions(arena)
    , m_uploadBlobs(arena)
    , m_markerText(arena)
    , m_generation(allocateGeneration())
    , m_declarationRevision(allocateGeneration())
    , m_activeRecordingAttemptGeneration(allocateGeneration())
{}

GpuTaskGraph::~GpuTaskGraph(){
    const bool taskPayloadsDestroyed = destroyTaskPayloads();
    if(!taskPayloadsDestroyed){
        NWB_FATAL_ASSERT_MSG(false, "GpuTaskGraph destruction requires in-flight task work to resolve first");
        return;
    }
    destroyTaskStateSnapshots();
    destroyResourceStateSnapshots();
}


GpuTaskId GpuTaskGraph::addTask(const GpuTaskDesc& desc){
    return appendTask(desc, nullptr, nullptr, nullptr, nullptr, nullptr, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

