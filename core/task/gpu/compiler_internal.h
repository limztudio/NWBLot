// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiler.h"
#include "compiler_resource_history.h"
#include "compiler_task_use_index.h"

#include <global/integer_rect.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{
    struct DenseDescendantBitMatrix;
};


class GpuTaskGraphCompiler final : NoCopy{
public:
    [[nodiscard]] b8 analyze(const GpuTaskGraph::DeclarationReadView& graph, Alloc::ScratchArena& scratchArena, GpuTaskGraphAnalysis& outAnalysis)const;

    [[nodiscard]] b8 assignQueues(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuPhysicalQueueTopology& topology,
        const GpuTaskGraphQueueAssignmentOptions& options,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraphQueueAssignments& outAssignments
    )const;

    [[nodiscard]] b8 compile(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuPhysicalQueueTopology& topology,
        const GpuTaskGraphCompileOptions& options,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraphAnalysis& outAnalysis,
        GpuTaskGraphQueueAssignments& outAssignments,
        GpuCompiledGraph& outCompiledGraph
    )const;


private:
    [[nodiscard]] b8 analyzeInternal(
        const GpuTaskGraph::DeclarationReadView& graph,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraphAnalysis& outAnalysis,
        GpuTaskGraphCompilerDetail::DenseDescendantBitMatrix* outDenseDesc
    )const;

    [[nodiscard]] b8 assignQueuesInternal(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuPhysicalQueueTopology& topology,
        const GpuTaskGraphQueueAssignmentOptions& options,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraphQueueAssignments& outAssignments,
        const GpuTaskGraphCompilerDetail::DenseDescendantBitMatrix* denseDesc,
        b8 hasNonHazardDomainResource
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_BitsPerWord = sizeof(u64) * s_BitsPerByte;


struct CompilerGraphAccess{
    [[nodiscard]] static inline const GpuTaskGraph* Graph(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph;
    }

    [[nodiscard]] static inline u64 Generation(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_generation : 0u;
    }

    [[nodiscard]] static inline u64 DeclarationRevision(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_declarationRevision : 0u;
    }

    [[nodiscard]] static inline usize TaskCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_tasks.size() : 0u;
    }

    [[nodiscard]] static inline usize ResourceCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_resources.size() : 0u;
    }

