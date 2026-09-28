// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiler.h"
#include "compiler_resource_history.h"
#include "compiler_task_use_index.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphCompiler final : NoCopy{
public:
    // Graph validation and hazards remain independent from physical queue policy, so later packet, barrier, recording, and submission stages can consume one validated, immutable analysis result.
    [[nodiscard]] bool analyze(
        const GpuTaskGraph::DeclarationReadView& graph,
        GpuTaskGraphAnalysis& outAnalysis,
        Alloc::ScratchArena& scratchArena
    )const;

    // This produces only a physical-queue decision. It never creates a command list or changes submission; the scheduler supplies the concrete topology discovered from its current device.
    [[nodiscard]] bool assignQueues(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuPhysicalQueueTopology& topology,
        GpuTaskGraphQueueAssignments& outAssignments,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphQueueAssignmentOptions& options = {}
    )const;

    // The packet compiler reuses the independently exposed analysis and queue-assignment results
    // scheduler admission, telemetry, and packet creation consume exactly the same immutable decisions. Tasks retain one packet by default
    [[nodiscard]] bool compile(
        const GpuTaskGraph::DeclarationReadView& graph,
        GpuTaskGraphAnalysis& outAnalysis,
        const GpuPhysicalQueueTopology& topology,
        GpuTaskGraphQueueAssignments& outAssignments,
        GpuCompiledGraph& outCompiledGraph,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphCompileOptions& options = {}
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskGraphCompiledPlanStorage{
    GraphicsVector<GpuCompiledTask>& tasks;
    GraphicsVector<u32>& compiledTaskIndexByTask;
    GraphicsVector<GpuSubmissionPacket>& packets;
    GraphicsVector<GpuTaskId>& packetTasks;
    GraphicsVector<GpuPacketDependency>& packetDependencies;
    GraphicsVector<GpuExternalCompletionId>& packetExternalDependencies;
    GraphicsVector<GpuPacketStateSeed>& prologueStateSeeds;
    GraphicsVector<GpuCompiledBarrier>& prologueBarriers;
    GraphicsVector<GpuCompiledBarrier>& epilogueBarriers;
    GraphicsVector<GpuCompiledOwnershipTransfer>& ownershipTransfers;
    GraphicsVector<GpuCompiledExternalResourceExport>& externalResourceExports;
    GraphicsVector<GpuCompiledExternalResourceExportSource>& externalResourceExportSources;
    const GraphicsVector<GpuPhysicalQueueInfo>& queueTopology;
    GpuCompiledPresentEndpoint& presentEndpoint;
    bool& hasPresentEndpoint;
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u16 deviceGeneration = 0u;
};

struct TrackedResourceStateFragment{
    GpuTaskResourceRange range;
    const TrackedCompiledResourceState* state = nullptr;
    usize stateIndex = Limit<usize>::s_Max;
};

struct PendingCompiledEpilogueBarrier{
    GpuTaskId task;
    GpuCompiledBarrier barrier;
};

struct GpuTaskGraphResourceStatePlan{
    const GpuTaskGraph::DeclarationReadView& graph;
    const GpuPhysicalQueueTopology& topology;
    const GraphicsVector<GpuTaskId>& topologicalOrder;
    GpuTaskGraphCompiledPlanStorage& compiledPlan;
    Alloc::ScratchArena& scratchArena;
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates;
    TrackedResourceStateHistory& resourceHistory;
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena>& pendingEpilogueBarriers;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies;
    Vector<GpuPacketDependency, Alloc::ScratchArena>& terminalFinalizationDependencies;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& stateFragments;
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& taskFirstUseRanges;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u64 AllocateCompiledPlanGeneration()noexcept;

[[nodiscard]] bool HasCapabilities(
    GpuQueueCapability::Mask available,
    GpuQueueCapability::Mask required
)noexcept;

[[nodiscard]] inline bool IsBetterQueue(
    const GpuPhysicalQueueInfo& candidate,
    const GpuPhysicalQueueInfo* const current
)noexcept{
    if(!current)
        return true;
    if(candidate.id.index != current->id.index)
        return candidate.id.index < current->id.index;
    return candidate.queueClass < current->queueClass;
}

[[nodiscard]] bool ResourceSharingAdmitsQueue(
    const GpuTaskGraphResourceView& resource,
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueInfo& queue
)noexcept;

[[nodiscard]] bool ResourceUsesConcurrentQueueSharing(
    const GpuTaskGraphResourceView& resource,
    const GpuPhysicalQueueTopology& topology
)noexcept;

[[nodiscard]] bool ResourceSharesQueuePairConcurrently(
    const GpuTaskGraphResourceView& resource,
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueInfo& sourceQueue,
    const GpuPhysicalQueueInfo& destinationQueue
)noexcept;

[[nodiscard]] bool IsValidQueueTopology(const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindDefaultGraphicsQueue(const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] bool IsValidCommandRequirements(const GpuTaskCommandRequirements& commands)noexcept;
[[nodiscard]] bool IsValidSchedulingHint(const GpuTaskSchedulingHint& hint)noexcept;
[[nodiscard]] u64 QueueCostWeight(GpuTaskCostHint::Enum cost)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindPhysicalQueueInfo(
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueId& queue
)noexcept;

[[nodiscard]] bool IsLegalQueueAssignmentCandidate(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate
)noexcept;

class GpuTaskSchedulingReachability final : NoCopy{
    friend bool BuildGpuTaskSchedulingReachability(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuTaskGraphAnalysis& analysis,
        GpuTaskSchedulingReachability& outReachability
    );

public:
    explicit GpuTaskSchedulingReachability(Alloc::ScratchArena& scratchArena);
    GpuTaskSchedulingReachability(GpuTaskSchedulingReachability&&) = delete;


public:
    [[nodiscard]] bool reaches(const GpuTaskId& source, const GpuTaskId& destination)const noexcept;
    [[nodiscard]] bool transitivelyIndependent(const GpuTaskId& lhs, const GpuTaskId& rhs)const noexcept;
    [[nodiscard]] bool mayContainIndependentTasks()const noexcept{ return !m_totalOrder; }

private:
    Vector<u64, Alloc::ScratchArena> m_words;
    Vector<u32, Alloc::ScratchArena> m_topologicalRanks;
    u64 m_graphGeneration = 0u;
    usize m_taskCount = 0u;
    usize m_wordsPerRow = 0u;
    bool m_totalOrder = false;
    bool m_valid = false;
};

[[nodiscard]] bool BuildGpuTaskSchedulingReachability(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    GpuTaskSchedulingReachability& outReachability
);

[[nodiscard]] const GpuTaskQueueAssignment* FindQueueAssignment(
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskId& task
)noexcept;


struct GpuTaskQueueScoringData{
    Vector<u64, Alloc::ScratchArena> taskCosts;
    Vector<usize, Alloc::ScratchArena> ownershipEdgeOffsets;
    Vector<const GpuTaskDependencyEdge*, Alloc::ScratchArena> ownershipEdges;
    Vector<GpuTaskQueueLoad, Alloc::ScratchArena> assignedQueueLoads;
    u64 totalAssignedCost = 0u;
    const GpuTaskQueueLoad* externalQueueLoads = nullptr;
    usize externalQueueLoadCount = 0u;


    [[nodiscard]] u64 externalQueueLoad(const GpuPhysicalQueueId& queue)const noexcept;
    GpuTaskQueueScoringData(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuTaskGraphQueueAssignmentOptions& options,
        Alloc::ScratchArena& scratchArena
    );

    void rebuildAssignmentLoads(const GraphicsVector<GpuTaskQueueAssignment>& assignments, const GpuPhysicalQueueTopology& topology);
    void updateAssignmentLoads(const GpuTaskId& task, const GpuPhysicalQueueId& previousQueue, const GpuPhysicalQueueId& selectedQueue)noexcept;
    [[nodiscard]] u64 assignedQueueLoad(const GpuPhysicalQueueId& queue)const noexcept;
};

struct GpuTaskQueueScoreExclusions{
    usize assignmentOffset = 0u;
    usize assignmentCount = 0u;
    u64 totalCost = 0u;
    u64 candidateQueueCost = 0u;
};

[[nodiscard]] GpuQueueAssignmentScore BuildQueueAssignmentScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate,
    const GpuTaskQueueScoreExclusions& exclusions = {}
)noexcept;

struct GpuTaskQueuePlacementGroup{
    usize assignmentOffset = 0u;
    usize assignmentCount = 0u;
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
    GpuPhysicalQueueId initialOwnershipQueue;
    GpuPhysicalQueueId diagnosticOverrideQueue;
};

[[nodiscard]] bool BuildQueuePlacementGroups(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphQueueAssignmentOptions& options,
    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena>& outGroups,
    GpuTaskQueueAssignmentDiagnostic& outDiagnostic,
    Alloc::ScratchArena& scratchArena
);

[[nodiscard]] const GpuPhysicalQueueInfo* FindBestLegalQueuePlacementGroupCandidate(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskQueuePlacementGroup& group,
    CommandQueue::Enum requiredClass = CommandQueue::kCount
)noexcept;

[[nodiscard]] GpuQueueAssignmentScore BuildQueuePlacementGroupScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskQueuePlacementGroup& group,
    const GpuPhysicalQueueInfo& candidate
)noexcept;

[[nodiscard]] bool AllowsTimingFeedbackRouting(const GpuTaskGraphTaskView& task)noexcept;

[[nodiscard]] bool IsLegalTimingFeedbackRoute(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuPhysicalQueueInfo& candidate
)noexcept;

[[nodiscard]] GpuTaskTimingKey TimingHistoryKeyForQueue(
    const GpuTaskTimingAssignmentKey& assignmentKey,
    const CommandQueue::Enum queueClass
)noexcept;

[[nodiscard]] bool HasUsableTimingFeedback(
    const GpuTaskGraphQueueAssignmentOptions& options,
    const u16 deviceGeneration
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindLeastLoadedSameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const Vector<u64, Alloc::ScratchArena>& prefixQueueCosts,
    const GpuTaskQueueScoringData& scoringData,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& baseQueue,
    const bool allowCrossFamilyRouting,
    const bool preferNonPrimaryQueue
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindDirectDependencySameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphTaskView& task,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const usize assignedPrefixCount,
    const bool allowCrossFamilyRouting
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackIncumbent(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& staticQueue,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackCalibrationQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept;

[[nodiscard]] bool IsBetterAutomaticQueueAssignmentCandidate(
    const GpuQueueAssignmentScore& candidateScore,
    const GpuPhysicalQueueInfo& candidate,
    const GpuQueueAssignmentScore& currentScore,
    const GpuPhysicalQueueInfo* current,
    bool compareTotalScore
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsReadAccess(GpuTaskResourceAccess::Enum access)noexcept;
[[nodiscard]] bool IsWriteAccess(GpuTaskResourceAccess::Enum access)noexcept;
[[nodiscard]] bool ResolveTextureRangeForPlanning(
    const Texture* texture,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept;
[[nodiscard]] bool ResolveResourceRangeForPlanning(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept;
[[nodiscard]] bool RangesOverlap(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& lhs,
    const GpuTaskResourceRange& rhs
)noexcept;
[[nodiscard]] bool RangeContains(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& outer,
    const GpuTaskResourceRange& inner
)noexcept;

[[nodiscard]] bool BuildResourceVersionDependencyEdges(
    const GpuTaskGraph::DeclarationReadView& graph,
    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& outEdges,
    GpuTaskGraphAnalysisDiagnostic& outDiagnostic,
    Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool CollectResourceFirstUseRangesWithinTask(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const TaskResourceUseIndex& useHistory,
    usize useIndex,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
);

[[nodiscard]] bool CollectLatestResourceStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    const Vector<GpuTaskResourceRange, Alloc::ScratchArena>& requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
);

[[nodiscard]] bool CollectTerminalResourceStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u32 FindCompiledTaskIndex(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept;

[[nodiscard]] const GpuCompiledTask* FindCompiledTask(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept;

[[nodiscard]] GpuCompiledTask* FindCompiledTask(
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept;

[[nodiscard]] GpuSubmissionPacketId FindCompiledPacketForTask(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindCompiledQueueInfo(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuPhysicalQueueId& queue
)noexcept;

[[nodiscard]] bool AppendCompiledOwnershipTransfer(
    GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuTaskId sourceTask,
    GpuTaskId destinationTask,
    GpuPhysicalQueueId sourceQueue,
    GpuPhysicalQueueId destinationQueue,
    GpuOwnershipTransferRoute::Enum route
);

[[nodiscard]] bool BuildSubmissionPackets(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    GpuTaskGraphPacketizationPolicy::Enum policy,
    const GpuTaskGraphPacketTimingEnvelopeOptions& timingEnvelope,
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    GpuSubmissionPacketRange& outTimingEnvelopeRange
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] GpuCompiledBarrierType::Enum TransitionBarrierType(
    GpuGraphResourceType::Enum resourceType
)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum UavBarrierType(
    GpuGraphResourceType::Enum resourceType
)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum StateExportBarrierType(
    GpuGraphResourceType::Enum resourceType
)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipReleaseBarrierType(
    GpuGraphResourceType::Enum resourceType
)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipAcquireBarrierType(
    GpuGraphResourceType::Enum resourceType
)noexcept;

[[nodiscard]] bool PlanTaskResourceStates(GpuTaskGraphResourceStatePlan& plan);
[[nodiscard]] bool PlanExternalResourceExports(GpuTaskGraphResourceStatePlan& plan);
void AppendPendingEpilogueBarriers(GpuTaskGraphResourceStatePlan& plan);

[[nodiscard]] bool PlanPacketDependencies(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies,
    const Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies,
    const Vector<GpuPacketDependency, Alloc::ScratchArena>& terminalFinalizationDependencies,
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

