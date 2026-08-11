// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u8 s_ValidQueueCapabilityMask =
    static_cast<u8>(GpuQueueCapability::Transfer)
    | static_cast<u8>(GpuQueueCapability::Compute)
    | static_cast<u8>(GpuQueueCapability::Graphics)
;

[[nodiscard]] static bool HasCapabilities(
    const GpuQueueCapability::Mask available,
    const GpuQueueCapability::Mask required
)noexcept{
    const u8 availableMask = static_cast<u8>(available);
    const u8 requiredMask = static_cast<u8>(required);
    return (availableMask & requiredMask) == requiredMask;
}

[[nodiscard]] static bool IsKnownQueueClass(const CommandQueue::Enum queueClass)noexcept{
    return queueClass < CommandQueue::kCount;
}

[[nodiscard]] static u8 QueueSharingBitForQueueClass(const CommandQueue::Enum queueClass)noexcept{
    switch(queueClass){
    case CommandQueue::Graphics:
        return static_cast<u8>(ResourceQueueSharing::Graphics);
    case CommandQueue::Compute:
        return static_cast<u8>(ResourceQueueSharing::AsyncCompute);
    case CommandQueue::Transfer:
        return static_cast<u8>(ResourceQueueSharing::Transfer);
    default:
        return 0u;
    }
}

[[nodiscard]] static bool ResourceSharingIncludesQueueClass(
    const ResourceQueueSharing::Mask sharing,
    const CommandQueue::Enum queueClass
)noexcept{
    const u8 queueBit = QueueSharingBitForQueueClass(queueClass);
    return queueBit != 0u && (static_cast<u8>(sharing) & queueBit) != 0u;
}

// A sharing mask becomes Vulkan concurrent sharing only when it names at least two distinct families supplied by
// this compile topology. A single requested family remains exclusive and may use ordinary ownership handoffs.
[[nodiscard]] static bool ResourceUsesConcurrentQueueSharing(
    const ResourceQueueSharing::Mask sharing,
    const GpuTaskGraphQueueTopology& topology
)noexcept{
    if(sharing == ResourceQueueSharing::Exclusive)
        return false;

    usize familyCount = 0u;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(!ResourceSharingIncludesQueueClass(sharing, queue.queueClass))
            continue;

        bool familyAlreadyIncluded = false;
        for(usize previousIndex = 0u; previousIndex < queueIndex; ++previousIndex){
            const GpuPhysicalQueueInfo& previous = topology.queues[previousIndex];
            familyAlreadyIncluded = ResourceSharingIncludesQueueClass(sharing, previous.queueClass)
                && previous.familyIndex == queue.familyIndex
            ;
            if(familyAlreadyIncluded)
                break;
        }
        if(!familyAlreadyIncluded && ++familyCount >= 2u)
            return true;
    }
    return false;
}

[[nodiscard]] static bool ResourceSharesQueuePairConcurrently(
    const ResourceQueueSharing::Mask sharing,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& sourceQueue,
    const GpuPhysicalQueueInfo& destinationQueue
)noexcept{
    return sourceQueue.familyIndex != destinationQueue.familyIndex
        && ResourceUsesConcurrentQueueSharing(sharing, topology)
        && ResourceSharingIncludesQueueClass(sharing, sourceQueue.queueClass)
        && ResourceSharingIncludesQueueClass(sharing, destinationQueue.queueClass)
    ;
}

[[nodiscard]] static bool IsBetterQueue(
    const GpuPhysicalQueueInfo& candidate,
    const GpuPhysicalQueueInfo* const current
)noexcept{
    if(!current)
        return true;
    if(candidate.id.index != current->id.index)
        return candidate.id.index < current->id.index;
    return candidate.queueClass < current->queueClass;
}

[[nodiscard]] static bool IsValidQueueTopology(
    const GpuTaskGraphQueueTopology& topology
)noexcept{
    if(!topology.queues || topology.queueCount == 0u)
        return false;

    u16 deviceGeneration = 0u;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        const u8 capabilityMask = static_cast<u8>(queue.capabilities);
        if(
            !queue.id.valid()
            || !IsKnownQueueClass(queue.queueClass)
            || queue.familyIndex == Limit<u32>::s_Max
            || capabilityMask == 0u
            || (capabilityMask & ~s_ValidQueueCapabilityMask) != 0u
        )
            return false;

        switch(queue.queueClass){
        case CommandQueue::Graphics:
            if(!HasCapabilities(queue.capabilities, GpuQueueCapability::Graphics))
                return false;
            break;
        case CommandQueue::Compute:
            if(!HasCapabilities(queue.capabilities, GpuQueueCapability::Compute))
                return false;
            break;
        case CommandQueue::Transfer:
            if(!HasCapabilities(queue.capabilities, GpuQueueCapability::Transfer))
                return false;
            break;
        default:
            return false;
        }

        if(queueIndex == 0u)
            deviceGeneration = queue.id.deviceGeneration;
        else if(queue.id.deviceGeneration != deviceGeneration)
            return false;

        for(usize previousIndex = 0u; previousIndex < queueIndex; ++previousIndex){
            if(topology.queues[previousIndex].id == queue.id)
                return false;
        }
    }
    return true;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindBestCompatibleQueue(
    const GpuTaskGraphQueueTopology& topology,
    const GpuQueueCapability::Mask requiredCapabilities,
    const CommandQueue::Enum requiredClass = CommandQueue::kCount
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(
            (requiredClass != CommandQueue::kCount && queue.queueClass != requiredClass)
            || !HasCapabilities(queue.capabilities, requiredCapabilities)
            || !IsBetterQueue(queue, result)
        )
            continue;
        result = &queue;
    }
    return result;
}

[[nodiscard]] static bool RequiresGraphics(const GpuQueueCapability::Mask requiredCapabilities)noexcept{
    return HasCapabilities(requiredCapabilities, GpuQueueCapability::Graphics);
}

[[nodiscard]] static bool ShouldUseDedicatedCompute(const GpuTaskSchedulingHint& hint)noexcept{
    return hint.cost != GpuTaskCostHint::Tiny
        && hint.overlapPreferred
        && !hint.avoidQueueCrossing
    ;
}

[[nodiscard]] static bool ShouldUseDedicatedTransfer(const GpuTaskSchedulingHint& hint)noexcept{
    // Dedicated copies buy queue overlap only when their synchronization cost is plausibly amortized. Keep the
    // same conservative threshold as Compute until per-packet timing feeds a richer queue score.
    return ShouldUseDedicatedCompute(hint);
}