    [[nodiscard]] static inline usize ResourceVersionCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_resourceVersions.size() : 0u;
    }

    [[nodiscard]] static inline usize ResourceSetCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_resourceSets.size() : 0u;
    }

    [[nodiscard]] static inline usize UploadBlobCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_uploadBlobs.size() : 0u;
    }

    [[nodiscard]] static inline usize ExternalCompletionCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_externalCompletions.size() : 0u;
    }

    [[nodiscard]] static inline const GpuTaskGraphDetail::GpuTaskNode& TaskNode(const GpuTaskGraph::DeclarationReadView& view, const usize index)noexcept{
        FOUNDATION_ASSERT(view.m_graph && index < view.m_graph->m_tasks.size());
        return view.m_graph->m_tasks[index];
    }

    [[nodiscard]] static inline const GpuTaskGraphDetail::GpuGraphResourceNode& ResourceNode(const GpuTaskGraph::DeclarationReadView& view, const usize index)noexcept{
        FOUNDATION_ASSERT(view.m_graph && index < view.m_graph->m_resources.size());
        return view.m_graph->m_resources[index];
    }

    [[nodiscard]] static inline GpuTaskId TaskIdAt(const GpuTaskGraph::DeclarationReadView& view, const usize index)noexcept{
        return GpuTaskId{ .generation = Generation(view), .index = static_cast<u32>(index) };
    }

    [[nodiscard]] static inline Span<const GpuTaskId> TaskDependencies(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskGraphDetail::GpuTaskNode& node)noexcept{
        return node.dependencyCount > 0u ? Span<const GpuTaskId>(view.m_graph->m_dependencies.data() + node.dependencyOffset, node.dependencyCount) : Span<const GpuTaskId>();
    }

    [[nodiscard]] static inline Span<const GpuExternalCompletionId> TaskExternalDependencies(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskGraphDetail::GpuTaskNode& node)noexcept{
        return node.externalDependencyCount > 0u ? Span<const GpuExternalCompletionId>(view.m_graph->m_externalDependencies.data() + node.externalDependencyOffset, node.externalDependencyCount) : Span<const GpuExternalCompletionId>();
    }

    [[nodiscard]] static inline Span<const GpuTaskExternalStateSource> TaskExternalStateSources(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskGraphDetail::GpuTaskNode& node)noexcept{
        return node.externalStateSourceCount > 0u ? Span<const GpuTaskExternalStateSource>(view.m_graph->m_externalStateSources.data() + node.externalStateSourceOffset, node.externalStateSourceCount) : Span<const GpuTaskExternalStateSource>();
    }

    [[nodiscard]] static inline Span<const GpuTaskResourceUse> TaskResourceUses(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskGraphDetail::GpuTaskNode& node)noexcept{
        return node.resourceUseCount > 0u ? Span<const GpuTaskResourceUse>(view.m_graph->m_resourceUses.data() + node.resourceUseOffset, node.resourceUseCount) : Span<const GpuTaskResourceUse>();
    }

    [[nodiscard]] static inline Span<const GpuTaskResourceVersionUse> TaskResourceVersionUses(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskGraphDetail::GpuTaskNode& node)noexcept{
        return node.resourceVersionUseCount > 0u ? Span<const GpuTaskResourceVersionUse>(view.m_graph->m_resourceVersionUses.data() + node.resourceVersionUseOffset, node.resourceVersionUseCount) : Span<const GpuTaskResourceVersionUse>();
    }

    [[nodiscard]] static inline usize TotalDependencyCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_dependencies.size() : 0u;
    }

    [[nodiscard]] static inline usize TotalResourceUseCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_resourceUses.size() : 0u;
    }

    [[nodiscard]] static inline usize TotalResourceVersionUseCount(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph ? view.m_graph->m_resourceVersionUses.size() : 0u;
    }

    [[nodiscard]] static inline b8 HasAcceptedQueueFrontierOrRecoveryTask(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph && view.m_graph->m_hasAcceptedQueueFrontierOrRecoveryTask;
    }

    [[nodiscard]] static inline ResourceQueueAdmissionSnapshot ResourceQueueAdmission(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskGraphDetail::GpuGraphResourceNode& node)noexcept{
        return ResourceQueueAdmissionSnapshot{
            .queueFamilyIndices = node.queueFamilyIndexCount != 0u ? view.m_graph->m_queueFamilyIndices.data() + node.queueFamilyIndexOffset : nullptr,
            .queueFamilyIndexCount = node.queueFamilyIndexCount,
            .admittedQueueClasses = node.queueSharing,
            .usesConcurrentSharing = node.usesConcurrentSharing,
        };
    }

    [[nodiscard]] static inline Span<const GpuTaskGraphInitialOwnerHandoffSourceView> ResourceInitialOwnerHandoffSources(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskGraphDetail::GpuGraphResourceNode& node)noexcept{
        return node.initialOwnerHandoffSourceCount != 0u ? Span<const GpuTaskGraphInitialOwnerHandoffSourceView>(view.m_graph->m_initialOwnerHandoffSources.data() + node.initialOwnerHandoffSourceOffset, node.initialOwnerHandoffSourceCount) : Span<const GpuTaskGraphInitialOwnerHandoffSourceView>();
    }

    [[nodiscard]] static inline const GpuPresentEndpoint* PresentEndpoint(const GpuTaskGraph::DeclarationReadView& view)noexcept{
        return view.m_graph && view.m_graph->m_hasPresentEndpoint ? &view.m_graph->m_presentEndpoint : nullptr;
    }

    [[nodiscard]] static inline b8 ValidTask(const GpuTaskGraph::DeclarationReadView& view, const GpuTaskId& id)noexcept{
        return view.m_graph && view.m_graph->validTask(id);
    }

    [[nodiscard]] static inline b8 ValidResource(const GpuTaskGraph::DeclarationReadView& view, const GpuGraphResourceId& id)noexcept{
        return view.m_graph && view.m_graph->validResource(id);
    }

    [[nodiscard]] static inline b8 ValidExternalCompletion(const GpuTaskGraph::DeclarationReadView& view, const GpuExternalCompletionId& id)noexcept{
        return view.m_graph && view.m_graph->validExternalCompletion(id);
    }

    [[nodiscard]] static inline b8 HasBackendResource(const GpuTaskGraphDetail::GpuGraphResourceNode& node)noexcept{
        return node.texture != nullptr || node.buffer != nullptr || node.accelStruct != nullptr;
    }

    [[nodiscard]] static inline Texture* TextureForResourceIndex(const GpuTaskGraph::DeclarationReadView& view, const u32 index)noexcept{
        const GpuTaskGraphDetail::GpuGraphResourceNode& node = view.m_graph->m_resources[index];
        return node.type == GpuGraphResourceType::Texture ? node.texture.get() : nullptr;
    }

    [[nodiscard]] static inline Buffer* BufferForResourceIndex(const GpuTaskGraph::DeclarationReadView& view, const u32 index)noexcept{
        const GpuTaskGraphDetail::GpuGraphResourceNode& node = view.m_graph->m_resources[index];
        return node.type == GpuGraphResourceType::Buffer ? node.buffer.get() : nullptr;
    }

    [[nodiscard]] static inline RayTracingAccelStruct* AccelStructForResourceIndex(const GpuTaskGraph::DeclarationReadView& view, const u32 index)noexcept{
        const GpuTaskGraphDetail::GpuGraphResourceNode& node = view.m_graph->m_resources[index];
        return node.type == GpuGraphResourceType::AccelStruct ? node.accelStruct.get() : nullptr;
    }

    [[nodiscard]] static inline GpuTaskGraphSchedulingTaskIndexView SchedulingConsumers(const GpuTaskGraphAnalysis& analysis, const u32 producerIndex)noexcept{
        FOUNDATION_ASSERT(analysis.m_valid && producerIndex < analysis.m_taskCount && analysis.m_schedulingOutgoingOffsets.size() == analysis.m_taskCount + 1u);
        const usize first = analysis.m_schedulingOutgoingOffsets[producerIndex];
        const usize end = analysis.m_schedulingOutgoingOffsets[producerIndex + 1u];
        return GpuTaskGraphSchedulingTaskIndexView{ .taskIndices = end != first ? analysis.m_schedulingOutgoingConsumers.data() + first : nullptr, .taskCount = end - first };
    }

    [[nodiscard]] static inline GpuTaskGraphSchedulingTaskIndexView SchedulingProducers(const GpuTaskGraphAnalysis& analysis, const u32 consumerIndex)noexcept{
        FOUNDATION_ASSERT(analysis.m_valid && consumerIndex < analysis.m_taskCount && analysis.m_schedulingIncomingOffsets.size() == analysis.m_taskCount + 1u);
        const usize first = analysis.m_schedulingIncomingOffsets[consumerIndex];
        const usize end = analysis.m_schedulingIncomingOffsets[consumerIndex + 1u];
        return GpuTaskGraphSchedulingTaskIndexView{ .taskIndices = end != first ? analysis.m_schedulingIncomingProducers.data() + first : nullptr, .taskCount = end - first };
    }

    [[nodiscard]] static inline const GpuTaskQueueAssignment* AssignmentForTaskIndex(const GpuTaskGraphQueueAssignments& assignments, const u32 taskIndex)noexcept{
        if(!assignments.m_valid || taskIndex >= assignments.m_assignmentIndicesByTask.size())
            return nullptr;
        const u32 assignmentIndex = assignments.m_assignmentIndicesByTask[taskIndex];
        return assignmentIndex < assignments.m_assignments.size() ? &assignments.m_assignments[assignmentIndex] : nullptr;
    }
};


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


    [[nodiscard]] Optional<NotNull<const TrackedCompiledResourceState*>> borrowState()const noexcept{
        return state ? Optional<NotNull<const TrackedCompiledResourceState*>>(MakeNotNull(state)) : Optional<NotNull<const TrackedCompiledResourceState*>>();
    }
    void setState(const TrackedCompiledResourceState& value)noexcept{ state = &value; }
    void setState(const TrackedCompiledResourceState&&) = delete;
};

