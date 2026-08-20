// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>

#include <global/atomic.h>
#include <global/simplemath.h>
#include <global/timer.h>


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

// These weights intentionally grow by cost class rather than representing measured durations. They keep the
// deterministic least-loaded queue tie-breaker biased toward reserving a lane for larger graph tasks.
inline constexpr u64 s_TinyTaskQueueCostWeight = 1u;
inline constexpr u64 s_SmallTaskQueueCostWeight = 2u;
inline constexpr u64 s_MediumTaskQueueCostWeight = 4u;
inline constexpr u64 s_LargeTaskQueueCostWeight = 8u;

// Compilers are short-lived value objects, so packet identity must be allocated process-wide.  The graph
// generation remains the identity of declared task/resource handles; this distinct generation invalidates every
// packet-local artifact whenever a graph is compiled into a replacement immutable plan.
static Atomic<u64> s_NextCompiledPlanGeneration{ 1u };

[[nodiscard]] static u64 AllocateCompiledPlanGeneration()noexcept{
    u64 generation = s_NextCompiledPlanGeneration.fetch_add(1u, MemoryOrder::relaxed);
    if(generation == 0u)
        generation = s_NextCompiledPlanGeneration.fetch_add(1u, MemoryOrder::relaxed);
    return generation;
}

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
            const GpuPhysicalQueueInfo& previous = topology.queues[previousIndex];
            if(
                previous.id == queue.id
                // Queue IDs are graph-facing handles, but one family/index pair must still name exactly one native
                // transport. Otherwise same-class routing could manufacture distinct packet identities for the same
                // VkQueue and incorrectly turn ordinary queue order into a timeline edge.
                || (previous.familyIndex == queue.familyIndex && previous.queueIndex == queue.queueIndex)
            )
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

[[nodiscard]] static u64 QueueCostWeight(const GpuTaskCostHint::Enum cost)noexcept{
    switch(cost){
    case GpuTaskCostHint::Tiny: return s_TinyTaskQueueCostWeight;
    case GpuTaskCostHint::Small: return s_SmallTaskQueueCostWeight;
    case GpuTaskCostHint::Medium: return s_MediumTaskQueueCostWeight;
    case GpuTaskCostHint::Large: return s_LargeTaskQueueCostWeight;
    default: return 0u;
    }
}

// Physical queues in one Vulkan family can exchange work through a timeline semaphore without a queue-family
// ownership transfer. Cross-family balancing is deliberately separate: opted-in tasks may use it, and resource
// planning below then emits the paired exclusive ownership handoff when required.
[[nodiscard]] static const GpuPhysicalQueueInfo* FindLeastLoadedSameClassQueue(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const GpuQueueCapability::Mask requiredCapabilities,
    const bool allowCrossFamilyRouting,
    const bool preferNonPrimaryQueue
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    u64 resultLoad = Limit<u64>::s_Max;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass != baseQueue.queueClass
            || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex)
            || !HasCapabilities(candidate.capabilities, requiredCapabilities)
        )
            continue;

        u64 load = 0u;
        for(const GpuTaskId assignedTask : analysis.topologicalOrder()){
            const GpuTaskQueueAssignment* assignment = nullptr;
            for(const GpuTaskQueueAssignment& existingAssignment : assignments){
                if(existingAssignment.task == assignedTask){
                    assignment = &existingAssignment;
                    break;
                }
            }
            if(!assignment || assignment->queue != candidate.id)
                continue;
            const u64 cost = QueueCostWeight(graph.taskAt(assignedTask.index).scheduling.cost);
            load = load > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : load + cost;
        }
        const bool candidateIsNonPrimary = candidate.id != baseQueue.id;
        const bool resultIsNonPrimary = result && result->id != baseQueue.id;
        bool winsEqualLoad = IsBetterQueue(candidate, result);
        if(preferNonPrimaryQueue && candidateIsNonPrimary != resultIsNonPrimary)
            winsEqualLoad = candidateIsNonPrimary;
        if(
            !result
            || load < resultLoad
            || (load == resultLoad && winsEqualLoad)
        ){
            result = &candidate;
            resultLoad = load;
        }
    }
    return result;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindDirectDependencySameClassQueue(
    const GpuTaskGraphTaskView& task,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const GpuQueueCapability::Mask requiredCapabilities,
    const bool allowCrossFamilyRouting
)noexcept{
    for(usize dependencyIndex = task.dependencyCount; dependencyIndex > 0u; --dependencyIndex){
        const GpuTaskId dependency = task.dependencies[dependencyIndex - 1u];
        const GpuTaskQueueAssignment* assignment = nullptr;
        for(const GpuTaskQueueAssignment& candidateAssignment : assignments){
            if(candidateAssignment.task == dependency){
                assignment = &candidateAssignment;
                break;
            }
        }
        if(!assignment)
            continue;

        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
            if(
                candidate.id != assignment->queue
                || candidate.queueClass != baseQueue.queueClass
                || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex)
                || !HasCapabilities(candidate.capabilities, requiredCapabilities)
            )
                continue;
            return &candidate;
        }
    }
    return nullptr;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindPhysicalQueue(
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueId& queueID
)noexcept{
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(queue.id == queueID)
            return &queue;
    }
    return nullptr;
}

[[nodiscard]] static const GpuTaskQueueAssignment* FindQueueAssignment(
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskId& task
)noexcept{
    for(const GpuTaskQueueAssignment& assignment : assignments){
        if(assignment.task == task)
            return &assignment;
    }
    return nullptr;
}

[[nodiscard]] static bool AllowsTimingFeedbackRouting(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowTimingFeedbackRouting
        && task.scheduling.allowSameClassQueueRouting
        && task.scheduling.overlapPreferred
        && !task.scheduling.avoidQueueCrossing
    ;
}

// Adaptive routing deliberately stays narrower than ordinary same-class routing: an immutable timing snapshot may
// select a different physical transport only inside the already selected queue class and Vulkan family. Existing
// explicit cross-family policy remains available through task scheduling, but history can never manufacture a new
// ownership-transfer route.
[[nodiscard]] static bool IsLegalTimingFeedbackRoute(
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    return AllowsTimingFeedbackRouting(task)
        && candidate.queueClass == incumbent.queueClass
        && candidate.familyIndex == incumbent.familyIndex
        && HasCapabilities(candidate.capabilities, task.queue.requiredCapabilities)
    ;
}

[[nodiscard]] static bool HasUsableTimingFeedback(
    const GpuTaskGraphQueueAssignmentOptions& options,
    const u16 deviceGeneration
)noexcept{
    return options.timingFeedbackPolicy
        && options.timingFeedbackPolicy->enabled
        && options.timingFeedbackPolicy->valid()
        && options.timingHistory
        && options.timingHistory->valid()
        && options.timingHistory->deviceGeneration() == deviceGeneration
    ;
}

[[nodiscard]] static i32 SaturateQueueScoreTerm(const u64 value)noexcept{
    return value > static_cast<u64>(Limit<i32>::s_Max)
        ? Limit<i32>::s_Max
        : static_cast<i32>(value)
    ;
}

// Timing history decides whether a switch is worthwhile. This lightweight score only gives deterministic meaning to
// equal-duration candidates: lower existing graph load and fewer already-known incoming queue crossings win, with
// the physical queue ID remaining the final tie-breaker.
[[nodiscard]] static GpuQueueAssignmentScore BuildTimingFeedbackScore(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    GpuQueueAssignmentScore score;
    score.preference = candidate.id == incumbent.id ? 1 : 0;
    score.overlap = candidate.id != incumbent.id && task.scheduling.overlapPreferred ? 1 : 0;

    u64 queueLoad = 0u;
    for(const GpuTaskQueueAssignment& assignment : assignments){
        if(assignment.queue != candidate.id)
            continue;

        const u64 cost = QueueCostWeight(graph.taskAt(assignment.task.index).scheduling.cost);
        queueLoad = queueLoad > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : queueLoad + cost;
    }
    score.queueLoad = SaturateQueueScoreTerm(queueLoad);

    u64 incomingCrossings = 0u;
    for(const GpuTaskDependencyEdge& edge : analysis.edges()){
        if(edge.consumer != task.id)
            continue;

        const GpuTaskQueueAssignment* const producerAssignment = FindQueueAssignment(assignments, edge.producer);
        if(producerAssignment && producerAssignment->queue != candidate.id)
            ++incomingCrossings;
    }
    score.incomingCrossings = SaturateQueueScoreTerm(incomingCrossings);
    // Future consumer assignments are not yet immutable. Same-family routes never need a Vulkan ownership
    // transfer, so neither term participates in this intentionally local scoring pass.
    score.outgoingCrossings = 0;
    score.ownershipTransfers = 0;
    return score;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindTimingFeedbackQueue(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept{
    const GpuTaskTimingHistory* const incumbentHistory = historySnapshot.find(key, incumbent.id);
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!incumbentHistory || !assignmentState)
        return nullptr;

    const GpuPhysicalQueueInfo* result = nullptr;
    const GpuTaskTimingHistory* resultHistory = nullptr;
    GpuQueueAssignmentScore resultScore;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.id == incumbent.id || !IsLegalTimingFeedbackRoute(task, incumbent, candidate))
            continue;

        const GpuTaskTimingHistory* const candidateHistory = historySnapshot.find(key, candidate.id);
        if(
            !candidateHistory
            || !GpuTaskTimingFeedbackCanSwitch(
                *incumbentHistory,
                *candidateHistory,
                *assignmentState,
                incumbent.id,
                candidate.id,
                frameIndex,
                policy
            )
        )
            continue;

        const GpuQueueAssignmentScore candidateScore = BuildTimingFeedbackScore(
            graph,
            analysis,
            assignments,
            task,
            incumbent,
            candidate
        );
        if(
            !result
            || candidateHistory->averageSeconds < resultHistory->averageSeconds
            || (
                candidateHistory->averageSeconds == resultHistory->averageSeconds
                && (
                    candidateScore.total() > resultScore.total()
                    || (
                        candidateScore.total() == resultScore.total()
                        && IsBetterQueue(candidate, result)
                    )
                )
            )
        ){
            result = &candidate;
            resultHistory = candidateHistory;
            resultScore = candidateScore;
        }
    }
    return result;
}

