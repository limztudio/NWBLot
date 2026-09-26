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
        OutputPlanInUse,
        InputGraphInUse,
        InvalidTask,
        MissingTaskRecordPayload,
        InvalidResource,
        InvalidTaskDependency,
        InvalidExternalCompletionDependency,
        InvalidResourceUse,
        InvalidResourceVersion,
        InvalidResourceVersionUse,
        MissingResourceVersionProducer,
        DuplicateResourceVersionProducer,
        InvalidAcceptedQueueFrontierTask,
        Cycle,
        InvalidPresentationEndpoint,
    };
};

struct GpuTaskGraphAnalysisDiagnostic{
    GpuTaskId task;
    GpuTaskId relatedTask;
    GpuGraphResourceId resource;
    GpuGraphResourceVersionId resourceVersion;
    GpuTaskGraphAnalysisStatus::Enum status = GpuTaskGraphAnalysisStatus::NotAnalyzed;
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
    static constexpr u8 kGpuTaskQueueAssignmentModifierNoneBase = 0u;
    enum Mask : u8{
        None = kGpuTaskQueueAssignmentModifierNoneBase,
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
        InvalidQueueLoad,
        InvalidTimingFeedback,
        NoCompatibleQueue,
    };
};

struct GpuTaskQueueAssignmentDiagnostic{
    GpuTaskId task;
    GpuTaskGraphQueueAssignmentStatus::Enum status = GpuTaskGraphQueueAssignmentStatus::NotAssigned;
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
};

struct GpuTaskQueueAssignment{
    GpuTaskId task;
    GpuPhysicalQueueId initialQueue;
    GpuPhysicalQueueId queue;
    GpuQueueAssignmentScore score;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuTaskQueueAssignmentReason::Enum reason = GpuTaskQueueAssignmentReason::Unknown;
    bool dedicated = false;
    GpuTaskQueueAssignmentModifier::Mask modifiers = GpuTaskQueueAssignmentModifier::None;
};

// Migration starts with explicitly requested compatible merges.
// Frontier-safe packetization preserves those requests unless a task already in the preceding packet enables a consumer on another physical queue
namespace GpuTaskGraphPacketizationPolicy{
    enum Enum : u8{
        ExplicitMerge,
        FrontierSafe,
        // Opt-in compiler scoring merges a cheap immediate same-queue successor only when the preceding packet has no cross-queue consumer frontier.
        // Existing renderer paths retain ExplicitMerge until each packet boundary has its own acceptance/timing proof.
        FrontierScored,

        kCount,
    };
};

// The timing system owns these immutable observations. Queue assignment only consumes a snapshot,
// graph validation and packet/barrier correctness remain independent from late query completion and history mutation.
struct GpuTaskQueueLoad{
    GpuPhysicalQueueId queue;
    u64 estimatedCost = 0u;
};

struct GpuTaskGraphQueueAssignmentOptions{
    const GpuTaskTimingHistorySnapshot* timingHistory = nullptr;
    // Scheduler-owned pressure sampled immediately before compilation. Costs use the same relative units as task cost hints and affect only movable routes
    const GpuTaskQueueLoad* queueLoads = nullptr;
    usize queueLoadCount = 0u;
    // Scalar policy is copied into one compile request so concurrent runtime policy changes cannot mutate an in-progress queue assignment.
    // The history remains an explicitly immutable snapshot owned by its producer.
    GpuTaskTimingFeedbackPolicy timingFeedbackPolicy;
    const GpuTaskTimingQueueOverride* timingQueueOverrides = nullptr;
    usize timingQueueOverrideCount = 0u;
    u64 timingFrameIndex = 0u;
};

// Optional timing-envelope anchors for one normal-execution packet (both omitted = off; partial = invalid).
// Endpoints in compiler order, resolved before frontier recovery; merged neighbours share scope.
struct GpuTaskGraphPacketTimingEnvelopeOptions{
    GpuTaskId firstTask;
    GpuTaskId lastTask;

    [[nodiscard]] bool enabled()const noexcept{ return firstTask.valid() && lastTask.valid(); }
};

