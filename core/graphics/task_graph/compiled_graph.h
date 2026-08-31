// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_desc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;
class GpuTaskGraphCompiler;
class GpuCommandIrCapture;


// Tasks describe semantic work. Packets are compiler-generated native-recording and submission units; a packet
// contains one or more explicitly compatible tasks.
struct GpuSubmissionPacket{
    GpuPhysicalQueueId queue;
    u32 taskOffset = 0u;
    u32 taskCount = 0u;
    u32 dependencyOffset = 0u;
    u32 dependencyCount = 0u;
    u32 externalDependencyOffset = 0u;
    u32 externalDependencyCount = 0u;
    // Compiler-derived ready-frontier depth for native recording. Packets in one depth have no recorded-state-seed
    // producer relationship, so the runtime may record opted-in packets concurrently while retaining deterministic
    // compile-order submission.
    u32 recordingFrontier = 0u;
    // A late recovery/finalization packet receives waits for the latest accepted packet on every other physical
    // queue directly from GpuGraphSubmissionTransaction. It is a graph runtime policy, not a renderer token ladder.
    bool joinsAcceptedQueueFrontier = false;
    // Exact recovery role within the broader accepted-frontier packet class. This remains false for ordinary late
    // finalization packets that need the same queue join but do not recover a partially accepted graph.
    bool isRecoverySubmission = false;
    // Distinguishes compiler-selected envelope membership from task-local PacketOnly/Task policies. recordsTiming
    // remains the combined native-ticket requirement so existing recording and submission paths stay coherent.
    bool recordsPacketEnvelopeTiming = false;
    bool recordsTiming = false;
};

struct GpuPacketDependency{
    GpuSubmissionPacketId producer;
    GpuSubmissionPacketId consumer;
};

// Explains each compiler packetization decision without exposing mutable compiler internals. Tooling can distinguish
// deliberate lifecycle/timing boundaries from a merge that was rejected to preserve a cross-queue signal frontier.
namespace GpuTaskPacketizationDecision{
    enum Enum : u8{
        Unknown,
        FirstTask,
        MergeNotRequested,
        TaskForcesBoundary,
        QueueChanged,
        PrecedingTaskForcesBoundary,
        ScoredMergeIneligible,
        MergeRequiresExplicitImmediateDependency,
        CrossQueueConsumerFrontier,
        MergedExplicit,
        MergedFrontierScored,
        ScoredMergeDomainMismatch,

        kCount,
    };
};

struct GpuCompiledTask{
    GpuTaskId task;
    GpuPhysicalQueueId queue;
    GpuSubmissionPacketId packet;
    GpuTaskPacketizationDecision::Enum packetizationDecision = GpuTaskPacketizationDecision::Unknown;
    GpuTaskTimingPolicy::Enum timingPolicy = GpuTaskTimingPolicy::None;
    // Calibration and debug-override observations contribute route duration history without becoming the
    // committed timing-feedback incumbent. Retain that decision so late query collection preserves policy dwell.
    bool recordsNonCommittingTimingSample = false;
    u32 prologueStateSeedOffset = 0u;
    u32 prologueStateSeedCount = 0u;
    u32 prologueBarrierOffset = 0u;
    u32 prologueBarrierCount = 0u;
    u32 epilogueBarrierOffset = 0u;
    u32 epilogueBarrierCount = 0u;
};

// One terminal declared range that contributes to an explicit graph-to-external release.  Textures may have
// several disjoint terminal subresource ranges, potentially recorded by different packets and physical queues.
// The runtime turns every source into one acceptance-gated producer token and merges their exact exported state
// ranges before handing the texture to native code. Buffer and AS releases remain one-packet because their native
// tracking is whole-allocation.
struct GpuCompiledExternalResourceExportSource{
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    GpuTaskResourceRange range;
};

// Imported texture/buffer/AS graph-to-external release metadata.  `producerTask`/`sourceQueue` retain the
// original one-producer convenience contract whenever every terminal range resolves to the same packet.  Callers
// that need the complete contract must consume `sourceCount` sources through externalResourceExportSources().
struct GpuCompiledExternalResourceExport{
    GpuGraphResourceId resource;
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    u32 sourceOffset = 0u;
    u32 sourceCount = 0u;
    GpuPhysicalQueueId destinationQueue;
    ResourceStates::Mask finalState = ResourceStates::Unknown;
};

// Immutable presentation completion resolved from a strict typed-texture declaration. It retains the semantic
// producer/backbuffer pair along with the compiler-owned packet and exact physical queue shared by every backbuffer
// user before the one native present signal.
struct GpuCompiledPresentEndpoint{
    GpuTaskId producer;
    GpuGraphResourceId backBuffer;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;

