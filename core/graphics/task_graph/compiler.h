// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_graph.h"
#include "timing_feedback.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphAnalysisStatus{
    enum Enum : u8{
        NotAnalyzed,
        Success,
        InvalidTask,
        MissingTaskRecordPayload,
        InvalidResource,
        InvalidTaskDependency,
        InvalidExternalCompletionDependency,
        InvalidResourceUse,
        InvalidAcceptedQueueFrontierTask,
        Cycle,
        InvalidPresentationEndpoint,
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
        DedicatedTransfer,
        Fallback,
        ConservativeAny,
        SameClassRouting,
        CompilerOverride,
        ScoredAny,

        kCount,
    };
};

namespace GpuTaskQueueAssignmentModifier{
    enum Mask : u8{
        None = 0u,
        DirectDependencyAffinity = 1u << 0u,
        SameClassLoadBalance = 1u << 1u,
        NonPrimaryPreference = 1u << 2u,
        DebugTimingOverride = 1u << 3u,
        TimingCalibration = 1u << 4u,
        TimingFeedback = 1u << 5u,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

namespace GpuTaskGraphQueueAssignmentStatus{
    enum Enum : u8{
        NotAssigned,
        Success,
        InvalidGraphAnalysis,
        InvalidQueueTopology,
        InvalidTimingFeedback,
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
    GpuPhysicalQueueId initialQueue;
    GpuPhysicalQueueId queue;
    GpuQueueAssignmentScore score;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuTaskQueueAssignmentReason::Enum reason = GpuTaskQueueAssignmentReason::Unknown;
    GpuTaskQueueAssignmentModifier::Mask modifiers = GpuTaskQueueAssignmentModifier::None;
    bool dedicated = false;
};

// Migration starts with explicitly requested compatible merges. Frontier-safe packetization preserves those requests
// unless a task already in the preceding packet enables a consumer on another physical queue; that producer needs
// its own signal point so the consumer does not wait for unrelated later same-queue work.
namespace GpuTaskGraphPacketizationPolicy{
    enum Enum : u8{
        ExplicitMerge,
        FrontierSafe,
        // Opt-in compiler scoring merges a cheap immediate same-queue successor only when the preceding packet has
        // no cross-queue consumer frontier. Existing renderer paths retain ExplicitMerge until each packet boundary
        // has its own acceptance/timing proof.
        FrontierScored,

        kCount,
    };
};

// The timing system owns these immutable observations. Queue assignment only consumes a snapshot, so graph
// validation and packet/barrier correctness remain independent from late query completion and history mutation.
struct GpuTaskGraphQueueAssignmentOptions{
    const GpuTaskTimingHistorySnapshot* timingHistory = nullptr;
    const GpuTaskTimingFeedbackPolicy* timingFeedbackPolicy = nullptr;
    const GpuTaskTimingQueueOverride* timingQueueOverrides = nullptr;
    usize timingQueueOverrideCount = 0u;
    u64 timingFrameIndex = 0u;
};

struct GpuTaskGraphCompileOptions{
    GpuTaskGraphPacketizationPolicy::Enum packetizationPolicy = GpuTaskGraphPacketizationPolicy::ExplicitMerge;
    GpuTaskGraphQueueAssignmentOptions queueAssignmentOptions;
    // Native packet recording requires every task to retain a payload and record thunk. Tooling-only callers that
    // compile metadata graphs may opt out explicitly; executable graph paths must retain the default.
    bool allowMetadataOnlyTasks = false;
    // Caller-owned wall time spent declaring/building the graph before this compiler begins. Accepted plans retain
    // finite nonnegative values separately from the compiler-only total duration; other values normalize to zero.
    f64 declarationSeconds = 0.0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphAnalysis final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuTaskGraphAnalysis(GraphicsArena& arena)
        : m_edges(arena)
        , m_schedulingEdges(arena)
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
    // Raw dependency pairs retain direct declarations and hazard reasons for validation and diagnostics, including
    // edges that are transitively redundant for scheduling.
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& edges()const noexcept{ return m_edges; }
    // Scheduling consumers use the stable transitive reduction so redundant raw relationships do not add queue
    // crossings, signal frontiers, or packet waits.
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& schedulingEdges()const noexcept{
        return m_schedulingEdges;
    }
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
    GraphicsVector<GpuTaskDependencyEdge> m_schedulingEdges;
    GraphicsVector<GpuTaskDependencyEdge> m_inferredEdges;
    GraphicsVector<GpuTaskExternalDependencyEdge> m_externalDependencies;
    GraphicsVector<GpuTaskId> m_topologicalOrder;
    GraphicsVector<GpuTaskId> m_cyclePath;
    GraphicsVector<GpuTaskDependencyEdge> m_cycleEdges;
    GpuTaskGraphAnalysisDiagnostic m_diagnostic;
    f64 m_validationSeconds = 0.0;
    f64 m_dependencyAnalysisSeconds = 0.0;
    f64 m_hazardAnalysisSeconds = 0.0;
    f64 m_topologicalOrderSeconds = 0.0;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    usize m_taskCount = 0u;
    usize m_resourceCount = 0u;
    usize m_externalCompletionCount = 0u;
    usize m_explicitEdgeCount = 0u;
    usize m_inferredEdgeCount = 0u;
    bool m_valid = false;
};

// Queue assignment is a separate immutable compile result. Renderer integrations may use it for native-recording
// selection; graph core never creates a command list or submits work.
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
    [[nodiscard]] const GpuTaskQueueAssignment* find(const GpuTaskId& task)const noexcept;


private:
    GraphicsVector<GpuTaskQueueAssignment> m_assignments;
    GpuTaskQueueAssignmentDiagnostic m_diagnostic;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    usize m_taskCount = 0u;
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
    // caller supplies the concrete topology discovered from its current device.
    [[nodiscard]] bool assignQueues(
        const GpuTaskGraph& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuTaskGraphQueueTopology& topology,
        GpuTaskGraphQueueAssignments& outAssignments,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphQueueAssignmentOptions& options = {}
    )const;

    // The packet compiler reuses the independently exposed analysis and queue-assignment results so telemetry and
    // live packet creation consume exactly the same immutable decisions.  Tasks retain one packet by default;
    // explicitly requested compatible successors may merge into the preceding packet.
    [[nodiscard]] bool compile(
        const GpuTaskGraph& graph,
        GpuTaskGraphAnalysis& outAnalysis,
        const GpuTaskGraphQueueTopology& topology,
        GpuTaskGraphQueueAssignments& outAssignments,
        GpuCompiledGraph& outCompiledGraph,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphCompileOptions& options = {}
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

