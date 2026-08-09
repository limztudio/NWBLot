// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_graph.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphAnalysisStatus{
    enum Enum : u8{
        NotAnalyzed,
        Success,
        InvalidTask,
        InvalidResource,
        InvalidTaskDependency,
        InvalidExternalCompletionDependency,
        InvalidResourceUse,
        Cycle,
    };
};

struct GpuTaskGraphAnalysisDiagnostic{
    GpuTaskGraphAnalysisStatus::Enum status = GpuTaskGraphAnalysisStatus::NotAnalyzed;
    GpuTaskId task;
    GpuTaskId relatedTask;
    GpuGraphResourceId resource;
};

namespace GpuTaskQueueAssignmentReason{
    enum Enum : u8{
        Unknown,
        RequiredGraphics,
        PreferredQueue,
        DedicatedCompute,
        Fallback,
        ConservativeAny,

        kCount,
    };
};

namespace GpuTaskGraphQueueAssignmentStatus{
    enum Enum : u8{
        NotAssigned,
        Success,
        InvalidGraphAnalysis,
        InvalidQueueTopology,
        NoCompatibleQueue,
    };
};

struct GpuTaskQueueAssignmentDiagnostic{
    GpuTaskGraphQueueAssignmentStatus::Enum status = GpuTaskGraphQueueAssignmentStatus::NotAssigned;
    GpuTaskId task;
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
};

struct GpuTaskQueueAssignment{
    GpuTaskId task;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuTaskQueueAssignmentReason::Enum reason = GpuTaskQueueAssignmentReason::Unknown;
    bool dedicated = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphAnalysis final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuTaskGraphAnalysis(GraphicsArena& arena)
        : m_edges(arena)
        , m_inferredEdges(arena)
        , m_externalDependencies(arena)
        , m_topologicalOrder(arena)
        , m_cyclePath(arena)
        , m_cycleEdges(arena)
    {}


public:
    void reset();

    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph)const noexcept;
    [[nodiscard]] const GpuTaskGraphAnalysisDiagnostic& diagnostic()const noexcept{ return m_diagnostic; }
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& edges()const noexcept{ return m_edges; }
    // Every resource reason remains available even when it shares one scheduling edge with an explicit dependency.
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& inferredEdges()const noexcept{ return m_inferredEdges; }
    [[nodiscard]] const GraphicsVector<GpuTaskExternalDependencyEdge>& externalDependencies()const noexcept{
        return m_externalDependencies;
    }
    [[nodiscard]] const GraphicsVector<GpuTaskId>& topologicalOrder()const noexcept{ return m_topologicalOrder; }
    [[nodiscard]] const GraphicsVector<GpuTaskId>& cyclePath()const noexcept{ return m_cyclePath; }
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& cycleEdges()const noexcept{ return m_cycleEdges; }
    [[nodiscard]] bool hasExplicitEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept;
    [[nodiscard]] bool hasInferredEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept;
    [[nodiscard]] usize explicitEdgeCount()const noexcept{ return m_explicitEdgeCount; }
    [[nodiscard]] usize inferredEdgeCount()const noexcept{ return m_inferredEdgeCount; }


private:
    GraphicsVector<GpuTaskDependencyEdge> m_edges;
    GraphicsVector<GpuTaskDependencyEdge> m_inferredEdges;
    GraphicsVector<GpuTaskExternalDependencyEdge> m_externalDependencies;
    GraphicsVector<GpuTaskId> m_topologicalOrder;
    GraphicsVector<GpuTaskId> m_cyclePath;
    GraphicsVector<GpuTaskDependencyEdge> m_cycleEdges;
    GpuTaskGraphAnalysisDiagnostic m_diagnostic;
    u64 m_generation = 0u;
    usize m_taskCount = 0u;
    usize m_resourceCount = 0u;
    usize m_externalCompletionCount = 0u;
    usize m_explicitEdgeCount = 0u;
    usize m_inferredEdgeCount = 0u;
    bool m_valid = false;
};

// Queue assignment is a separate immutable compile result. Renderer integrations may use it for native-recording
// selection only after their own legacy-parity checks; graph core never creates a command list or submits work.
class GpuTaskGraphQueueAssignments final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuTaskGraphQueueAssignments(GraphicsArena& arena)
        : m_assignments(arena)
    {}


public:
    void reset();

    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph)const noexcept;
    [[nodiscard]] const GpuTaskQueueAssignmentDiagnostic& diagnostic()const noexcept{ return m_diagnostic; }
    [[nodiscard]] const GraphicsVector<GpuTaskQueueAssignment>& assignments()const noexcept{ return m_assignments; }
    [[nodiscard]] const GpuTaskQueueAssignment* find(const GpuTaskId& task)const noexcept;


private:
    GraphicsVector<GpuTaskQueueAssignment> m_assignments;
    GpuTaskQueueAssignmentDiagnostic m_diagnostic;
    u64 m_generation = 0u;
    usize m_taskCount = 0u;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphCompiler final : NoCopy{
public:
    // Graph validation and hazards remain independent from physical queue policy, so later packet, barrier,
    // recording, and submission stages can consume one validated, immutable analysis result.
    [[nodiscard]] bool analyze(
        const GpuTaskGraph& graph,
        GpuTaskGraphAnalysis& outAnalysis,
        Alloc::ScratchArena& scratchArena
    )const;

    // This produces only a physical-queue decision. It never creates a command list or changes submission; the
    // caller supplies the concrete topology discovered from its current device and owns any parity-gated use.
    [[nodiscard]] bool assignQueues(
        const GpuTaskGraph& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuTaskGraphQueueTopology& topology,
        GpuTaskGraphQueueAssignments& outAssignments
    )const;

    // Phase 3's packet compiler deliberately starts with one task per packet.  It reuses the independently exposed
    // analysis and queue-assignment results so telemetry and live packet creation consume exactly the same immutable
    // decisions.  Packet merging and graph-owned barrier seeds are later compiler phases.
    [[nodiscard]] bool compile(
        const GpuTaskGraph& graph,
        GpuTaskGraphAnalysis& outAnalysis,
        const GpuTaskGraphQueueTopology& topology,
        GpuTaskGraphQueueAssignments& outAssignments,
        GpuCompiledGraph& outCompiledGraph,
        Alloc::ScratchArena& scratchArena
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