struct GpuTaskGraphCompileOptions{
    GpuTaskGraphQueueAssignmentOptions queueAssignmentOptions;
    GpuTaskGraphPacketTimingEnvelopeOptions packetTimingEnvelope;
    f64 declarationSeconds = 0.0;
    // Caller-owned declare/build wall time (accepted: finite nonneg kept, else zero). Native recording needs per-task payload + thunk; only metadata tooling may opt out.
    GpuTaskGraphPacketizationPolicy::Enum packetizationPolicy = GpuTaskGraphPacketizationPolicy::ExplicitMerge;
    bool allowMetadataOnlyTasks = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskGraphSchedulingTaskIndexView{
    const u32* taskIndices = nullptr;
    usize taskCount = 0u;


    [[nodiscard]] bool empty()const noexcept{ return taskCount == 0u; }
    [[nodiscard]] u32 operator[](const usize index)const noexcept{
        NWB_ASSERT(index < taskCount);
        return taskIndices[index];
    }
};


class GpuTaskGraphAnalysis final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuTaskGraphAnalysis(GraphicsArena& arena)
        : m_edges(arena)
        , m_schedulingEdges(arena)
        , m_schedulingOutgoingOffsets(arena)
        , m_schedulingOutgoingConsumers(arena)
        , m_schedulingIncomingOffsets(arena)
        , m_schedulingIncomingProducers(arena)
        , m_inferredEdges(arena)
        , m_externalDependencies(arena)
        , m_topologicalOrder(arena)
        , m_cyclePath(arena)
        , m_cycleEdges(arena)
    {}


public:
    void reset();

    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] bool validFor(const GpuTaskGraph::DeclarationReadView& graph)const noexcept;
    [[nodiscard]] const GpuTaskGraphAnalysisDiagnostic& diagnostic()const noexcept{ return m_diagnostic; }
    // Raw dependency pairs retain direct declarations and hazard reasons for validation and diagnostics, including edges that are transitively redundant for scheduling.
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& edges()const noexcept{ return m_edges; }
    // Scheduling consumers use the stable transitive reduction so redundant raw relationships do not add queue crossings, signal frontiers, or packet waits.
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& schedulingEdges()const noexcept{
        return m_schedulingEdges;
    }
    [[nodiscard]] GpuTaskGraphSchedulingTaskIndexView schedulingConsumers(const GpuTaskId& producer)const noexcept;
    [[nodiscard]] GpuTaskGraphSchedulingTaskIndexView schedulingProducers(const GpuTaskId& consumer)const noexcept;
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
    [[nodiscard]] usize resourceVersionEdgeCount()const noexcept{ return m_resourceVersionEdgeCount; }


private:
    GraphicsVector<GpuTaskDependencyEdge> m_edges;
    GraphicsVector<GpuTaskDependencyEdge> m_schedulingEdges;
    GraphicsVector<usize> m_schedulingOutgoingOffsets;
    GraphicsVector<u32> m_schedulingOutgoingConsumers;
    GraphicsVector<usize> m_schedulingIncomingOffsets;
    GraphicsVector<u32> m_schedulingIncomingProducers;
    GraphicsVector<GpuTaskDependencyEdge> m_inferredEdges;
    GraphicsVector<GpuTaskExternalDependencyEdge> m_externalDependencies;
    GraphicsVector<GpuTaskId> m_topologicalOrder;
    GraphicsVector<GpuTaskId> m_cyclePath;
    GraphicsVector<GpuTaskDependencyEdge> m_cycleEdges;
    f64 m_validationSeconds = 0.0;
    f64 m_dependencyAnalysisSeconds = 0.0;
    f64 m_hazardAnalysisSeconds = 0.0;
    f64 m_topologicalOrderSeconds = 0.0;
    GpuTaskGraphAnalysisDiagnostic m_diagnostic;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    usize m_taskCount = 0u;
    usize m_resourceCount = 0u;
    usize m_resourceVersionCount = 0u;
    usize m_externalCompletionCount = 0u;
    usize m_explicitEdgeCount = 0u;
    usize m_inferredEdgeCount = 0u;
    usize m_resourceVersionEdgeCount = 0u;
    bool m_valid = false;
};

// Queue assignment is a separate immutable compile result. Renderer integrations may use it for native-recording selection; graph core never creates a command list or submits work.
class GpuTaskGraphQueueAssignments final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuTaskGraphQueueAssignments(GraphicsArena& arena)
        : m_assignments(arena)
        , m_assignmentIndicesByTask(arena)
    {}


public:
    void reset()noexcept;

    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] bool validFor(const GpuTaskGraph::DeclarationReadView& graph)const noexcept;
    [[nodiscard]] bool validFor(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuCompiledGraph::ReadView& compiledPlan
    )const noexcept;
    [[nodiscard]] const GpuTaskQueueAssignmentDiagnostic& diagnostic()const noexcept{ return m_diagnostic; }
    [[nodiscard]] const GpuTaskQueueAssignment* find(const GpuTaskId& task)const noexcept;


private:
    GraphicsVector<GpuTaskQueueAssignment> m_assignments;
    // Dense declaration-task index to topological m_assignments offset.
    GraphicsVector<u32> m_assignmentIndicesByTask;
    GpuTaskQueueAssignmentDiagnostic m_diagnostic;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    u64 m_compiledPlanGeneration = 0u;
    usize m_taskCount = 0u;
    bool m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

