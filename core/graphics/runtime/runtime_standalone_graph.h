// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/task/gpu/compiler.h>
#include <core/task/gpu/packet_runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One reusable owner set for a synchronous standalone graph submission.
// Returning a lease resets declarations and execution state but keeps container capacity and the recorded graph's command-list cache.
struct StandaloneGraphWorkspace final : NoCopy{
    GpuTaskGraph graph;
    GpuTaskGraphAnalysis analysis;
    GpuTaskGraphQueueAssignments assignments;
    GpuCompiledGraph compiledGraph;
    GpuRecordedGraph recordedGraph;
    GpuGraphSubmissionTransaction transaction;
    bool leased = false;


public:
    explicit StandaloneGraphWorkspace(GraphicsArena& arena)
        : graph(arena)
        , analysis(arena)
        , assignments(arena)
        , compiledGraph(arena)
        , recordedGraph(arena)
        , transaction(arena)
    {}


public:
    void reset();
};


// Overlapping or reentrant submissions each lease their own workspace, so the pool grows only to the peak overlap.
class StandaloneGraphWorkspacePool final : NoCopy{
private:
    using WorkspaceOwner = GlobalUniquePtr<StandaloneGraphWorkspace>;


public:
    class Lease final : NoCopy{
    public:
        explicit Lease(StandaloneGraphWorkspacePool& pool);
        ~Lease()noexcept(false);


    public:
        [[nodiscard]] StandaloneGraphWorkspace* workspace()const noexcept{ return m_workspace; }


    private:
        StandaloneGraphWorkspacePool& m_pool;
        StandaloneGraphWorkspace* m_workspace = nullptr;
        const i32 m_exceptionCount = 0;
    };


public:
    explicit StandaloneGraphWorkspacePool(GraphicsArena& arena)noexcept
        : m_arena(arena)
        , m_workspaces(arena)
    {}


public:
    // Device teardown must drop device-bound command lists first. False means a submission still holds a workspace.
    [[nodiscard]] bool releaseCommandListCaches()noexcept;


private:
    [[nodiscard]] StandaloneGraphWorkspace* acquire();
    void release(StandaloneGraphWorkspace& workspace);
    void discard(StandaloneGraphWorkspace& workspace);


private:
    GraphicsArena& m_arena;
    GraphicsVector<WorkspaceOwner> m_workspaces;
    Futex m_mutex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