[[nodiscard]] static bool IsValidQueueRequest(const GpuQueueRequest& request)noexcept{
    return (static_cast<u8>(request.requiredCapabilities) & ~s_ValidQueueCapabilityMask) == 0u
        && request.preferredQueue < GpuQueuePreference::kCount;
}

[[nodiscard]] static bool IsValidSchedulingHint(const GpuTaskSchedulingHint& hint)noexcept{
    return hint.cost < GpuTaskCostHint::kCount;
}

[[nodiscard]] static bool IsReadAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Read || access == GpuTaskResourceAccess::ReadWrite;
}

[[nodiscard]] static bool IsWriteAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Write || access == GpuTaskResourceAccess::ReadWrite;
}

[[nodiscard]] static bool IsValidTextureRange(const TextureSubresourceSet& range)noexcept{
    return range.numMipLevels != 0u && range.numArraySlices != 0u;
}

[[nodiscard]] static bool IsValidBufferRange(const BufferRange& range)noexcept{
    return range.byteSize != 0u
        && (
            range.byteSize == BufferRange::AllBytes
            || range.byteOffset <= Limit<u64>::s_Max - range.byteSize
        )
    ;
}

[[nodiscard]] static u64 RangeEnd(const u32 base, const u32 count, const u32 all)noexcept{
    return count == all ? Limit<u64>::s_Max : static_cast<u64>(base) + static_cast<u64>(count);
}

[[nodiscard]] static bool RangesOverlap(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& lhs,
    const GpuTaskResourceRange& rhs
)noexcept{
    // Buffers intentionally stay whole-resource in Phase 1. Their declared byte ranges become useful when the
    // compiler grows a tested interval tracker; treating them as independent before then would be unsafe.
    if(resource.type != GpuGraphResourceType::Texture)
        return true;

    const TextureSubresourceSet& left = lhs.textureSubresources;
    const TextureSubresourceSet& right = rhs.textureSubresources;
    const u64 leftMipEnd = RangeEnd(left.baseMipLevel, left.numMipLevels, TextureSubresourceSet::AllMipLevels);
    const u64 rightMipEnd = RangeEnd(right.baseMipLevel, right.numMipLevels, TextureSubresourceSet::AllMipLevels);
    const u64 leftArrayEnd = RangeEnd(left.baseArraySlice, left.numArraySlices, TextureSubresourceSet::AllArraySlices);
    const u64 rightArrayEnd = RangeEnd(right.baseArraySlice, right.numArraySlices, TextureSubresourceSet::AllArraySlices);
    return left.baseMipLevel < rightMipEnd
        && right.baseMipLevel < leftMipEnd
        && left.baseArraySlice < rightArrayEnd
        && right.baseArraySlice < leftArrayEnd;
}

[[nodiscard]] static bool RangeContains(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& outer,
    const GpuTaskResourceRange& inner
)noexcept{
    if(resource.type != GpuGraphResourceType::Texture)
        return true;

    const TextureSubresourceSet& outerTexture = outer.textureSubresources;
    const TextureSubresourceSet& innerTexture = inner.textureSubresources;
    return outerTexture.baseMipLevel <= innerTexture.baseMipLevel
        && RangeEnd(
            outerTexture.baseMipLevel,
            outerTexture.numMipLevels,
            TextureSubresourceSet::AllMipLevels
        ) >= RangeEnd(
            innerTexture.baseMipLevel,
            innerTexture.numMipLevels,
            TextureSubresourceSet::AllMipLevels
        )
        && outerTexture.baseArraySlice <= innerTexture.baseArraySlice
        && RangeEnd(
            outerTexture.baseArraySlice,
            outerTexture.numArraySlices,
            TextureSubresourceSet::AllArraySlices
        ) >= RangeEnd(
            innerTexture.baseArraySlice,
            innerTexture.numArraySlices,
            TextureSubresourceSet::AllArraySlices
        )
    ;
}

[[nodiscard]] static bool IncludesEdge(
    const GpuTaskDependencyEdge& edge,
    const bool explicitOnly
)noexcept{
    return !explicitOnly || edge.hazard == GpuTaskHazardType::Explicit;
}

struct TrackedResourceAccess{
    GpuTaskId task;
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    bool active = true;
};

// This is deliberately separate from hazard analysis.  Hazards decide execution order; the barrier planner retains
// the last state required for each overlapping declared range so native recording can establish the next task's
// state without renderer-owned subset/fan-in bookkeeping.
struct TrackedCompiledResourceState{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask state = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    GpuTaskId task;
    GpuPhysicalQueueId queue;
};

struct PendingCompiledEpilogueBarrier{
    GpuTaskId task;
    GpuCompiledBarrier barrier;
};

[[nodiscard]] static GpuCompiledBarrierType::Enum TransitionBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureTransition;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferTransition;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructTransition;
    default:
        NWB_ASSERT(false);
        return GpuCompiledBarrierType::TextureTransition;
    }
}

[[nodiscard]] static GpuCompiledBarrierType::Enum UavBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureUav;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferUav;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructUav;
    default:
        NWB_ASSERT(false);
        return GpuCompiledBarrierType::TextureUav;
    }
}

[[nodiscard]] static GpuCompiledBarrierType::Enum OwnershipReleaseBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureOwnershipRelease;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferOwnershipRelease;
    default:
        return GpuCompiledBarrierType::kCount;
    }
}

[[nodiscard]] static GpuCompiledBarrierType::Enum OwnershipAcquireBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureOwnershipAcquire;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferOwnershipAcquire;
    default:
        return GpuCompiledBarrierType::kCount;
    }
}