struct ResourceRangeScratch{
    Vector<IntegerRectBounds, Alloc::ScratchArena> uncovered;
    Vector<IntegerRectBounds, Alloc::ScratchArena> remainders;
    Vector<IntegerRectBounds, Alloc::ScratchArena> remaining;
    Vector<IntegerRectBounds, Alloc::ScratchArena> covered;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> discovered;


    explicit ResourceRangeScratch(Alloc::ScratchArena& scratchArena)noexcept
        : uncovered(scratchArena)
        , remainders(scratchArena)
        , remaining(scratchArena)
        , covered(scratchArena)
        , discovered(scratchArena)
    {}
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
    Vector<u8, Alloc::ScratchArena>& resourceHasPendingEpilogueBarrier;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies;
    Vector<GpuPacketDependency, Alloc::ScratchArena>& resourceStateDependencies;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& stateFragments;
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& taskFirstUseRanges;
    ResourceRangeScratch& rangeScratch;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u64 AllocateCompiledPlanGeneration()noexcept;

[[nodiscard]] inline b8 HasCapabilities(const GpuQueueCapability::Mask available, const GpuQueueCapability::Mask required)noexcept{
    const u8 availableMask = static_cast<u8>(available);
    const u8 requiredMask = static_cast<u8>(required);
    return (availableMask & requiredMask) == requiredMask;
}

[[nodiscard]] inline b8 TaskAllowsPacketMerge(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowPacketMerge && !task.scheduling.forceSubmissionBoundary && !task.scheduling.joinsAcceptedQueueFrontier;
}

[[nodiscard]] inline b8 TaskNodeAllowsPacketMerge(const GpuTaskGraphDetail::GpuTaskNode& task)noexcept{
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

[[nodiscard]] b8 ResourceNodeSharingAdmitsQueue(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphDetail::GpuGraphResourceNode& resource, const GpuPhysicalQueueTopology& topology, const GpuPhysicalQueueInfo& queue)noexcept;

[[nodiscard]] b8 ResourceNodeUsesConcurrentQueueSharing(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphDetail::GpuGraphResourceNode& resource, const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] b8 ResourceNodeSharesQueuePairConcurrently(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphDetail::GpuGraphResourceNode& resource, const GpuPhysicalQueueTopology& topology, const GpuPhysicalQueueInfo& sourceQueue, const GpuPhysicalQueueInfo& destinationQueue)noexcept;

[[nodiscard]] b8 IsValidQueueTopology(const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindDefaultGraphicsQueue(const GpuPhysicalQueueTopology& topology)noexcept;

[[nodiscard]] b8 IsValidCommandRequirements(const GpuTaskCommandRequirements& commands)noexcept;
[[nodiscard]] b8 IsValidSchedulingHint(const GpuTaskSchedulingHint& hint)noexcept;

[[nodiscard]] inline u64 QueueCostWeight(const GpuTaskCostHint::Enum cost)noexcept{
    switch(cost){
    case GpuTaskCostHint::Tiny: return 1u;
    case GpuTaskCostHint::Small: return 2u;
    case GpuTaskCostHint::Medium: return 4u;
    case GpuTaskCostHint::Large: return 8u;
    default: return 0u;
    }
}

[[nodiscard]] inline const GpuPhysicalQueueInfo* FindPhysicalQueueInfo(const GpuPhysicalQueueTopology& topology, const GpuPhysicalQueueId& queueID)noexcept{
    if(queueID.index < topology.queueCount && topology.queues[queueID.index].id == queueID)
        return &topology.queues[queueID.index];
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(queue.id == queueID)
            return &queue;
    }
    return nullptr;
}

[[nodiscard]] b8 IsLegalQueueAssignmentCandidate(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& candidate)noexcept;

struct DenseDescendantBitMatrix{
    static constexpr usize s_MaxTasks = 512u;

    Vector<u64, Alloc::ScratchArena> strictDesc;
    usize taskCount = 0u;
    usize wordsPerRow = 0u;
    b8 valid = false;

    explicit DenseDescendantBitMatrix(Alloc::ScratchArena& scratchArena)noexcept
        : strictDesc(scratchArena)
    {}
};

class GpuTaskSchedulingReachability final : NoCopy{
    friend struct GpuTaskQueueScoringData;
    friend b8 BuildGpuTaskSchedulingReachability(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphAnalysis& analysis, GpuTaskSchedulingReachability& outReachability, const DenseDescendantBitMatrix* precomputedStrictDesc);


private:
    struct WordRange{
        usize begin = 0u;
        usize end = 0u;
    };


public:
    explicit GpuTaskSchedulingReachability(Alloc::ScratchArena& scratchArena)noexcept;
    GpuTaskSchedulingReachability(GpuTaskSchedulingReachability&&) = delete;


public:
    [[nodiscard]] b8 reaches(const GpuTaskId& source, const GpuTaskId& destination)const noexcept;
    [[nodiscard]] b8 transitivelyIndependent(const GpuTaskId& lhs, const GpuTaskId& rhs)const noexcept;
    [[nodiscard]] b8 mayContainIndependentTasks()const noexcept{ return !m_totalOrder; }


private:
    [[nodiscard]] b8 hasRelatedBit(u32 sourceIndex, u32 destinationIndex)const noexcept;


private:
    Vector<u64, Alloc::ScratchArena> m_words;
    Vector<u32, Alloc::ScratchArena> m_topologicalRanks;
    Vector<WordRange, Alloc::ScratchArena> m_relatedWordRanges;
    Vector<usize, Alloc::ScratchArena> m_rowOffsets;
    u64 m_graphGeneration = 0u;
    usize m_taskCount = 0u;
    usize m_wordsPerRow = 0u;
    b8 m_totalOrder = false;
    b8 m_valid = false;
};

[[nodiscard]] b8 BuildGpuTaskSchedulingReachability(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphAnalysis& analysis, GpuTaskSchedulingReachability& outReachability, const DenseDescendantBitMatrix* precomputedStrictDesc = nullptr);

[[nodiscard]] inline const GpuTaskQueueAssignment* FindQueueAssignment(const Span<const GpuTaskQueueAssignment> assignments, const Span<const u32> assignmentIndicesByTask, const GpuTaskId& task)noexcept{
    if(!task.valid() || task.index >= assignmentIndicesByTask.size())
        return nullptr;
    const u32 assignmentIndex = assignmentIndicesByTask[task.index];
    if(assignmentIndex >= assignments.size())
        return nullptr;
    const GpuTaskQueueAssignment& assignment = assignments[assignmentIndex];
    return assignment.task == task ? &assignment : nullptr;
}


struct GpuTaskQueueLegalityCache{
    Vector<u64, Alloc::ScratchArena> taskLegalQueueMasks;
    Array<u64, CommandQueue::kCount> classQueueMasks{};
    Array<u8, CommandQueue::kCount> orderedQueueIndicesByClassCount{};
    Array<Array<u8, 64u>, CommandQueue::kCount> orderedQueueIndicesByClass{};
    Array<u8, 64u> orderedAllQueueIndices{};
    u64 primaryGraphicsMask = 0u;
    u8 orderedAllQueueCount = 0u;
    b8 active = false;


public:
    explicit GpuTaskQueueLegalityCache(Alloc::ScratchArena& scratchArena)noexcept
        : taskLegalQueueMasks(scratchArena)
    {}


public:
    void build(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology);
    [[nodiscard]] b8 isLegal(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphTaskView& task, usize queueIndex)const noexcept;
    [[nodiscard]] u64 taskMask(u32 taskIndex)const noexcept{
        return active ? taskLegalQueueMasks[taskIndex] : 0u;
    }
};

struct GpuTaskQueueScoringData{
    struct OwnershipEdge{
        u32 producerIndex = Limit<u32>::s_Max;
        u32 consumerIndex = Limit<u32>::s_Max;
        u32 resourceIndex = Limit<u32>::s_Max;
    };

    Vector<u64, Alloc::ScratchArena> taskCosts;
    Vector<u8, Alloc::ScratchArena> taskCostGroups;
    Array<u64, GpuTaskCostHint::Count> costGroupWeights;
    Vector<usize, Alloc::ScratchArena> ownershipEdgeOffsets;
    Vector<OwnershipEdge, Alloc::ScratchArena> ownershipEdges;
    Vector<u64, Alloc::ScratchArena> resourceConcurrentAdmittedMasks;
    Vector<GpuTaskQueueLoad, Alloc::ScratchArena> assignedQueueLoads;
    Vector<u64, Alloc::ScratchArena> externalQueueLoadsByIndex;
    Vector<u32, Alloc::ScratchArena> queueFamilyByIndex;
    Vector<u16, Alloc::ScratchArena> assignedQueueIndexByTask;
    Vector<u64, Alloc::ScratchArena> assignedCostWords;
    usize costWordsPerGroup = 0u;
    u64 totalAssignedCost = 0u;
    const GpuTaskQueueLoad* externalQueueLoads = nullptr;
    usize externalQueueLoadCount = 0u;
    u8 nonEmptyCostGroupMask = 0u;
    b8 hasResourceConcurrentMasks = false;


public:
    GpuTaskQueueScoringData(
        const GpuTaskGraph::DeclarationReadView& graph,
        const GpuTaskGraphAnalysis& analysis,
        const GpuTaskSchedulingReachability& reachability,
        const GpuTaskGraphQueueAssignmentOptions& options,
        Alloc::ScratchArena& scratchArena,
        const GpuPhysicalQueueTopology* topology = nullptr,
        b8 hasNonHazardDomainResource = true
    );


public:
    [[nodiscard]] usize findAssignedQueueIndex(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] u64 externalQueueLoad(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] inline u64 externalQueueLoadByIndex(const usize queueIndex)const noexcept{
        return queueIndex < externalQueueLoadsByIndex.size() ? externalQueueLoadsByIndex[queueIndex] : 0u;
    }
    void rebuildAssignmentLoads(Span<const GpuTaskQueueAssignment> assignments, const GpuPhysicalQueueTopology& topology);
    void updateAssignmentLoads(const GpuTaskId& task, const GpuPhysicalQueueId& previousQueue, const GpuPhysicalQueueId& selectedQueue)noexcept;
    [[nodiscard]] u64 assignedQueueLoad(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] u64 independentQueueCost(const GpuTaskSchedulingReachability& reachability, const GpuPhysicalQueueId& queue, Span<const GpuTaskQueueAssignment> members)const noexcept;
    [[nodiscard]] u64 independentQueueCostByIndex(const GpuTaskSchedulingReachability& reachability, usize queueIndex, Span<const GpuTaskQueueAssignment> members)const noexcept;
};

struct GpuTaskQueueScoreExclusions{
    usize assignmentOffset = 0u;
    usize assignmentCount = 0u;
    u64 totalCost = 0u;
    u64 candidateQueueCost = 0u;
};

[[nodiscard]] GpuQueueAssignmentScore BuildTaskQueueAssignmentScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    Span<const GpuTaskQueueAssignment> assignments,
    Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    GpuTaskId taskId,
    const GpuTaskSchedulingHint& taskScheduling,
    const GpuPhysicalQueueInfo& candidate,
    const GpuTaskQueueScoreExclusions& exclusions = {},
    b8 computeOverlap = true
)noexcept;

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
    const GpuTaskQueueScoreExclusions& exclusions = {},
    b8 computeOverlap = true
)noexcept;

