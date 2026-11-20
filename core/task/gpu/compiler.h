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
        Conservative,
        Scored,

        Count,
    };
};

namespace GpuTaskQueueAssignmentModifier{
    static constexpr u8 s_GpuTaskQueueAssignmentModifierNoneBase = 0u;
    enum Mask : u8{
        None = s_GpuTaskQueueAssignmentModifierNoneBase,
        DirectDependencyAffinity = 1u << 0u,
        SameClassLoadBalance = 1u << 1u,
        NonPrimaryRouting = 1u << 2u,
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
    b8 dedicated = false;
    GpuTaskQueueAssignmentModifier::Mask modifiers = GpuTaskQueueAssignmentModifier::None;
};

namespace GpuTaskGraphPacketizationPolicy{
    enum Enum : u8{
        ExplicitMerge,
        FrontierSafe,
        FrontierScored,

        Count,
    };
};

struct GpuTaskQueueLoad{
    GpuPhysicalQueueId queue;
    u64 estimatedCost = 0u;
};

struct GpuTaskGraphQueueAssignmentOptions{
    const GpuTaskTimingHistorySnapshot* timingHistory = nullptr;
    const GpuTaskQueueLoad* queueLoads = nullptr;
    usize queueLoadCount = 0u;
    GpuTaskTimingFeedbackPolicy timingFeedbackPolicy;
    u64 timingFrameIndex = 0u;


public:
    void setQueueLoads(Span<const GpuTaskQueueLoad> values)noexcept{ queueLoads = values.data(); queueLoadCount = values.size(); }
    void clearTimingHistory()noexcept{ timingHistory = nullptr; }
    void setTimingHistory(const GpuTaskTimingHistorySnapshot& snapshot)noexcept{ timingHistory = &snapshot; }
    void setTimingHistory(const GpuTaskTimingHistorySnapshot&&) = delete;
};

struct GpuTaskGraphPacketTimingEnvelopeOptions{
    GpuTaskId firstTask;
    GpuTaskId lastTask;


public:
    [[nodiscard]] b8 enabled()const noexcept{ return firstTask.valid() && lastTask.valid(); }
};

struct GpuTaskGraphCompileOptions{
    GpuTaskGraphQueueAssignmentOptions queueAssignmentOptions;
    GpuTaskGraphPacketTimingEnvelopeOptions packetTimingEnvelope;
    f64 declarationSeconds = 0.0;
    GpuTaskGraphPacketizationPolicy::Enum packetizationPolicy = GpuTaskGraphPacketizationPolicy::ExplicitMerge;
    b8 allowMetadataOnlyTasks = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskGraphSchedulingTaskIndexView{
    const u32* taskIndices = nullptr;
    usize taskCount = 0u;


public:
    [[nodiscard]] u32 operator[](usize index)const noexcept;


public:
    [[nodiscard]] b8 empty()const noexcept{ return taskCount == 0u; }
};


class GpuTaskGraphAnalysis final : NoCopy{
    friend class GpuTaskGraphCompiler;


public:
    explicit GpuTaskGraphAnalysis(GraphicsArena& arena)noexcept
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
    void reset()noexcept;

    [[nodiscard]] b8 valid()const noexcept{ return m_valid; }
    [[nodiscard]] b8 validFor(const GpuTaskGraph::DeclarationReadView& graph)const noexcept;
    [[nodiscard]] const GpuTaskGraphAnalysisDiagnostic& diagnostic()const noexcept{ return m_diagnostic; }
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& edges()const noexcept{ return m_edges; }
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& schedulingEdges()const noexcept{
        return m_schedulingEdges;
    }
    [[nodiscard]] GpuTaskGraphSchedulingTaskIndexView schedulingConsumers(const GpuTaskId& producer)const noexcept;
    [[nodiscard]] GpuTaskGraphSchedulingTaskIndexView schedulingProducers(const GpuTaskId& consumer)const noexcept;
    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& inferredEdges()const noexcept{ return m_inferredEdges; }

    [[nodiscard]] const GraphicsVector<GpuTaskExternalDependencyEdge>& externalDependencies()const noexcept{
        return m_externalDependencies;
    }

    [[nodiscard]] const GraphicsVector<GpuTaskId>& topologicalOrder()const noexcept{ return m_topologicalOrder; }

    [[nodiscard]] const GraphicsVector<GpuTaskId>& cyclePath()const noexcept{ return m_cyclePath; }

    [[nodiscard]] const GraphicsVector<GpuTaskDependencyEdge>& cycleEdges()const noexcept{ return m_cycleEdges; }
    [[nodiscard]] b8 hasExplicitEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept;
    [[nodiscard]] b8 hasInferredEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept;
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
    b8 m_valid = false;
};

class GpuTaskGraphQueueAssignments final : NoCopy{
    friend class GpuTaskGraphCompiler;


public:
    explicit GpuTaskGraphQueueAssignments(GraphicsArena& arena)noexcept
        : m_assignments(arena)
        , m_assignmentIndicesByTask(arena)
    {}


public:
    void reset()noexcept;

    [[nodiscard]] b8 valid()const noexcept{ return m_valid; }
    [[nodiscard]] u64 assignmentRevision()const noexcept{ return m_assignmentRevision; }
    [[nodiscard]] b8 validFor(const GpuTaskGraph::DeclarationReadView& graph)const noexcept;
    [[nodiscard]] b8 validFor(const GpuTaskGraph::DeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledPlan)const noexcept;
    [[nodiscard]] const GpuTaskQueueAssignmentDiagnostic& diagnostic()const noexcept{ return m_diagnostic; }
    [[nodiscard]] Optional<GpuTaskQueueAssignment> findAssignment(GpuTaskId task)const noexcept{
        if(const auto* result = find(task))
            return *result;
        return {};
    }
    [[nodiscard]] const GpuTaskQueueAssignment* find(const GpuTaskId& task)const noexcept;


private:
    GraphicsVector<GpuTaskQueueAssignment> m_assignments;
    GraphicsVector<u32> m_assignmentIndicesByTask;
    GpuTaskQueueAssignmentDiagnostic m_diagnostic;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    u64 m_compiledPlanGeneration = 0u;
    u64 m_assignmentRevision = 0u;
    usize m_taskCount = 0u;
    b8 m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

