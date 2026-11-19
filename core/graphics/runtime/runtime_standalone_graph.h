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


struct StandaloneGraphWorkspace final : NoCopy{
    GpuTaskGraph graph;
    GpuTaskGraphAnalysis analysis;
    GpuTaskGraphQueueAssignments assignments;
    GpuCompiledGraph compiledGraph;
    GpuRecordedGraph recordedGraph;
    GpuGraphSubmissionTransaction transaction;
    b8 leased = false;


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
    [[nodiscard]] b8 releaseCommandListCaches()noexcept;


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