struct GpuTaskQueuePlacementGroup{
    usize assignmentOffset = 0u;
    usize assignmentCount = 0u;
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
    GpuPhysicalQueueId initialOwnershipQueue;
    u64 legalQueueMask = 0u;
};

[[nodiscard]] const GpuPhysicalQueueInfo* FindBestLegalQueuePlacementGroupCandidate(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskQueuePlacementGroup& group,
    CommandQueue::Enum requiredClass = CommandQueue::kCount,
    const GpuTaskQueueLegalityCache* legalityCache = nullptr
)noexcept;

[[nodiscard]] Expected<void, GpuTaskQueueAssignmentDiagnostic> BuildQueuePlacementGroups(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena>& outGroups,
    const GpuTaskQueueLegalityCache* legalityCache = nullptr
);

[[nodiscard]] GpuQueueAssignmentScore BuildQueuePlacementGroupScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    Span<const GpuTaskQueueAssignment> assignments,
    Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskQueuePlacementGroup& group,
    const GpuPhysicalQueueInfo& candidate,
    b8 computeOverlap = true
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
    const b8 preferNonPrimaryQueue,
    const GpuTaskQueueLegalityCache* legalityCache = nullptr
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
    const b8 allowCrossFamilyRouting,
    const GpuTaskQueueLegalityCache* legalityCache = nullptr
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo& FindTimingFeedbackIncumbent(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& staticQueue,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskQueueLegalityCache* legalityCache = nullptr
)noexcept;

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackQueueCached(
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
    const u64 frameIndex,
    const GpuTaskQueueLegalityCache* legalityCache
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
    const u64 frameIndex,
    const GpuTaskQueueLegalityCache* legalityCache = nullptr
)noexcept;