// Calibration is deliberately bounded and narrower than adaptive selection. It only visits already-legal
// same-family routes until each has enough accepted samples, then ordinary hysteresis resumes. Returning the
// incumbent is meaningful: it reserves this frame for a baseline sample instead of switching on incomplete data.
[[nodiscard]] static const GpuPhysicalQueueInfo* FindTimingFeedbackCalibrationQueue(
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept{
    if(
        policy.calibrationIntervalFrames == 0u
        || frameIndex % policy.calibrationIntervalFrames != 0u
        || !AllowsTimingFeedbackRouting(task)
    )
        return nullptr;

    const auto sampleCountFor = [&](const GpuPhysicalQueueInfo& queue){
        const GpuTaskTimingHistory* const history = historySnapshot.find(key, queue.id);
        return history ? history->sampleCount : 0u;
    };

    const GpuPhysicalQueueInfo* result = &incumbent;
    u32 resultSampleCount = sampleCountFor(incumbent);
    bool needsCalibration = resultSampleCount < policy.minimumSampleCount;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.id == incumbent.id || !IsLegalTimingFeedbackRoute(task, incumbent, candidate))
            continue;

        const u32 candidateSampleCount = sampleCountFor(candidate);
        if(candidateSampleCount >= policy.minimumSampleCount)
            continue;

        needsCalibration = true;
        if(
            candidateSampleCount < resultSampleCount
            || (
                candidateSampleCount == resultSampleCount
                && IsBetterQueue(candidate, result)
            )
        ){
            result = &candidate;
            resultSampleCount = candidateSampleCount;
        }
    }
    return needsCalibration ? result : nullptr;
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

// Typed imports retain the physical Texture descriptor, so compile-time state planning must use the same finite
// subresource extent as native recording. Metadata-only texture declarations intentionally remain symbolic: their
// dimensions are not known until a later backend import.
[[nodiscard]] static bool ResolveTextureRangeForPlanning(
    const Texture* const texture,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept{
    outRange = range;
    if(!texture)
        return true;

    outRange.textureSubresources = range.textureSubresources.resolve(
        texture->getDescription(),
        TextureSubresourceMipResolve::Range
    );
    return IsValidTextureRange(outRange.textureSubresources);
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

[[nodiscard]] static const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraphResourceView& resource,
    const Texture* const texture,
    const GpuTaskResourceRange& firstUseRange,
    const GpuPhysicalQueueId& destinationQueue
)noexcept{
    if(
        resource.initialOwnerHandoffSourceCount == 0u
        || !resource.initialOwnerHandoffSources
    )
        return nullptr;

    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u;
        sourceIndex < resource.initialOwnerHandoffSourceCount;
        ++sourceIndex
    ){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        GpuTaskResourceRange plannedSourceRange;
        if(
            source.destinationQueue != destinationQueue
            || !ResolveTextureRangeForPlanning(texture, source.range, plannedSourceRange)
            || !RangeContains(resource, plannedSourceRange, firstUseRange)
        )
            continue;
        // One first graph use must have one exact external owner. A broader range that straddles two released mips
        // cannot safely select one state source or one completion token, so reject it rather than guessing.
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
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

// Texture state tracking is subresource-granular, but graph declarations can name rectangles that straddle several
// independently produced regions. Keep the interval endpoints symbolic so metadata-only graphs retain the same
// correct partition as typed textures whose physical mip/array bounds are only known at recording time.
struct TextureRangeBounds{
    u64 mipBegin = 0u;
    u64 mipEnd = 0u;
    u64 arrayBegin = 0u;
    u64 arrayEnd = 0u;
};

inline constexpr usize s_InvalidTrackedCompiledResourceStateIndex = Limit<usize>::s_Max;

struct TrackedTextureStateFragment{
    GpuTaskResourceRange range;
    const TrackedCompiledResourceState* state = nullptr;
    usize stateIndex = s_InvalidTrackedCompiledResourceStateIndex;
};

[[nodiscard]] static bool TextureRangeBoundsFrom(
    const GpuTaskResourceRange& range,
    TextureRangeBounds& outBounds
)noexcept{
    const TextureSubresourceSet& texture = range.textureSubresources;
    outBounds = TextureRangeBounds{
        .mipBegin = texture.baseMipLevel,
        .mipEnd = RangeEnd(texture.baseMipLevel, texture.numMipLevels, TextureSubresourceSet::AllMipLevels),
        .arrayBegin = texture.baseArraySlice,
        .arrayEnd = RangeEnd(texture.baseArraySlice, texture.numArraySlices, TextureSubresourceSet::AllArraySlices),
    };
    return outBounds.mipBegin < outBounds.mipEnd && outBounds.arrayBegin < outBounds.arrayEnd;
}

[[nodiscard]] static bool TextureRangeBoundsTo(
    const TextureRangeBounds& bounds,
    GpuTaskResourceRange& outRange
)noexcept{
    if(
        bounds.mipBegin >= bounds.mipEnd
        || bounds.arrayBegin >= bounds.arrayEnd
        || bounds.mipBegin > Limit<MipLevel>::s_Max
        || bounds.arrayBegin > Limit<ArraySlice>::s_Max
    )
        return false;

    const u64 mipCount = bounds.mipEnd == Limit<u64>::s_Max
        ? TextureSubresourceSet::AllMipLevels
        : bounds.mipEnd - bounds.mipBegin
    ;
    const u64 arrayCount = bounds.arrayEnd == Limit<u64>::s_Max
        ? TextureSubresourceSet::AllArraySlices
        : bounds.arrayEnd - bounds.arrayBegin
    ;
    if(
        mipCount == 0u
        || arrayCount == 0u
        || mipCount > Limit<MipLevel>::s_Max
        || arrayCount > Limit<ArraySlice>::s_Max
        || (bounds.mipEnd != Limit<u64>::s_Max && mipCount == TextureSubresourceSet::AllMipLevels)
        || (bounds.arrayEnd != Limit<u64>::s_Max && arrayCount == TextureSubresourceSet::AllArraySlices)
    )
        return false;

    outRange = GpuTaskResourceRange{
        .textureSubresources = TextureSubresourceSet{
            static_cast<MipLevel>(bounds.mipBegin),
            static_cast<MipLevel>(mipCount),
            static_cast<ArraySlice>(bounds.arrayBegin),
            static_cast<ArraySlice>(arrayCount),
        },
    };
    return true;
}

[[nodiscard]] static bool IntersectTextureRangeBounds(
    const TextureRangeBounds& lhs,
    const TextureRangeBounds& rhs,
    TextureRangeBounds& outIntersection
)noexcept{
    outIntersection = TextureRangeBounds{
        .mipBegin = lhs.mipBegin > rhs.mipBegin ? lhs.mipBegin : rhs.mipBegin,
        .mipEnd = lhs.mipEnd < rhs.mipEnd ? lhs.mipEnd : rhs.mipEnd,
        .arrayBegin = lhs.arrayBegin > rhs.arrayBegin ? lhs.arrayBegin : rhs.arrayBegin,
        .arrayEnd = lhs.arrayEnd < rhs.arrayEnd ? lhs.arrayEnd : rhs.arrayEnd,
    };
    return outIntersection.mipBegin < outIntersection.mipEnd
        && outIntersection.arrayBegin < outIntersection.arrayEnd
    ;
}

static void AppendTextureRangeRemainder(
    const TextureRangeBounds& outer,
    const TextureRangeBounds& cut,
    Vector<TextureRangeBounds, Alloc::ScratchArena>& outRanges
){
    TextureRangeBounds intersection;
    if(!IntersectTextureRangeBounds(outer, cut, intersection)){
        outRanges.push_back(outer);
        return;
    }

    if(outer.mipBegin < intersection.mipBegin){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = outer.mipBegin,
            .mipEnd = intersection.mipBegin,
            .arrayBegin = outer.arrayBegin,
            .arrayEnd = outer.arrayEnd,
        });
    }
    if(intersection.mipEnd < outer.mipEnd){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = intersection.mipEnd,
            .mipEnd = outer.mipEnd,
            .arrayBegin = outer.arrayBegin,
            .arrayEnd = outer.arrayEnd,
        });
    }
    if(outer.arrayBegin < intersection.arrayBegin){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = intersection.mipBegin,
            .mipEnd = intersection.mipEnd,
            .arrayBegin = outer.arrayBegin,
            .arrayEnd = intersection.arrayBegin,
        });
    }
    if(intersection.arrayEnd < outer.arrayEnd){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = intersection.mipBegin,
            .mipEnd = intersection.mipEnd,
            .arrayBegin = intersection.arrayEnd,
            .arrayEnd = outer.arrayEnd,
        });
    }
}