    [[nodiscard]] bool valid()const noexcept{
        return producer.valid() && backBuffer.valid() && packet.valid() && queue.valid();
    }
};


// One semantic exclusive-ownership movement. Internal moves remain one record even though native lowering emits a
// release and an acquire barrier; external routes deliberately leave the task and packet on the external side
// invalid. Queue-family indices and sharing intent are captured by value so telemetry never has to reinterpret a
// later topology or resource declaration.
namespace GpuOwnershipTransferRoute{
    enum Enum : u8{
        Internal,
        ExternalImport,
        ExternalExport,

        kCount,
    };
};

struct GpuCompiledOwnershipTransfer{
    GpuGraphResourceId resource;
    Name resourceIdentity = NAME_NONE;
    GpuTaskResourceRange range;
    GpuTaskId sourceTask;
    GpuTaskId destinationTask;
    GpuSubmissionPacketId sourcePacket;
    GpuSubmissionPacketId destinationPacket;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    u32 sourceQueueFamilyIndex = Limit<u32>::s_Max;
    u32 destinationQueueFamilyIndex = Limit<u32>::s_Max;
    ResourceQueueSharing::Mask declaredQueueSharing = ResourceQueueSharing::Exclusive;
    GpuGraphResourceType::Enum resourceType = GpuGraphResourceType::HazardDomain;
    GpuOwnershipTransferRoute::Enum route = GpuOwnershipTransferRoute::kCount;
    bool concurrentSharingCouldAvoid = false;

    [[nodiscard]] bool valid()const noexcept{
        if(
            !resource.valid()
            || resourceIdentity == NAME_NONE
            || resourceType >= GpuGraphResourceType::HazardDomain
            || route >= GpuOwnershipTransferRoute::kCount
            || !sourceQueue.valid()
            || !destinationQueue.valid()
            || sourceQueue == destinationQueue
            || sourceQueue.deviceGeneration != destinationQueue.deviceGeneration
            || sourceQueueFamilyIndex == Limit<u32>::s_Max
            || destinationQueueFamilyIndex == Limit<u32>::s_Max
            || sourceQueueFamilyIndex == destinationQueueFamilyIndex
            || !ResourceQueueSharing::IsValid(declaredQueueSharing)
        )
            return false;

        switch(route){
        case GpuOwnershipTransferRoute::Internal:
            return sourceTask.valid()
                && destinationTask.valid()
                && sourceTask != destinationTask
                && sourceTask.generation == resource.generation
                && destinationTask.generation == resource.generation
                && sourcePacket.valid()
                && destinationPacket.valid()
                && sourcePacket != destinationPacket
                && sourcePacket.generation == destinationPacket.generation
            ;
        case GpuOwnershipTransferRoute::ExternalImport:
            return !sourceTask.valid()
                && destinationTask.valid()
                && destinationTask.generation == resource.generation
                && !sourcePacket.valid()
                && destinationPacket.valid()
            ;
        case GpuOwnershipTransferRoute::ExternalExport:
            return sourceTask.valid()
                && sourceTask.generation == resource.generation
                && !destinationTask.valid()
                && sourcePacket.valid()
                && !destinationPacket.valid()
            ;
        default:
            return false;
        }
    }
};