[[nodiscard]] b8 IsBetterAutomaticQueueAssignmentCandidate(
    const GpuQueueAssignmentScore& candidateScore,
    const GpuPhysicalQueueInfo& candidate,
    const GpuQueueAssignmentScore& currentScore,
    const GpuPhysicalQueueInfo* current,
    b8 compareTotalScore
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 IsReadAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Read || access == GpuTaskResourceAccess::ReadWrite;
}

[[nodiscard]] inline b8 IsWriteAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Write || access == GpuTaskResourceAccess::ReadWrite;
}

[[nodiscard]] Expected<GpuTaskResourceRange> ResolveTextureRangeForPlanning(const Texture* texture, const GpuTaskResourceRange& range)noexcept;
[[nodiscard]] Expected<GpuTaskResourceRange> ResolveResourceRangeForPlanning(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& range)noexcept;
[[nodiscard]] Expected<GpuTaskResourceRange> ResolveResourceNodeRangeForPlanning(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphDetail::GpuGraphResourceNode& resource, const u32 resourceIndex, const GpuTaskResourceRange& range)noexcept;

[[nodiscard]] inline b8 RangesOverlapForType(const GpuGraphResourceType::Enum type, const GpuTaskResourceRange& lhs, const GpuTaskResourceRange& rhs)noexcept{
    if(type == GpuGraphResourceType::Texture)
        return lhs.textureSubresources.overlaps(rhs.textureSubresources);
    if(type == GpuGraphResourceType::Buffer)
        return lhs.bufferRange.overlaps(rhs.bufferRange);
    return true;
}