// One task owns transitions between its own commands, but only for subresources it already declared earlier in that
// task. A later overlapping range can also introduce previously untouched cells, which still need the graph's
// packet-boundary state source or declared initial state before native task recording begins.
[[nodiscard]] static bool CollectTextureFirstUseRangesWithinTask(
    const GpuTaskGraphTaskView& task,
    const usize useIndex,
    const GpuGraphResourceId& resource,
    const Texture* const texture,
    const GpuTaskResourceRange& range,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
){
    outRanges.clear();

    TextureRangeBounds requestedBounds;
    if(!TextureRangeBoundsFrom(range, requestedBounds))
        return false;

    Vector<TextureRangeBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    uncovered.push_back(requestedBounds);

    for(usize previousUseIndex = 0u; previousUseIndex < useIndex && !uncovered.empty(); ++previousUseIndex){
        const GpuTaskResourceUse& previousUse = task.resourceUses[previousUseIndex];
        if(previousUse.resource != resource)
            continue;

        GpuTaskResourceRange previousRange;
        if(!ResolveTextureRangeForPlanning(texture, previousUse.range, previousRange))
            return false;

        TextureRangeBounds previousBounds;
        if(!TextureRangeBoundsFrom(previousRange, previousBounds))
            return false;

        remainders.clear();
        for(const TextureRangeBounds& uncoveredRange : uncovered)
            AppendTextureRangeRemainder(uncoveredRange, previousBounds, remainders);

        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const TextureRangeBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    outRanges.reserve(uncovered.size());
    for(const TextureRangeBounds& uncoveredRange : uncovered){
        GpuTaskResourceRange firstUseRange;
        if(!TextureRangeBoundsTo(uncoveredRange, firstUseRange))
            return false;
        outRanges.push_back(firstUseRange);
    }
    return true;
}

static void AppendTextureStateFragmentsInStateOrder(
    const Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& discovered,
    const usize stateCount,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
){
    outFragments.clear();
    outFragments.reserve(discovered.size());
    for(usize stateIndex = 0u; stateIndex < stateCount; ++stateIndex){
        for(const TrackedTextureStateFragment& fragment : discovered){
            if(fragment.stateIndex == stateIndex)
                outFragments.push_back(fragment);
        }
    }
    for(const TrackedTextureStateFragment& fragment : discovered){
        if(!fragment.state)
            outFragments.push_back(fragment);
    }
}

// Walk newest-to-oldest and consume only still-uncovered portions of the requested ranges. A selected state
// therefore owns exactly the terminal cells that it actually produced; the remaining cells retain their declared
// graph initial state rather than inheriting an unrelated adjacent producer.
[[nodiscard]] static bool CollectLatestTextureStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuGraphResourceId& resource,
    const Vector<GpuTaskResourceRange, Alloc::ScratchArena>& requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
){
    Vector<TextureRangeBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena> discovered(scratchArena);
    for(const GpuTaskResourceRange& requestedRange : requestedRanges){
        TextureRangeBounds requestedBounds;
        if(!TextureRangeBoundsFrom(requestedRange, requestedBounds))
            return false;
        uncovered.push_back(requestedBounds);
    }

    for(usize stateIndex = trackedStates.size(); stateIndex > 0u && !uncovered.empty(); --stateIndex){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex - 1u];
        if(state.resource != resource)
            continue;

        TextureRangeBounds stateBounds;
        if(!TextureRangeBoundsFrom(state.range, stateBounds))
            return false;

        remainders.clear();
        for(const TextureRangeBounds& uncoveredRange : uncovered){
            TextureRangeBounds intersection;
            if(!IntersectTextureRangeBounds(uncoveredRange, stateBounds, intersection)){
                remainders.push_back(uncoveredRange);
                continue;
            }

            GpuTaskResourceRange fragmentRange;
            if(!TextureRangeBoundsTo(intersection, fragmentRange))
                return false;
            discovered.push_back(TrackedTextureStateFragment{
                .range = fragmentRange,
                .state = &state,
                .stateIndex = stateIndex - 1u,
            });
            AppendTextureRangeRemainder(uncoveredRange, intersection, remainders);
        }
        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const TextureRangeBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    for(const TextureRangeBounds& uncoveredRange : uncovered){
        GpuTaskResourceRange fragmentRange;
        if(!TextureRangeBoundsTo(uncoveredRange, fragmentRange))
            return false;
        discovered.push_back(TrackedTextureStateFragment{
            .range = fragmentRange,
        });
    }

    AppendTextureStateFragmentsInStateOrder(discovered, trackedStates.size(), outFragments);
    return true;
}

// Terminal graph-to-external exports have no one requested range. Subtract the union of every later declared
// texture state from each earlier range, leaving only the portions whose final state snapshot still belongs to that
// earlier task. This uses the same symbolic rectangle representation as inter-task consumer fan-in.
[[nodiscard]] static bool CollectTerminalTextureStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuGraphResourceId& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
){
    Vector<TextureRangeBounds, Alloc::ScratchArena> covered(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remaining(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena> discovered(scratchArena);

    for(usize stateIndex = trackedStates.size(); stateIndex > 0u; --stateIndex){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex - 1u];
        if(state.resource != resource)
            continue;

        TextureRangeBounds stateBounds;
        if(!TextureRangeBoundsFrom(state.range, stateBounds))
            return false;

        remaining.clear();
        remaining.push_back(stateBounds);
        for(const TextureRangeBounds& coveredRange : covered){
            remainders.clear();
            for(const TextureRangeBounds& remainingRange : remaining)
                AppendTextureRangeRemainder(remainingRange, coveredRange, remainders);
            remaining.clear();
            remaining.reserve(remainders.size());
            for(const TextureRangeBounds& remainder : remainders)
                remaining.push_back(remainder);
            if(remaining.empty())
                break;
        }

        for(const TextureRangeBounds& terminalRange : remaining){
            GpuTaskResourceRange fragmentRange;
            if(!TextureRangeBoundsTo(terminalRange, fragmentRange))
                return false;
            discovered.push_back(TrackedTextureStateFragment{
                .range = fragmentRange,
                .state = &state,
                .stateIndex = stateIndex - 1u,
            });
        }
        covered.push_back(stateBounds);
    }

    AppendTextureStateFragmentsInStateOrder(discovered, trackedStates.size(), outFragments);
    return true;
}

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

[[nodiscard]] static GpuCompiledBarrierType::Enum StateExportBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureStateExport;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferStateExport;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructStateExport;
    default:
        return GpuCompiledBarrierType::kCount;
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
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructOwnershipRelease;
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
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructOwnershipAcquire;
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
    m_declarationRevision = 0u;
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
        && m_declarationRevision == graph.declarationRevision()
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
    m_declarationRevision = 0u;
    m_taskCount = 0u;
    m_valid = false;
}

bool GpuTaskGraphQueueAssignments::validFor(const GpuTaskGraph& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_declarationRevision == graph.declarationRevision()
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
    outAnalysis.m_declarationRevision = graph.declarationRevision();
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
        if(task.externalStateSourceCount != 0u && !task.externalStateSources)
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
        for(usize sourceIndex = 0u; sourceIndex < task.externalStateSourceCount; ++sourceIndex){
            if(!task.externalStateSources[sourceIndex].states)
                return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
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

    if(const GpuPresentEndpoint* const endpoint = graph.presentEndpoint()){
        if(!graph.validTask(endpoint->producer) || !graph.validResource(endpoint->backBuffer))
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, {}, endpoint->backBuffer);

        const GpuTaskGraphTaskView producer = graph.taskAt(endpoint->producer.index);
        const GpuTaskGraphResourceView backBuffer = graph.resourceAt(endpoint->backBuffer.index);
        if(
            (backBuffer.type != GpuGraphResourceType::Texture && backBuffer.type != GpuGraphResourceType::HazardDomain)
            || !__hidden_gpu_task_graph_compiler::HasCapabilities(
                producer.queue.requiredCapabilities,
                GpuQueueCapability::Graphics
            )
        )
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, producer.id, {}, backBuffer.id);

        Vector<u8, Alloc::ScratchArena> reachesProducer(graph.taskCount(), scratchArena);
        for(usize taskIndex = 0u; taskIndex < reachesProducer.size(); ++taskIndex)
            reachesProducer[taskIndex] = 0u;
        reachesProducer[endpoint->producer.index] = 1u;
        for(usize orderIndex = outAnalysis.m_topologicalOrder.size(); orderIndex > 0u; --orderIndex){
            const GpuTaskId consumer = outAnalysis.m_topologicalOrder[orderIndex - 1u];
            if(!reachesProducer[consumer.index])
                continue;
            for(const GpuTaskDependencyEdge& edge : outAnalysis.m_edges){
                if(edge.consumer == consumer)
                    reachesProducer[edge.producer.index] = 1u;
            }
        }

        bool hasBackBufferWriter = false;
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                if(use.resource != endpoint->backBuffer || !__hidden_gpu_task_graph_compiler::IsWriteAccess(use.access))
                    continue;

                hasBackBufferWriter = true;
                if(!reachesProducer[task.id.index]){
                    return fail(
                        GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint,
                        endpoint->producer,
                        task.id,
                        endpoint->backBuffer
                    );
                }
            }
        }
        if(!hasBackBufferWriter)
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, {}, endpoint->backBuffer);
    }

    outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::Success;
    outAnalysis.m_valid = true;
    return true;
}


