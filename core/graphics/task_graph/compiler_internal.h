// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


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
    u16 deviceGeneration = 0u;
    u64 planGeneration = 0u;
};

struct TrackedCompiledResourceState{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask state = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    GpuTaskId task;
    GpuPhysicalQueueId queue;
};

struct TrackedTextureStateFragment{
    GpuTaskResourceRange range;
    const TrackedCompiledResourceState* state = nullptr;
    usize stateIndex = Limit<usize>::s_Max;
};

struct PendingCompiledEpilogueBarrier{
    GpuTaskId task;
    GpuCompiledBarrier barrier;
};

struct GpuTaskGraphResourceStatePlan{
    const GpuTaskGraph& graph;
    const GpuTaskGraphQueueTopology& topology;
    const GraphicsVector<GpuTaskId>& topologicalOrder;
    GpuTaskGraphCompiledPlanStorage& compiledPlan;
    Alloc::ScratchArena& scratchArena;
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates;
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena>& pendingEpilogueBarriers;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies;
    Vector<GpuPacketDependency, Alloc::ScratchArena>& terminalFinalizationDependencies;
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& stateFragments;
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
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& queue
)noexcept;

[[nodiscard]] bool ResourceUsesConcurrentQueueSharing(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskGraphQueueTopology& topology
)noexcept;

[[nodiscard]] bool ResourceSharesQueuePairConcurrently(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& sourceQueue,
    const GpuPhysicalQueueInfo& destinationQueue
)noexcept;

[[nodiscard]] bool IsValidQueueTopology(const GpuTaskGraphQueueTopology& topology)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindBestCompatibleQueue(
    const GpuTaskGraphQueueTopology& topology,
    GpuQueueCapability::Mask requiredCapabilities,
    CommandQueue::Enum requiredClass = CommandQueue::kCount
)noexcept;

[[nodiscard]] bool RequiresGraphics(GpuQueueCapability::Mask requiredCapabilities)noexcept;
[[nodiscard]] bool ShouldUseDedicatedCompute(const GpuTaskSchedulingHint& hint)noexcept;
[[nodiscard]] bool ShouldUseDedicatedTransfer(const GpuTaskSchedulingHint& hint)noexcept;
[[nodiscard]] bool IsValidQueueRequest(const GpuQueueRequest& request)noexcept;
[[nodiscard]] bool IsValidSchedulingHint(const GpuTaskSchedulingHint& hint)noexcept;
[[nodiscard]] u64 QueueCostWeight(GpuTaskCostHint::Enum cost)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindPhysicalQueueInfo(
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueId& queue
)noexcept;

[[nodiscard]] bool IsLegalQueueAssignmentCandidate(
    const GpuTaskGraph& graph,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindBestLegalQueueAssignmentCandidate(
    const GpuTaskGraph& graph,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    CommandQueue::Enum requiredClass = CommandQueue::kCount,
    bool dedicatedOnly = false
)noexcept;

class GpuTaskSchedulingReachability final : NoCopy{
    friend bool BuildGpuTaskSchedulingReachability(
        const GpuTaskGraph& graph,
        const GpuTaskGraphAnalysis& analysis,
        GpuTaskSchedulingReachability& outReachability
    );

public:
    explicit GpuTaskSchedulingReachability(Alloc::ScratchArena& scratchArena);
    GpuTaskSchedulingReachability(GpuTaskSchedulingReachability&&) = delete;

    [[nodiscard]] bool reaches(const GpuTaskId& source, const GpuTaskId& destination)const noexcept;
    [[nodiscard]] bool transitivelyIndependent(const GpuTaskId& lhs, const GpuTaskId& rhs)const noexcept;

private:
    Vector<u64, Alloc::ScratchArena> m_words;
    u64 m_graphGeneration = 0u;
    usize m_taskCount = 0u;
    usize m_wordsPerRow = 0u;
    bool m_valid = false;
};

[[nodiscard]] bool BuildGpuTaskSchedulingReachability(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    GpuTaskSchedulingReachability& outReachability
);

[[nodiscard]] bool HasTransitivelyIndependentRequiredGraphicsTask(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskGraphTaskView& task
)noexcept;

[[nodiscard]] const GpuTaskQueueAssignment* FindQueueAssignment(
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskId& task
)noexcept;

[[nodiscard]] GpuQueueAssignmentScore BuildQueueAssignmentScore(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate
)noexcept;

[[nodiscard]] bool IsBetterAnyQueueAssignmentCandidate(
    const GpuQueueAssignmentScore& candidateScore,
    const GpuPhysicalQueueInfo& candidate,
    const GpuQueueAssignmentScore& currentScore,
    const GpuPhysicalQueueInfo* current
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsReadAccess(GpuTaskResourceAccess::Enum access)noexcept;
[[nodiscard]] bool IsWriteAccess(GpuTaskResourceAccess::Enum access)noexcept;
[[nodiscard]] bool IsValidTextureRange(const TextureSubresourceSet& range)noexcept;
[[nodiscard]] bool ResolveTextureRangeForPlanning(
    const Texture* texture,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept;
[[nodiscard]] bool IsValidBufferRange(const BufferRange& range)noexcept;
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
    const GpuTaskGraph& graph,
    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& outEdges,
    GpuTaskGraphAnalysisDiagnostic& outDiagnostic,
    Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool CollectTextureFirstUseRangesWithinTask(
    const GpuTaskGraphTaskView& task,
    usize useIndex,
    const GpuGraphResourceId& resource,
    const Texture* texture,
    const GpuTaskResourceRange& range,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
);

[[nodiscard]] bool CollectLatestTextureStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuGraphResourceId& resource,
    const Vector<GpuTaskResourceRange, Alloc::ScratchArena>& requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
);

[[nodiscard]] bool CollectTerminalTextureStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuGraphResourceId& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
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
    const GpuTaskGraph& graph,
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
    const GpuTaskGraph& graph,
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