// Immutable accepted-plan telemetry for one concrete task-graph plan. Core compiler counters and the sanitized
// caller-provided declaration duration publish only after success; failed or superseded plans retain an empty
// snapshot, so tooling never has to reconcile partial barrier/packet data with a later generation.
struct GpuTaskGraphCompileStatistics{
    static constexpr usize s_QueueClassCount = static_cast<usize>(CommandQueue::kCount);
    static constexpr usize s_PacketizationDecisionCount = static_cast<usize>(GpuTaskPacketizationDecision::kCount);
    static constexpr usize s_OwnershipTransferRouteCount = static_cast<usize>(GpuOwnershipTransferRoute::kCount);

    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u16 deviceGeneration = 0u;
    usize taskCount = 0u;
    usize resourceCount = 0u;
    usize resourceUseCount = 0u;
    usize resourceVersionCount = 0u;
    // Counts both producer-to-consumer dependencies and consumer-to-overwriter lifetime edges.
    usize resourceVersionEdgeCount = 0u;
    // These are analysis categories, not a disjoint scheduling-edge partition: one task pair can have both an
    // explicit declaration and inferred resource reason, so callers must not sum them as a final edge count.
    usize explicitDependencyCount = 0u;
    usize inferredDependencyCount = 0u;
    usize declaredExternalDependencyCount = 0u;
    // Initial-ownership completion reasons before per-packet completion-token deduplication.
    usize initialOwnershipExternalDependencyCount = 0u;
    // Total resolved task-to-packet external completion edges, including compiler-created initial-ownership waits.
    usize externalDependencyCount = 0u;
    usize packetCount = 0u;
    usize packetDependencyCount = 0u;
    usize packetExternalDependencyCount = 0u;
    usize crossQueuePacketDependencyCount = 0u;
    usize crossFamilyPacketDependencyCount = 0u;
    usize mergedTaskCount = 0u;
    usize prologueStateSeedCount = 0u;
    usize prologueBarrierCount = 0u;
    usize epilogueBarrierCount = 0u;
    usize transitionBarrierCount = 0u;
    usize uavBarrierCount = 0u;
    usize ownershipReleaseBarrierCount = 0u;
    usize ownershipAcquireBarrierCount = 0u;
    usize stateExportBarrierCount = 0u;
    // Logical ownership movements are distinct from the raw release/acquire marker counts above. Signature counts
    // collapse fragmented ranges of the same route/task/queue handoff. Repeated signatures and the advice-resource
    // count therefore identify actual repeated movement rather than texture-fragment fan-out.
    usize logicalOwnershipTransferCount = 0u;
    usize logicalOwnershipTransferSignatureCount = 0u;
    usize repeatedOwnershipTransferSignatureCount = 0u;
    usize concurrentSharingCouldAvoidTransferCount = 0u;
    usize concurrentSharingAdviceResourceCount = 0u;
    usize logicalOwnershipTransferCountByRoute[s_OwnershipTransferRouteCount] = {};
    // Recording-frontier depth: max packet recordingFrontier plus one, rather than the number of packets.
    usize recordingFrontierCount = 0u;
    usize taskCountByQueueClass[s_QueueClassCount] = {};
    usize packetCountByQueueClass[s_QueueClassCount] = {};
    usize packetizationDecisionCounts[s_PacketizationDecisionCount] = {};
    // Sanitized caller-measured graph declaration/build wall time. totalSeconds covers the core compiler only.
    f64 declarationSeconds = 0.0;
    // analysisSeconds remains the backward-compatible umbrella duration. The detail buckets measure disjoint named
    // analysis passes, but unclassified scaffolding and timing overhead mean they need not sum to analysisSeconds.
    f64 analysisSeconds = 0.0;
    f64 validationSeconds = 0.0;
    f64 dependencyAnalysisSeconds = 0.0;
    f64 hazardAnalysisSeconds = 0.0;
    // Includes cycle detection and stable ordering for both semantic-only and final inferred dependency graphs.
    f64 topologicalOrderSeconds = 0.0;
    f64 queueAssignmentSeconds = 0.0;
    f64 planningSeconds = 0.0;
    f64 totalSeconds = 0.0;
    // Declaration structure is retained separately from resourceUseCount above, which remains the final
    // materialized resource-use total after immutable resource-set expansion.
    usize resourceSetCount = 0u;
    usize resourceSetMemberCount = 0u;
    usize directResourceUseCount = 0u;
    usize declaredResourceSetUseCount = 0u;
    usize expandedResourceSetMemberUseCount = 0u;
    // Payload bytes count only the graph-owned payload objects themselves; dynamic payload allocations remain
    // owned by their individual payload types and are deliberately excluded.
    usize payloadObjectCount = 0u;
    usize payloadObjectBytes = 0u;
    usize uploadBlobCount = 0u;
    usize uploadBlobBytes = 0u;
    // planningSeconds remains the umbrella duration. These detail buckets cover only their named core planning
    // passes, so callers must not expect them to sum to planningSeconds.
    f64 packetizationSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    f64 packetDependencyPlanningSeconds = 0.0;

    [[nodiscard]] bool valid()const noexcept{ return graphGeneration != 0u && planGeneration != 0u; }
};