bool GpuTaskGraphCompiler::assignQueues(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueTopology& topology,
    GpuTaskGraphQueueAssignments& outAssignments,
    const GpuTaskGraphQueueAssignmentOptions& options
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

    if(
        options.timingFeedbackPolicy
        && options.timingFeedbackPolicy->enabled
        && !options.timingFeedbackPolicy->valid()
    )
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback);

    if(ValidateGpuTaskTimingQueueOverrides(
        options.timingQueueOverrides,
        options.timingQueueOverrideCount,
        topology.queues[0u].id.deviceGeneration
    ) != GpuTaskTimingQueueOverrideStatus::Success)
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback);

    outAssignments.m_generation = graph.generation();
    outAssignments.m_declarationRevision = graph.declarationRevision();
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

        const GpuPhysicalQueueInfo* const initiallySelectedQueue = selectedQueue;
        if(
            task.scheduling.allowSameClassQueueRouting
            && task.scheduling.overlapPreferred
            && !task.scheduling.avoidQueueCrossing
        ){
            const GpuPhysicalQueueInfo* const dependencyQueue =
                task.scheduling.preserveSameClassQueueWithDirectDependency
                    ? FindDirectDependencySameClassQueue(
                        task,
                        outAssignments.m_assignments,
                        topology,
                        *selectedQueue,
                        task.queue.requiredCapabilities,
                        task.scheduling.allowCrossFamilySameClassQueueRouting
                    )
                    : nullptr
            ;
            if(dependencyQueue){
                selectedQueue = dependencyQueue;
            }
            else if(const GpuPhysicalQueueInfo* const balancedQueue = FindLeastLoadedSameClassQueue(
                graph,
                analysis,
                outAssignments.m_assignments,
                topology,
                *selectedQueue,
                task.queue.requiredCapabilities,
                task.scheduling.allowCrossFamilySameClassQueueRouting,
                task.scheduling.preferNonPrimarySameClassQueue
            )
            )
                selectedQueue = balancedQueue;
        }
        if(selectedQueue != initiallySelectedQueue)
            reason = GpuTaskQueueAssignmentReason::SameClassRouting;

        const GpuTaskTimingKey timingKey{
            .task = task.identity,
            .variant = task.timing.variant,
            .resolutionClass = task.timing.resolutionClass,
            .queue = selectedQueue->queueClass,
        };
        const GpuTaskTimingQueueOverride* const timingOverride = FindGpuTaskTimingQueueOverride(
            options.timingQueueOverrides,
            options.timingQueueOverrideCount,
            timingKey
        );
        if(timingOverride){
            const GpuPhysicalQueueInfo* const forcedQueue = FindPhysicalQueue(topology, timingOverride->queue);
            if(!forcedQueue || !IsLegalTimingFeedbackRoute(task, *selectedQueue, *forcedQueue)){
                return fail(
                    GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback,
                    task.id,
                    task.queue.requiredCapabilities
                );
            }
            if(forcedQueue != selectedQueue){
                selectedQueue = forcedQueue;
                reason = GpuTaskQueueAssignmentReason::SameClassRouting;
            }
        }
        else if(HasUsableTimingFeedback(options, topology.queues[0u].id.deviceGeneration)){
            const GpuPhysicalQueueInfo* const calibrationQueue = FindTimingFeedbackCalibrationQueue(
                topology,
                task,
                *selectedQueue,
                timingKey,
                *options.timingHistory,
                *options.timingFeedbackPolicy,
                options.timingFrameIndex
            );
            if(calibrationQueue){
                if(calibrationQueue != selectedQueue){
                    selectedQueue = calibrationQueue;
                    reason = GpuTaskQueueAssignmentReason::SameClassRouting;
                }
            }
            else{
                const GpuPhysicalQueueInfo* const timingQueue = FindTimingFeedbackQueue(
                    graph,
                    analysis,
                    outAssignments.m_assignments,
                    topology,
                    task,
                    *selectedQueue,
                    timingKey,
                    *options.timingHistory,
                    *options.timingFeedbackPolicy,
                    options.timingFrameIndex
                );
                if(timingQueue){
                    selectedQueue = timingQueue;
                    reason = GpuTaskQueueAssignmentReason::SameClassRouting;
                }
            }
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
    const Timer compileBegin = TimerNow();
    const Timer analysisBegin = compileBegin;
    if(!analyze(graph, outAnalysis, scratchArena))
        return false;
    const f64 analysisSeconds = DurationInSeconds<f64>(TimerNow(), analysisBegin);

    if(!options.allowMetadataOnlyTasks){
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            if(!task.hasPayload || !task.hasRecordPayload){
                outAssignments.reset();
                outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::MissingTaskRecordPayload;
                outAnalysis.m_diagnostic.task = task.id;
                outAnalysis.m_diagnostic.relatedTask = {};
                outAnalysis.m_diagnostic.resource = {};
                outAnalysis.m_valid = false;
                return false;
            }

            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
                switch(resource.type){
                case GpuGraphResourceType::Texture:
                    if(graph.textureForResource(use.resource))
                        continue;
                    break;
                case GpuGraphResourceType::Buffer:
                    if(graph.bufferForResource(use.resource))
                        continue;
                    break;
                case GpuGraphResourceType::AccelStruct:
                    if(graph.accelStructForResource(use.resource))
                        continue;
                    break;
                case GpuGraphResourceType::HazardDomain:
                    continue;
                default:
                    break;
                }

                outAssignments.reset();
                outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::InvalidResourceUse;
                outAnalysis.m_diagnostic.task = task.id;
                outAnalysis.m_diagnostic.relatedTask = {};
                outAnalysis.m_diagnostic.resource = use.resource;
                outAnalysis.m_valid = false;
                return false;
            }
        }
    }

    const Timer queueAssignmentBegin = TimerNow();
    if(!assignQueues(graph, outAnalysis, topology, outAssignments, options.queueAssignmentOptions))
        return false;
    const f64 queueAssignmentSeconds = DurationInSeconds<f64>(TimerNow(), queueAssignmentBegin);
    const Timer planningBegin = TimerNow();

    if(
        topology.queueCount == 0u
        || !topology.queues
        || options.packetizationPolicy >= GpuTaskGraphPacketizationPolicy::kCount
        || !graph.validForDeviceGeneration(topology.queues[0].id.deviceGeneration)
    )
        return false;

    outCompiledGraph.m_generation = graph.generation();
    outCompiledGraph.m_declarationRevision = graph.declarationRevision();
    outCompiledGraph.m_planGeneration = AllocateCompiledPlanGeneration();
    outCompiledGraph.m_deviceGeneration = topology.queues[0].id.deviceGeneration;
    outCompiledGraph.m_graphTaskCount = graph.taskCount();
    outCompiledGraph.m_tasks.reserve(graph.taskCount());
    outCompiledGraph.m_packets.reserve(graph.taskCount());
    outCompiledGraph.m_packetTasks.reserve(graph.taskCount());
    outCompiledGraph.m_prologueStateSeeds.reserve(graph.taskCount());
    outCompiledGraph.m_prologueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_epilogueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_externalResourceExports.reserve(graph.resourceCount());
    outCompiledGraph.m_externalResourceExportSources.reserve(graph.resourceCount());
    outCompiledGraph.m_queueTopology.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        outCompiledGraph.m_queueTopology.push_back(topology.queues[queueIndex]);

    // An import may name the exact physical queue that owned an exclusive texture/buffer/acceleration structure
    // before graph work began.
    // A different first packet must also name a fixed release destination, an imported completion, and a native
    // state handoff source. That keeps the compiler from manufacturing an acquire or cross-queue wait on its own.
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        const bool hasExternalFinalRelease = resource.externalFinalReleaseDestinationQueue.valid();
        if(
            hasExternalFinalRelease
            && (
                resource.externalFinalState == ResourceStates::Unknown
                || ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
                || !outCompiledGraph.queueInfo(resource.externalFinalReleaseDestinationQueue)
            )
        ){
            outCompiledGraph.reset();
            return false;
        }
        if(resource.initialOwnerHandoffSourceCount != 0u){
            const Texture* const typedTexture = graph.textureForResource(resource.id);
            if(
                resource.type != GpuGraphResourceType::Texture
                || !resource.initialOwnerHandoffSources
                || ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
            ){
                outCompiledGraph.reset();
                return false;
            }
            for(usize sourceIndex = 0u;
                sourceIndex < resource.initialOwnerHandoffSourceCount;
                ++sourceIndex
            ){
                const GpuTaskGraphInitialOwnerHandoffSourceView& source =
                    resource.initialOwnerHandoffSources[sourceIndex]
                ;
                GpuTaskResourceRange plannedSourceRange;
                const GpuPhysicalQueueInfo* const sourceQueueInfo = outCompiledGraph.queueInfo(source.sourceQueue);
                if(
                    !ResolveTextureRangeForPlanning(typedTexture, source.range, plannedSourceRange)
                    || !source.sourceQueue.valid()
                    || !source.destinationQueue.valid()
                    || !sourceQueueInfo
                    || !outCompiledGraph.queueInfo(source.destinationQueue)
                    || !graph.validExternalCompletion(source.completion)
                    || !source.minimumCompletionToken.valid()
                    || source.minimumCompletionToken.queue != sourceQueueInfo->queueClass
                    || !source.minimumCompletionToken.matchesPhysicalQueue(
                        source.sourceQueue.index,
                        source.sourceQueue.deviceGeneration
                    )
                    || !source.stateSource
                ){
                    outCompiledGraph.reset();
                    return false;
                }
            }
            continue;
        }
        if(!resource.initialOwnerQueue.valid())
            continue;
        const bool hasInitialOwnerHandoff = resource.initialOwnerReleaseDestinationQueue.valid();
        const GpuPhysicalQueueInfo* const initialOwnerQueueInfo = outCompiledGraph.queueInfo(resource.initialOwnerQueue);
        if(
            ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
            || !initialOwnerQueueInfo
            || (
                hasInitialOwnerHandoff
                && (
                    !outCompiledGraph.queueInfo(resource.initialOwnerReleaseDestinationQueue)
                    || !graph.validExternalCompletion(resource.initialOwnerCompletion)
                    || !resource.initialOwnerMinimumCompletionToken.valid()
                    || resource.initialOwnerMinimumCompletionToken.queue != initialOwnerQueueInfo->queueClass
                    || !resource.initialOwnerMinimumCompletionToken.matchesPhysicalQueue(
                        resource.initialOwnerQueue.index,
                        resource.initialOwnerQueue.deviceGeneration
                    )
                    || !resource.initialOwnerStateSource
                )
            )
        ){
            outCompiledGraph.reset();
            return false;
        }
    }

    const Timer packetizationBegin = TimerNow();
    // Tasks retain one exact acceptance and synchronization point by default. An explicitly opted-in successor may
    // share its immediately preceding compatible packet, preserving task order while retaining one submission for a
    // temporary imported/native recording bridge. The separate FrontierScored policy is deliberately opt-in too:
    // it only absorbs a cheap immediate successor after proving that the preceding packet has no cross-queue signal
    // frontier. This keeps current renderer packet boundaries stable while the generic compiler can reduce safe
    // one-task submission overhead for new callers.
    for(const GpuTaskId taskID : outAnalysis.topologicalOrder()){
        const GpuTaskQueueAssignment* const assignment = outAssignments.find(taskID);
        if(!assignment || !assignment->queue.valid()){
            outCompiledGraph.reset();
            return false;
        }

        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        GpuSubmissionPacketId packetID;
        GpuTaskPacketizationDecision::Enum packetizationDecision = outCompiledGraph.m_packets.empty()
            ? GpuTaskPacketizationDecision::FirstTask
            : GpuTaskPacketizationDecision::MergeNotRequested
        ;
        const bool scoredMergeRequested =
            options.packetizationPolicy == GpuTaskGraphPacketizationPolicy::FrontierScored
            && !task.scheduling.mergeWithPrevious
        ;
        const bool mergeRequested =
            (task.scheduling.mergeWithPrevious || scoredMergeRequested)
            && task.scheduling.allowPacketMerge
            && !task.scheduling.forceSubmissionBoundary
            && !task.scheduling.joinsAcceptedQueueFrontier
            && !outCompiledGraph.m_packets.empty()
        ;
        if(
            !outCompiledGraph.m_packets.empty()
            && (
                !task.scheduling.allowPacketMerge
                || task.scheduling.forceSubmissionBoundary
                || task.scheduling.joinsAcceptedQueueFrontier
            )
        )
            packetizationDecision = GpuTaskPacketizationDecision::TaskForcesBoundary;
        if(mergeRequested){
            GpuSubmissionPacket& precedingPacket = outCompiledGraph.m_packets.back();
            bool precedingPacketAllowsMerge = precedingPacket.queue == assignment->queue;
            if(!precedingPacketAllowsMerge)
                packetizationDecision = GpuTaskPacketizationDecision::QueueChanged;
            for(u32 precedingTaskIndex = 0u;
                precedingPacketAllowsMerge && precedingTaskIndex < precedingPacket.taskCount;
                ++precedingTaskIndex
            ){
                const GpuTaskId precedingTask = outCompiledGraph.m_packetTasks[
                    precedingPacket.taskOffset + precedingTaskIndex
                ];
                const GpuTaskGraphTaskView preceding = graph.taskAt(precedingTask.index);
                const bool precedingTaskAllowsMerge = preceding.scheduling.allowPacketMerge
                    && !preceding.scheduling.forceSubmissionBoundary
                    && !preceding.scheduling.joinsAcceptedQueueFrontier
                ;
                if(!precedingTaskAllowsMerge)
                    packetizationDecision = GpuTaskPacketizationDecision::PrecedingTaskForcesBoundary;
                precedingPacketAllowsMerge = precedingTaskAllowsMerge;
            }
            if(precedingPacketAllowsMerge && scoredMergeRequested){
                const Name& frontierScoredMergeDomain = task.scheduling.frontierScoredMergeDomain;
                bool precedingPacketMatchesScoredMergeDomain = static_cast<bool>(frontierScoredMergeDomain);
                for(u32 precedingTaskIndex = 0u;
                    precedingPacketMatchesScoredMergeDomain && precedingTaskIndex < precedingPacket.taskCount;
                    ++precedingTaskIndex
                ){
                    const GpuTaskId precedingTask = outCompiledGraph.m_packetTasks[
                        precedingPacket.taskOffset + precedingTaskIndex
                    ];
                    const GpuTaskGraphTaskView preceding = graph.taskAt(precedingTask.index);
                    precedingPacketMatchesScoredMergeDomain =
                        static_cast<bool>(preceding.scheduling.frontierScoredMergeDomain)
                        && preceding.scheduling.frontierScoredMergeDomain == frontierScoredMergeDomain
                    ;
                }
                if(!precedingPacketMatchesScoredMergeDomain)
                    packetizationDecision = GpuTaskPacketizationDecision::ScoredMergeDomainMismatch;
                precedingPacketAllowsMerge = precedingPacketMatchesScoredMergeDomain;
            }
            if(precedingPacketAllowsMerge && scoredMergeRequested){
                // A score is useful only for an actual immediate serial chain. Never use packet coalescing to
                // manufacture an order between unrelated work, and keep Medium/Large work independently
                // accept/recoverable unless its owner deliberately asks for an explicit merge.
                const GpuTaskId precedingTask = outCompiledGraph.m_packetTasks[
                    precedingPacket.taskOffset + precedingPacket.taskCount - 1u
                ];
                bool directlyDependsOnPrecedingTask = false;
                for(const GpuTaskDependencyEdge& edge : outAnalysis.edges()){
                    if(edge.producer == precedingTask && edge.consumer == taskID){
                        directlyDependsOnPrecedingTask = true;
                        break;
                    }
                }
                const bool eligibleScoredMerge = directlyDependsOnPrecedingTask
                    && task.scheduling.cost <= GpuTaskCostHint::Small
                    && !task.scheduling.allowMergeAcrossConsumerFrontier
                ;
                if(!eligibleScoredMerge)
                    packetizationDecision = GpuTaskPacketizationDecision::ScoredMergeIneligible;
                precedingPacketAllowsMerge = eligibleScoredMerge;
            }
            if(precedingPacketAllowsMerge && task.scheduling.allowMergeAcrossConsumerFrontier){
                // This narrow opt-in is only valid for an explicit immediate successor. It must not let an
                // unrelated same-queue task quietly absorb an already-signallable cross-queue frontier.
                const GpuTaskId precedingTask = outCompiledGraph.m_packetTasks[
                    precedingPacket.taskOffset + precedingPacket.taskCount - 1u
                ];
                bool explicitlyDependsOnPrecedingTask = false;
                for(usize dependencyIndex = 0u;
                    dependencyIndex < task.dependencyCount;
                    ++dependencyIndex
                ){
                    explicitlyDependsOnPrecedingTask = explicitlyDependsOnPrecedingTask
                        || task.dependencies[dependencyIndex] == precedingTask
                    ;
                }
                if(!explicitlyDependsOnPrecedingTask)
                    packetizationDecision = GpuTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency;
                precedingPacketAllowsMerge = explicitlyDependsOnPrecedingTask;
            }
            if(
                precedingPacketAllowsMerge
                && (
                    options.packetizationPolicy == GpuTaskGraphPacketizationPolicy::FrontierSafe
                    || options.packetizationPolicy == GpuTaskGraphPacketizationPolicy::FrontierScored
                )
                && !task.scheduling.allowMergeAcrossConsumerFrontier
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
                            packetizationDecision = GpuTaskPacketizationDecision::CrossQueueConsumerFrontier;
                            precedingPacketAllowsMerge = false;
                            break;
                        }
                    }
                }
            }
            if(precedingPacketAllowsMerge){
                packetID = GpuSubmissionPacketId{
                    static_cast<u32>(outCompiledGraph.m_packets.size() - 1u),
                    outCompiledGraph.m_planGeneration,
                };
                ++precedingPacket.taskCount;
                packetizationDecision = scoredMergeRequested
                    ? GpuTaskPacketizationDecision::MergedFrontierScored
                    : GpuTaskPacketizationDecision::MergedExplicit
                ;
            }
        }

        if(!packetID.valid()){
            packetID = GpuSubmissionPacketId{
                static_cast<u32>(outCompiledGraph.m_packets.size()),
                outCompiledGraph.m_planGeneration,
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
            .packetizationDecision = packetizationDecision,
        });
    }
    const f64 packetizationSeconds = DurationInSeconds<f64>(TimerNow(), packetizationBegin);

    const Timer resourceStatePlanningBegin = TimerNow();
    // Start with graph-planned packet state seeds, transitions, UAV dependencies, and exclusive-family ownership
    // releases. A state seed selects the actual final-state snapshot of a graph-internal producer, so it also carries
    // the release destination into CommandList::open where the paired Vulkan acquire is emitted before the consumer.
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena> trackedResourceStates(scratchArena);
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena> pendingEpilogueBarriers(scratchArena);
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena> initialOwnershipDependencies(scratchArena);
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena> stateFragments(scratchArena);
    Vector<GpuTaskResourceRange, Alloc::ScratchArena> taskFirstUseRanges(scratchArena);
    trackedResourceStates.reserve(graph.taskCount());
    pendingEpilogueBarriers.reserve(graph.taskCount());
    initialOwnershipDependencies.reserve(graph.taskCount());
    stateFragments.reserve(graph.taskCount());
    taskFirstUseRanges.reserve(graph.taskCount());
    // The construction pass appended one compiled task for each entry in this stable order, and no later phase
    // mutates that vector. Preserve the prior fail-closed contract while avoiding a re-scan for every task.
    const GraphicsVector<GpuTaskId>& topologicalOrder = outAnalysis.topologicalOrder();
    for(usize taskIndex = 0u; taskIndex < topologicalOrder.size(); ++taskIndex){
        const GpuTaskId taskID = topologicalOrder[taskIndex];
        if(
            taskIndex >= outCompiledGraph.m_tasks.size()
            || outCompiledGraph.m_tasks[taskIndex].task != taskID
        ){
            outCompiledGraph.reset();
            return false;
        }
        GpuCompiledTask* const compiledTask = &outCompiledGraph.m_tasks[taskIndex];

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
                (
                    resource.type == GpuGraphResourceType::Texture
                    || resource.type == GpuGraphResourceType::Buffer
                    || resource.type == GpuGraphResourceType::AccelStruct
                )
                && ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
                && !ResourceSharingIncludesQueueClass(resource.queueSharing, taskQueue->queueClass)
            ){
                // A Vulkan-concurrent resource may only be used by the families named when it was created. Do not
                // turn a missing Transfer bit into an illegal ownership handoff for a concurrent image/buffer.
                outCompiledGraph.reset();
                return false;
            }

            const Texture* const typedTexture = resource.type == GpuGraphResourceType::Texture
                ? graph.textureForResource(use.resource)
                : nullptr
            ;
            GpuTaskResourceRange plannedRange = use.range;
            if(
                resource.type == GpuGraphResourceType::Texture
                && !ResolveTextureRangeForPlanning(typedTexture, use.range, plannedRange)
            ){
                outCompiledGraph.reset();
                return false;
            }

            // A task owns its internal resource ordering. Texture portions already declared earlier in this task
            // remain local CommandList work, while newly introduced subresources still receive normal graph seeds
            // and transitions. Buffers and acceleration structures retain their intentionally whole-resource path.
            bool alreadyPlannedByTask = false;
            if(resource.type != GpuGraphResourceType::Texture){
                for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                    const GpuTaskResourceUse& previousUse = task.resourceUses[previousUseIndex];
                    if(previousUse.resource != use.resource)
                        continue;

                    if(RangesOverlap(resource, previousUse.range, plannedRange)){
                        alreadyPlannedByTask = true;
                        break;
                    }
                }
            }
            if(alreadyPlannedByTask){
                // The task thunk owns this local transition, but the declared final state/access must still become
                // the source for later tasks, packet seeds, ownership handoffs, and terminal exports.  Do not emit
                // another packet-boundary barrier for it here.
                trackedResourceStates.push_back(TrackedCompiledResourceState{
                    .resource = use.resource,
                    .range = plannedRange,
                    .state = use.requiredState,
                    .access = use.access,
                    .task = taskID,
                    .queue = compiledTask->queue,
                });
                continue;
            }

            if(resource.type == GpuGraphResourceType::Texture){
                if(!CollectTextureFirstUseRangesWithinTask(
                    task,
                    useIndex,
                    use.resource,
                    typedTexture,
                    plannedRange,
                    scratchArena,
                    taskFirstUseRanges
                )){
                    outCompiledGraph.reset();
                    return false;
                }
                stateFragments.clear();
                if(!CollectLatestTextureStateFragments(
                    trackedResourceStates,
                    use.resource,
                    taskFirstUseRanges,
                    scratchArena,
                    stateFragments
                )){
                    outCompiledGraph.reset();
                    return false;
                }

                for(const TrackedTextureStateFragment& fragment : stateFragments){
                    const TrackedCompiledResourceState* const previousState = fragment.state;
                    const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSource = nullptr;
                    bool usesInitialOwnerOnlyHandoff = false;
                    GpuCompiledBarrierType::Enum initialOwnerAcquireType = GpuCompiledBarrierType::kCount;
                    if(!previousState){
                        if(
                            resource.initialOwnerReleaseDestinationQueue.valid()
                            && resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
                        ){
                            // The descriptor's release destination stays authoritative. Resolve ownership only for
                            // the exact first-use fragment so an earlier local task use cannot broaden the source.
                            outCompiledGraph.reset();
                            return false;
                        }
                        if(resource.initialOwnerHandoffSourceCount != 0u){
                            initialOwnerHandoffSource = FindInitialOwnerHandoffSource(
                                resource,
                                typedTexture,
                                fragment.range,
                                compiledTask->queue
                            );
                            if(!initialOwnerHandoffSource){
                                outCompiledGraph.reset();
                                return false;
                            }
                            initialOwnerAcquireType = OwnershipAcquireBarrierType(resource.type);
                            if(initialOwnerAcquireType >= GpuCompiledBarrierType::kCount){
                                outCompiledGraph.reset();
                                return false;
                            }
                            initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                                .completion = initialOwnerHandoffSource->completion,
                                .consumer = taskID,
                            });
                        }
                        else if(
                            resource.initialOwnerQueue.valid()
                            && resource.initialOwnerQueue != compiledTask->queue
                        ){
                            if(
                                !resource.initialOwnerReleaseDestinationQueue.valid()
                                || resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
                                || !resource.initialOwnerCompletion.valid()
                                || !resource.initialOwnerStateSource
                            ){
                                // Owner-only imports retain the original exact-queue restriction. A different first
                                // consumer needs all three explicit pieces of an external handoff: fixed destination,
                                // completion, and exported native state source.
                                outCompiledGraph.reset();
                                return false;
                            }
                            initialOwnerAcquireType = OwnershipAcquireBarrierType(resource.type);
                            if(initialOwnerAcquireType >= GpuCompiledBarrierType::kCount){
                                outCompiledGraph.reset();
                                return false;
                            }
                            usesInitialOwnerOnlyHandoff = true;
                            initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                                .completion = resource.initialOwnerCompletion,
                                .consumer = taskID,
                            });
                        }
                    }
                    const ResourceStates::Mask before = previousState ? previousState->state : resource.initialState;
                    const bool hasInitialOwnerStateSeed =
                        !previousState
                        && (initialOwnerHandoffSource != nullptr || usesInitialOwnerOnlyHandoff)
                    ;
                    // An initial-owner handoff opens the packet with its immutable producer snapshot, so that
                    // snapshot already materializes the native starting state. Ordinary initial fragments still
                    // need an explicit marker: their declared state can differ from creation metadata and can be
                    // a no-op transition whose state must survive into the packet snapshot.
                    const bool materializesGraphInitialState =
                        !previousState
                        && !hasInitialOwnerStateSeed
                        && resource.initialState != ResourceStates::Unknown
                    ;
                    const bool requiresExplicitInitialStateSource =
                        !previousState
                        && !hasInitialOwnerStateSeed
                        && resource.hasBackendResource
                        && resource.initialState == ResourceStates::Unknown
                        && IsReadAccess(use.access)
                    ;
                    if(!previousState && initialOwnerHandoffSource){
                        // The descriptor's one selected source has already been proven to cover this fragment.
                        // Emit only the unseeded fragment range so its immutable snapshot does not
                        // overwrite a graph-internal producer's adjacent subresources during packet fan-in.
                        outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = initialOwnerHandoffSource->sourceQueue,
                            .destinationQueue = compiledTask->queue,
                            .type = initialOwnerAcquireType,
                            .isInitialOwnerHandoff = true,
                        });
                    }
                    else if(!previousState && usesInitialOwnerOnlyHandoff){
                        // This marker imports the descriptor-owned snapshot before normal graph state fragments,
                        // allowing CommandList::open to emit the paired acquire only for the uncovered range.
                        outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = resource.initialOwnerQueue,
                            .destinationQueue = compiledTask->queue,
                            .type = initialOwnerAcquireType,
                            .isInitialOwnerHandoff = true,
                        });
                    }

                    const bool needsUavDependency =
                        previousState
                        && before == use.requiredState
                        && ResourceStates::HasUnorderedAccess(before)
                        && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access))
                    ;
                    if(previousState){
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
                                // The resource was created concurrent for another family set. Vulkan cannot
                                // transfer ownership to an omitted family, so fail compilation instead of
                                // recording invalid work.
                                outCompiledGraph.reset();
                                return false;
                            }

                            const bool requiresExclusiveOwnershipHandoff =
                                !resourceUsesConcurrentSharing
                                && differentQueueFamilies
                            ;
                            if(requiresExclusiveOwnershipHandoff){
                                const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                                const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                                if(
                                    releaseType >= GpuCompiledBarrierType::kCount
                                    || acquireType >= GpuCompiledBarrierType::kCount
                                ){
                                    outCompiledGraph.reset();
                                    return false;
                                }
                                pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                                    .task = previousState->task,
                                    .barrier = GpuCompiledBarrier{
                                        .resource = use.resource,
                                        .range = fragment.range,
                                        .before = before,
                                        .after = before,
                                        .sourceQueue = previousState->queue,
                                        .destinationQueue = compiledTask->queue,
                                        .type = releaseType,
                                    },
                                });
                                outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                                    .resource = use.resource,
                                    .range = fragment.range,
                                    .before = before,
                                    .after = before,
                                    .sourceQueue = previousState->queue,
                                    .destinationQueue = compiledTask->queue,
                                    .type = acquireType,
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
                                    .range = fragment.range,
                                    .sourcePacket = sourcePacket,
                                });
                            }
                        }
                    }
                    if(materializesGraphInitialState || before != use.requiredState || needsUavDependency){
                        outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = before,
                            .after = use.requiredState,
                            .sourceQueue = previousState ? previousState->queue : compiledTask->queue,
                            .destinationQueue = compiledTask->queue,
                            .type = needsUavDependency
                                ? UavBarrierType(resource.type)
                                : TransitionBarrierType(resource.type),
                            .isGraphInitialState = materializesGraphInitialState || requiresExplicitInitialStateSource,
                        });
                    }
                }

                trackedResourceStates.push_back(TrackedCompiledResourceState{
                    .resource = use.resource,
                    .range = plannedRange,
                    .state = use.requiredState,
                    .access = use.access,
                    .task = taskID,
                    .queue = compiledTask->queue,
                });
                continue;
            }

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
            // An initial-owner handoff opens the packet with its immutable producer snapshot, so that snapshot
            // already materializes the native starting state. Ordinary first uses still need an explicit marker:
            // the graph declaration may differ from the resource's creation metadata and can also be a no-op
            // transition whose state must survive into the packet snapshot.
            const bool hasInitialOwnerStateSeed =
                !previousState
                && (
                    resource.initialOwnerHandoffSourceCount != 0u
                    || (
                        resource.initialOwnerReleaseDestinationQueue.valid()
                        && resource.initialOwnerStateSource != nullptr
                    )
                )
            ;
            const bool materializesGraphInitialState =
                !previousState
                && !hasInitialOwnerStateSeed
                && resource.initialState != ResourceStates::Unknown
            ;
            const bool requiresExplicitInitialStateSource =
                !previousState
                && !hasInitialOwnerStateSeed
                && resource.hasBackendResource
                && resource.initialState == ResourceStates::Unknown
                && IsReadAccess(use.access)
            ;
            if(
                !previousState
                && resource.initialOwnerReleaseDestinationQueue.valid()
                && resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
            ){
                // A producer that already released ownership has relinquished it even when the source happens to
                // be this task's broad queue class. Its fixed release destination remains authoritative.
                outCompiledGraph.reset();
                return false;
            }
            if(!previousState && resource.initialOwnerHandoffSourceCount != 0u){
                const GpuTaskGraphInitialOwnerHandoffSourceView* const source = FindInitialOwnerHandoffSource(
                    resource,
                    nullptr,
                    use.range,
                    compiledTask->queue
                );
                if(!source){
                    outCompiledGraph.reset();
                    return false;
                }
                const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                if(acquireType >= GpuCompiledBarrierType::kCount){
                    outCompiledGraph.reset();
                    return false;
                }
                // The packet recorder imports the exact immutable source range before prologue lowering. This marker
                // keeps one source owner and one completion bound to every first consumer range, including a
                // same-physical-queue source whose timeline wait still proves the external producer accepted.
                outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = use.range,
                    .before = before,
                    .after = before,
                    .sourceQueue = source->sourceQueue,
                    .destinationQueue = compiledTask->queue,
                    .type = acquireType,
                    .isInitialOwnerHandoff = true,
                });
                initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                    .completion = source->completion,
                    .consumer = taskID,
                });
            }
            else if(!previousState && resource.initialOwnerQueue.valid() && resource.initialOwnerQueue != compiledTask->queue){
                if(
                    !resource.initialOwnerReleaseDestinationQueue.valid()
                    || resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
                    || !resource.initialOwnerCompletion.valid()
                    || !resource.initialOwnerStateSource
                ){
                    // Owner-only imports retain the original exact-queue restriction. A different first consumer
                    // needs all three explicit pieces of an external handoff: fixed destination, completion, and
                    // exported native state source.
                    outCompiledGraph.reset();
                    return false;
                }
                const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                if(acquireType >= GpuCompiledBarrierType::kCount){
                    outCompiledGraph.reset();
                    return false;
                }
                // The recorder imports the descriptor-owned state snapshot before prologue lowering. That emits
                // the native paired acquire (if families differ); this immutable marker also proves the snapshot
                // and the external completion belong to this first range consumer.
                outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = use.range,
                    .before = before,
                    .after = before,
                    .sourceQueue = resource.initialOwnerQueue,
                    .destinationQueue = compiledTask->queue,
                    .type = acquireType,
                    .isInitialOwnerHandoff = true,
                });
                initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                    .completion = resource.initialOwnerCompletion,
                    .consumer = taskID,
                });
            }
            const bool needsUavDependency =
                previousState
                && before == use.requiredState
                && ResourceStates::HasUnorderedAccess(before)
                && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access))
            ;
            if(
                previousState
                && (
                    resource.type == GpuGraphResourceType::Texture
                    || resource.type == GpuGraphResourceType::Buffer
                    || resource.type == GpuGraphResourceType::AccelStruct
                )
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
                        && differentQueueFamilies
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
                                .resource = use.resource,
                                .range = use.range,
                                .before = before,
                                .after = before,
                                .sourceQueue = previousState->queue,
                                .destinationQueue = compiledTask->queue,
                                .type = releaseType,
                            },
                        });
                        outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = use.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = previousState->queue,
                            .destinationQueue = compiledTask->queue,
                            .type = acquireType,
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
            if(materializesGraphInitialState || before != use.requiredState || needsUavDependency){
                outCompiledGraph.m_prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = use.range,
                    .before = before,
                    .after = use.requiredState,
                    .sourceQueue = previousState ? previousState->queue : compiledTask->queue,
                    .destinationQueue = compiledTask->queue,
                    .type = needsUavDependency
                        ? UavBarrierType(resource.type)
                        : TransitionBarrierType(resource.type),
                    .isGraphInitialState = materializesGraphInitialState || requiresExplicitInitialStateSource,
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

    // Imported texture/buffer/acceleration-structure metadata can require a graph-owned terminal state for code
    // that resumes outside this compiled graph. Texture exports retain every terminal subresource fragment; buffers
    // and acceleration structures remain whole-allocation. The runtime lowers each export through the native state
    // tracker and retains the requested state even when no native transition was required.
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        if(resource.externalFinalState == ResourceStates::Unknown)
            continue;

        const bool hasExternalFinalRelease = resource.externalFinalReleaseDestinationQueue.valid();

        const GpuCompiledBarrierType::Enum exportType = StateExportBarrierType(resource.type);
        if(exportType >= GpuCompiledBarrierType::kCount){
            outCompiledGraph.reset();
            return false;
        }

        bool hasTerminalDeclaredRange = false;
        GpuTaskId externalExportTask;
        GpuSubmissionPacketId externalExportPacket;
        GpuPhysicalQueueId externalExportSourceQueue;
        bool externalExportUsesOnePacket = true;
        const u32 externalExportSourceOffset = static_cast<u32>(
            outCompiledGraph.m_externalResourceExportSources.size()
        );
        u32 externalExportSourceCount = 0u;
        const auto appendTerminalState = [&](const TrackedCompiledResourceState& state, const GpuTaskResourceRange& terminalRange){
            if(hasExternalFinalRelease){
                const GpuSubmissionPacketId terminalPacket = outCompiledGraph.packetForTask(state.task);
                if(!terminalPacket.valid())
                    return false;
                if(!externalExportTask.valid()){
                    externalExportTask = state.task;
                    externalExportPacket = terminalPacket;
                    externalExportSourceQueue = state.queue;
                }
                else if(
                    externalExportPacket != terminalPacket
                    || externalExportSourceQueue != state.queue
                ){
                    externalExportUsesOnePacket = false;
                }
                // Texture ownership and state snapshots are subresource-granular, so disjoint terminal mips may
                // safely arrive from different physical queues. Buffer and acceleration-structure state tracking
                // remains whole-allocation, so two packet-local external releases would publish contradictory native
                // ownership/state snapshots even when their declared byte ranges do not overlap. Keep that existing
                // rejection until range-granular Buffer/AS native handoffs exist.
                if(
                    resource.type != GpuGraphResourceType::Texture
                    && (
                        externalExportPacket != terminalPacket
                        || externalExportSourceQueue != state.queue
                    )
                )
                    return false;
                outCompiledGraph.m_externalResourceExportSources.push_back(
                    GpuCompiledExternalResourceExportSource{
                        .producerTask = state.task,
                        .sourceQueue = state.queue,
                        .range = terminalRange,
                    }
                );
                ++externalExportSourceCount;
            }

            pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                .task = state.task,
                .barrier = GpuCompiledBarrier{
                    .resource = state.resource,
                    .range = terminalRange,
                    .before = state.state,
                    .after = resource.externalFinalState,
                    .sourceQueue = state.queue,
                    .destinationQueue = state.queue,
                    .type = exportType,
                },
            });
            if(hasExternalFinalRelease && state.queue != resource.externalFinalReleaseDestinationQueue){
                const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                if(releaseType >= GpuCompiledBarrierType::kCount)
                    return false;
                // Export the exact final state before ownership moves. Native lowering therefore captures the same
                // state in the released snapshot and the paired Vulkan queue-family release barrier.
                pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                    .task = state.task,
                    .barrier = GpuCompiledBarrier{
                        .resource = state.resource,
                        .range = terminalRange,
                        .before = resource.externalFinalState,
                        .after = resource.externalFinalState,
                        .sourceQueue = state.queue,
                        .destinationQueue = resource.externalFinalReleaseDestinationQueue,
                        .type = releaseType,
                    },
                });
            }
            hasTerminalDeclaredRange = true;
            return true;
        };

        if(resource.type == GpuGraphResourceType::Texture){
            stateFragments.clear();
            if(!CollectTerminalTextureStateFragments(
                trackedResourceStates,
                resource.id,
                scratchArena,
                stateFragments
            )){
                outCompiledGraph.reset();
                return false;
            }
            for(const TrackedTextureStateFragment& fragment : stateFragments){
                if(!fragment.state || !appendTerminalState(*fragment.state, fragment.range)){
                    outCompiledGraph.reset();
                    return false;
                }
            }
        }
        else{
            for(usize stateIndex = 0u; stateIndex < trackedResourceStates.size(); ++stateIndex){
                const TrackedCompiledResourceState& state = trackedResourceStates[stateIndex];
                if(state.resource != resource.id)
                    continue;

                bool hasLaterOverlappingUse = false;
                for(usize laterStateIndex = stateIndex + 1u;
                    laterStateIndex < trackedResourceStates.size();
                    ++laterStateIndex
                ){
                    const TrackedCompiledResourceState& later = trackedResourceStates[laterStateIndex];
                    if(
                        later.resource == resource.id
                        && RangesOverlap(resource, state.range, later.range)
                    ){
                        hasLaterOverlappingUse = true;
                        break;
                    }
                }
                if(hasLaterOverlappingUse)
                    continue;
                if(!appendTerminalState(state, state.range)){
                    outCompiledGraph.reset();
                    return false;
                }
            }
        }
        if(!hasTerminalDeclaredRange){
            // A final-state requirement cannot be published from an untouched or state-unknown resource. Reject
            // compilation rather than leaving a direct renderer bridge to guess whether the requirement held.
            outCompiledGraph.reset();
            return false;
        }
        if(hasExternalFinalRelease){
            if(
                !externalExportTask.valid()
                || !externalExportPacket.valid()
                || !externalExportSourceQueue.valid()
                || externalExportSourceCount == 0u
            ){
                outCompiledGraph.reset();
                return false;
            }
            outCompiledGraph.m_externalResourceExports.push_back(GpuCompiledExternalResourceExport{
                .resource = resource.id,
                .producerTask = externalExportUsesOnePacket ? externalExportTask : GpuTaskId{},
                .sourceQueue = externalExportUsesOnePacket ? externalExportSourceQueue : GpuPhysicalQueueId{},
                .sourceOffset = externalExportSourceOffset,
                .sourceCount = externalExportSourceCount,
                .destinationQueue = resource.externalFinalReleaseDestinationQueue,
                .finalState = resource.externalFinalState,
            });
        }
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
    const f64 resourceStatePlanningSeconds = DurationInSeconds<f64>(TimerNow(), resourceStatePlanningBegin);

    const Timer packetDependencyPlanningBegin = TimerNow();
    for(usize consumerPacketIndex = 0u; consumerPacketIndex < outCompiledGraph.m_packets.size(); ++consumerPacketIndex){
        GpuSubmissionPacket& consumerPacket = outCompiledGraph.m_packets[consumerPacketIndex];
        const GpuSubmissionPacketId consumerPacketID{
            static_cast<u32>(consumerPacketIndex),
            outCompiledGraph.m_planGeneration,
        };
        consumerPacket.dependencyOffset = static_cast<u32>(outCompiledGraph.m_packetDependencies.size());
        const auto appendPacketDependency = [&](const GpuSubmissionPacketId producerPacket){
            if(producerPacket == consumerPacketID)
                return true;
            if(
                !producerPacket.valid()
                || producerPacket.index >= consumerPacketIndex
                || producerPacket.generation != outCompiledGraph.m_planGeneration
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

            const auto appendExternalDependency = [&](const GpuTaskExternalDependencyEdge& edge){
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
                    return;

                outCompiledGraph.m_packetExternalDependencies.push_back(edge.completion);
                ++consumerPacket.externalDependencyCount;
            };
            for(const GpuTaskExternalDependencyEdge& edge : outAnalysis.externalDependencies()){
                if(edge.consumer == consumerTask)
                    appendExternalDependency(edge);
            }
            for(const GpuTaskExternalDependencyEdge& edge : initialOwnershipDependencies){
                if(edge.consumer == consumerTask)
                    appendExternalDependency(edge);
            }
        }
    }

    // Packet dependencies are already constrained to earlier compiler-order packets. Persist the longest producer
    // chain as immutable ready-frontier depth so native recording can fan out independent packets without changing
    // the stable compile-order traversal used for submission, timing, and failure reporting.
    for(usize packetIndex = 0u; packetIndex < outCompiledGraph.m_packets.size(); ++packetIndex){
        GpuSubmissionPacket& packet = outCompiledGraph.m_packets[packetIndex];
        u32 frontier = 0u;
        for(u32 dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            const GpuPacketDependency& dependency = outCompiledGraph.m_packetDependencies[
                packet.dependencyOffset + dependencyIndex
            ];
            if(
                dependency.consumer.index != packetIndex
                || dependency.consumer.generation != outCompiledGraph.m_planGeneration
                || dependency.producer.index >= packetIndex
                || dependency.producer.generation != outCompiledGraph.m_planGeneration
            ){
                outCompiledGraph.reset();
                return false;
            }
            const u32 producerFrontier = outCompiledGraph.m_packets[dependency.producer.index].recordingFrontier;
            if(producerFrontier == Limit<u32>::s_Max){
                outCompiledGraph.reset();
                return false;
            }
            const u32 candidateFrontier = producerFrontier + 1u;
            if(candidateFrontier > frontier)
                frontier = candidateFrontier;
        }
        packet.recordingFrontier = frontier;
    }

    if(const GpuPresentEndpoint* const endpoint = graph.presentEndpoint()){
        const GpuCompiledTask* const producer = outCompiledGraph.findTask(endpoint->producer);
        const GpuPhysicalQueueInfo* const queue = producer ? outCompiledGraph.queueInfo(producer->queue) : nullptr;
        if(
            !producer
            || !producer->packet.valid()
            || producer->packet.generation != outCompiledGraph.m_planGeneration
            || !queue
            || queue->queueClass != CommandQueue::Graphics
        ){
            outCompiledGraph.reset();
            return false;
        }
        outCompiledGraph.m_presentEndpoint = GpuCompiledPresentEndpoint{
            .producer = endpoint->producer,
            .backBuffer = endpoint->backBuffer,
            .packet = producer->packet,
            .queue = queue->id,
        };
        outCompiledGraph.m_hasPresentEndpoint = true;
    }
    const f64 packetDependencyPlanningSeconds = DurationInSeconds<f64>(TimerNow(), packetDependencyPlanningBegin);

    GpuTaskGraphCompileStatistics& statistics = outCompiledGraph.m_compileStatistics;
    statistics.graphGeneration = outCompiledGraph.m_generation;
    statistics.planGeneration = outCompiledGraph.m_planGeneration;
    statistics.deviceGeneration = outCompiledGraph.m_deviceGeneration;
    statistics.taskCount = graph.taskCount();
    statistics.resourceCount = graph.resourceCount();
    statistics.resourceSetCount = graph.m_resourceSets.size();
    statistics.resourceSetMemberCount = graph.m_resourceSetMembers.size();
    statistics.uploadBlobCount = graph.m_uploadBlobs.size();
    for(const auto& blob : graph.m_uploadBlobs)
        statistics.uploadBlobBytes += blob.bytes.size();
    statistics.explicitDependencyCount = outAnalysis.explicitEdgeCount();
    statistics.inferredDependencyCount = outAnalysis.inferredEdgeCount();
    statistics.declaredExternalDependencyCount = outAnalysis.externalDependencies().size();
    statistics.packetCount = outCompiledGraph.m_packets.size();
    statistics.packetDependencyCount = outCompiledGraph.m_packetDependencies.size();
    statistics.packetExternalDependencyCount = outCompiledGraph.m_packetExternalDependencies.size();
    statistics.externalDependencyCount = statistics.packetExternalDependencyCount;
    statistics.initialOwnershipExternalDependencyCount = initialOwnershipDependencies.size();
    statistics.prologueStateSeedCount = outCompiledGraph.m_prologueStateSeeds.size();
    statistics.prologueBarrierCount = outCompiledGraph.m_prologueBarriers.size();
    statistics.epilogueBarrierCount = outCompiledGraph.m_epilogueBarriers.size();
    for(const GpuCompiledTask& compiledTask : outCompiledGraph.m_tasks){
        const GpuTaskGraphTaskView task = graph.taskAt(compiledTask.task.index);
        const auto& declaredTask = graph.m_tasks[compiledTask.task.index];
        statistics.resourceUseCount += task.resourceUseCount;
        statistics.directResourceUseCount += declaredTask.directResourceUseCount;
        statistics.declaredResourceSetUseCount += declaredTask.declaredResourceSetUseCount;
        statistics.expandedResourceSetMemberUseCount += declaredTask.expandedResourceSetMemberUseCount;
        if(declaredTask.payload){
            ++statistics.payloadObjectCount;
            statistics.payloadObjectBytes += declaredTask.payloadObjectSize;
        }
        if(compiledTask.packetizationDecision < GpuTaskPacketizationDecision::kCount)
            ++statistics.packetizationDecisionCounts[compiledTask.packetizationDecision];

        const GpuPhysicalQueueInfo* const queue = outCompiledGraph.queueInfo(compiledTask.queue);
        if(queue && queue->queueClass < CommandQueue::kCount)
            ++statistics.taskCountByQueueClass[queue->queueClass];
    }
    for(usize packetIndex = 0u; packetIndex < outCompiledGraph.m_packets.size(); ++packetIndex){
        const GpuSubmissionPacket& packet = outCompiledGraph.m_packets[packetIndex];
        if(packet.taskCount > 1u)
            statistics.mergedTaskCount += packet.taskCount - 1u;
        if(packet.recordingFrontier != Limit<u32>::s_Max)
            statistics.recordingFrontierCount = Max(
                statistics.recordingFrontierCount,
                static_cast<usize>(packet.recordingFrontier) + 1u
            );

        const GpuPhysicalQueueInfo* const queue = outCompiledGraph.queueInfo(packet.queue);
        if(queue && queue->queueClass < CommandQueue::kCount)
            ++statistics.packetCountByQueueClass[queue->queueClass];

        const GpuPacketDependency* const dependencies = packet.dependencyCount > 0u
            ? outCompiledGraph.m_packetDependencies.data() + packet.dependencyOffset
            : nullptr
        ;
        for(u32 dependencyIndex = 0u; dependencies && dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            const GpuPacketDependency& dependency = dependencies[dependencyIndex];
            if(
                !dependency.producer.valid()
                || dependency.producer.generation != outCompiledGraph.m_planGeneration
                || dependency.producer.index >= outCompiledGraph.m_packets.size()
            )
                continue;
            const GpuSubmissionPacket& producer = outCompiledGraph.m_packets[dependency.producer.index];
            if(producer.queue == packet.queue)
                continue;

            ++statistics.crossQueuePacketDependencyCount;
            const GpuPhysicalQueueInfo* const producerQueue = outCompiledGraph.queueInfo(producer.queue);
            if(producerQueue && queue && producerQueue->familyIndex != queue->familyIndex)
                ++statistics.crossFamilyPacketDependencyCount;
        }
    }
    const auto countBarriers = [&](const GraphicsVector<GpuCompiledBarrier>& barriers){
        for(const GpuCompiledBarrier& barrier : barriers){
            switch(barrier.type){
            case GpuCompiledBarrierType::TextureTransition:
            case GpuCompiledBarrierType::BufferTransition:
            case GpuCompiledBarrierType::AccelStructTransition:
                ++statistics.transitionBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureUav:
            case GpuCompiledBarrierType::BufferUav:
            case GpuCompiledBarrierType::AccelStructUav:
                ++statistics.uavBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipRelease:
            case GpuCompiledBarrierType::BufferOwnershipRelease:
            case GpuCompiledBarrierType::AccelStructOwnershipRelease:
                ++statistics.ownershipReleaseBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipAcquire:
            case GpuCompiledBarrierType::BufferOwnershipAcquire:
            case GpuCompiledBarrierType::AccelStructOwnershipAcquire:
                ++statistics.ownershipAcquireBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureStateExport:
            case GpuCompiledBarrierType::BufferStateExport:
            case GpuCompiledBarrierType::AccelStructStateExport:
                ++statistics.stateExportBarrierCount;
                break;
            default:
                break;
            }
        }
    };
    countBarriers(outCompiledGraph.m_prologueBarriers);
    countBarriers(outCompiledGraph.m_epilogueBarriers);
    statistics.declarationSeconds = IsFinite(options.declarationSeconds) && options.declarationSeconds >= 0.0
        ? options.declarationSeconds
        : 0.0
    ;
    statistics.analysisSeconds = analysisSeconds;
    statistics.queueAssignmentSeconds = queueAssignmentSeconds;
    statistics.planningSeconds = DurationInSeconds<f64>(TimerNow(), planningBegin);
    statistics.packetizationSeconds = packetizationSeconds;
    statistics.resourceStatePlanningSeconds = resourceStatePlanningSeconds;
    statistics.packetDependencyPlanningSeconds = packetDependencyPlanningSeconds;
    statistics.totalSeconds = DurationInSeconds<f64>(TimerNow(), compileBegin);

    outCompiledGraph.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