[[nodiscard]] inline b8 RangeContainsForType(const GpuGraphResourceType::Enum type, const GpuTaskResourceRange& outer, const GpuTaskResourceRange& inner)noexcept{
    if(type == GpuGraphResourceType::Texture)
        return outer.textureSubresources.contains(inner.textureSubresources);
    if(type == GpuGraphResourceType::Buffer)
        return outer.bufferRange.contains(inner.bufferRange);
    return true;
}

[[nodiscard]] inline b8 RangesOverlap(const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& lhs, const GpuTaskResourceRange& rhs)noexcept{
    return RangesOverlapForType(resource.type, lhs, rhs);
}

[[nodiscard]] inline b8 RangeContains(const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& outer, const GpuTaskResourceRange& inner)noexcept{
    return RangeContainsForType(resource.type, outer, inner);
}
[[nodiscard]] const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const GpuPhysicalQueueId& destinationQueue,
    const GpuPhysicalQueueId* requiredSourceQueue = nullptr,
    const GpuTaskGraph::DeclarationReadView* planningGraph = nullptr
)noexcept;

[[nodiscard]] Expected<void, GpuTaskGraphAnalysisDiagnostic> BuildResourceVersionDependencyEdges(const GpuTaskGraph::DeclarationReadView& graph, Alloc::ScratchArena& scratchArena, Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& outEdges);