// Immutable-by-value accepted-plan telemetry for one exact physical queue. Barrier counts belong to the compiled
// task that lowers them: a cross-queue ownership release therefore belongs to its producer queue, while the
// matching acquire belongs to its consumer queue.
struct GpuTaskGraphPhysicalQueueCompileStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u16 deviceGeneration = 0u;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    usize taskCount = 0u;
    usize packetCount = 0u;
    usize mergedTaskCount = 0u;
    usize prologueBarrierCount = 0u;
    usize epilogueBarrierCount = 0u;
    usize ownershipReleaseBarrierCount = 0u;
    usize ownershipAcquireBarrierCount = 0u;
    usize incomingLogicalOwnershipTransferCount = 0u;
    usize outgoingLogicalOwnershipTransferCount = 0u;
    usize incomingLogicalOwnershipTransferSignatureCount = 0u;
    usize outgoingLogicalOwnershipTransferSignatureCount = 0u;
    usize incomingRepeatedOwnershipTransferSignatureCount = 0u;
    usize outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    usize concurrentSharingAdviceResourceCount = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return graphGeneration != 0u
            && planGeneration != 0u
            && deviceGeneration != 0u
            && queue.valid()
            && queue.deviceGeneration == deviceGeneration
            && queueClass < CommandQueue::kCount
        ;
    }
};


class GpuCompiledGraph final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuCompiledGraph(GraphicsArena& arena);