static bool BuildTopologicalOrder(
    const GpuTaskGraph& graph,
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    const bool explicitOnly,
    GraphicsVector<GpuTaskId>& outOrder,
    GraphicsVector<GpuTaskId>& outCyclePath,
    GraphicsVector<GpuTaskDependencyEdge>& outCycleEdges,
    Alloc::ScratchArena& scratchArena
){
    const usize taskCount = graph.taskCount();
    Vector<u32, Alloc::ScratchArena> indegrees(taskCount, scratchArena);
    Vector<u8, Alloc::ScratchArena> scheduled(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        indegrees[taskIndex] = 0u;
        scheduled[taskIndex] = 0u;
    }
    for(const GpuTaskDependencyEdge& edge : edges){
        if(IncludesEdge(edge, explicitOnly))
            ++indegrees[edge.consumer.index];
    }

    outOrder.clear();
    outCyclePath.clear();
    outCycleEdges.clear();
    outOrder.reserve(taskCount);
    for(usize emittedCount = 0u; emittedCount < taskCount; ++emittedCount){
        usize nextTask = taskCount;
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            if(!scheduled[taskIndex] && indegrees[taskIndex] == 0u){
                nextTask = taskIndex;
                break;
            }
        }
        if(nextTask == taskCount)
            break;

        scheduled[nextTask] = 1u;
        outOrder.push_back(graph.taskAt(nextTask).id);
        for(const GpuTaskDependencyEdge& edge : edges){
            if(IncludesEdge(edge, explicitOnly) && edge.producer.index == nextTask){
                NWB_ASSERT(indegrees[edge.consumer.index] > 0u);
                --indegrees[edge.consumer.index];
            }
        }
    }
    if(outOrder.size() == taskCount)
        return true;

    Vector<u8, Alloc::ScratchArena> visitState(taskCount, scratchArena);
    Vector<u32, Alloc::ScratchArena> visitStack(scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        visitState[taskIndex] = 0u;

    const auto appendCycleEdge = [&](const u32 producerIndex, const u32 consumerIndex){
        for(const GpuTaskDependencyEdge& edge : edges){
            if(
                IncludesEdge(edge, explicitOnly)
                && edge.producer.index == producerIndex
                && edge.consumer.index == consumerIndex
            ){
                outCycleEdges.push_back(edge);
                return;
            }
        }
        NWB_ASSERT(false);
    };
    const auto visit = [&](auto&& self, const u32 taskIndex) -> bool{
        visitState[taskIndex] = 1u;
        visitStack.push_back(taskIndex);
        for(const GpuTaskDependencyEdge& edge : edges){
            if(!IncludesEdge(edge, explicitOnly) || edge.producer.index != taskIndex)
                continue;

            const u32 consumerIndex = edge.consumer.index;
            if(visitState[consumerIndex] == 1u){
                usize cycleStart = 0u;
                while(visitStack[cycleStart] != consumerIndex)
                    ++cycleStart;
                for(usize cycleIndex = cycleStart; cycleIndex < visitStack.size(); ++cycleIndex){
                    outCyclePath.push_back(graph.taskAt(visitStack[cycleIndex]).id);
                    if(cycleIndex + 1u < visitStack.size())
                        appendCycleEdge(visitStack[cycleIndex], visitStack[cycleIndex + 1u]);
                }
                outCyclePath.push_back(graph.taskAt(consumerIndex).id);
                appendCycleEdge(taskIndex, consumerIndex);
                return true;
            }
            if(visitState[consumerIndex] == 0u && self(self, consumerIndex))
                return true;
        }
        visitStack.pop_back();
        visitState[taskIndex] = 2u;
        return false;
    };

    for(u32 taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        if(visitState[taskIndex] == 0u && visit(visit, taskIndex))
            break;
    }
    outOrder.clear();
    return false;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraphAnalysis::reset(){
    m_edges.clear();
    m_inferredEdges.clear();
    m_externalDependencies.clear();
    m_topologicalOrder.clear();
    m_cyclePath.clear();
    m_cycleEdges.clear();
    m_diagnostic = GpuTaskGraphAnalysisDiagnostic{};
    m_generation = 0u;
    m_taskCount = 0u;
    m_resourceCount = 0u;
    m_externalCompletionCount = 0u;
    m_explicitEdgeCount = 0u;
    m_inferredEdgeCount = 0u;
    m_valid = false;
}

bool GpuTaskGraphAnalysis::validFor(const GpuTaskGraph& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_taskCount == graph.taskCount()
        && m_resourceCount == graph.resourceCount()
        && m_externalCompletionCount == graph.externalCompletionCount()
    ;
}

bool GpuTaskGraphAnalysis::hasExplicitEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept{
    for(const GpuTaskDependencyEdge& edge : m_edges){
        if(
            edge.producer == producer
            && edge.consumer == consumer
            && edge.hazard == GpuTaskHazardType::Explicit
        )
            return true;
    }
    return false;
}

bool GpuTaskGraphAnalysis::hasInferredEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept{
    for(const GpuTaskDependencyEdge& edge : m_inferredEdges){
        if(edge.producer == producer && edge.consumer == consumer)
            return true;
    }
    return false;
}


void GpuTaskGraphQueueAssignments::reset(){
    m_assignments.clear();
    m_diagnostic = GpuTaskQueueAssignmentDiagnostic{};
    m_generation = 0u;
    m_taskCount = 0u;
    m_valid = false;
}

bool GpuTaskGraphQueueAssignments::validFor(const GpuTaskGraph& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_taskCount == graph.taskCount()
    ;
}

const GpuTaskQueueAssignment* GpuTaskGraphQueueAssignments::find(const GpuTaskId& task)const noexcept{
    if(!m_valid || task.generation != m_generation)
        return nullptr;

    for(const GpuTaskQueueAssignment& assignment : m_assignments){
        if(assignment.task == task)
            return &assignment;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphCompiler::analyze(
    const GpuTaskGraph& graph,
    GpuTaskGraphAnalysis& outAnalysis,
    Alloc::ScratchArena& scratchArena
)const{
    outAnalysis.reset();
    outAnalysis.m_generation = graph.generation();
    outAnalysis.m_taskCount = graph.taskCount();
    outAnalysis.m_resourceCount = graph.resourceCount();
    outAnalysis.m_externalCompletionCount = graph.externalCompletionCount();

    const auto fail = [&](const GpuTaskGraphAnalysisStatus::Enum status, const GpuTaskId task = {}, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {}){
        outAnalysis.m_diagnostic.status = status;
        outAnalysis.m_diagnostic.task = task;
        outAnalysis.m_diagnostic.relatedTask = relatedTask;
        outAnalysis.m_diagnostic.resource = resource;
        return false;
    };
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        if(!resource.identity || resource.type >= GpuGraphResourceType::kCount)
            return fail(GpuTaskGraphAnalysisStatus::InvalidResource, {}, {}, resource.id);
    }
    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex){
        if(!graph.externalCompletionAt(completionIndex).identity)
            return fail(GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency);
    }
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        if(!task.identity || !__hidden_gpu_task_graph_compiler::IsValidQueueRequest(task.queue) || !__hidden_gpu_task_graph_compiler::IsValidSchedulingHint(task.scheduling))
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
        for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
            if(!graph.validTask(task.dependencies[dependencyIndex]))
                return fail(GpuTaskGraphAnalysisStatus::InvalidTaskDependency, task.id, task.dependencies[dependencyIndex]);
        }
        for(usize dependencyIndex = 0u; dependencyIndex < task.externalDependencyCount; ++dependencyIndex){
            if(!graph.validExternalCompletion(task.externalDependencies[dependencyIndex])){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency,
                    task.id
                );
            }
        }
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(
                !graph.validResource(use.resource)
                || use.requiredState == ResourceStates::Unknown
                || use.access >= GpuTaskResourceAccess::kCount
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, task.id, {}, use.resource);

            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(
                (
                    resource.type == GpuGraphResourceType::Texture
                    && !__hidden_gpu_task_graph_compiler::IsValidTextureRange(use.range.textureSubresources)
                )
                || (
                    resource.type == GpuGraphResourceType::Buffer
                    && !__hidden_gpu_task_graph_compiler::IsValidBufferRange(use.range.bufferRange)
                )
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, task.id, {}, use.resource);
        }
    }

    const auto appendSchedulingEdge = [&](const GpuTaskDependencyEdge& edge){
        for(GpuTaskDependencyEdge& existing : outAnalysis.m_edges){
            if(existing.producer != edge.producer || existing.consumer != edge.consumer)
                continue;
            if(
                edge.hazard == GpuTaskHazardType::Explicit
                && existing.hazard != GpuTaskHazardType::Explicit
            ){
                existing = edge;
                ++outAnalysis.m_explicitEdgeCount;
            }
            return;
        }
        outAnalysis.m_edges.push_back(edge);
        if(edge.hazard == GpuTaskHazardType::Explicit)
            ++outAnalysis.m_explicitEdgeCount;
    };
    const auto appendInferredEdge = [&](const GpuTaskDependencyEdge& edge){
        NWB_ASSERT(edge.hazard != GpuTaskHazardType::Explicit);

        bool hasTaskPair = false;
        for(const GpuTaskDependencyEdge& existing : outAnalysis.m_inferredEdges){
            if(existing.producer != edge.producer || existing.consumer != edge.consumer)
                continue;
            hasTaskPair = true;
            if(existing.resource == edge.resource && existing.hazard == edge.hazard)
                return;
        }
        outAnalysis.m_inferredEdges.push_back(edge);
        if(!hasTaskPair)
            ++outAnalysis.m_inferredEdgeCount;
        appendSchedulingEdge(edge);
    };
    const auto appendExternalDependency = [&](const GpuTaskExternalDependencyEdge& edge){
        for(const GpuTaskExternalDependencyEdge& existing : outAnalysis.m_externalDependencies){
            if(existing.completion == edge.completion && existing.consumer == edge.consumer)
                return;
        }
        outAnalysis.m_externalDependencies.push_back(edge);
    };

    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
            appendSchedulingEdge(GpuTaskDependencyEdge{
                .producer = task.dependencies[dependencyIndex],
                .consumer = task.id,
                .resource = {},
                .hazard = GpuTaskHazardType::Explicit,
            });
        }
        for(usize dependencyIndex = 0u; dependencyIndex < task.externalDependencyCount; ++dependencyIndex){
            appendExternalDependency(GpuTaskExternalDependencyEdge{
                .completion = task.externalDependencies[dependencyIndex],
                .consumer = task.id,
            });
        }
    }

    if(!__hidden_gpu_task_graph_compiler::BuildTopologicalOrder(
        graph,
        outAnalysis.m_edges,
        true,
        outAnalysis.m_topologicalOrder,
        outAnalysis.m_cyclePath,
        outAnalysis.m_cycleEdges,
        scratchArena
    )){
        const GpuTaskDependencyEdge* const cycleEdge = outAnalysis.m_cycleEdges.empty()
            ? nullptr
            : &outAnalysis.m_cycleEdges.front()
        ;
        return fail(
            GpuTaskGraphAnalysisStatus::Cycle,
            cycleEdge ? cycleEdge->consumer : GpuTaskId{},
            cycleEdge ? cycleEdge->producer : GpuTaskId{},
            cycleEdge ? cycleEdge->resource : GpuGraphResourceId{}
        );
    }

    // Process resource use in the explicit stable order. Explicit relationships therefore outrank declaration
    // order, while per-resource writer/readers state emits only the nearest required RAW/WAR/WAW dependencies.
    usize potentialAccessCount = 0u;
    for(const GpuTaskId task : outAnalysis.m_topologicalOrder)
        potentialAccessCount += graph.taskAt(task.index).resourceUseCount;
    Vector<__hidden_gpu_task_graph_compiler::TrackedResourceAccess, Alloc::ScratchArena> writers(scratchArena);
    Vector<__hidden_gpu_task_graph_compiler::TrackedResourceAccess, Alloc::ScratchArena> readers(scratchArena);
    writers.reserve(potentialAccessCount);
    readers.reserve(potentialAccessCount);

    for(const GpuTaskId taskID : outAnalysis.m_topologicalOrder){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            const auto overlaps = [&](const __hidden_gpu_task_graph_compiler::TrackedResourceAccess& access){
                return access.active
                    && access.task != task.id
                    && access.resource == use.resource
                    && __hidden_gpu_task_graph_compiler::RangesOverlap(resource, access.range, use.range)
                ;
            };

            if(__hidden_gpu_task_graph_compiler::IsReadAccess(use.access)){
                for(const __hidden_gpu_task_graph_compiler::TrackedResourceAccess& writer : writers){
                    if(!overlaps(writer))
                        continue;
                    appendInferredEdge(GpuTaskDependencyEdge{
                        .producer = writer.task,
                        .consumer = task.id,
                        .resource = use.resource,
                        .hazard = GpuTaskHazardType::ReadAfterWrite,
                    });
                }
            }
            if(__hidden_gpu_task_graph_compiler::IsWriteAccess(use.access)){
                for(const __hidden_gpu_task_graph_compiler::TrackedResourceAccess& writer : writers){
                    if(!overlaps(writer))
                        continue;
                    appendInferredEdge(GpuTaskDependencyEdge{
                        .producer = writer.task,
                        .consumer = task.id,
                        .resource = use.resource,
                        .hazard = GpuTaskHazardType::WriteAfterWrite,
                    });
                }
                for(const __hidden_gpu_task_graph_compiler::TrackedResourceAccess& reader : readers){
                    if(!overlaps(reader))
                        continue;
                    appendInferredEdge(GpuTaskDependencyEdge{
                        .producer = reader.task,
                        .consumer = task.id,
                        .resource = use.resource,
                        .hazard = GpuTaskHazardType::WriteAfterRead,
                    });
                }
                for(__hidden_gpu_task_graph_compiler::TrackedResourceAccess& writer : writers){
                    if(
                        writer.active
                        && writer.resource == use.resource
                        && __hidden_gpu_task_graph_compiler::RangeContains(resource, use.range, writer.range)
                    )
                        writer.active = false;
                }
                for(__hidden_gpu_task_graph_compiler::TrackedResourceAccess& reader : readers){
                    if(
                        reader.active
                        && reader.resource == use.resource
                        && __hidden_gpu_task_graph_compiler::RangeContains(resource, use.range, reader.range)
                    )
                        reader.active = false;
                }
                writers.push_back(__hidden_gpu_task_graph_compiler::TrackedResourceAccess{
                    .task = task.id,
                    .resource = use.resource,
                    .range = use.range,
                });
            }
            else if(__hidden_gpu_task_graph_compiler::IsReadAccess(use.access)){
                bool alreadyWrittenByTask = false;
                for(const __hidden_gpu_task_graph_compiler::TrackedResourceAccess& writer : writers){
                    if(
                        writer.active
                        && writer.task == task.id
                        && writer.resource == use.resource
                        && __hidden_gpu_task_graph_compiler::RangeContains(resource, writer.range, use.range)
                    ){
                        alreadyWrittenByTask = true;
                        break;
                    }
                }
                if(!alreadyWrittenByTask){
                    readers.push_back(__hidden_gpu_task_graph_compiler::TrackedResourceAccess{
                        .task = task.id,
                        .resource = use.resource,
                        .range = use.range,
                    });
                }
            }
        }
    }

    if(!__hidden_gpu_task_graph_compiler::BuildTopologicalOrder(
        graph,
        outAnalysis.m_edges,
        false,
        outAnalysis.m_topologicalOrder,
        outAnalysis.m_cyclePath,
        outAnalysis.m_cycleEdges,
        scratchArena
    )){
        const GpuTaskDependencyEdge* const cycleEdge = outAnalysis.m_cycleEdges.empty()
            ? nullptr
            : &outAnalysis.m_cycleEdges.front()
        ;
        return fail(
            GpuTaskGraphAnalysisStatus::Cycle,
            cycleEdge ? cycleEdge->consumer : GpuTaskId{},
            cycleEdge ? cycleEdge->producer : GpuTaskId{},
            cycleEdge ? cycleEdge->resource : GpuGraphResourceId{}
        );
    }

    outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::Success;
    outAnalysis.m_valid = true;
    return true;
}