[[nodiscard]] b8 CollectResourceFirstUseRangesWithinTaskWithScratch(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const TaskResourceUseIndex& useHistory,
    usize useIndex,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    ResourceRangeScratch& rangeScratch,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
);

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

[[nodiscard]] b8 CollectLatestResourceStateFragmentsWithScratch(
    Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Span<const GpuTaskResourceRange> requestedRanges,
    ResourceRangeScratch& rangeScratch,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
);

[[nodiscard]] b8 CollectLatestResourceStateFragments(
    Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Span<const GpuTaskResourceRange> requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
);

[[nodiscard]] b8 CollectTerminalResourceStateFragmentsWithScratch(
    Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    ResourceRangeScratch& rangeScratch,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments,
    const GpuTaskResourceRange* fullResolvedRange = nullptr
);

[[nodiscard]] b8 CollectTerminalResourceStateFragments(
    Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 FindCompiledTaskIndex(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept{
    if(!task.valid() || task.generation != compiledPlan.graphGeneration || task.index >= compiledPlan.compiledTaskIndexByTask.size())
        return Limit<u32>::s_Max;
    const u32 compiledTaskIndex = compiledPlan.compiledTaskIndexByTask[task.index];
    if(compiledTaskIndex >= compiledPlan.tasks.size() || compiledPlan.tasks[compiledTaskIndex].task != task)
        return Limit<u32>::s_Max;
    return compiledTaskIndex;
}

[[nodiscard]] inline const GpuCompiledTask* FindCompiledTask(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept{
    const u32 compiledTaskIndex = FindCompiledTaskIndex(compiledPlan, task);
    return compiledTaskIndex != Limit<u32>::s_Max ? &compiledPlan.tasks[compiledTaskIndex] : nullptr;
}

[[nodiscard]] inline GpuCompiledTask* FindCompiledTask(GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept{
    const u32 compiledTaskIndex = FindCompiledTaskIndex(compiledPlan, task);
    return compiledTaskIndex != Limit<u32>::s_Max ? &compiledPlan.tasks[compiledTaskIndex] : nullptr;
}

[[nodiscard]] inline GpuSubmissionPacketId FindCompiledPacketForTask(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuTaskId& task)noexcept{
    const GpuCompiledTask* const compiledTask = FindCompiledTask(compiledPlan, task);
    return compiledTask ? compiledTask->packet : GpuSubmissionPacketId{};
}

[[nodiscard]] inline const GpuPhysicalQueueInfo* FindCompiledQueueInfo(const GpuTaskGraphCompiledPlanStorage& compiledPlan, const GpuPhysicalQueueId& queue)noexcept{
    if(!queue.valid() || queue.deviceGeneration != compiledPlan.deviceGeneration)
        return nullptr;
    if(queue.index < compiledPlan.queueTopology.size() && compiledPlan.queueTopology[queue.index].id == queue)
        return &compiledPlan.queueTopology[queue.index];
    for(const GpuPhysicalQueueInfo& info : compiledPlan.queueTopology){
        if(info.id == queue)
            return &info;
    }
    return nullptr;
}

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