public:
    void reset();

    [[nodiscard]] bool valid()const noexcept{ return m_valid && m_planGeneration != 0u; }
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph)const noexcept;
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    // Graph tasks/resources retain generation(), while compiler-owned packet identities use this immutable-plan
    // generation so a same-graph recompile cannot alias old native recording or submission state.
    [[nodiscard]] u64 planGeneration()const noexcept{ return m_planGeneration; }
    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] usize taskCount()const noexcept{ return m_tasks.size(); }
    [[nodiscard]] usize packetCount()const noexcept{ return m_packets.size(); }
    [[nodiscard]] bool validPacket(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] bool validPacketRange(const GpuSubmissionPacketRange& range)const noexcept;
    // Compiler packet indices follow the stable topological task order.  Full-graph native record/submit traversal
    // consumes this order so each packet observes already-recorded and accepted internal producers.
    [[nodiscard]] GpuSubmissionPacketId packetIdAt(usize index)const noexcept;
    // Derives an inclusive contiguous compiler-order range from packet handles. This keeps callers independent from
    // the compiler's raw packet indices while still rejecting handles from another immutable compiled plan.
    [[nodiscard]] GpuSubmissionPacketRange packetRange(
        const GpuSubmissionPacketId& first,
        const GpuSubmissionPacketId& last
    )const noexcept;
    // Semantic companion to packetRange(). Resolves both declared task endpoints through this compiled generation,
    // keeping renderer record/submit spans independent from packet splitting and merging. The result remains an
    // inclusive contiguous compiler-order range, so deliberately late tails retain their existing traversal rules.
    [[nodiscard]] GpuSubmissionPacketRange packetRangeForTasks(
        const GpuTaskId& first,
        const GpuTaskId& last
    )const noexcept;
    [[nodiscard]] GpuSubmissionPacketRange allPacketRange()const noexcept;
    // Immutable compiler-selected timing span. Endpoint packets are included whole because packet queries bracket
    // native submissions rather than individual merged task payloads.
    [[nodiscard]] GpuSubmissionPacketRange packetTimingEnvelopeRange()const noexcept{
        return validPacketRange(m_packetTimingEnvelopeRange) ? m_packetTimingEnvelopeRange : GpuSubmissionPacketRange{};
    }
    [[nodiscard]] const GpuCompiledTask* findTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] GpuSubmissionPacketId packetForTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] GpuTaskPacketizationDecision::Enum packetizationDecisionForTask(const GpuTaskId& task)const noexcept;
    // Semantic packet-topology queries.  Renderer policy can validate coalescing and routing without retaining
    // compiler packet IDs; packet handles remain available below for packet-local runtime compatibility only.
    [[nodiscard]] bool tasksSharePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    [[nodiscard]] bool taskPrecedesOrSharesPacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    // Strict semantic compiler-order query for two tasks in one packet. This keeps renderer validation out of the
    // packet task array while retaining the difference between an in-packet ordering guarantee and an ordinary
    // cross-packet dependency.
    [[nodiscard]] bool taskPrecedesInSamePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    // Returns true only when the declared tasks occur as one exact contiguous sequence in the same compiled
    // packet. The runtime owns the packet-local search, so renderer code need not retain compiler packet IDs just
    // to validate graph-owned producer/raster alternation.
    [[nodiscard]] bool tasksFormContiguousPacketSequence(
        const GpuTaskId* tasks,
        usize taskCount
    )const noexcept;
    [[nodiscard]] bool taskJoinsAcceptedQueueFrontier(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* queueInfoForTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuSubmissionPacket& packet(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuTaskId* packetTasks(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuPacketDependency* packetDependencies(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuExternalCompletionId* packetExternalDependencies(
        const GpuSubmissionPacketId& packet
    )const noexcept;
    [[nodiscard]] const GpuPacketStateSeed* taskPrologueStateSeeds(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuCompiledBarrier* taskPrologueBarriers(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuCompiledBarrier* taskEpilogueBarriers(const GpuTaskId& task)const noexcept;
    [[nodiscard]] usize logicalOwnershipTransferCount()const noexcept{ return m_ownershipTransfers.size(); }
    [[nodiscard]] const GpuCompiledOwnershipTransfer* logicalOwnershipTransfers()const noexcept;
    [[nodiscard]] const GpuCompiledOwnershipTransfer* logicalOwnershipTransferAt(usize index)const noexcept;
    // Resolves the terminal graph-to-external release declaration for this imported resource. No result means the
    // resource did not request a graph-to-external handoff in this compiled generation.
    [[nodiscard]] const GpuCompiledExternalResourceExport* externalResourceExport(
        const GpuGraphResourceId& resource
    )const noexcept;
    [[nodiscard]] usize externalResourceExportCount()const noexcept{ return m_externalResourceExports.size(); }
    [[nodiscard]] const GpuCompiledPresentEndpoint* presentEndpoint()const noexcept{
        return valid() && m_hasPresentEndpoint ? &m_presentEndpoint : nullptr;
    }
    [[nodiscard]] const GpuTaskGraphCompileStatistics& compileStatistics()const noexcept{ return m_compileStatistics; }
    [[nodiscard]] const GpuCompiledExternalResourceExport* externalResourceExportAt(usize index)const noexcept;
    [[nodiscard]] const GpuCompiledExternalResourceExportSource* externalResourceExportSources(
        const GpuCompiledExternalResourceExport& exportInfo
    )const noexcept;
    // Builds a value snapshot from immutable accepted-plan storage. A topology queue with no tasks or packets
    // returns a valid zero snapshot; stale, non-plan, reset, and failed-plan queries return an invalid result.
    // Callers externally serialize this scan with reset/recompile.
    [[nodiscard]] GpuTaskGraphPhysicalQueueCompileStatistics physicalQueueCompileStatistics(
        const GpuPhysicalQueueId& queue
    )const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* queueInfo(const GpuPhysicalQueueId& queue)const noexcept;
    // Borrowed immutable-plan topology view. It is empty after reset/invalid compilation; callers serialize access
    // with reset/recompile and consume or copy queue identities before that compiled-plan storage can change.
    [[nodiscard]] GpuPhysicalQueueTopology queueTopology()const noexcept;


private:
    GraphicsVector<GpuCompiledTask> m_tasks;
    // Dense declaration-task index to topological m_tasks offset. Offsets remain stable when arena vector storage moves.
    GraphicsVector<u32> m_compiledTaskIndexByTask;
    GraphicsVector<GpuSubmissionPacket> m_packets;
    GraphicsVector<GpuTaskId> m_packetTasks;
    GraphicsVector<GpuPacketDependency> m_packetDependencies;
    GraphicsVector<GpuExternalCompletionId> m_packetExternalDependencies;
    GraphicsVector<GpuPacketStateSeed> m_prologueStateSeeds;
    GraphicsVector<GpuCompiledBarrier> m_prologueBarriers;
    GraphicsVector<GpuCompiledBarrier> m_epilogueBarriers;
    GraphicsVector<GpuCompiledOwnershipTransfer> m_ownershipTransfers;
    GraphicsVector<GpuCompiledExternalResourceExport> m_externalResourceExports;
    GraphicsVector<GpuCompiledExternalResourceExportSource> m_externalResourceExportSources;
    GraphicsVector<GpuPhysicalQueueInfo> m_queueTopology;
    GpuCompiledPresentEndpoint m_presentEndpoint;
    GpuSubmissionPacketRange m_packetTimingEnvelopeRange;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    u64 m_planGeneration = 0u;
    u16 m_deviceGeneration = 0u;
    usize m_graphTaskCount = 0u;
    GpuTaskGraphCompileStatistics m_compileStatistics;
    bool m_hasPresentEndpoint = false;
    bool m_valid = false;
};


// Recording happens only after the compiler has selected a concrete physical queue and packet.  Task record thunks
// receive immutable compiled metadata rather than a nullable renderer-specific context.
struct GpuTaskRecordContext{
    const GpuTaskGraph& taskGraph;
    const GpuCompiledGraph& graph;
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
    // Recording attempts isolate retryable native captures sharing one immutable compiler plan.
    u64 recordingAttemptGeneration = 0u;
    // Optional Phase 11 tooling sink. Null keeps direct native task recording on the ordinary runtime path.
    GpuCommandIrCapture* commandIrCapture = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

