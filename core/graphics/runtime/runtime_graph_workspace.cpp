// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_standalone_graph.h"

#include <global/exception.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void StandaloneGraphWorkspace::reset(){
    {
        const GpuCompiledGraph::ReadView planAccess(compiledGraph);
        if(planAccess.valid()){
            const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
            if(recordingAttemptGeneration != 0u && !transaction.discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration))
                NWB_FATAL_ASSERT_MSG(false, "standalone graph workspace reuse requires every unaccepted packet to resolve");
            transaction.reset(compiledGraph);
            recordedGraph.reset(compiledGraph);
        }
    }

    graph.reset();
    analysis.reset();
    assignments.reset();
    compiledGraph.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StandaloneGraphWorkspacePool::Lease::Lease(StandaloneGraphWorkspacePool& pool)
    : m_pool(pool)
    , m_workspace(pool.acquire())
    , m_exceptionCount(UncaughtExceptionCount())
{}

StandaloneGraphWorkspacePool::Lease::~Lease()noexcept(false){
    if(!m_workspace)
        return;
    // An unwinding submission destroys its workspace instead of resetting it, so no reset callback runs during the unwind.
    if(UncaughtExceptionCount() > m_exceptionCount){
        m_pool.discard(*m_workspace);
        return;
    }
    m_pool.release(*m_workspace);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 StandaloneGraphWorkspacePool::releaseCommandListCaches()noexcept{
    NothrowScopedLock lock(m_mutex);
    b8 released = true;
    for(WorkspaceOwner& workspace : m_workspaces){
        if(workspace->leased || !workspace->recordedGraph.releaseCommandListCache())
            released = false;
    }
    return released;
}

StandaloneGraphWorkspace* StandaloneGraphWorkspacePool::acquire(){
    ScopedLock lock(m_mutex);
    for(WorkspaceOwner& workspace : m_workspaces){
        if(!workspace->leased){
            workspace->leased = true;
            return workspace.get();
        }
    }

    // Only a new peak of overlapping or reentrant submissions creates another owner set.
    WorkspaceOwner created = MakeGlobalUnique<StandaloneGraphWorkspace>(m_arena, m_arena);
    if(!created)
        return nullptr;
    StandaloneGraphWorkspace& workspace = *created;
    m_workspaces.push_back(Move(created));
    workspace.leased = true;
    return &workspace;
}

void StandaloneGraphWorkspacePool::release(StandaloneGraphWorkspace& workspace){
    // A throwing reset destroys this owner set during the unwind, so a stuck lease never blocks device teardown.
    ScopeExit discardOnUnwind([this, &workspace]()noexcept{ discard(workspace); });

    // Reset outside the lock because payload teardown may run task callbacks.
    workspace.reset();
    discardOnUnwind.release();

    ScopedLock lock(m_mutex);
    workspace.leased = false;
}

void StandaloneGraphWorkspacePool::discard(StandaloneGraphWorkspace& workspace){
    WorkspaceOwner discarded;
    {
        ScopedLock lock(m_mutex);
        for(auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it){
            if(it->get() != &workspace)
                continue;
            discarded = Move(*it);
            m_workspaces.erase(it);
            break;
        }
    }
    NWB_ASSERT(discarded);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

