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
    // Validation and hazards stay independent of queue policy, so later stages share one analysis result.
    [[nodiscard]] b8 analyze(const GpuTaskGraph::DeclarationReadView& graph, Alloc::ScratchArena& scratchArena, GpuTaskGraphAnalysis& outAnalysis)const;

    // Queue decision only. No command lists, no submission; the scheduler supplies the device topology.
    [[nodiscard]] b8 assignQueues(
        GpuTaskGraphQueueAssignments& outAssignments,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuPhysicalQueueTopology& topology,
        const GpuTaskGraphQueueAssignmentOptions& options = {}
    )const;

    // The packet compiler reuses the analysis and queue-assignment results shared by scheduler admission, telemetry, and packet creation.
    // Tasks retain one packet by default.
    [[nodiscard]] b8 compile(
        GpuTaskGraphAnalysis& outAnalysis,
        GpuTaskGraphQueueAssignments& outAssignments,
        GpuCompiledGraph& outCompiledGraph,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuPhysicalQueueTopology& topology,
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
    Span<const GpuPhysicalQueueInfo> queueTopology;
    GpuCompiledPresentEndpoint& presentEndpoint;
    b8& hasPresentEndpoint;
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
    Span<const GpuTaskId> topologicalOrder;
    GpuTaskGraphCompiledPlanStorage& compiledPlan;
    Alloc::ScratchArena& scratchArena;
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates;
    TrackedResourceStateHistory& resourceHistory;
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena>& pendingEpilogueBarriers;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies;
    Vector<GpuPacketDependency, Alloc::ScratchArena>& resourceStateDependencies;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& stateFragments;
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& taskFirstUseRanges;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u64 AllocateCompiledPlanGeneration()noexcept;

[[nodiscard]] b8 HasCapabilities(GpuQueueCapability::Mask available, GpuQueueCapability::Mask required)noexcept;

[[nodiscard]] inline b8 TaskAllowsPacketMerge(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowPacketMerge && !task.scheduling.forceSubmissionBoundary && !task.scheduling.joinsAcceptedQueueFrontier;
}

[[nodiscard]] inline b8 IsBetterQueue(const GpuPhysicalQueueInfo& candidate, const GpuPhysicalQueueInfo* const current)noexcept{
    if(!current)
        return true;
    if(candidate.id.index != current->id.index)
        return candidate.id.index < current->id.index;
    return candidate.queueClass < current->queueClass;
}

[[nodiscard]] b8 ResourceSharingAdmitsQueue(const GpuTaskGraphResourceView& resource, const GpuPhysicalQueueTopology& topology, const GpuPhysicalQueueInfo& queue)noexcept;

[[nodiscard]] b8 ResourceUsesConcurrentQueueSharing(const GpuTaskGraphResourceView& resource, const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] b8 ResourceSharesQueuePairConcurrently(const GpuTaskGraphResourceView& resource, const GpuPhysicalQueueTopology& topology, const GpuPhysicalQueueInfo& sourceQueue, const GpuPhysicalQueueInfo& destinationQueue)noexcept;

[[nodiscard]] b8 IsValidQueueTopology(const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindDefaultGraphicsQueue(const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] b8 IsValidCommandRequirements(const GpuTaskCommandRequirements& commands)noexcept;
[[nodiscard]] b8 IsValidSchedulingHint(const GpuTaskSchedulingHint& hint)noexcept;
[[nodiscard]] u64 QueueCostWeight(GpuTaskCostHint::Enum cost)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindPhysicalQueueInfo(const GpuPhysicalQueueTopology& topology, const GpuPhysicalQueueId& queueID)noexcept;

[[nodiscard]] b8 IsLegalQueueAssignmentCandidate(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& candidate)noexcept;

class GpuTaskSchedulingReachability final : NoCopy{
    friend struct GpuTaskQueueScoringData;
    friend b8 BuildGpuTaskSchedulingReachability(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphAnalysis& analysis, GpuTaskSchedulingReachability& outReachability);


private:
    struct WordRange{
        usize m_begin = 0u;
        usize m_end = 0u;
    };


public:
    explicit GpuTaskSchedulingReachability(Alloc::ScratchArena& scratchArena)noexcept;
    GpuTaskSchedulingReachability(GpuTaskSchedulingReachability&&) = delete;


public:
    [[nodiscard]] b8 reaches(const GpuTaskId& source, const GpuTaskId& destination)const noexcept;
    [[nodiscard]] b8 transitivelyIndependent(const GpuTaskId& lhs, const GpuTaskId& rhs)const noexcept;
    [[nodiscard]] b8 mayContainIndependentTasks()const noexcept{ return !m_totalOrder; }


private:
    // Symmetric ancestor/descendant/self masks; topological ranks retain directed reachability.
    Vector<u64, Alloc::ScratchArena> m_words;
    Vector<u32, Alloc::ScratchArena> m_topologicalRanks;
    Vector<WordRange, Alloc::ScratchArena> m_relatedWordRanges;
    // Empty for dense rows; compact rows retain global bit positions within their exact word spans.
    Vector<usize, Alloc::ScratchArena> m_rowOffsets;
    u64 m_graphGeneration = 0u;
    usize m_taskCount = 0u;
    usize m_wordsPerRow = 0u;
    b8 m_totalOrder = false;
    b8 m_valid = false;
};

[[nodiscard]] b8 BuildGpuTaskSchedulingReachability(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphAnalysis& analysis, GpuTaskSchedulingReachability& outReachability);

[[nodiscard]] const GpuTaskQueueAssignment* FindQueueAssignment(Span<const GpuTaskQueueAssignment> assignments, Span<const u32> assignmentIndicesByTask, const GpuTaskId& task)noexcept;


struct GpuTaskQueueScoringData{
    Vector<u64, Alloc::ScratchArena> m_taskCosts;
    Vector<u8, Alloc::ScratchArena> m_taskCostGroups;
    Array<u64, GpuTaskCostHint::Count> m_costGroupWeights;
    Vector<usize, Alloc::ScratchArena> m_ownershipEdgeOffsets;
    Vector<NotNull<const GpuTaskDependencyEdge*>, Alloc::ScratchArena> m_ownershipEdges;
    Vector<GpuTaskQueueLoad, Alloc::ScratchArena> m_assignedQueueLoads;
    // Cost-group bitsets: all assigned tasks first, then one block per topology queue, never sparse queue IDs.
    Vector<u64, Alloc::ScratchArena> m_assignedCostWords;
    usize m_costWordsPerGroup = 0u;
    u64 m_totalAssignedCost = 0u;
    const GpuTaskQueueLoad* m_externalQueueLoads = nullptr;
    usize m_externalQueueLoadCount = 0u;


public:
    GpuTaskQueueScoringData(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphAnalysis& analysis, const GpuTaskSchedulingReachability& reachability, const GpuTaskGraphQueueAssignmentOptions& options, Alloc::ScratchArena& scratchArena);


public:
    [[nodiscard]] u64 externalQueueLoad(const GpuPhysicalQueueId& queue)const noexcept;
    void rebuildAssignmentLoads(Span<const GpuTaskQueueAssignment> assignments, const GpuPhysicalQueueTopology& topology);
    void updateAssignmentLoads(const GpuTaskId& task, const GpuPhysicalQueueId& previousQueue, const GpuPhysicalQueueId& selectedQueue)noexcept;
    [[nodiscard]] u64 assignedQueueLoad(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] u64 independentQueueCost(const GpuTaskSchedulingReachability& reachability, const GpuPhysicalQueueId& queue, NotNull<const GpuTaskQueueAssignment*> members, usize memberCount)const noexcept;
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
    Span<const GpuTaskQueueAssignment> assignments,
    Span<const u32> assignmentIndicesByTask,
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
};

[[nodiscard]] Expected<void, GpuTaskQueueAssignmentDiagnostic> BuildQueuePlacementGroups(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena>& outGroups
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
    Span<const GpuTaskQueueAssignment> assignments,
    Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskQueuePlacementGroup& group,
    const GpuPhysicalQueueInfo& candidate
)noexcept;

[[nodiscard]] b8 AllowsTimingFeedbackRouting(const GpuTaskGraphTaskView& task)noexcept;

[[nodiscard]] b8 IsLegalTimingFeedbackRoute(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& incumbent, const GpuPhysicalQueueInfo& candidate)noexcept;

[[nodiscard]] GpuTaskTimingKey TimingHistoryKeyForQueue(const GpuTaskTimingAssignmentKey& assignmentKey, const CommandQueue::Enum queueClass)noexcept;

[[nodiscard]] b8 HasUsableTimingFeedback(const GpuTaskGraphQueueAssignmentOptions& options, const u16 deviceGeneration)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindLeastLoadedSameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    Span<const u64> prefixQueueCosts,
    const GpuTaskQueueScoringData& scoringData,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& baseQueue,
    const b8 allowCrossFamilyRouting,
    const b8 preferNonPrimaryQueue
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindDirectDependencySameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphTaskView& task,
    Span<const GpuTaskQueueAssignment> assignments,
    Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const usize assignedPrefixCount,
    const b8 allowCrossFamilyRouting
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo& FindTimingFeedbackIncumbent(
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
    Span<const GpuTaskQueueAssignment> assignments,
    Span<const u32> assignmentIndicesByTask,
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

[[nodiscard]] b8 IsBetterAutomaticQueueAssignmentCandidate(
    const GpuQueueAssignmentScore& candidateScore,
    const GpuPhysicalQueueInfo& candidate,
    const GpuQueueAssignmentScore& currentScore,
    const GpuPhysicalQueueInfo* current,
    b8 compareTotalScore
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsReadAccess(GpuTaskResourceAccess::Enum access)noexcept;
[[nodiscard]] b8 IsWriteAccess(GpuTaskResourceAccess::Enum access)noexcept;
[[nodiscard]] Expected<GpuTaskResourceRange> ResolveTextureRangeForPlanning(const Texture* texture, const GpuTaskResourceRange& range)noexcept;
[[nodiscard]] Expected<GpuTaskResourceRange> ResolveResourceRangeForPlanning(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& range)noexcept;
[[nodiscard]] b8 RangesOverlap(const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& lhs, const GpuTaskResourceRange& rhs)noexcept;
[[nodiscard]] b8 RangeContains(const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& outer, const GpuTaskResourceRange& inner)noexcept;

[[nodiscard]] Expected<void, GpuTaskGraphAnalysisDiagnostic> BuildResourceVersionDependencyEdges(const GpuTaskGraph::DeclarationReadView& graph, Alloc::ScratchArena& scratchArena, Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& outEdges);

[[nodiscard]] b8 CollectResourceFirstUseRangesWithinTask(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const TaskResourceUseIndex& useHistory,
    usize useIndex,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
);

[[nodiscard]] b8 CollectLatestResourceStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Span<const GpuTaskResourceRange> requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
);

[[nodiscard]] b8 CollectTerminalResourceStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u32 FindCompiledTaskIndex(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept;

[[nodiscard]] const GpuCompiledTask* FindCompiledTask(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept;

[[nodiscard]] GpuCompiledTask* FindCompiledTask(GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept;

[[nodiscard]] GpuSubmissionPacketId FindCompiledPacketForTask(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindCompiledQueueInfo(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuPhysicalQueueId& queue)noexcept;

[[nodiscard]] b8 AppendCompiledOwnershipTransfer(
    GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuTaskId sourceTask,
    GpuTaskId destinationTask,
    GpuPhysicalQueueId sourceQueue,
    GpuPhysicalQueueId destinationQueue,
    GpuOwnershipTransferRoute::Enum route
);

[[nodiscard]] Expected<GpuSubmissionPacketRange> BuildSubmissionPackets(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    GpuTaskGraphPacketizationPolicy::Enum policy,
    const GpuTaskGraphPacketTimingEnvelopeOptions& timingEnvelope,
    GpuTaskGraphCompiledPlanStorage& compiledPlan
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] GpuCompiledBarrierType::Enum TransitionBarrierType(GpuGraphResourceType::Enum resourceType)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum UavBarrierType(GpuGraphResourceType::Enum resourceType)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum StateExportBarrierType(GpuGraphResourceType::Enum resourceType)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipReleaseBarrierType(GpuGraphResourceType::Enum resourceType)noexcept;

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipAcquireBarrierType(GpuGraphResourceType::Enum resourceType)noexcept;

[[nodiscard]] b8 PlanTaskResourceStates(GpuTaskGraphResourceStatePlan& plan);
[[nodiscard]] b8 PlanExternalResourceExports(GpuTaskGraphResourceStatePlan& plan);
[[nodiscard]] b8 AppendPendingEpilogueBarriers(GpuTaskGraphResourceStatePlan& plan);

[[nodiscard]] b8 PlanPacketDependencies(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    Span<const GpuTaskExternalDependencyEdge> initialOwnershipDependencies,
    Span<const GpuTaskExternalDependencyEdge> initialAvailabilityDependencies,
    Alloc::ScratchArena& scratchArena,
    Span<GpuPacketDependency> resourceStateDependencies,
    GpuTaskGraphCompiledPlanStorage& compiledPlan
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