bool GpuTaskGraphCompiler::assignQueues(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueTopology& topology,
    GpuTaskGraphQueueAssignments& outAssignments
)const{
    using namespace __hidden_gpu_task_graph_compiler;

    outAssignments.reset();
    const auto fail = [&](const GpuTaskGraphQueueAssignmentStatus::Enum status, const GpuTaskId task = {}, const GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None){
        outAssignments.m_diagnostic.status = status;
        outAssignments.m_diagnostic.task = task;
        outAssignments.m_diagnostic.requiredCapabilities = requiredCapabilities;
        return false;
    };
    if(!analysis.validFor(graph))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis);

    if(!IsValidQueueTopology(topology))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);

    outAssignments.m_generation = graph.generation();
    outAssignments.m_taskCount = graph.taskCount();
    outAssignments.m_assignments.reserve(graph.taskCount());

    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const GpuPhysicalQueueInfo* const graphicsQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities,
            CommandQueue::Graphics
        );
        const GpuPhysicalQueueInfo* const computeQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities,
            CommandQueue::Compute
        );
        const GpuPhysicalQueueInfo* const transferQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities,
            CommandQueue::Transfer
        );
        const GpuPhysicalQueueInfo* const fallbackQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities
        );

        const GpuPhysicalQueueInfo* selectedQueue = nullptr;
        GpuTaskQueueAssignmentReason::Enum reason = GpuTaskQueueAssignmentReason::Unknown;
        if(RequiresGraphics(task.queue.requiredCapabilities)){
            // A task that declares Graphics must stay on the physical Graphics transport even if a malformed future
            // topology happens to advertise Graphics capability on another queue class.
            selectedQueue = graphicsQueue;
            reason = GpuTaskQueueAssignmentReason::RequiredGraphics;
        }
        else{
            switch(task.queue.preferredQueue){
            case GpuQueuePreference::Graphics:
                selectedQueue = graphicsQueue;
                reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                if(!selectedQueue && task.queue.allowFallback){
                    selectedQueue = fallbackQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                break;
            case GpuQueuePreference::Compute:
                if(
                    computeQueue
                    && (
                        !task.queue.compilerMayOverridePreference
                        || (computeQueue->dedicated && ShouldUseDedicatedCompute(task.scheduling))
                    )
                ){
                    selectedQueue = computeQueue;
                    reason = computeQueue->dedicated
                        ? GpuTaskQueueAssignmentReason::DedicatedCompute
                        : GpuTaskQueueAssignmentReason::PreferredQueue
                    ;
                }
                else if(task.queue.allowFallback && graphicsQueue){
                    selectedQueue = graphicsQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(computeQueue){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                }
                else if(task.queue.allowFallback){
                    selectedQueue = fallbackQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                break;
            case GpuQueuePreference::Transfer:
                if(
                    transferQueue
                    && (
                        !task.queue.compilerMayOverridePreference
                        || (transferQueue->dedicated && ShouldUseDedicatedTransfer(task.scheduling))
                    )
                ){
                    selectedQueue = transferQueue;
                    reason = transferQueue->dedicated
                        ? GpuTaskQueueAssignmentReason::DedicatedTransfer
                        : GpuTaskQueueAssignmentReason::PreferredQueue
                    ;
                }
                else if(task.queue.allowFallback && computeQueue && computeQueue->dedicated && ShouldUseDedicatedCompute(task.scheduling)){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(task.queue.allowFallback && graphicsQueue){
                    selectedQueue = graphicsQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(transferQueue){
                    selectedQueue = transferQueue;
                    reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                }
                else if(task.queue.allowFallback && computeQueue){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(task.queue.allowFallback){
                    selectedQueue = fallbackQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                break;
            case GpuQueuePreference::Any:
                // Keep the initial Any policy conservative and stable. Packet/frontier scoring arrives after this
                // observational assignment has proven parity against the current renderer schedule.
                selectedQueue = graphicsQueue ? graphicsQueue : fallbackQueue;
                reason = GpuTaskQueueAssignmentReason::ConservativeAny;
                break;
            default:
                NWB_ASSERT(false);
                break;
            }
        }
        if(!selectedQueue){
            return fail(
                GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue,
                task.id,
                task.queue.requiredCapabilities
            );
        }

        outAssignments.m_assignments.push_back(GpuTaskQueueAssignment{
            .task = task.id,
            .queue = selectedQueue->id,
            .queueClass = selectedQueue->queueClass,
            .reason = reason,
            .dedicated = selectedQueue->dedicated,
        });
    }

    outAssignments.m_diagnostic.status = GpuTaskGraphQueueAssignmentStatus::Success;
    outAssignments.m_valid = true;
    return true;
}


bool GpuTaskGraphCompiler::compile(
    const GpuTaskGraph& graph,
    GpuTaskGraphAnalysis& outAnalysis,
    const GpuTaskGraphQueueTopology& topology,
    GpuTaskGraphQueueAssignments& outAssignments,
    GpuCompiledGraph& outCompiledGraph,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphCompileOptions& options
)const{
    using namespace __hidden_gpu_task_graph_compiler;

    outCompiledGraph.reset();
    if(!analyze(graph, outAnalysis, scratchArena) || !assignQueues(graph, outAnalysis, topology, outAssignments))
        return false;

    if(
        topology.queueCount == 0u
        || !topology.queues
        || options.packetizationPolicy >= GpuTaskGraphPacketizationPolicy::kCount
    )
        return false;

    outCompiledGraph.m_generation = graph.generation();
    outCompiledGraph.m_deviceGeneration = topology.queues[0].id.deviceGeneration;
    outCompiledGraph.m_graphTaskCount = graph.taskCount();
    outCompiledGraph.m_tasks.reserve(graph.taskCount());
    outCompiledGraph.m_packets.reserve(graph.taskCount());
    outCompiledGraph.m_packetTasks.reserve(graph.taskCount());
    outCompiledGraph.m_prologueStateSeeds.reserve(graph.taskCount());
    outCompiledGraph.m_prologueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_epilogueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_queueTopology.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        outCompiledGraph.m_queueTopology.push_back(topology.queues[queueIndex]);

    // Tasks retain one exact acceptance and synchronization point by default.  An explicitly opted-in successor may
    // share its immediately preceding compatible packet, preserving task order while retaining one submission for a
    // temporary imported/native recording bridge.
    for(const GpuTaskId taskID : outAnalysis.topologicalOrder()){
        const GpuTaskQueueAssignment* const assignment = outAssignments.find(taskID);
        if(!assignment || !assignment->queue.valid()){
            outCompiledGraph.reset();
            return false;
        }

        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        GpuSubmissionPacketId packetID;
        const bool mergeRequested =
            task.scheduling.mergeWithPrevious
            && task.scheduling.allowPacketMerge
            && !task.scheduling.forceSubmissionBoundary
            && !task.scheduling.joinsAcceptedQueueFrontier
            && !outCompiledGraph.m_packets.empty()
        ;
        if(mergeRequested){
            GpuSubmissionPacket& precedingPacket = outCompiledGraph.m_packets.back();
            bool precedingPacketAllowsMerge = precedingPacket.queue == assignment->queue;
            for(u32 precedingTaskIndex = 0u;
                precedingPacketAllowsMerge && precedingTaskIndex < precedingPacket.taskCount;
                ++precedingTaskIndex
            ){
                const GpuTaskId precedingTask = outCompiledGraph.m_packetTasks[
                    precedingPacket.taskOffset + precedingTaskIndex
                ];
                const GpuTaskGraphTaskView preceding = graph.taskAt(precedingTask.index);
                precedingPacketAllowsMerge = preceding.scheduling.allowPacketMerge
                    && !preceding.scheduling.forceSubmissionBoundary
                    && !preceding.scheduling.joinsAcceptedQueueFrontier
                ;
            }
            if(
                precedingPacketAllowsMerge
                && options.packetizationPolicy == GpuTaskGraphPacketizationPolicy::FrontierSafe
            ){
                for(u32 precedingTaskIndex = 0u;
                    precedingPacketAllowsMerge && precedingTaskIndex < precedingPacket.taskCount;
                    ++precedingTaskIndex
                ){
                    const GpuTaskId precedingTask = outCompiledGraph.m_packetTasks[
                        precedingPacket.taskOffset + precedingTaskIndex
                    ];
                    for(const GpuTaskDependencyEdge& edge : outAnalysis.edges()){
                        if(edge.producer != precedingTask)
                            continue;

                        const GpuTaskQueueAssignment* const consumerAssignment = outAssignments.find(edge.consumer);
                        // Queue assignment covers every analyzed task. Treat a broken assignment conservatively so a
                        // missing consumer identity cannot hide a required cross-queue signal frontier.
                        if(
                            !consumerAssignment
                            || !consumerAssignment->queue.valid()
                            || consumerAssignment->queue != precedingPacket.queue
                        ){
                            precedingPacketAllowsMerge = false;
                            break;
                        }
                    }
                }
            }
            if(precedingPacketAllowsMerge){
                packetID = GpuSubmissionPacketId{
                    static_cast<u32>(outCompiledGraph.m_packets.size() - 1u),
                    outCompiledGraph.m_generation,
                };
                ++precedingPacket.taskCount;
            }
        }

        if(!packetID.valid()){
            packetID = GpuSubmissionPacketId{
                static_cast<u32>(outCompiledGraph.m_packets.size()),
                outCompiledGraph.m_generation,
            };
            outCompiledGraph.m_packets.push_back(GpuSubmissionPacket{
                .queue = assignment->queue,
                .taskOffset = static_cast<u32>(outCompiledGraph.m_packetTasks.size()),
                .taskCount = 1u,
                .joinsAcceptedQueueFrontier = task.scheduling.joinsAcceptedQueueFrontier,
            });
        }
        outCompiledGraph.m_packetTasks.push_back(taskID);
        outCompiledGraph.m_tasks.push_back(GpuCompiledTask{
            .task = taskID,
            .queue = assignment->queue,
            .packet = packetID,
        });
    }

    // Start with graph-planned packet state seeds, transitions, UAV dependencies, and exclusive-family ownership
    // releases. A state seed selects the actual final-state snapshot of a graph-internal producer, so it also carries
    // the release destination into CommandList::open where the paired Vulkan acquire is emitted before the consumer.
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena> trackedResourceStates(scratchArena);
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena> pendingEpilogueBarriers(scratchArena);
    trackedResourceStates.reserve(graph.taskCount());
    pendingEpilogueBarriers.reserve(graph.taskCount());
    for(const GpuTaskId taskID : outAnalysis.topologicalOrder()){
        GpuCompiledTask* compiledTask = nullptr;
        for(GpuCompiledTask& candidate : outCompiledGraph.m_tasks){
            if(candidate.task == taskID){
                compiledTask = &candidate;
                break;
            }
        }
        if(!compiledTask){
            outCompiledGraph.reset();
            return false;
        }

        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const GpuPhysicalQueueInfo* const taskQueue = outCompiledGraph.queueInfo(compiledTask->queue);
        if(!taskQueue){
            outCompiledGraph.reset();
            return false;
        }
        compiledTask->prologueStateSeedOffset = static_cast<u32>(outCompiledGraph.m_prologueStateSeeds.size());
        compiledTask->prologueBarrierOffset = static_cast<u32>(outCompiledGraph.m_prologueBarriers.size());
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(resource.type == GpuGraphResourceType::HazardDomain || use.requiredState == ResourceStates::Unknown)
                continue;
            if(
                (resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer)
                && ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
                && !ResourceSharingIncludesQueueClass(resource.queueSharing, taskQueue->queueClass)
            ){
                // A Vulkan-concurrent resource may only be used by the families named when it was created. Do not
                // turn a missing Transfer bit into an illegal ownership handoff for a concurrent image/buffer.
                outCompiledGraph.reset();
                return false;
            }

            // A task owns its internal resource ordering.  Only its first declared use becomes a packet-boundary
            // transition; later conflicting uses remain local CommandList work inside the task thunk.
            bool alreadyPlannedByTask = false;
            for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                const GpuTaskResourceUse& previousUse = task.resourceUses[previousUseIndex];
                if(
                    previousUse.resource == use.resource
                    && RangesOverlap(resource, previousUse.range, use.range)
                ){
                    alreadyPlannedByTask = true;
                    break;
                }
            }
            if(alreadyPlannedByTask)
                continue;

            const TrackedCompiledResourceState* previousState = nullptr;
            for(usize stateIndex = trackedResourceStates.size(); stateIndex > 0u; --stateIndex){
                const TrackedCompiledResourceState& candidate = trackedResourceStates[stateIndex - 1u];
                if(
                    candidate.resource == use.resource
                    && RangesOverlap(resource, candidate.range, use.range)
                ){
                    previousState = &candidate;
                    break;
                }
            }

            const ResourceStates::Mask before = previousState ? previousState->state : resource.initialState;
            const bool needsUavDependency =
                previousState
                && before == ResourceStates::UnorderedAccess
                && use.requiredState == ResourceStates::UnorderedAccess
                && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access))
            ;
            if(
                previousState
                && (resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer)
            ){
                const GpuSubmissionPacketId sourcePacket = outCompiledGraph.packetForTask(previousState->task);
                if(!sourcePacket.valid()){
                    outCompiledGraph.reset();
                    return false;
                }
                if(sourcePacket != compiledTask->packet){
                    const GpuPhysicalQueueInfo* const sourceQueue = outCompiledGraph.queueInfo(
                        previousState->queue
                    );
                    const GpuPhysicalQueueInfo* const destinationQueue = outCompiledGraph.queueInfo(
                        compiledTask->queue
                    );
                    if(!sourceQueue || !destinationQueue){
                        outCompiledGraph.reset();
                        return false;
                    }
                    if(
                        sourceQueue->queueClass == destinationQueue->queueClass
                        && sourceQueue->familyIndex != destinationQueue->familyIndex
                    ){
                        // CommandQueue names one resolved transport. Multiple same-class families need a richer
                        // physical-owner token before they can participate in explicit ownership transfers.
                        outCompiledGraph.reset();
                        return false;
                    }
                    const bool differentQueueFamilies = sourceQueue->familyIndex != destinationQueue->familyIndex;
                    const bool resourceUsesConcurrentSharing = ResourceUsesConcurrentQueueSharing(
                        resource.queueSharing,
                        topology
                    );
                    const bool concurrentQueuePair = ResourceSharesQueuePairConcurrently(
                        resource.queueSharing,
                        topology,
                        *sourceQueue,
                        *destinationQueue
                    );
                    if(differentQueueFamilies && resourceUsesConcurrentSharing && !concurrentQueuePair){
                        // The resource was created concurrent for another family set. Vulkan cannot transfer
                        // ownership to an omitted family, so fail compilation instead of recording invalid work.
                        outCompiledGraph.reset();
                        return false;
                    }

                    const bool requiresExclusiveOwnershipHandoff =
                        !resourceUsesConcurrentSharing
                        && sourceQueue->queueClass != destinationQueue->queueClass
                    ;
                    if(requiresExclusiveOwnershipHandoff){
                        const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                        const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                        if(releaseType >= GpuCompiledBarrierType::kCount || acquireType >= GpuCompiledBarrierType::kCount){
                            outCompiledGraph.reset();
                            return false;
                        }
                        pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                            .task = previousState->task,
                            .barrier = GpuCompiledBarrier{
                                .type = releaseType,
                                .resource = use.resource,
                                .range = use.range,
                                .before = before,
                                .after = before,
                                .sourceQueue = previousState->queue,
                                .destinationQueue = compiledTask->queue,
                            },
                        });
                        outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                            .type = acquireType,
                            .resource = use.resource,
                            .range = use.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = previousState->queue,
                            .destinationQueue = compiledTask->queue,
                        });
                    }
                    const bool readOnlySameState =
                        previousState->access == GpuTaskResourceAccess::Read
                        && use.access == GpuTaskResourceAccess::Read
                        && before == use.requiredState
                        && !needsUavDependency
                    ;
                    const bool mayOmitInternalStateSeed =
                        use.hasIndependentStateSource
                        && readOnlySameState
                        && concurrentQueuePair
                    ;
                    if(!mayOmitInternalStateSeed){
                        outCompiledGraph.m_prologueStateSeeds.push_back(GpuPacketStateSeed{
                            .resource = use.resource,
                            .range = use.range,
                            .sourcePacket = sourcePacket,
                        });
                    }
                }
            }
            if(before != use.requiredState || needsUavDependency){
                outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                    .type = before == use.requiredState
                        ? UavBarrierType(resource.type)
                        : TransitionBarrierType(resource.type),
                    .resource = use.resource,
                    .range = use.range,
                    .before = before,
                    .after = use.requiredState,
                    .sourceQueue = previousState ? previousState->queue : compiledTask->queue,
                    .destinationQueue = compiledTask->queue,
                });
            }

            trackedResourceStates.push_back(TrackedCompiledResourceState{
                .resource = use.resource,
                .range = use.range,
                .state = use.requiredState,
                .access = use.access,
                .task = taskID,
                .queue = compiledTask->queue,
            });
        }
        compiledTask->prologueStateSeedCount = static_cast<u32>(outCompiledGraph.m_prologueStateSeeds.size())
            - compiledTask->prologueStateSeedOffset
        ;
        compiledTask->prologueBarrierCount = static_cast<u32>(outCompiledGraph.m_prologueBarriers.size())
            - compiledTask->prologueBarrierOffset
        ;
    }

    // Consumers are visited after their producer, so ownership releases are discovered late. Group them only after
    // planning completes to keep every task's epilogue span contiguous in the immutable compiled graph.
    for(GpuCompiledTask& compiledTask : outCompiledGraph.m_tasks){
        compiledTask.epilogueBarrierOffset = static_cast<u32>(outCompiledGraph.m_epilogueBarriers.size());
        for(const PendingCompiledEpilogueBarrier& pending : pendingEpilogueBarriers){
            if(pending.task == compiledTask.task)
                outCompiledGraph.m_epilogueBarriers.push_back(pending.barrier);
        }
        compiledTask.epilogueBarrierCount = static_cast<u32>(outCompiledGraph.m_epilogueBarriers.size())
            - compiledTask.epilogueBarrierOffset
        ;
    }

    for(usize consumerPacketIndex = 0u; consumerPacketIndex < outCompiledGraph.m_packets.size(); ++consumerPacketIndex){
        GpuSubmissionPacket& consumerPacket = outCompiledGraph.m_packets[consumerPacketIndex];
        const GpuSubmissionPacketId consumerPacketID{
            static_cast<u32>(consumerPacketIndex),
            outCompiledGraph.m_generation,
        };
        consumerPacket.dependencyOffset = static_cast<u32>(outCompiledGraph.m_packetDependencies.size());
        const auto appendPacketDependency = [&](const GpuSubmissionPacketId producerPacket){
            if(producerPacket == consumerPacketID)
                return true;
            if(
                !producerPacket.valid()
                || producerPacket.index >= consumerPacketIndex
                || producerPacket.generation != outCompiledGraph.m_generation
            )
                return false;

            for(u32 dependencyIndex = 0u; dependencyIndex < consumerPacket.dependencyCount; ++dependencyIndex){
                const GpuPacketDependency& existing = outCompiledGraph.m_packetDependencies[
                    consumerPacket.dependencyOffset + dependencyIndex
                ];
                if(existing.producer == producerPacket)
                    return true;
            }

            outCompiledGraph.m_packetDependencies.push_back(GpuPacketDependency{
                .producer = producerPacket,
                .consumer = consumerPacketID,
            });
            ++consumerPacket.dependencyCount;
            return true;
        };
        consumerPacket.externalDependencyOffset = static_cast<u32>(outCompiledGraph.m_packetExternalDependencies.size());
        for(u32 taskIndex = 0u; taskIndex < consumerPacket.taskCount; ++taskIndex){
            const GpuTaskId consumerTask = outCompiledGraph.m_packetTasks[consumerPacket.taskOffset + taskIndex];
            const GpuCompiledTask* const compiledConsumerTask = outCompiledGraph.findTask(consumerTask);
            if(!compiledConsumerTask){
                outCompiledGraph.reset();
                return false;
            }

            for(const GpuTaskDependencyEdge& edge : outAnalysis.edges()){
                if(edge.consumer != consumerTask)
                    continue;

                const GpuSubmissionPacketId producerPacket = outCompiledGraph.packetForTask(edge.producer);
                if(!appendPacketDependency(producerPacket)){
                    outCompiledGraph.reset();
                    return false;
                }
            }

            const GpuPacketStateSeed* const stateSeeds = outCompiledGraph.taskPrologueStateSeeds(consumerTask);
            if(compiledConsumerTask->prologueStateSeedCount != 0u && !stateSeeds){
                outCompiledGraph.reset();
                return false;
            }
            for(u32 stateSeedIndex = 0u; stateSeedIndex < compiledConsumerTask->prologueStateSeedCount; ++stateSeedIndex){
                if(!appendPacketDependency(stateSeeds[stateSeedIndex].sourcePacket)){
                    outCompiledGraph.reset();
                    return false;
                }
            }

            for(const GpuTaskExternalDependencyEdge& edge : outAnalysis.externalDependencies()){
                if(edge.consumer != consumerTask)
                    continue;

                bool alreadyAdded = false;
                for(u32 dependencyIndex = 0u; dependencyIndex < consumerPacket.externalDependencyCount; ++dependencyIndex){
                    if(outCompiledGraph.m_packetExternalDependencies[
                        consumerPacket.externalDependencyOffset + dependencyIndex
                    ] == edge.completion){
                        alreadyAdded = true;
                        break;
                    }
                }
                if(alreadyAdded)
                    continue;

                outCompiledGraph.m_packetExternalDependencies.push_back(edge.completion);
                ++consumerPacket.externalDependencyCount;
            }
        }
    }

    outCompiledGraph.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

